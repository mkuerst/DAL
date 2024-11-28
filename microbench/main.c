#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
// #include <fcntl.h>
#include <stdio.h>
#include <unistd.h>
// #include <pthread.h>
#include <numa.h>
#include <sched.h>
#include <sys/syscall.h>
#include <inttypes.h>
#include <sys/resource.h>
#define gettid() syscall(SYS_gettid)
#include "rdtsc.h"
#include "lock.h"

#ifndef CYCLE_PER_US
#error Must define CYCLE_PER_US for the current machine in the Makefile or elsewhere
#endif

#ifndef CACHELINE_SIZE
#define CACHELINE_SIZE 64
#endif

#ifndef NUMA_NODES
#define NUMA_NODES 1
#endif

typedef unsigned long long ull;
typedef struct {
    volatile int *stop;
    volatile ull *global_its;
    pthread_t thread;
    int priority;
    int id;
    double cs;
    int ncpu;
    int nnodes;
    // outputs
    ull loop_in_cs;
    ull lock_acquires;
    ull lock_hold;
} task_t __attribute__ ((aligned (CACHELINE_SIZE)));

lock_t lock;
// TODO: add barrier before threads start actual lock acquisitions 
pthread_barrier_t global_barrier;
pthread_barrier_t init_barrier;

void *worker(void *arg) {
    int ret;
    task_t *task = (task_t *) arg;
    int node = task->id % task->nnodes;

    if (task->ncpu != 0) {
        cpu_set_t cpuset;
        CPU_ZERO(&cpuset);
        CPU_SET(task->id/2, &cpuset);
        ret = pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &cpuset);
        if (ret != 0) {
            fprintf(stderr, "pthread_set_affinity_np");
            exit(-1);
        }
        if (numa_run_on_node(node) != 0) {
            fprintf(stderr, "numa_run_on_node failed");
            exit(-1);
        }
    }

    // pid_t tid = gettid();
    // ret = setpriority(PRIO_PROCESS, tid, task->priority);
    // if (ret != 0) {
    //     perror("setpriority");
    //     exit(-1);
    // }

    // loop
    ull now, start, then;
    ull lock_acquires = 0;
    ull lock_hold = 0;
    ull loop_in_cs = 0;
    const ull delta = CYCLE_PER_US * task->cs;
    pthread_barrier_wait(&global_barrier);
    while (!*task->stop) {
    // while (*task->global_its < 1000) {
        // fprintf(stderr, "thread %d acquiring lock\n", task->id);

        lock_acquire(&lock);
        // fprintf(stderr, "thread %d acquired lock\n", task->id);
        now = rdtscp();

        lock_acquires++;
        (*task->global_its)++;
        start = now;
        then = now + delta;

        do {
            loop_in_cs++;
        } while (((now = rdtscp()) < then));

        lock_hold += now - start;

        // fprintf(stderr, "thread %d releasing lock\n", task->id);
        lock_release(&lock);
        // fprintf(stderr, "thread %d released lock\n", task->id);
    }

    task->lock_acquires = lock_acquires;
    task->loop_in_cs = loop_in_cs;
    task->lock_hold = lock_hold;

    // fprintf(stderr,"FINISHED tid %d\n", task->id);
    return 0;
}

int main(int argc, char *argv[]) {
    int nthreads = atoi(argv[1]);
    int duration = atoi(argv[2]);
    int ncpu = atoi(argv[4]);
    int nnodes = atoi(argv[5]);
    task_t *tasks = malloc(sizeof(task_t) * nthreads);

    pthread_attr_t attr;
    pthread_attr_init(&attr);

    volatile int stop __attribute__((aligned (CACHELINE_SIZE))) = 0;
    volatile ull global_its __attribute__((aligned (CACHELINE_SIZE))) = 0;
    // int stop_warmup __attribute__((aligned (CACHELINE_SIZE))) = 0;
    // int ncpu = argc > 3 + nthreads*2 ? atoi(argv[3+nthreads*2]) : 0;
    for (int i = 0; i < nthreads; i++) {
        tasks[i].stop = &stop;
        tasks[i].global_its = &global_its;
        // tasks[i].stop_warmup = &stop_warmup;
        // tasks[i].cs = atof(argv[3+i*2]);
        tasks[i].cs = atof(argv[3]);

        // int priority = atoi(argv[4+i*2]);
        // tasks[i].priority = priority;
        tasks[i].priority = 1;

        tasks[i].ncpu = ncpu;
        tasks[i].id = i;
        tasks[i].nnodes = nnodes;

        tasks[i].loop_in_cs = 0;
        tasks[i].lock_acquires = 0;
        tasks[i].lock_hold = 0;
    }

    lock_init(&lock);
    pthread_barrier_init(&global_barrier, NULL, nthreads+1);

    // pthread_t *w_threads = malloc(sizeof(pthread_t)*nthreads);
    // fprintf(stderr, "WARMUP\n");
    // for (int i = 0; i < nthreads; i++) {
    //     pthread_create(&tasks[i].thread, &attr, worker, &tasks[i]);
    // }
    // sleep(2);
    // stop = 1;
    // for (int i = 0; i < nthreads; i++) {
    //     pthread_join(tasks[i].thread, NULL);
    // }
    // for (int i = 0; i < nthreads; i++) {
    //     tasks[i].loop_in_cs = 0;
    //     tasks[i].lock_acquires = 0;
    //     tasks[i].lock_hold = 0;
    //     global_its = 0;
    // }

    stop = 0;
    for (int i = 0; i < nthreads; i++) {
        pthread_create(&tasks[i].thread, NULL, worker, &tasks[i]);
    }
    // sleep(2);
    pthread_barrier_wait(&global_barrier);
    fprintf(stderr, "MEASUREMENTS\n");
    sleep(duration);
    stop = 1;
    for (int i = 0; i < nthreads; i++) {
        pthread_join(tasks[i].thread, NULL);
    }

    for (int i = 0; i < nthreads; i++) {
        task_t task = (task_t) tasks[i];
        printf("%03d,%10llu,%8llu,%10.3f\n",
                task.id,
                task.loop_in_cs,
                task.lock_acquires,
                task.lock_hold / (float) (CYCLE_PER_US * 1000)
                );
    }
    // WAIT SO SERVER CAN SHUTDOWN
    sleep(2);
    return 0;
}

// LD_PRELOAD=/home/mihi/Desktop/DAL/litl2/lib/original/libcbomcs_spinlock.so ./main 2 3 1000 8 
// LD_PRELOAD=/home/kumichae/DAL/litl2/lib/original/libcbomcs_spinlock.so ./main 2 3 1000 8 
