#define _GNU_SOURCE
#include <stdio.h>
#include <string.h>
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
#include "rdtsc.h"
#include "lock.h"
#include "../litl2/include/topology.h"

#define gettid() syscall(SYS_gettid)

#ifndef CYCLE_PER_US
#error Must define CYCLE_PER_US for the current machine in the Makefile or elsewhere
#endif

#ifndef CACHELINE_SIZE
#define CACHELINE_SIZE 64
#endif

#define KB(x) ((x) * 1024LL)
#define MB(x) (KB(x) * 1024LL)
#define GB(x) (MB(x) * 1024LL)
#define MAX_ARRAY_SIZE GB(1LL)

double *array;

// TODO: MOVE INTO UTILS.H
typedef unsigned long long ull;
typedef struct {
    volatile int *stop;
    volatile ull *global_its;
    pthread_t thread;
    int priority;
    int id;
    double cs;
    char* server_ip;
    // outputs
    ull loop_in_cs;
    ull lock_acquires;
    ull lock_hold;
    size_t array_size;
} task_t __attribute__ ((aligned (CACHELINE_SIZE)));

lock_t lock;
pthread_barrier_t global_barrier;
pthread_barrier_t mem_barrier;

int pin_thread(int task_id) {
    int ret = 0;
    int node = task_id % NUMA_NODES;
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(task_id%CPU_NUMBER, &cpuset);
    // fprintf(stderr, "pinning client thread %d to cpu %d\n", task_id, (task_id)%CPU_NUMBER);
    ret = pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &cpuset);
    if (ret != 0) {
        fprintf(stderr, "pthread_set_affinity_np");
        exit(-1);
    }
    if (numa_run_on_node(node) != 0) {
        fprintf(stderr, "numa_run_on_node failed");
        exit(-1);
    }
    return ret;
}

int set_prio(int prio) {
    int ret;
    pid_t tid = gettid();
    ret = setpriority(PRIO_PROCESS, tid, prio);
    if (ret != 0) {
        perror("setpriority");
        exit(-1);
    }
    return ret;
}

void *cs_worker(void *arg) {
    task_t *task = (task_t *) arg;
    int task_id = task->id;
    pin_thread(task_id);

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
        } while (((now = rdtscp()) < then) && !task->stop);

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

