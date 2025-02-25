using namespace std;

#include "Tree.h"
#include "mb_utils.h"
#include <iostream>
#include <string>
#include <cstdlib>
#include <unistd.h>
#include <regex>
#include <fstream>
#include <vector>
#include <sstream>
#include <stdexcept>

extern thread_local Timer timer;
// extern thread_local int Tree::threadID;

extern Measurements measurements;

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
    int *nodeID, int* duration, int* mode, int* use_zipfan, 
	int* kReadRatio, int* pinning, int* chipSize, uint64_t* dsmSize,
    char **res_file_tp, char **res_file_lat, char **res_file_lock,
    int argc, char **argv
) {
    int option;
	*nodeID = getNodeNumber();
	while ((option = getopt(argc, argv,
    "d:t:l:i:d:s:m:r:f:g:n:z:w:p:c:y:h:")) != -1) 
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
			case 'h':
                *res_file_lock = optarg; 
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
			case 'z':
				*use_zipfan = atoi(optarg);
				break;
			case 'w':
				*kReadRatio = atoi(optarg);
				break;
			case 'p':
				*pinning = atoi(optarg);
				break;
			case 'c':
				*chipSize = atoi(optarg);
			case 'y':
				*dsmSize = atoi(optarg);
			default:
				break;
		}
	}
}

void clear_measurements(int lockNR) {
	memset(measurements.lock_hold, 0, MAX_APP_THREAD * LATENCY_WINDOWS * sizeof(uint16_t));
	memset(measurements.lwait_acq, 0, MAX_APP_THREAD * LWAIT_WINDOWS * sizeof(uint16_t));
	memset(measurements.lwait_rel, 0, MAX_APP_THREAD * LATENCY_WINDOWS * sizeof(uint16_t));
	memset(measurements.gwait_acq, 0, MAX_APP_THREAD * LWAIT_WINDOWS * sizeof(uint16_t));
	memset(measurements.gwait_rel, 0, MAX_APP_THREAD * LATENCY_WINDOWS * sizeof(uint16_t));
	memset(measurements.data_read, 0, MAX_APP_THREAD * LATENCY_WINDOWS * sizeof(uint16_t));
	memset(measurements.data_write, 0, MAX_APP_THREAD * LATENCY_WINDOWS * sizeof(uint16_t));
	memset(measurements.end_to_end, 0, MAX_APP_THREAD * LATENCY_WINDOWS * sizeof(uint16_t));
	memset(measurements.lock_acqs, 0, MAX_MACHINE * lockNR * sizeof(uint32_t));
	for (int i = 0; i < MAX_APP_THREAD; i++) {
		measurements.handovers[i] = 0;
		measurements.handovers_data[i] = 0;
		measurements.glock_tries[i] = 0;
		measurements.tp[i] = 0;
		measurements.loop_in_cs[i] = 0;
	}
}

void free_measurements() {
	free(measurements.data_read);
	free(measurements.data_write);
	free(measurements.lwait_acq);
	free(measurements.lwait_rel);
	free(measurements.gwait_acq);
	free(measurements.gwait_rel);
	free(measurements.lock_hold);
	free(measurements.lock_acqs);
}

