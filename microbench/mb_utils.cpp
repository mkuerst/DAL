using namespace std;

#include "mb_utils.h"
#include <iostream>
#include <string>
#include <cstdlib>
#include <unistd.h>
#include <regex>

thread_local char* Rlock::curr_page_buffer = nullptr;
thread_local uint64_t* Rlock::curr_cas_buffer = nullptr;
thread_local GlobalAddress Rlock::curr_lock_addr;


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

int check_MN_correctness(DSM *dsm, size_t dsmSize) {
	uint64_t mn_sum = 0;
	char *cn_sum_ptr = dsm->get_DSMKeeper()->memGet(ck.c_str(), ck.size());
	uint64_t cn_sum;
    memcpy(&cn_sum, cn_sum_ptr, sizeof(uint64_t));
	uint64_t baseAddr = dsm->get_baseAddr();
	uint64_t *long_data = (uint64_t *) baseAddr;

	for (size_t i = 0; i < GB(dsmSize) / sizeof(uint64_t); i++) {
		mn_sum += long_data[i];
	}
	if (mn_sum != cn_sum) {
		_error("mn_sum = %lu", mn_sum);
		_error("cn_sum = %lu", cn_sum);
		return -1;
	}
	return 0;
}

int check_CN_correctness(
	Task* tasks, uint64_t *lock_acqs, uint64_t *lock_rels,
	uint32_t lockNR, uint32_t threadNR, DSM *dsm) 
{
	uint64_t node_sum = 0;
	uint64_t task_sum = 0;
	for (uint32_t i = 0; i < lockNR; i++) {
		if (lock_acqs[i] != lock_rels[i]) {
			_error("lock_acqs[%d] = %ln", i, lock_acqs);
			_error("lock_rels[%d] = %ln", i, lock_rels);
			return -1;
		}
		node_sum += lock_acqs[i];
	}
	for (uint32_t i = 0; i < threadNR; i++) {
		task_sum += tasks[i].lock_acqs;
	}
	assert(task_sum == node_sum && "TASK_SUM != NODE_SUM");
	if (task_sum != node_sum) {
		_error("task_sum = %lu", task_sum);
		_error("node_sum = %lu", node_sum);
		return -1;
	}
	dsm->get_DSMKeeper()->memFetchAndAdd(ck.c_str(), ck.size(), task_sum);
	return 0;
}

Rlock::Rlock(DSM *dsm, uint32_t lockNR) : dsm(dsm), lockNR(lockNR) {
	for (int i = 0; i < dsm->getClusterSize(); ++i) {
		local_locks[i] = new LocalLockNode[lockNR];
		for (size_t k = 0; k < lockNR; ++k) {
			auto &n = local_locks[i][k];
			n.ticket_lock.store(0);
			n.hand_over = false;
			n.hand_time = 0;
		}
	}
}

GlobalAddress Rlock::get_lock_addr(GlobalAddress base_addr) {
	uint64_t lock_index =
		CityHash64((char *)&base_addr, sizeof(base_addr)) % lockNR;

	GlobalAddress lock_addr;
	lock_addr.nodeID = base_addr.nodeID;
	lock_addr.offset = lock_index * sizeof(uint64_t);
	return lock_addr;
}

// TODO: MAKE SENSE OF THIS!? HOW DOES THIS BUF BUIS WORK?
void Rlock::get_bufs() {
	auto rbuf = dsm->get_rbuf(0);
	curr_cas_buffer = rbuf.get_cas_buffer();
	curr_page_buffer = rbuf.get_page_buffer();
}

void Rlock::lock_acquire(GlobalAddress base_addr, int data_size) {
	curr_lock_addr = get_lock_addr(base_addr);

	get_bufs();
	auto tag = dsm->getThreadTag();
	assert(tag != 0);

	try_lock_addr(curr_lock_addr, tag, curr_cas_buffer, NULL, 0);
	if (data_size > 0) {
		dsm->read_sync(curr_page_buffer, base_addr, data_size, NULL);
	}
}

// TODO: ASYNC WRITE BACK?!
void Rlock::lock_release(GlobalAddress base_addr, int data_size) {
	auto tag = dsm->getThreadTag();
	assert(tag != 0);
	write_and_unlock(curr_page_buffer, base_addr, data_size, curr_cas_buffer,
					curr_lock_addr, tag, NULL, 0, false);

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
	int idx = 0;
	if (page_size > 0) {
		rs[0].source = (uint64_t)page_buffer;
		rs[0].dest = page_addr;
		rs[0].size = page_size;
		rs[0].is_on_chip = false;
		idx++;
	}

	rs[idx].source = (uint64_t)dsm->get_rbuf(coro_id).get_cas_buffer();
	rs[idx].dest = lock_addr;
	rs[idx].size = sizeof(uint64_t);
	rs[idx].is_on_chip = true;
	idx++;

	*(uint64_t *)rs[idx].source = 0;
	if (async) {
		dsm->write_batch(rs, idx, false);
	} else {
		dsm->write_batch_sync(rs, idx, cxt);
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