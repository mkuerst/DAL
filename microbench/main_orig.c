#define _GNU_SOURCE
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <numa.h>
#include <sched.h>
#include <sys/syscall.h>
#include <inttypes.h>
#include <sys/resource.h>
#include "rdtsc.h"
#include "lock.h"
#include <utils.h>
#include <tcp_client.c>

#define gettid() syscall(SYS_gettid)

#ifndef CYCLE_PER_US
#error Must define CYCLE_PER_US for the current machine in the Makefile or elsewhere
#endif

char *array0;
char *array1;
int nthreads;

lock_t lock;
pthread_barrier_t global_barrier;
pthread_barrier_t mem_barrier;

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

// void *cs_worker(void *arg) {
//     task_t *task = (task_t *) arg;
//     int task_id = task->id;
//     pin_thread(task_id);
//     ull now, start, then;
//     ull lock_acquires;
//     ull lock_hold;
//     ull loop_in_cs;

//     const ull delta = CYCLE_PER_US * task->cs;

//     for (int i = 0; i < NUM_RUNS; i++) {
//         lock_acquires = 0;
//         lock_hold = 0;
//         loop_in_cs = 0;
//         pthread_barrier_wait(&global_barrier);
//         while (!*task->stop) {
//         // while (*task->global_its < 1000) {
//             // fprintf(stderr, "thread %d acquiring lock\n", task->id);

//             lock_acquire(&lock);
//             // fprintf(stderr, "thread %d acquired lock\n", task->id);
//             now = rdtscp();

//             lock_acquires++;
//             (*task->global_its)++;
//             start = now;
//             then = now + delta;

//             do {
//                 loop_in_cs++;
//             } while (((now = rdtscp()) < then) && !task->stop);

//             lock_hold += now - start;

//             // fprintf(stderr, "thread %d releasing lock\n", task->id);
//             lock_release(&lock);
//             // fprintf(stderr, "thread %d released lock\n", task->id);
//         }

//         task->lock_acquires[i][0] = lock_acquires;
//         task->loop_in_cs[i][0] = loop_in_cs;
//         task->lock_hold[i][0] = lock_hold;
//         pthread_barrier_wait(&global_barrier);
//     }

//     // fprintf(stderr,"FINISHED tid %d\n", task->id);
//     return 0;
// }
void *lat_worker(void *arg) {
    task_t *task = (task_t *) arg;
    int task_id = task->id;
    pin_thread(task_id, nthreads);
    ull now, start, end;
    ull lock_acquires;

    for (int i = 0; i < NUM_RUNS; i++) {
        pthread_barrier_wait(&global_barrier);
        lock_acquires = 0;

        for (int j = 0; j < NUM_LAT_RUNS; j++) {
                start = rdtscp();
                lock_acquire(&lock);
                now = rdtscp();
                task->wait_acq[i][j] = now - start;
                lock_acquires++;
                end = rdtscp();
                task->lock_hold[i][j] = end - now;
                lock_release(&lock);
                task->wait_rel[i][j] = rdtscp() - end;
        }
        pthread_barrier_wait(&global_barrier);
    }
    return 0;
}

void *empty_cs_worker(void *arg) {
    task_t *task = (task_t *) arg;
    int task_id = task->id;
    // fprintf(stderr,"RUNNING ON %d ndoes\n", NUMA_NODES);
    pin_thread(task_id, nthreads);
    ull start;
    ull lock_acquires;
    ull lock_hold;
    ull loop_in_cs;
    ull wait_rel;
    ull wait_acq;

    for (int i = 0; i < NUM_RUNS; i++) {
        lock_acquires = 0;
        lock_hold = 0;
        loop_in_cs = 0;
        wait_acq = 0;
        wait_rel = 0;
        pthread_barrier_wait(&global_barrier);
        while (!*task->stop) {
            // fprintf(stderr, "thread %d acquiring lock\n", task->id);
            start = rdtscp();
            lock_acquire(&lock);
            ull s = rdtscp();
            wait_acq += s - start;
            // fprintf(stderr, "thread %d acquired lock\n", task->id);
            lock_acquires++;
            loop_in_cs++;
            start = rdtscp();
            // fprintf(stderr, "thread %d releasing lock\n", task->id);
            lock_hold += start - s;
            lock_release(&lock);
            wait_rel += rdtscp() - start;
            // fprintf(stderr, "thread %d released lock\n", task->id);
        }

        task->lock_acquires[i][0] = lock_acquires;
        task->loop_in_cs[i][0] = loop_in_cs;
        task->lock_hold[i][0] = lock_hold;
        task->wait_acq[i][0] = wait_acq;
        task->wait_rel[i][0] = wait_rel;
        pthread_barrier_wait(&global_barrier);
    }

    // fprintf(stderr,"FINISHED tid %d\n", task->id);
    return 0;
}

