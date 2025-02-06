#ifndef __MB_UTILS_H__
#define __MB_UTILS_H__

using namespace std;
#include <pthread.h>
#include <string>

#ifndef CACHELINE_SIZE
#define CACHELINE_SIZE 64
#endif

#ifndef HOST_NAME_MAX
#define HOST_NAME_MAX 256 
#endif

#define KB(x) ((x) * 1024L)
#define MB(x) (KB(x) * 1024L)
#define GB(x) (MB(x) * 1024L)

#define MAX_ARRAY_SIZE  KB(512) 
#define PRIVATE_ARRAY_SZ KB(256) 

static string ck = "CORRECTNESS";

#ifdef DE
#undef DE
#define DE(msg, ...) do {\
    char host[HOST_NAME_MAX];\
    gethostname(host, sizeof(host));\
    fprintf(stderr, "%s : %s : %d : " msg, host, __FILE__, __LINE__, ##__VA_ARGS__);\
} while(0)

#define debug(msg, ...) do {\
    char host[HOST_NAME_MAX];\
    gethostname(host, sizeof(host));\
    fprintf(stderr, "%s : %s : %d : " msg, host, __FILE__, __LINE__, ##__VA_ARGS__);\
} while(0)
#else
#define DE(...)
#define debug(...)
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

#include "DSM.h"
#include <atomic>
#include <city.h>
#include <functional>
#include <iostream>

struct LocalLockNode {
  std::atomic<uint64_t> ticket_lock;
  bool hand_over;
  uint8_t hand_time;
};

class Rlock {
public:
    Rlock(DSM *dsm, uint32_t lockNR);

    void index_cache_statistics();
    void clear_statistics();

    void lock_acquire(GlobalAddress base_addr, int data_size);
    void lock_release(GlobalAddress base_addr, int data_size);
    char *getCurrPB() {return curr_page_buffer;}

private:
    DSM *dsm;
    uint32_t lockNR;
    LocalLockNode *local_locks[MAX_MACHINE];
    static thread_local uint64_t *curr_cas_buffer;
    static thread_local char *curr_page_buffer;
    static thread_local GlobalAddress curr_lock_addr;

    GlobalAddress get_lock_addr(GlobalAddress base_addr);
    void get_bufs();
    bool try_lock_addr(GlobalAddress lock_addr, uint64_t tag, uint64_t *buf,
                        CoroContext *cxt, int coro_id);
    void unlock_addr(GlobalAddress lock_addr, uint64_t tag, uint64_t *buf,
                    CoroContext *cxt, int coro_id, bool async);
    void write_and_unlock(char *page_buffer, GlobalAddress page_addr,
                                 int page_size, uint64_t *cas_buffer,
                                 GlobalAddress lock_addr, uint64_t tag,
                                 CoroContext *cxt, int coro_id, bool async);
    void lock_and_read_page(char *page_buffer, GlobalAddress page_addr,
                            int page_size, uint64_t *cas_buffer,
                            GlobalAddress lock_addr, uint64_t tag,
                            CoroContext *cxt, int coro_id);
    bool acquire_local_lock(GlobalAddress lock_addr, CoroContext *cxt,
                            int coro_id);
    bool can_hand_over(GlobalAddress lock_addr);
    void releases_local_lock(GlobalAddress lock_addr);
};

struct alignas(CACHELINE_SIZE) Task {
    volatile int* stop;
    pthread_t thread;
    char disa;
    char* byte_data;
    int* int_data;
    uint64_t lock_acqs = 0;

    // MISC
    int id, run, idx;
    int private_int_array[PRIVATE_ARRAY_SZ / sizeof(int)];
};

int uniform_rand_int(int x);

int check_MN_correctness(DSM *dsm, size_t dsmSize, int mnNR, int nodeNR, int node_id);

int check_CN_correctness(
	Task* tasks, uint64_t *lock_acqs, uint64_t *lock_rels,
	uint32_t lockNR, uint32_t threadNR, DSM *dsm, int node_id);

int getNodeNumber();

void parse_cli_args(
    int *threadNR, int *nodeNR, int* mnNR, int *lockNR, int *runNR,
    int *node_id, int* duration, int* mode,
    char **res_file_cum, char **res_file_single,
    int argc, char **argv
);

#endif /* __MB_UTILS_H__ */