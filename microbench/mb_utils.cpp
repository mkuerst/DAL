using namespace std;

#include "mb_utils.h"
#include <iostream>
#include <string>
#include <cstdlib>
#include <unistd.h>
#include <regex>
#include <fstream>

thread_local char* Rlock::curr_page_buffer = nullptr;
thread_local uint64_t* Rlock::curr_cas_buffer = nullptr;
thread_local GlobalAddress Rlock::curr_lock_addr;
thread_local Timer timer;
thread_local int threadID;

Measurements measurements;

int uniform_rand_int(int x) {
    return rand() % x;
}

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
    int *nodeID, int* duration, int* mode,
    char **res_file_tp, char **res_file_lat,
    int argc, char **argv
) {
    int option;
	*nodeID = getNodeNumber();
	while ((option = getopt(argc, argv,
    "d:t:l:i:d:s:m:r:f:g:n:")) != -1) 
    {
		switch (option) {
			case 's':
				*mnNR = atoi(optarg);
				break;
			case 'f':
                *res_file_tp = optarg; 
				break;
			case 'g':
                *res_file_lat = optarg; 
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
                *nodeID = atoi(optarg);
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

void set_id(int id) {
	threadID = id;
}

void clear_measurements() {
	int tmp = measurements.duration;
	memset(&measurements, 0, sizeof(Measurements));
	measurements.duration = tmp;
}

uint64_t* cal_latency(uint64_t latency[MAX_APP_THREAD][LATENCY_WINDOWS], const string measurement) {
	uint64_t latency_th_all[LATENCY_WINDOWS];
	uint64_t all_lat = 0;
	for (int i = 0; i < LATENCY_WINDOWS; ++i) {
		latency_th_all[i] = 0;
		for (int k = 0; k < MAX_APP_THREAD; ++k) {
			latency_th_all[i] += latency[k][i];
		}
		all_lat += latency_th_all[i];
	}

	uint64_t th50 = all_lat / 2;
	uint64_t th90 = all_lat * 9 / 10;
	uint64_t th95 = all_lat * 95 / 100;
	uint64_t th99 = all_lat * 99 / 100;
	uint64_t th999 = all_lat * 999 / 1000;

	uint64_t *lats = new uint64_t[2];

	uint64_t cum = 0;
	for (int i = 0; i < LATENCY_WINDOWS; ++i) {
		cum += latency_th_all[i];

		if (cum >= th50) {
			// DE("%s : p50 %f\t", measurement.c_str(), i / 10.0);
			th50 = -1;
			lats[0] = i / 10;
		}
		if (cum >= th90) {
			// DE("%s : p90 %f\t", measurement.c_str(), i / 10.0);
			th90 = -1;
		}
		if (cum >= th95) {
			// DE("%s : p95 %f\t", measurement.c_str(), i / 10.0);
			th95 = -1;
		}
		if (cum >= th99) {
			// DE("%s : p99 %f\t", measurement.c_str(), i / 10.0);
			th99 = -1;
		}
		if (cum >= th999) {
			// DE("%s : p999 %f\n", measurement.c_str(), i / 10.0);
			th999 = -1;
			lats[1] = i / 10;
			break;
		}
	}
	return lats;
}

void save_measurement(uint64_t arr[MAX_APP_THREAD][LATENCY_WINDOWS], int factor) {
	auto us_10 = timer.end() / factor;
    if (us_10 >= LATENCY_WINDOWS) {
      us_10 = LATENCY_WINDOWS - 1;
    }
    arr[threadID][us_10]++;
}

void write_tp(char* res_file, int run, int threadNR, int lockNR, int nodeID, size_t array_size) {
	std::ofstream file(res_file, std::ios::app);
	if (!file)
		__error("Failed to open %s\n", res_file);
	for (int t = 0; t < threadNR; t++) {
				file << std::setfill('0') << std::setw(3) << t << ","
					<< std::setw(8) << measurements.loop_in_cs[t] << ","
					<< std::setw(8) << measurements.lock_acquires[t] << ","
					<< std::setw(8) << measurements.duration << ","
					<< std::setw(8) << measurements.glock_tries[t] << ","
					<< std::setw(6) << array_size << ","
					<< std::setw(3) << nodeID << ","
					<< std::setw(3) << run << ","
					<< std::setw(4) << lockNR << "\n";

	}
    file.close();	
}

// in us
void write_lat(char* res_file, int run, int lockNR, int nodeID, size_t array_size) {
	std::ofstream file(res_file, std::ios::app);
	if (!file)
		__error("Failed to open %s\n", res_file);

	uint64_t* lock_hold = cal_latency(measurements.lock_hold, "lock_hold");
	uint64_t* lwait_acq = cal_latency(measurements.lwait_acq, "lwait_acq");
	uint64_t* lwait_rel = cal_latency(measurements.lwait_rel, "lwait_rel");
	uint64_t* gwait_acq = cal_latency(measurements.gwait_acq, "gwait_acq");
	uint64_t* gwait_rel = cal_latency(measurements.gwait_rel, "gwait_rel");
	uint64_t* data_read = cal_latency(measurements.data_read, "data_read");
	uint64_t* data_write = cal_latency(measurements.data_write, "data_write");

	for (int i = 0; i < LATNR; i++) {
		file << std::setfill('0')
			<< std::setw(6) << lock_hold[i] << ","
			<< std::setw(6) << lwait_acq[i] << ","
			<< std::setw(6) << lwait_rel[i] << ","
			<< std::setw(6) << gwait_acq[i] << ","
			<< std::setw(6) << gwait_rel[i] << ","
			<< std::setw(6) << data_read[i] << ","
			<< std::setw(6) << data_write[i] << ","
			<< std::setw(6) << array_size << ","
			<< std::setw(3) << nodeID << ","
			<< std::setw(3) << run << ","
			<< std::setw(4) << lockNR << "\n";
	}
	file.close();
}

int check_MN_correctness(DSM *dsm, size_t dsmSize, int mnNR, int nodeNR, int nodeID) {
	uint64_t mn_sum = 0;
	uint64_t cn_sum = 0;
	for (int i = 0; i < nodeNR; i++) {
		if (i == nodeID)
			continue;
		string key = "CORRECTNESS" + to_string(i);
		char *cn_sum_ptr = dsm->get_DSMKeeper()->memGet(key.c_str(), key.size());
		uint64_t cn_sum_;
		memcpy(&cn_sum_, cn_sum_ptr, sizeof(uint64_t));
		DE("%ld LOCK_ACQS FROM NODE %d\n", cn_sum_, i);
		cn_sum = cn_sum + cn_sum_;
	}
	uint64_t baseAddr = dsm->get_baseAddr();
	uint64_t *long_data = (uint64_t *) baseAddr;

	for (size_t i = 0; i < GB(dsmSize) / sizeof(uint64_t); i++) {
		mn_sum += long_data[i];
	}
	if (mn_sum != cn_sum) {
		__error("mn_sum = %lu", mn_sum);
		__error("cn_sum = %lu", cn_sum);
		return -1;
	}
	return 0;
}

int check_CN_correctness(
	Task* tasks, uint64_t *lock_acqs, uint64_t *lock_rels,
	uint32_t lockNR, uint32_t threadNR, DSM *dsm, int nodeID) 
{
	uint64_t node_sum = 0;
	uint64_t task_sum = 0;
	for (uint32_t i = 0; i < lockNR; i++) {
		if (lock_acqs[i] != lock_rels[i]) {
			__error("lock_acqs[%d] = %ld", i, lock_acqs[i]);
			__error("lock_rels[%d] = %ld", i, lock_rels[i]);
			return -1;
		}
		node_sum += lock_acqs[i];
	}
	for (uint32_t i = 0; i < threadNR; i++) {
		task_sum += tasks[i].lock_acqs;
	}
	if (task_sum != node_sum) {
		__error("task_sum = %lu", task_sum);
		__error("node_sum = %lu", node_sum);
		return -1;
	}
	string key = "CORRECTNESS" + to_string(nodeID);
    char val[sizeof(uint64_t)];
    memcpy(val, &task_sum, sizeof(uint64_t));
    dsm->get_DSMKeeper()->memSet(key.c_str(), key.size(), val, sizeof(uint64_t));
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

void Rlock::get_bufs() {
	auto rbuf = dsm->get_rbuf(0);
	curr_cas_buffer = rbuf.get_cas_buffer();
	curr_page_buffer = rbuf.get_page_buffer();
}

void Rlock::lock_acquire(GlobalAddress base_addr, int data_size) {
	timer.begin();
	curr_lock_addr = get_lock_addr(base_addr);

	get_bufs();
	auto tag = dsm->getThreadTag();
	assert(tag != 0);

	try_lock_addr(curr_lock_addr, tag, curr_cas_buffer, NULL, 0);
	measurements.lock_acquires[threadID]++;
	timer.begin();
	if (data_size > 0) {
		dsm->read_sync(curr_page_buffer, base_addr, data_size, NULL);
	}
	save_measurement(measurements.data_read);
}

void Rlock::lock_release(GlobalAddress base_addr, int data_size) {
	timer.begin();
	auto tag = dsm->getThreadTag();
	assert(tag != 0);
	if (data_size > 0) {
		write_and_unlock(curr_page_buffer, base_addr, data_size, curr_cas_buffer,
			curr_lock_addr, tag, NULL, 0, false);
	}
	else {
		unlock_addr(curr_lock_addr, tag, curr_cas_buffer, NULL, 0, false);
	}
	}
	
// TODO: ASYNC WRITE BACK?!
inline void Rlock::unlock_addr(GlobalAddress lock_addr, uint64_t tag,
                              uint64_t *buf, CoroContext *cxt, int coro_id,
                              bool async) {

	bool hand_over_other = can_hand_over(lock_addr);
	if (hand_over_other) {
		releases_local_lock(lock_addr);
		save_measurement(measurements.lwait_rel);
		return;
	}

	auto cas_buf = dsm->get_rbuf(coro_id).get_cas_buffer();

	*cas_buf = 0;
	if (async) {
		dsm->write_dm((char *)cas_buf, lock_addr, sizeof(uint64_t), false);
	} else {
		dsm->write_dm_sync((char *)cas_buf, lock_addr, sizeof(uint64_t), cxt);
	}
	save_measurement(measurements.gwait_rel);
	timer.begin();
	releases_local_lock(lock_addr);
	save_measurement(measurements.lwait_rel);
}

void Rlock::write_and_unlock(char *page_buffer, GlobalAddress page_addr,
                                 int page_size, uint64_t *cas_buffer,
                                 GlobalAddress lock_addr, uint64_t tag,
                                 CoroContext *cxt, int coro_id, bool async) {

	bool hand_over_other = can_hand_over(lock_addr);
	if (hand_over_other) {
		dsm->write_sync(page_buffer, page_addr, page_size, cxt);
		save_measurement(measurements.data_write);
		timer.begin();
		releases_local_lock(lock_addr);
		save_measurement(measurements.lwait_rel);
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
	save_measurement(measurements.data_write);
	save_measurement(measurements.gwait_rel);
	timer.begin();
	releases_local_lock(lock_addr);
	save_measurement(measurements.lwait_rel);
}

inline bool Rlock::try_lock_addr(GlobalAddress lock_addr, uint64_t tag,
                                uint64_t *buf, CoroContext *cxt, int coro_id) {

	bool hand_over = acquire_local_lock(lock_addr, cxt, coro_id);
	save_measurement(measurements.lwait_acq, 1000);
	if (hand_over) {
		return true;
	}

	{
		timer.begin();
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
		measurements.glock_tries[threadID] += retry_cnt;
	}
	save_measurement(measurements.gwait_acq);

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