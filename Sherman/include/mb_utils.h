#ifndef __MB_UTILS_H__
#define __MB_UTILS_H__

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/syscall.h>
#include <unistd.h>
#include <linux/perf_event.h>
#include <fcntl.h>
#include <pthread.h>

#ifndef CACHELINE_SIZE
#define CACHELINE_SIZE 64
#endif

#ifndef HOST_NAME_MAX
#define HOST_NAME_MAX 256 
#endif

#define KB(x) ((x) * 1024L)
#define MB(x) (KB(x) * 1024L)
#define GB(x) (MB(x) * 1024L)

#define MAX_ARRAY_SIZE  GB(1) 
#define PRIVATE_ARRAY_SZ MB(64) 

#define LATNR 2


#ifdef DE
#undef DE
#define DE(msg, ...) do {\
    char host[HOST_NAME_MAX];\
    gethostname(host, sizeof(host));\
    fprintf(stderr, "%s : %s : %d : " msg, host, __FILE__, __LINE__, ##__VA_ARGS__);\
} while(0)
#else
#define DE(...)
#endif

#define _error(msg, ...) do {\
    char host[HOST_NAME_MAX];\
    gethostname(host, sizeof(host));\
    fprintf(stderr, "\033[1;31m%s : %s : %d : ERROR : \033[0m" msg, host, __FILE__, __LINE__, ##__VA_ARGS__);\
    fprintf(stderr, "\n");\
    exit(EXIT_FAILURE);\
} while(0)

#define __error(msg, ...) do {\
    char host[HOST_NAME_MAX];\
    gethostname(host, sizeof(host));\
    fprintf(stderr, "\033[1;31m%s : %s : %d : ERROR : \033[0m" msg, host, __FILE__, __LINE__, ##__VA_ARGS__);\
    fprintf(stderr, "\n");\
} while(0)
using namespace std;

#include <string>
#include "Timer.h"
#include "DSM.h"
#include <atomic>
#include <city.h>
#include <functional>
#include <iostream>

static string ck = "CORRECTNESS";

// struct alignas(CACHELINE_SIZE) Task {
//     pthread_t thread;
//     char disa = 'y';
//     uint64_t lock_acqs = 0;
//     uint64_t inc = 0;

//     // MISC
//     int id;
//     int private_int_array[PRIVATE_ARRAY_SZ / sizeof(int)];
// };
struct Task {
    pthread_t thread;
    char disa = 'y';
    uint64_t lock_acqs = 0;
    uint64_t inc = 0;

    // MISC
    int id;
    int private_int_array[PRIVATE_ARRAY_SZ / sizeof(int)];
};

// SAME NUMA NODES r6525
constexpr int thread_to_cpu_1n[64] = {
    0,  1,  2,  3,  4,  5,  6,  7,
    8,  9, 10, 11, 12, 13, 14, 15,
    16, 17, 18, 19, 20, 21, 22, 23,
    24, 25, 26, 27, 28, 29, 30, 31,
    64, 65, 66, 67, 68, 69, 70, 71,
    72, 73, 74, 75, 76, 77, 78, 79,
    80, 81, 82, 83, 84, 85, 86, 87,
    88, 89, 90, 91, 92, 93, 94, 95
};

// DIFFERENT NUMA NODES r6525
constexpr int thread_to_cpu_2n[64] = {
    0,  32,  1,  33,  2,  34,  3,  35,
    4,  36,  5,  37,  6,  38,  7,  39,
    8,  40,  9,  41, 10,  42, 11,  43,
    12, 44, 13,  45, 14,  46, 15,  47,
    16, 48, 17,  49, 18,  50, 19,  51,
    20, 52, 21,  53, 22,  54, 23,  55,
    24, 56, 25,  57, 26,  58, 27,  59,
    28, 60, 29,  61, 30,  62, 31,  63
};

// // SAME NUMA NODES d7525
// constexpr int thread_to_cpu_1n[32] = {
//     0,  1,  2,  3,  4,  5,  6,  7,
//     8,  9, 10, 11, 12, 13, 14, 15,
//    32, 33, 34, 35, 36, 37, 38, 39,
//    40, 41, 42, 43, 44, 45, 46, 47
// };

// // DIFFERENT NUMA NODES d7525
// constexpr int thread_to_cpu_2n[32] = {
//     0,  1,  2,  3,  4,  5,  6,  7,
//     8,  9, 10, 11, 12, 13, 14, 15,
//    16, 17, 18, 19, 20, 21, 22, 23,
//    24, 25, 26, 27, 28, 29, 30, 31
// };

// // SAME NUMA NODES c6525-25g
// constexpr int thread_to_cpu_1n[32] = {
//     0,  1,  2,  3,  4,  5,  6,  7,
//     8,  9, 10, 11, 12, 13, 14, 15,
//    16, 17, 18, 19, 20, 21, 22, 23,
//    24, 25, 26, 27, 28, 29, 30, 31
// };

// // SAME NUMA NODES c6525-25g
// constexpr int thread_to_cpu_2n[32] = {
//     0,  1,  2,  3,  4,  5,  6,  7,
//     8,  9, 10, 11, 12, 13, 14, 15,
//    16, 17, 18, 19, 20, 21, 22, 23,
//    24, 25, 26, 27, 28, 29, 30, 31
// };

int setup_perf_event(int cpu);

void start_perf_event(int fd);

long long stop_perf_event(int fd);

int uniform_rand_int(int x);

void clear_measurements(int lockNR);

void write_tp(char* tp_path, char* lock_path, int run, int lockNR, int nodeID, size_t array_size, int pinning,
                uint16_t cnNR, uint16_t mnNR, int threadNR, uint16_t maxHandover, int colocate);

void write_lat(char* res_file, int run, int lockNR, int nodeID, size_t array_size, int pinning,
                uint16_t cnNR, uint16_t mnNR, uint16_t threadNR, uint16_t maxHandover, int colocate);

void save_measurement(int threadID, uint16_t *arr, int factor = 1, bool is_lwait = false);

void init_measurements(int lockNR);

int check_MN_correctness(DSM *dsm, size_t dsmSize, int mnNR, int nodeNR, int nodeID, uint64_t page_size = 1);

int check_CN_correctness(
	Task* tasks, uint64_t *lock_acqs, uint64_t *lock_rels,
	uint32_t lockNR, uint32_t threadNR, DSM *dsm, int nodeID);

int getNodeNumber();

void parse_cli_args(
    int *threadNR, int *nodeNR, int* mnNR, int *lockNR, int *runNR,
    int *nodeID, int* duration, int* mode, int* use_zipfan, 
    int* kReadRatio, int* pinning, int *chipSize, uint64_t* dsmSize,
    int* maxHandover, int* colocate,
    char **res_file_tp, char **res_file_lat, char **res_file_lock,
    int argc, char **argv
);

void free_measurements();



void register_sighandler(DSM *m);
void signal_handler(int sig);
void cleanup();
#endif /* __MB_UTILS_H__ */