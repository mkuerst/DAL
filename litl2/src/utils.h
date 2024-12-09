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
#include <malloc.h>
#include <padding.h>
#include <stdint.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <unistd.h>
#include <infiniband/verbs.h>

#include <sched.h>
#include <numa.h>

#ifndef __UTILS_H__
#define __UTILS_H__
#include <topology.h>


#define MAX_THREADS 2048
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
#define DEBUG(...)                        fprintf(stderr, ## __VA_ARGS__)
#else
#define DEBUG(...)
#endif

// #define DEBUG_PTHREAD(...)                        fprintf(stderr, ## __VA_ARGS__)
#define DEBUG_PTHREAD(...)

// MICROBENCH PARAMS
/**************************************************************************************/
#define NUM_RUNS 2
#ifndef CACHELINE_SIZE
#define CACHELINE_SIZE 64
#endif

#define KB(x) ((x) * 1024L)
#define MB(x) (KB(x) * 1024L)
#define GB(x) (MB(x) * 1024L)
// CACHE: L1: 512 KiB (x16 instances) | L2: 4 MiB (x16 instances) | L3: 40 MiB (x2 instances)
// 256 KiB -*8> 2 MiB -*8> 16 -*4>
#define MAX_ARRAY_SIZE MB(64L)
#define NUM_MEM_RUNS 3 
#define NUM_LAT_RUNS 10

extern size_t array_sizes[NUM_MEM_RUNS];
/**************************************************************************************/

#define _error(msg, args...) do {\
	fprintf(stderr, "\033[1;31m%s : %d : ERROR : \033[0m"msg, __FILE__, __LINE__, ## args);\
	fprintf(stderr, "\n");\
	exit(EXIT_FAILURE);\
}while(0);


typedef unsigned long long ull;
// typedef struct {
//     volatile int *stop;
//     volatile ull *global_its;
//     pthread_t thread;
//     int rdma;
//     int priority;
//     int id;
//     double cs;
//     char* server_ip;
//     int sockfd;
//     // outputs
//     ull loop_in_cs[NUM_RUNS];
//     ull lock_acquires[NUM_RUNS];
//     ull lock_hold[NUM_RUNS];
//     ull mem_duration[NUM_RUNS];
//     size_t array_size[NUM_RUNS];
// } task_t __attribute__ ((aligned (CACHELINE_SIZE)));

typedef struct {
    volatile int *stop;
    volatile ull *global_its;
    pthread_t thread;
    int rdma;
    int priority;
    int id;
    double cs;
    char* server_ip;
    int sockfd;
    // outputs
    ull duration[NUM_RUNS][NUM_MEM_RUNS];
    ull loop_in_cs[NUM_RUNS][NUM_MEM_RUNS];
    ull lock_acquires[NUM_RUNS][NUM_MEM_RUNS];
    ull lock_hold[NUM_RUNS][NUM_MEM_RUNS];
    ull wait_acq[NUM_RUNS][NUM_MEM_RUNS];
    ull wait_rel[NUM_RUNS][NUM_MEM_RUNS];
    // LATENCY MEASUREMENTS
    ull lat_lock_hold[NUM_RUNS][NUM_LAT_RUNS];
    ull lat_wait_acq[NUM_RUNS][NUM_LAT_RUNS];
    ull lat_wait_rel[NUM_RUNS][NUM_LAT_RUNS];
    size_t array_size[NUM_RUNS][NUM_MEM_RUNS];
} task_t __attribute__ ((aligned (CACHELINE_SIZE)));

typedef struct thread_data {
    pthread_t thread;
    unsigned int server_tid;
    unsigned int client_tid;
    int sockfd;
    int mode;
    ull lock_impl_time[NUM_RUNS][NUM_LAT_RUNS];
} thread_data;

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

typedef struct rdma_connection {
	struct rdma_cm_id *cm_client_id;
    struct ibv_qp *qp;
    struct ibv_cq *cq;
    struct ibv_pd *pd;
    struct ibv_comp_channel *io_comp_chan;
    struct ibv_mr *client_mr;
    struct ibv_mr *server_mr;
    struct rdma_buffer_attr client_metadata_attr;
    struct rdma_buffer_attr server_metadata_attr;
    struct ibv_recv_wr client_recv_wr;
    struct ibv_recv_wr *bad_client_recv_wr;
    struct ibv_send_wr server_send_wr; 
    struct ibv_send_wr *bad_server_send_wr;
    struct ibv_sge client_recv_sge; 
    struct ibv_sge server_send_sge;
} rdma_connection;

typedef struct rdma_thread {
    pthread_t thread;
    int rdma;
    unsigned int server_tid;
    unsigned int client_tid;
    ull lock_impl_time[NUM_RUNS];
    rdma_connection connection;
} rdma_thread;

void *alloc_cache_align(size_t n);

int pin_thread(unsigned int id);

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

static inline uint64_t rdtsc(void) {
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
#define getticks rdtsc
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
#endif