void *mem_worker(void *arg) {
    task_t *task = (task_t *) arg;
    int task_id = task->id;

    pin_thread(task_id, nthreads);
    ull start, lock_start;
    ull lock_acquires, lock_hold, loop_in_cs;
    ull wait_acq, wait_rel;

    for (int i = 0; i < NUM_RUNS; i++) {
        for (int j = 0; j < NUM_MEM_RUNS; j++) {
            volatile char sum = 'a'; // Prevent compiler optimizations
            size_t repeat = array_sizes[NUM_MEM_RUNS-1] / array_sizes[j];
            ull array_size = array_sizes[j];
            lock_acquires = 0;
            lock_hold = 0;
            loop_in_cs = 0;
            wait_acq = 0;
            wait_rel = 0;
            pthread_barrier_wait(&global_barrier);
            while (!*task->stop) {
                for (int x = 0; x < repeat; x++) {
                    for (size_t k = 0; k < array_size; k += 1) {
                        int u = 0;
                        if(*task->stop)
                            break;
                        start = rdtscp();
                        lock_acquire(&lock);
                        lock_start = rdtscp();
                        wait_acq += lock_start-start;
                        lock_acquires++;
                        while (u < CACHELINE_SIZE) {
                            array0[k] += sum;
                            u++;
                            loop_in_cs++;
                        }
                        ull rel_start = rdtscp();
                        lock_release(&lock);
                        ull rel_end = rdtscp();
                        lock_hold += rel_end - lock_start;
                        wait_rel += rel_end - rel_start;
                    }
                }
            }
            task->lock_acquires[i][j] = lock_acquires;
            task->loop_in_cs[i][j] = loop_in_cs;
            task->lock_hold[i][j] = lock_hold;
            task->array_size[i][j] = array_size;
            // task->duration[i][j] = duration;
            task->wait_acq[i][j] = wait_acq;
            task->wait_rel[i][j] = wait_rel;
            pthread_barrier_wait(&global_barrier);
        }
    }
    // fprintf(stderr,"FINISHED tid %d\n", task->id);
    return 0;
}

// double random_double(double min, double max) {
//     if (min > max) {
//         fprintf(stderr, "Invalid range: min must be <= max\n");
//         exit(EXIT_FAILURE);
//     }
//     // Generate a random double in [0, 1)
//     double scale = rand() / (double) RAND_MAX;

//     // Scale and shift to [min, max)
//     return min + scale * (max - min);
// }

int cs_result_to_out(task_t* tasks, int nthreads, int mode) {
    int snd_runs = mode == 2 ? NUM_MEM_RUNS : (mode == 1 ? NUM_LAT_RUNS : 1);
    float cycle_to_ms = (float) (CYCLES_MAX * 1e3);
    for (int j = 0; j < NUM_RUNS; j++) {
        float total_lock_hold = 0;
        ull total_lock_acq = 0;
        printf("RUN %d\n", j);
        for (int i = 0; i < nthreads; i++) {
            task_t task = (task_t) tasks[i];
            for (int l = 0; l < snd_runs; l++) {
                float lock_hold = task.lock_hold[j][l] / (float) cycle_to_ms;
                float wait_acq = task.wait_acq[j][l] / (float) cycle_to_ms;
                float wait_rel = task.wait_rel[j][l] / (float) cycle_to_ms;
                float lwait_acq = task.lwait_acq[j][l] / cycle_to_ms;
                float lwait_rel = task.lwait_rel[j][l] / cycle_to_ms;
                float gwait_acq = task.gwait_acq[j][l] / cycle_to_ms;
                float gwait_rel = task.gwait_rel[j][l] / cycle_to_ms;


                float total_duration = (float) task.duration[j][l];
                size_t array_size = task.array_size[j][l];
                total_lock_hold += lock_hold;
                total_lock_acq += task.lock_acquires[j][l];
                printf("%03d,%10llu,%8llu,%12.6f,%12.6f,%12.6f,%12.6f,%12.6f,%12.6f,%12.6f,%12.6f,%16lu,%03d\n",
                        task.id,
                        task.loop_in_cs[j][l],
                        task.lock_acquires[j][l],
                        lock_hold,
                        total_duration,
                        wait_acq,
                        wait_rel,
                        lwait_acq,
                        lwait_rel,
                        gwait_acq,
                        gwait_rel,
                        array_size,
                        task.client_id
                        );
            }
        }
        printf("-------------------------------------------------------------------------------------------------------\n\n");
        fprintf(stderr, "Total lock hold time(ms): %f\n", total_lock_hold);
        fprintf(stderr, "Total lock acquisitions: %llu\n\n", total_lock_acq);
    }
    return 0;
}

