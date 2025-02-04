/*
 * The MIT License (MIT)
 *
 * Copyright (c) 2016 Hugo Guiroux <hugo.guiroux at gmail dot com>
 *               2013 Tudor David
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of his software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */
#ifndef __UTILS_H__
#define __UTILS_H__

#ifdef __cplusplus
#include <atomic>
using namespace std;
extern "C" {
#endif

#include <stdatomic.h>
#include <malloc.h>
#include <padding.h>
#include <stdint.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <unistd.h>
#include <sys/time.h>
#include <infiniband/verbs.h>
#include <rdma_cma.h>
#include <stdbool.h>
#include <limits.h>

#include <sched.h>
#include <numa.h>

#include <topology.h>

#define MAX_IP_LENGTH 16

#define CPU_PAUSE() asm volatile("pause\n" : : : "memory")
#define COMPILER_BARRIER() asm volatile("" : : : "memory")
#define MEMORY_BARRIER() __sync_synchronize()
#define REP_VAL 23

#define OPTERON_OPTIMIZE 1
#ifdef OPTERON_OPTIMIZE
#define PREFETCHW(x) asm volatile("prefetchw %0" ::"m"(*(unsigned long *)x))
#else
#define PREFETCHW(x)
#endif

#ifdef UNUSED
#elif defined(__GNUC__)
#define UNUSED(x) UNUSED_##x __attribute__((unused))
#elif defined(__LCLINT__)
#define UNUSED(x) /*@unused@*/ x
#else
#define UNUSED(x) x
#endif


#ifdef DEBUG
#undef DEBUG
#define DEBUG(msg, args...) do {\
    char host[HOST_NAME_MAX];\
    gethostname(host, sizeof(host));\
	fprintf(stderr, "%s : %s : %d : "msg, host, __FILE__, __LINE__, ## args);\
}while(0);
#define debug(msg, args...) do {\
    char host[HOST_NAME_MAX];\
    gethostname(host, sizeof(host));\
	fprintf(stderr, "%s : %s : %d : "msg, host, __FILE__, __LINE__, ## args);\
}while(0);
#else
#define DEBUG(...)
#define debug(msg, args...)
#endif

// #define DEBUG_PTHREAD(...)                        fprintf(stderr, ## __VA_ARGS__)
#define DEBUG_PTHREAD(...)

#define _error(msg, args...) do {\
    char host[HOST_NAME_MAX];\
    gethostname(host, sizeof(host));\
	fprintf(stderr, "\033[1;31m%s : %s : %d : ERROR : \033[0m"msg, host, __FILE__, __LINE__, ## args);\
	fprintf(stderr, "\n");\
	exit(EXIT_FAILURE);\
}while(0);

#define __error(msg, args...) do {\
    char host[HOST_NAME_MAX];\
    gethostname(host, sizeof(host));\
	fprintf(stderr, "\033[1;31m%s : %s : %d : ERROR : \033[0m"msg, host, __FILE__, __LINE__, ## args);\
	fprintf(stderr, "\n");\
}while(0);

// MICROBENCH PARAMS
/**************************************************************************************/
#ifndef CACHELINE_SIZE
#define CACHELINE_SIZE 64
#endif

#define DEFAULT_PORT 20053

#define KB(x) ((x) * 1024L)
#define MB(x) (KB(x) * 1024L)
#define GB(x) (MB(x) * 1024L)
// CACHE: L1: 512 KiB (x16 instances) | L2: 4 MiB (x16 instances) | L3: 40 MiB (x2 instances)
// 256 KiB -*8> 2 MiB -*8> 16 -*4>
#define MAX_ARRAY_SIZE  KB(512) 
#define PRIVATE_ARRAY_SZ KB(256) 

#define BO (1 << 9)
#define MAX_BO ((1 << 24) - 1)

#define MAX_THREADS 128
#define MAX_CLIENTS 12
#define MAX_MEASUREMENTS 10
#define NUM_MEASUREMENTS 10
#define IDX_NONCYCLE_MEASURES 9
#define NUM_STATS 3

#define NUM_MEM_RUNS 1

#define LOCKS_PER_MEMRUN MAX_ARRAY_SIZE
#define CYCLES_11 1200L
#define CYCLES_12 2400L
#define CYCLES_MAX 3200L

#define RLOCK_SIZE 8
#define MAX_LOCK_NUM MAX_ARRAY_SIZE / CACHELINE_SIZE
#define THREADS_PER_CLIENT 32
extern size_t array_sizes[NUM_MEM_RUNS];
/**************************************************************************************/

typedef unsigned long long ull;
typedef long int li;

typedef struct rdma_connection {
	struct rdma_cm_id *cm_client_id;
    struct ibv_qp *qp;
    struct ibv_cq *cq;
    struct ibv_pd *pd;
    struct ibv_wc wc[2];
    struct ibv_comp_channel *io_comp_chan;

    struct ibv_mr *rlock_mr;
    struct ibv_mr *data_mr;
    struct ibv_mr *server_mr;

    struct ibv_recv_wr rlock_wr;
    struct ibv_recv_wr data_wr;

    struct ibv_sge rlock_sge; 
    struct ibv_sge data_sge; 
} rdma_connection;

typedef struct {
	uint64_t rlock_addr;
	uint32_t rlock_rkey;
	uint64_t data_addr;
	uint32_t data_rkey;
    uint64_t *unlock_val;
    uint64_t *cas_result[THREADS_PER_CLIENT];
    char *data[THREADS_PER_CLIENT];

	uint64_t peer_data_addrs[MAX_CLIENTS];
	uint32_t peer_data_rkeys[MAX_CLIENTS];
	uint64_t peer_cas_addrs[MAX_CLIENTS];
	uint32_t peer_cas_rkeys[MAX_CLIENTS];

    struct ibv_qp **qp;
    struct ibv_comp_channel **io_comp_chan;
    struct ibv_wc *wc;
	struct ibv_send_wr *cas_wr, **bad_wr, *w_wr, *data_wr;
    struct ibv_sge *cas_sge, *w_sge, *data_sge;

    struct ibv_qp *peer_qps[MAX_CLIENTS][THREADS_PER_CLIENT];
    struct ibv_comp_channel *peer_io_chans[MAX_CLIENTS][THREADS_PER_CLIENT];
} rdma_client_meta;

typedef struct {
    volatile int *stop;
    volatile ull *global_its;
    pthread_t thread;
    int priority, id, sockfd, client_id, nlocks, nthreads, nclients;
    double cs;
    char* server_ip;
    char disa;
    char *byte_data;
    int *int_data;
    rdma_client_meta* client_meta;

    // MEASUREMENTS CUM
    ull *loop_in_cs;
    ull *lock_acquires;
    ull *lock_hold;
    ull *wait_acq;
    ull *wait_rel;
    ull *lwait_acq;
    ull *lwait_rel;
    ull *gwait_acq;
    ull *gwait_rel;
    ull *glock_tries;
    ull *data_read;
    ull *data_write;
    size_t *array_size;
    ull duration, cnt;

    // MISC
    int run, idx;
    int private_int_array[PRIVATE_ARRAY_SZ / sizeof(int)];

    // MEASUREMENTS SINGLE 
    ull slock_hold[MAX_MEASUREMENTS];
    ull swait_acq[MAX_MEASUREMENTS];
    ull swait_rel[MAX_MEASUREMENTS];
    ull slwait_acq[MAX_MEASUREMENTS];
    ull slwait_rel[MAX_MEASUREMENTS];
    ull sgwait_acq[MAX_MEASUREMENTS];
    ull sgwait_rel[MAX_MEASUREMENTS];
    ull sdata_read[MAX_MEASUREMENTS];
    ull sdata_write[MAX_MEASUREMENTS];
    ull sglock_tries[MAX_MEASUREMENTS];
    ull sloop_in_cs[MAX_MEASUREMENTS];

} task_t __attribute__ ((aligned (CACHELINE_SIZE)));


typedef struct rdma_server_meta {
    int id;
    ull *lock_impl_time;
    rdma_connection *connections;
} rdma_server_meta;


// TODO: ADAPT to removal of num_runs / num_snd_runs
typedef struct thread_data {
    pthread_t thread;
    unsigned int server_tid;
    unsigned int client_tid;
    int task_id;
    int sockfd;
    int mode;
    ull *wait_acq;
    ull *wait_rel;
} thread_data;

// TODO: same
typedef struct client_data {
    pthread_t thread;
    int id;
    int sockfd;
    int mode;
    ull wait_acq[MAX_THREADS];
    ull wait_rel[MAX_THREADS];
} client_data;

typedef struct {
    pthread_mutex_t mutex;
    uint64_t ow_safety0, ow_safety1;
    char disa;
    uint64_t rlock_addr, data_addr;
    int id;
    int offset;
    size_t elem_sz;
    size_t data_len;
    volatile atomic_int other __attribute__((aligned (CACHELINE_SIZE))); 
    volatile atomic_int turns __attribute__((aligned (CACHELINE_SIZE)));
    int *int_data;
    char *byte_data;
} disa_mutex_t __attribute__ ((aligned (CACHELINE_SIZE)));

/* 
 * We use attribute so that compiler does not step in and try to pad the structure.
 * We use this structure to exchange information between the server and the client. 
 *
 * For details see: http://gcc.gnu.org/onlinedocs/gcc/Type-Attributes.html
 */
struct __attribute((packed)) rdma_buffer_attr {
    uint64_t address;
    uint32_t length;
    union stag {
        /* if we send, we call it local stags */
        uint32_t local_stag;
        /* if we receive, we call it remote stag */
        uint32_t remote_stag;
    }stag;
};


/*FUNC DECLARATIONS*/
void *alloc_cache_align(size_t n);

int pin_thread(unsigned int id, int nthreads, int use_nodes);

ull write_res_cum(task_t* tasks, int nthreads, int mode, char* res_file, int num_runs, int snd_runs);

int write_res_single(task_t* tasks, int nthreads, int mode, char* res_file);

int current_numa_node();

int uniform_rand_int(int x);

bool is_power_of_2(int n);

void flush_cache(void *ptr, size_t size);

void log_single(task_t * task, int num_runs);

void allocate_task_mem(task_t *tasks, int num_runs, int num_mem_runs, int nthreads);

int get_addr(char *dst, struct sockaddr *addr);

int check_correctness(ull total_acq, ull* lock_acqs, ull* lock_rels, int nlocks);

void parse_cli_args(
    int *nthreads, int *num_nodes, int* num_mn,
    int *nlocks, int *node_id, int* duration,
    int* mode, int* num_runs, int *num_mem_runs,
    char **res_file_cum, char **res_file_single,
    char **mn_ip, char peer_ips[MAX_CLIENTS][MAX_IP_LENGTH],
    int argc, char **argv
);

int create_sockaddr(char *addr, struct sockaddr_in* sa, uint16_t port);

static inline void *xchg_64(void *ptr, void *x) {
    __asm__ __volatile__("xchgq %0,%1"
                         : "=r"((unsigned long long)x)
                         : "m"(*(volatile long long *)ptr),
                           "0"((unsigned long long)x)
                         : "memory");

    return x;
}

static inline unsigned xchg_32(void *ptr, unsigned x) {
    __asm__ __volatile__("xchgl %0,%1"
                         : "=r"((unsigned)x)
                         : "m"(*(volatile unsigned *)ptr), "0"(x)
                         : "memory");

    return x;
}

// test-and-set uint8_t, from libslock
static inline uint8_t l_tas_uint8(volatile uint8_t *addr) {
    uint8_t oldval;
    __asm__ __volatile__("xchgb %0,%1"
                         : "=q"(oldval), "=m"(*addr)
                         : "0"((unsigned char)0xff), "m"(*addr)
                         : "memory");
    return (uint8_t)oldval;
}

static inline uint64_t rdpmc(unsigned int counter) {
    uint32_t low, high;

    asm volatile("rdpmc" : "=a"(low), "=d"(high) : "c"(counter));

    return low | ((uint64_t)high) << 32;
}

static inline uint64_t rdtscc(void) {
    uint32_t low, high;

    asm volatile("rdtsc" : "=a"(low), "=d"(high));

    return low | ((uint64_t)high) << 32;
}

static __inline__ unsigned long long rdtscp(void)
{
	unsigned hi, lo;
	asm volatile ("rdtscp\n\t"
		  "mov %%edx, %0\n\t"
		  "mov %%eax, %1\n\t"
		  : "=r" (hi), "=r" (lo)
		  :: "%rax", "%rbx", "%rcx", "%rdx");
	return ( (unsigned long long)lo)|( ((unsigned long long)hi)<<32 );
}

// EPFL libslock
#define my_random xorshf96
#define getticks rdtscc
typedef uint64_t ticks;

static inline unsigned long xorshf96(unsigned long *x, unsigned long *y,
                                     unsigned long *z) { // period 2^96-1
    unsigned long t;
    (*x) ^= (*x) << 16;
    (*x) ^= (*x) >> 5;
    (*x) ^= (*x) << 1;

    t    = *x;
    (*x) = *y;
    (*y) = *z;
    (*z) = t ^ (*x) ^ (*y);

    return *z;
}

static inline void cdelay(ticks cycles) {
    ticks __ts_end = getticks() + (ticks)cycles;
    while (getticks() < __ts_end)
        ;
}

static inline unsigned long *seed_rand() {
    unsigned long *seeds;
    int num_seeds = L_CACHE_LINE_SIZE / sizeof(unsigned long);
    if (num_seeds < 3)
        num_seeds = 3;

    seeds    = (unsigned long *)memalign(L_CACHE_LINE_SIZE,
                                         num_seeds * sizeof(unsigned long));
    seeds[0] = getticks() % 123456789;
    seeds[1] = getticks() % 362436069;
    seeds[2] = getticks() % 521288629;
    return seeds;
}

static inline void nop_rep(uint32_t num_reps) {
    uint32_t i;
    for (i = 0; i < num_reps; i++) {
        asm volatile("NOP");
    }
}

static inline void pause_rep(uint32_t num_reps) {
    uint32_t i;
    for (i = 0; i < num_reps; i++) {
        CPU_PAUSE();
        /* PAUSE; */
        /* asm volatile ("NOP"); */
    }
}

static inline void wait_cycles(uint64_t cycles) {
    if (cycles < 256) {
        cycles /= 6;
        while (cycles--) {
            CPU_PAUSE();
        }
    } else {
        ticks _start_ticks = getticks();
        ticks _end_ticks   = _start_ticks + cycles - 130;
        while (getticks() < _end_ticks)
            ;
    }
}

#ifdef __cplusplus
}
#endif

#endif