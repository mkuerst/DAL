#ifndef __MB_UTILS_H__
#define __MB_UTILS_H__

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

struct alignas(CACHELINE_SIZE) Task {
    pthread_t thread;
    char disa = 'y';
    uint64_t lock_acqs = 0;
    uint64_t inc = 0;

    // MISC
    int id;
    int private_int_array[PRIVATE_ARRAY_SZ / sizeof(int)];
};


int uniform_rand_int(int x);

void clear_measurements();

void write_tp(char* res_file, int run, int threadNR, int lockNR, int nodeID, size_t array_size);

void write_lat(char* res_file, int run, int lockNR, int nodeID, size_t array_size);

void save_measurement(int threadID, uint16_t *arr, int factor = 1, bool is_lwait = false);

int check_MN_correctness(DSM *dsm, size_t dsmSize, int mnNR, int nodeNR, int nodeID, uint64_t page_size = 1);

int check_CN_correctness(
	Task* tasks, uint64_t *lock_acqs, uint64_t *lock_rels,
	uint32_t lockNR, uint32_t threadNR, DSM *dsm, int nodeID);

int getNodeNumber();

void parse_cli_args(
    int *threadNR, int *nodeNR, int* mnNR, int *lockNR, int *runNR,
    int *nodeID, int* duration, int* mode, int* use_zipfan, int* kReadRatio,
    char **res_file_tp, char **res_file_lat,
    int argc, char **argv
);

void free_measurements();

#endif /* __MB_UTILS_H__ */