uint64_t* cal_latency(uint16_t *latency, const string measurement, int lw = LATENCY_WINDOWS, uint64_t factor = 1.0) {
	// uint16_t latency_th_all[lw]
	uint32_t* latency_th_all = (uint32_t *) malloc(lw*sizeof(uint32_t));
	uint64_t all_lat = 0;
	for (int i = 0; i < lw; ++i) {
		latency_th_all[i] = 0;
		for (int k = 0; k < MAX_APP_THREAD; ++k) {
			latency_th_all[i] += latency[k*lw+i];
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
	for (int i = 0; i < lw; ++i) {
		cum += latency_th_all[i];

		if (cum >= th50) {
			// DE("%s : p50 %f\t", measurement.c_str(), i / 10.0);
			th50 = -1;
			lats[0] = i / factor;
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
			lats[1] = i / factor;
			break;
		}
	}
	free(latency_th_all);
	return lats;
}

void save_measurement(int threadID, uint16_t *arr, int factor, bool is_lwait) {
	auto us_10 = timer.end();
	uint64_t lw = is_lwait ? LWAIT_WINDOWS : LATENCY_WINDOWS;
	if (us_10 >= 1000 && is_lwait) {
		us_10 = 1000 + us_10 / 1000;
    }
	// TODO:
	// IN PLOT.PY MULTIPLY ALL VALUES >= 1000 by 1000 and subtract 1000!
	// PLEASE DONT HAPPEN
	if (us_10 >= lw) {
		us_10 = lw - 1;
	}
    arr[threadID*lw + us_10]++;
}

std::vector<std::vector<uint32_t>> readExistingData(char *path, int lockNR) {
    std::vector<std::vector<uint32_t>> data(MAX_MACHINE, std::vector<uint32_t>(lockNR, 0));
    std::ifstream file(path);

    if (!file.is_open()) {
		return data;
    }

    std::string line;
    int row = 0;
    while (std::getline(file, line) && row < MAX_MACHINE) {
        std::stringstream ss(line);
        std::string cell;
        int col = 0;

        while (std::getline(ss, cell, ',') && col < lockNR) {
			try {
				data[row][col] = std::stol(cell);
			} catch (const std::invalid_argument& e) {
				fprintf(stderr, "caught invalid lock acqs line\n");
			}
            col++;
        }
        row++;
    }

    file.close();
    return data;
}

void writeData(char *path, const std::vector<std::vector<uint32_t>>& data) {
    std::ofstream file(path, std::ios::trunc);

    for (const auto& row : data) {
        for (size_t i = 0; i < row.size(); ++i) {
            file << row[i];
            if (i < row.size() - 1) file << ",";
        }
        file << "\n";
    }

	file.flush();
    file.close();
	sleep(2);
}

void write_tp(char* tp_path, char* lock_path, int run, int threadNR, int lockNR, int nodeID, size_t array_size) {
	std::ofstream file(tp_path, std::ios::app);
	uint64_t total_handovers = 0;
	uint64_t total_Hod = 0;
	if (!file)
		__error("Failed to open %s\n", tp_path);
	for (int t = 0; t < threadNR; t++) {
		total_handovers += measurements.handovers[t];
		total_Hod += measurements.handovers_data[t];
		file << std::setfill('0') << std::setw(3) << t << ","
			<< std::setw(8) << measurements.loop_in_cs[t] << ","
			<< std::setw(8) << measurements.tp[t] << ","
			<< std::setw(3) << measurements.duration << ","
			<< std::setw(8) << measurements.glock_tries[t] << ","
			<< std::setw(8) << measurements.handovers[t] << ","
			<< std::setw(8) << measurements.handovers_data[t] << ","
			<< std::setw(6) << array_size << ","
			<< std::setw(3) << nodeID << ","
			<< std::setw(3) << run << ","
			<< std::setw(8) << lockNR << "\n";
	}

	file.flush();
    file.close();	
	DEB("TOTAL HANDOVERS: %lu\n", total_handovers);
	DEB("TOTAL DATA_HANDOVERS: %lu\n", total_Hod);

	std::vector<std::vector<uint32_t>> data = readExistingData(lock_path, lockNR);
	for (int m = 0; m < MAX_MACHINE; m++) {
		for (int i = 0; i < lockNR; ++i) {
			data[m][i] += measurements.lock_acqs[m * lockNR + i];
		}

	}
    writeData(lock_path, data);
}

// in us
void write_lat(char* res_file, int run, int lockNR, int nodeID, size_t array_size) {
	std::ofstream file(res_file, std::ios::app);
	if (!file)
		__error("Failed to open %s\n", res_file);

	uint64_t* lock_hold = cal_latency(measurements.lock_hold, "lock_hold");
	uint64_t* lwait_acq = cal_latency(measurements.lwait_acq, "lwait_acq", LWAIT_WINDOWS);
	uint64_t* lwait_rel = cal_latency(measurements.lwait_rel, "lwait_rel");
	uint64_t* gwait_acq = cal_latency(measurements.gwait_acq, "gwait_acq", LWAIT_WINDOWS);
	uint64_t* gwait_rel = cal_latency(measurements.gwait_rel, "gwait_rel");
	uint64_t* data_read = cal_latency(measurements.data_read, "data_read");
	uint64_t* data_write = cal_latency(measurements.data_write, "data_write");
	uint64_t* end_to_end = cal_latency(measurements.end_to_end, "end_to_end", LATENCY_WINDOWS, 10);

	for (int i = 0; i < LATNR; i++) {
		file << std::setfill('0')
			<< std::setw(7) << lock_hold[i] << ","
			<< std::setw(7) << lwait_acq[i] << ","
			<< std::setw(7) << lwait_rel[i] << ","
			<< std::setw(7) << gwait_acq[i] << ","
			<< std::setw(7) << gwait_rel[i] << ","
			<< std::setw(7) << data_read[i] << ","
			<< std::setw(7) << data_write[i] << ","
			<< std::setw(7) << end_to_end[i] << ","
			<< std::setw(6) << array_size << ","
			<< std::setw(3) << nodeID << ","
			<< std::setw(3) << run << ","
			<< std::setw(4) << lockNR << "\n";
	}
	file.close();
}

int check_MN_correctness(DSM *dsm, size_t dsmSize, int mnNR, int nodeNR, int nodeID, uint64_t page_size) {
	uint64_t datasum = 0;
	uint64_t cn_sum = 0;
	uint64_t cn_inc = 0;
	for (int i = 0; i < nodeNR; i++) {
		// if (i == nodeID)
		// 	continue;
		string key = "CORRECTNESS" + to_string(i);
		char *cn_sum_ptr = dsm->get_DSMKeeper()->memGet(key.c_str(), key.size());
		uint64_t cn_sum_;
		memcpy(&cn_sum_, cn_sum_ptr, sizeof(uint64_t));
		DEB("%ld LOCK_ACQS FROM NODE %d\n", cn_sum_, i);
		cn_sum = cn_sum + cn_sum_;

		string key_inc = "CORRECTNESS_INC" + to_string(i);
		char *cn_inc_ptr = dsm->get_DSMKeeper()->memGet(key_inc.c_str(), key_inc.size());
		uint64_t cn_inc_;
		memcpy(&cn_inc_, cn_inc_ptr, sizeof(uint64_t));
		DEB("%ld INCREMENTS FROM NODE %d\n", cn_inc_, i);
		cn_inc = cn_inc + cn_inc_;

		string key_datasum = "DATASUM" + to_string(i);
		char *datasum_ptr = dsm->get_DSMKeeper()->memGet(key_datasum.c_str(), key_datasum.size());
		uint64_t datasum_;
		memcpy(&datasum_, datasum_ptr, sizeof(uint64_t));
		DEB("%ld DATASUM FROM NODE %d\n", datasum_, i);
		datasum = datasum + datasum_;
	}

	if (datasum != cn_inc) {
		__error("datasum = %lu", datasum);
		__error("cn_inc = %lu", cn_inc);
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
	uint64_t node_inc_sum = 0;
	uint64_t datasum = 0;
	int ret = 0;
	for (int m = 0; m < dsm->getClusterSize(); m++) {
		for (uint32_t i = 0; i < lockNR; i++) {
			if (lock_acqs[m * lockNR + i] != lock_rels[m * lockNR + i]) {
				__error("lock_acqs[%d][%d] = %ld", m, i, lock_acqs[m * lockNR + i]);
				__error("lock_rels[%d][%d] = %ld", m, i, lock_rels[m * lockNR + i]);
				ret = 1;
			}
			node_sum += lock_acqs[m * lockNR + i];
		}
	}
	for (uint32_t i = 0; i < threadNR; i++) {
		task_sum += tasks[i].lock_acqs;
		node_inc_sum += tasks[i].inc;
	}
	if (task_sum != node_sum) {
		__error("task_sum = %lu", task_sum);
		__error("node_sum = %lu", node_sum);
		ret = 2;
	}

	uint64_t baseAddr = dsm->get_baseAddr();
	uint64_t *long_data = (uint64_t *) baseAddr;

	for (size_t i = 0; i < GB(dsm->getConf().dsmSize) / sizeof(uint64_t); i++) {
		datasum += long_data[i];
	}

	string key = "CORRECTNESS" + to_string(nodeID);
    char val[sizeof(uint64_t)];
    memcpy(val, &task_sum, sizeof(uint64_t));
    dsm->get_DSMKeeper()->memSet(key.c_str(), key.size(), val, sizeof(uint64_t));

	string key_inc = "CORRECTNESS_INC" + to_string(nodeID);
    char val_inc[sizeof(uint64_t)];
    memcpy(val_inc, &node_inc_sum, sizeof(uint64_t));
    dsm->get_DSMKeeper()->memSet(key_inc.c_str(), key_inc.size(), val_inc, sizeof(uint64_t));

	string key_datasum = "DATASUM" + to_string(nodeID);
    char val_datasum[sizeof(uint64_t)];
    memcpy(val_datasum, &datasum, sizeof(uint64_t));
    dsm->get_DSMKeeper()->memSet(key_datasum.c_str(), key_datasum.size(), val_datasum, sizeof(uint64_t));

	return ret;
}