int main(int argc, char *argv[]) {
    srand(42);
    nthreads = atoi(argv[1]);
    int duration = atoi(argv[2]);
    double cs = atoi(argv[3]);
    char *server_ip = (argv[4]);
    int mode = argc < 6 ? 0 : atoi(argv[5]);
    task_t *tasks = malloc(sizeof(task_t) * nthreads);

    pthread_attr_t attr;
    pthread_attr_init(&attr);

    volatile int stop __attribute__((aligned (CACHELINE_SIZE))) = 0;
    volatile ull global_its __attribute__((aligned (CACHELINE_SIZE))) = 0;
    double short_cs = duration * 1e6 / 100000.; //range in (us)
    double long_cs = duration * 1e6 / 100.;
    // int stop_warmup __attribute__((aligned (CACHELINE_SIZE))) = 0;
    for (int i = 0; i < nthreads; i++) {
        tasks[i].stop = &stop;
        tasks[i].global_its = &global_its;
        tasks[i].cs = cs == 0 ? (i%2 == 0 ? short_cs : long_cs) : cs;
        tasks[i].priority = 1;
        tasks[i].id = i;
        tasks[i].server_ip = server_ip;
        // fprintf(stderr, "random cs: %f\n", tasks[i].cs);
        // int priority = atoi(argv[4+i*2]);
        // tasks[i].priority = priority;

        for (int j = 0 ; j < NUM_RUNS; j++) {
            for (int k = 0; k < NUM_MEM_RUNS; k++) {
                tasks[i].duration[j][k] = duration;
                tasks[i].loop_in_cs[j][k] = 0;
                tasks[i].lock_acquires[j][k] = 0;
                tasks[i].lock_hold[j][k] = 0;
                tasks[i].array_size[j][k] = 0;
                tasks[i].wait_acq[j][k] = 0;
                tasks[i].wait_rel[j][k] = 0;
            }
        }
    }
    lock_init(&lock);
    pthread_barrier_init(&global_barrier, NULL, nthreads+1);
    pthread_barrier_init(&mem_barrier, NULL, nthreads);

    void* worker; 
    int num_mem_runs = mode == 2 ? NUM_MEM_RUNS : 1;
    worker = mode == 0 ? empty_cs_worker : (mode == 1 ? lat_worker : mem_worker);
    for (int i = 0; i < nthreads; i++) {
        pthread_create(&tasks[i].thread, NULL, worker, &tasks[i]);
    }
    for (int i = 0; i < NUM_RUNS; i++) {
        for (int k = 0; k < num_mem_runs; k++) {
            size_t array_sz = array_sizes[k];
            if (mode == 2) {
                array0 = (char *) numa_alloc_onnode(array_sz, 0);
                array1 = (char *) numa_alloc_onnode(array_sz, 1);
                if (!array0 || !array1) {
                    _error("Failed to allocate memory for size %lu bytes\n", array_sizes[k]);
                }
                // memset(array, 0, array_sizes[k]); // Touch all pages to ensure allocation
            }
            stop = 0;
            fprintf(stderr, "MEASUREMENTS RUN %d_%d\n", i, k);
            pthread_barrier_wait(&global_barrier);
            sleep(duration);
            stop = 1;
            pthread_barrier_wait(&global_barrier);
            if (mode == 2) {
                numa_free(array0, array_sz);
                numa_free(array1, array_sz);
            }
        }
    }

    for (int i = 0; i < nthreads; i++) {
        pthread_join(tasks[i].thread, NULL);
    }
    cs_result_to_out(tasks, nthreads, mode);
    // free(array);
    // WAIT SO SERVER CAN SHUTDOWN
    sleep(2);
    fprintf(stderr, "DONE\n");
    return 0;
}

// LD_PRELOAD=/home/mihi/Desktop/DAL/litl2/lib/original/libcbomcs_spinlock.so ./main 2 3 1000 8 
// LD_PRELOAD=/home/kumichae/DAL/litl2/lib/original/libcbomcs_spinlock.so ./main 2 3 1000 8 