void *empty_cs_worker(void *arg) {
    task_t *task = (task_t *) arg;
    int task_id = task->id;
    // fprintf(stderr,"RUNNING ON %d ndoes\n", NUMA_NODES);
    pin_thread(task_id);

    ull start;
    ull lock_acquires = 0;
    ull lock_hold = 0;
    ull loop_in_cs = 0;
    pthread_barrier_wait(&global_barrier);
    while (!*task->stop) {
        // fprintf(stderr, "thread %d acquiring lock\n", task->id);
        lock_acquire(&lock);
        // fprintf(stderr, "thread %d acquired lock\n", task->id);
        start = rdtscp();
        lock_acquires++;
        (*task->global_its)++;
        loop_in_cs++;
        lock_hold += rdtscp() - start;
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

void *mem_worker(void *arg) {
    task_t *task = (task_t *) arg;
    int task_id = task->id;
    size_t array_size = KB(2);

    pin_thread(task_id);
    ull start, lock_start;
    ull lock_acquires, lock_hold, loop_in_cs, duration;

    while (array_size < MAX_ARRAY_SIZE) {
        volatile double sum = 0; // Prevent compiler optimizations
        lock_acquires = 0;
        lock_hold = 0;
        loop_in_cs = 0;
        duration = 0;

        if (task_id == 0) {
            array = malloc(array_size);
            if (!array) {
                fprintf(stderr, "Failed to allocate memory for size %zu bytes\n", array_size);
            }
            memset(array, 0, array_size); // Touch all pages to ensure allocation
        }

        pthread_barrier_wait(&mem_barrier);

        start = rdtscp();
        for (size_t i = 0; i < array_size / sizeof(double); i += CACHELINE_SIZE / sizeof(double)) {
            lock_acquire(&lock);
            lock_start = rdtscp();
            lock_acquires++;
            sum += array[i];
            loop_in_cs++;
            lock_hold += rdtscp() - lock_start;
            lock_release(&lock);
        }

        duration = rdtscp() - start;
        pthread_barrier_wait(&mem_barrier);
        float lock_hold_ms = lock_hold / (float) (CYCLE_PER_US * 1000);
        float duration_ms = duration / (float) (CYCLE_PER_US * 1000);
        printf("%03d,%10llu,%8llu,%10.3f,%12.3f,%10zu\n",
                task_id,
                loop_in_cs,
                lock_acquires,
                lock_hold_ms,
                duration_ms,
                array_size
                );
        array_size *= 2;
    }
    // fprintf(stderr,"FINISHED tid %d\n", task->id);
    return 0;
}

double random_double(double min, double max) {
    if (min > max) {
        fprintf(stderr, "Invalid range: min must be <= max\n");
        exit(EXIT_FAILURE);
    }
    // Generate a random double in [0, 1)
    double scale = rand() / (double) RAND_MAX;

    // Scale and shift to [min, max)
    return min + scale * (max - min);
}

int main(int argc, char *argv[]) {
    srand(42);
    int nthreads = atoi(argv[1]);
    int duration = atoi(argv[2]);
    double cs = atoi(argv[3]);
    char *server_ip = (argv[4]);
    int mode = argc < 6 ? 0 : atoi(argv[5]);
    task_t *tasks = malloc(sizeof(task_t) * nthreads);

    pthread_attr_t attr;
    pthread_attr_init(&attr);

    volatile int stop __attribute__((aligned (CACHELINE_SIZE))) = 0;
    volatile ull global_its __attribute__((aligned (CACHELINE_SIZE))) = 0;
    double short_cs = duration * 1e6 / 1000.; //range in (us)
    double long_cs = duration * 1e6 / 10.;
    // int stop_warmup __attribute__((aligned (CACHELINE_SIZE))) = 0;
    for (int i = 0; i < nthreads; i++) {
        tasks[i].stop = &stop;
        tasks[i].global_its = &global_its;
        tasks[i].cs = cs == 0 ? (i%2 == 0 ? short_cs : long_cs) : cs;
        // fprintf(stderr, "random cs: %f\n", tasks[i].cs);

        // int priority = atoi(argv[4+i*2]);
        // tasks[i].priority = priority;
        tasks[i].priority = 1;
        tasks[i].id = i;
        tasks[i].server_ip = server_ip;

        tasks[i].loop_in_cs = 0;
        tasks[i].lock_acquires = 0;
        tasks[i].lock_hold = 0;
    }

    lock_init(&lock);
    pthread_barrier_init(&global_barrier, NULL, nthreads+1);
    pthread_barrier_init(&mem_barrier, NULL, nthreads);

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

    void* worker; 
    stop = 0;
    if (mode == 0 || mode == 1) {
        worker = mode == 0 ? empty_cs_worker : cs_worker;
        for (int i = 0; i < nthreads; i++) {
            pthread_create(&tasks[i].thread, NULL, worker, &tasks[i]);
        }
        pthread_barrier_wait(&global_barrier);
        fprintf(stderr, "MEASUREMENTS\n");
        sleep(duration);
        stop = 1;
        for (int i = 0; i < nthreads; i++) {
            pthread_join(tasks[i].thread, NULL);
        }

        float total_lock_hold = 0;
        ull total_lock_acq = 0;
        for (int i = 0; i < nthreads; i++) {
            task_t task = (task_t) tasks[i];
            float lock_hold = task.lock_hold / (float) (CYCLE_PER_US * 1000);
            total_lock_hold += lock_hold;
            total_lock_acq += task.lock_acquires;
            printf("%03d,%10llu,%8llu,%10.3f\n",
                    task.id,
                    task.loop_in_cs,
                    task.lock_acquires,
                    lock_hold
                    );
        }
        fprintf(stderr, "Total lock hold time(ms): %f\n", total_lock_hold);
        fprintf(stderr, "Total lock acquisitions: %llu\n", total_lock_acq);
    }
    else {
        worker = mem_worker;
        for (int i = 0; i < nthreads; i++) {
            pthread_create(&tasks[i].thread, NULL, worker, &tasks[i]);
        }
        fprintf(stderr, "MEASUREMENTS\n");
        for (int i = 0; i < nthreads; i++) {
            pthread_join(tasks[i].thread, NULL);
        }
        free(array);
    }
    // WAIT SO SERVER CAN SHUTDOWN
    sleep(2);
    fprintf(stderr, "DONE\n");
    return 0;
}

// LD_PRELOAD=/home/mihi/Desktop/DAL/litl2/lib/original/libcbomcs_spinlock.so ./main 2 3 1000 8 
// LD_PRELOAD=/home/kumichae/DAL/litl2/lib/original/libcbomcs_spinlock.so ./main 2 3 1000 8 
