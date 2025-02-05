#include "mb_utils.h"
#include <iostream>
#include <string>
#include <cstdlib>
#include <unistd.h>
#include <regex>


int getNodeNumber() {
    char hostname[256];
    if (gethostname(hostname, sizeof(hostname)) != 0) {
        perror("gethostname failed");
        return -1;  // Return -1 on error
    }

    std::string hostStr(hostname);
    std::regex pattern(R"(node(\d+))");
    std::smatch match;

    if (std::regex_search(hostStr, match, pattern)) {
        return std::stoi(match[1]);
    }

    return -1;
}

void parse_cli_args(
    int *threadNR, int *nodeNR, int* mnNR, int *lockNR, int *runNR,
    int *node_id, int* duration, int* mode,
    char **res_file_cum, char **res_file_single,
    int argc, char **argv
) {
    int option;
	*node_id = getNodeNumber();
	while ((option = getopt(argc, argv,
    "d:t:l:i:d:s:m:r:f:g:n:")) != -1) 
    {
		switch (option) {
			case 's':
				*mnNR = atoi(optarg);
				break;
			case 'f':
                *res_file_cum = optarg; 
				break;
			case 'g':
                *res_file_single = optarg; 
				break;
			case 'n':
				*nodeNR = atoi(optarg);
				break;
			case 'r':
				*runNR = atoi(optarg);
				break;
			case 'm':
				*mode = atoi(optarg);
				break;
			case 'd':
				*duration = atoi(optarg);
				break;
            case 'i':
                *node_id = atoi(optarg);
                break;
			case 't':
				*threadNR = atoi(optarg);
				break;
			case 'l':
				*lockNR = atoi(optarg);
				break;
			default:
				break;
		}
	}
}

Rlock::Rlock(DSM *dsm) : dsm(dsm) {
	for (int i = 0; i < dsm->getClusterSize(); ++i) {
		local_locks[i] = new LocalLockNode[define::kNumOfLock];
		for (size_t k = 0; k < define::kNumOfLock; ++k) {
			auto &n = local_locks[i][k];
			n.ticket_lock.store(0);
			n.hand_over = false;
			n.hand_time = 0;
		}
	}
}

GlobalAddress Rlock::get_lock_addr(GlobalAddress base_addr) {
	uint64_t lock_index =
		CityHash64((char *)&base_addr, sizeof(base_addr)) % define::kNumOfLock;

	GlobalAddress lock_addr;
	lock_addr.nodeID = base_addr.nodeID;
	lock_addr.offset = lock_index * sizeof(uint64_t);
	return lock_addr;
}

void Rlock::get_bufs() {
	auto rbuf = dsm->get_rbuf(0);
	curr_cas_buffer = rbuf.get_cas_buffer();
	curr_page_buffer = rbuf.get_page_buffer();
}

void Rlock::lock_acquire(GlobalAddress base_addr, int data_size) {
	GlobalAddress lock_addr = get_lock_addr(base_addr);

	auto &rbuf = dsm->get_rbuf(0);
	uint64_t *cas_buffer = rbuf.get_cas_buffer();
	auto page_buffer = rbuf.get_page_buffer();

	auto tag = dsm->getThreadTag();
	assert(tag != 0);


	try_lock_addr(lock_addr, tag, cas_buffer, NULL, 0);
	dsm->read_sync(page_buffer, base_addr, data_size, NULL);

}
void Rlock::lock_release(GlobalAddress base_addr, int data_size) {
	write_and_unlock();

}
void Rlock::write_and_unlock(char *page_buffer, GlobalAddress page_addr,
                                 int page_size, uint64_t *cas_buffer,
                                 GlobalAddress lock_addr, uint64_t tag,
                                 CoroContext *cxt, int coro_id, bool async) {

	bool hand_over_other = can_hand_over(lock_addr);
	if (hand_over_other) {
		dsm->write_sync(page_buffer, page_addr, page_size, cxt);
		releases_local_lock(lock_addr);
		return;
	}

	RdmaOpRegion rs[2];
	rs[0].source = (uint64_t)page_buffer;
	rs[0].dest = page_addr;
	rs[0].size = page_size;
	rs[0].is_on_chip = false;

	rs[1].source = (uint64_t)dsm->get_rbuf(coro_id).get_cas_buffer();
	rs[1].dest = lock_addr;
	rs[1].size = sizeof(uint64_t);

	rs[1].is_on_chip = true;

	*(uint64_t *)rs[1].source = 0;
	if (async) {
		dsm->write_batch(rs, 2, false);
	} else {
		dsm->write_batch_sync(rs, 2, cxt);
	}

	releases_local_lock(lock_addr);
}

inline bool Rlock::try_lock_addr(GlobalAddress lock_addr, uint64_t tag,
                                uint64_t *buf, CoroContext *cxt, int coro_id) {

  bool hand_over = acquire_local_lock(lock_addr, cxt, coro_id);
  if (hand_over) {
    return true;
  }

  {

    uint64_t retry_cnt = 0;
    uint64_t pre_tag = 0;
    uint64_t conflict_tag = 0;
  retry:
    retry_cnt++;
    if (retry_cnt > 1000000) {
      std::cout << "Deadlock " << lock_addr << std::endl;

      std::cout << dsm->getMyNodeID() << ", " << dsm->getMyThreadID()
                << " locked by " << (conflict_tag >> 32) << ", "
                << (conflict_tag << 32 >> 32) << std::endl;
      assert(false);
    }

    bool res = dsm->cas_dm_sync(lock_addr, 0, tag, buf, cxt);

    if (!res) {
      conflict_tag = *buf - 1;
      if (conflict_tag != pre_tag) {
        retry_cnt = 0;
        pre_tag = conflict_tag;
      }
      goto retry;
    }
  }

  return true;
}

inline bool Rlock::acquire_local_lock(GlobalAddress lock_addr, CoroContext *cxt,
                                     int coro_id) {
  auto &node = local_locks[lock_addr.nodeID][lock_addr.offset / 8];

  uint64_t lock_val = node.ticket_lock.fetch_add(1);

  uint32_t ticket = lock_val << 32 >> 32;
  uint32_t current = lock_val >> 32;

  while (ticket != current) { // lock failed

    // if (cxt != nullptr) {
    //   hot_wait_queue.push(coro_id);
    //   (*cxt->yield)(*cxt->master);
    // }

    current = node.ticket_lock.load(std::memory_order_relaxed) >> 32;
  }

  node.hand_time++;

  return node.hand_over;
}

inline bool Rlock::can_hand_over(GlobalAddress lock_addr) {

  auto &node = local_locks[lock_addr.nodeID][lock_addr.offset / 8];
  uint64_t lock_val = node.ticket_lock.load(std::memory_order_relaxed);

  uint32_t ticket = lock_val << 32 >> 32;
  uint32_t current = lock_val >> 32;

  if (ticket <= current + 1) { // no pending locks
    node.hand_over = false;
  } else {
    node.hand_over = node.hand_time < define::kMaxHandOverTime;
  }
  if (!node.hand_over) {
    node.hand_time = 0;
  }

  return node.hand_over;
}

inline void Rlock::releases_local_lock(GlobalAddress lock_addr) {
  auto &node = local_locks[lock_addr.nodeID][lock_addr.offset / 8];

  node.ticket_lock.fetch_add((1ull << 32));
}