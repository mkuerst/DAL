#define _GNU_SOURCE
#include <dlfcn.h>
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
#include <rdma_client.c>
#include <mpi.h>

#define gettid() syscall(SYS_gettid)

#ifndef CYCLE_PER_US
#error Must define CYCLE_PER_US for the current machine in the Makefile or elsewhere
#endif

char *array0;
char *array1;
int nthreads;
int client;
int num_clients;

disa_lock_t lock;
pthread_barrier_t global_barrier;
pthread_barrier_t local_barrier;


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

//     const ull delta = CYCLES_11 * task->cs;

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
        task->run = i;
        lock_acquires = 0;

        pthread_barrier_wait(&local_barrier);
        if (task_id == 0)
            MPI_Barrier(MPI_COMM_WORLD);
        pthread_barrier_wait(&global_barrier);

        for (int j = 0; j < NUM_LAT_RUNS; j++) {
                task->snd_run = j;
                start = rdtscp();
                lock_acquire((pthread_mutex_t *)&lock);
                now = rdtscp();
                task->wait_acq[i][j] = now - start;
                lock_acquires++;
                end = rdtscp();
                task->lock_hold[i][j] = end - now;
                lock_release((pthread_mutex_t *)&lock);
                task->wait_rel[i][j] = rdtscp() - end;
        }
        pthread_barrier_wait(&global_barrier);
    }
    return 0;
}

void *empty_cs_worker(void *arg) {
    task_t *task = (task_t *) arg;
    int task_id = task->id;
    pin_thread(task_id, nthreads);
    ull start;
    ull lock_acquires;
    ull lock_hold;
    ull loop_in_cs;
    ull wait_rel;

    ull wait_acq;
    for (int i = 0; i < NUM_RUNS; i++) {
        task->run = i;
        lock_acquires = 0;
        lock_hold = 0;
        loop_in_cs = 0;
        wait_acq = 0;
        wait_rel = 0;

        pthread_barrier_wait(&local_barrier);
        if (task_id == 0)
            MPI_Barrier(MPI_COMM_WORLD);
        pthread_barrier_wait(&global_barrier);
        
        while (!*task->stop) {
            start = rdtscp();
            lock_acquire((pthread_mutex_t *)&lock);
            ull s = rdtscp();
            wait_acq += s - start;
            lock_acquires++;
            loop_in_cs++;
            start = rdtscp();
            lock_release((pthread_mutex_t *)&lock);
            wait_rel += rdtscp() - start;
            lock_hold += start - s;
        }

        task->lock_acquires[i][0] = lock_acquires;
        task->loop_in_cs[i][0] = loop_in_cs;
        task->lock_hold[i][0] = lock_hold;
        task->wait_acq[i][0] = wait_acq;
        task->wait_rel[i][0] = wait_rel;
        pthread_barrier_wait(&global_barrier);
    }
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
        task->run = i;
        for (int j = 0; j < NUM_MEM_RUNS; j++)  {
            task->snd_run = j;
            volatile char sum = 'a'; // Prevent compiler optimizations
            size_t repeat = array_sizes[NUM_MEM_RUNS-1] / array_sizes[j];
            ull array_size = array_sizes[j];
            lock_acquires = 0;
            lock_hold = 0;
            loop_in_cs = 0;
            wait_acq = 0;
            wait_rel = 0;

            pthread_barrier_wait(&local_barrier);
            if (task_id == 0)
                MPI_Barrier(MPI_COMM_WORLD);
            pthread_barrier_wait(&global_barrier);

            while (!*task->stop) {
                for (int x = 0; x < repeat; x++) {
                    for (size_t k = 0; k < array_size; k += 1) {
                        int u = 0;
                        if(*task->stop)
                            break;
                        start = rdtscp();
                        lock_acquire((pthread_mutex_t *)&lock);
                        lock_start = rdtscp();
                        wait_acq += lock_start-start;
                        lock_acquires++;
                        while (u < CACHELINE_SIZE) {
                            array0[k] += sum;
                            u++;
                            loop_in_cs++;
                        }
                        ull rel_start = rdtscp();
                        lock_release((pthread_mutex_t *)&lock);
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
            task->wait_acq[i][j] = wait_acq;
            task->wait_rel[i][j] = wait_rel;
            pthread_barrier_wait(&global_barrier);
        }
    }
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
        for (int i = 0; i < nthreads; i++) {
            task_t task = (task_t) tasks[i];
            for (int l = 0; l < snd_runs; l++) {
                float lock_hold = task.lock_hold[j][l] / cycle_to_ms;
                float wait_acq = task.wait_acq[j][l] / cycle_to_ms;
                float wait_rel = task.wait_rel[j][l] / cycle_to_ms;
                float lwait_acq = task.lwait_acq[j][l] / cycle_to_ms;
                float lwait_rel = task.lwait_rel[j][l] / cycle_to_ms;
                float gwait_acq = task.gwait_acq[j][l] / cycle_to_ms;
                float gwait_rel = task.gwait_rel[j][l] / cycle_to_ms;


                float total_duration = (float) task.duration[j][l];
                size_t array_size = task.array_size[j][l];
                total_lock_hold += lock_hold;
                total_lock_acq += task.lock_acquires[j][l];
                printf("%03d,%10llu,%8llu,%12.6f,%12.6f,%12.6f,%12.6f,%12.6f,%12.6f,%12.6f,%12.6f,%10llu,%16lu,%03d,%03d\n",
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
                        task.glock_tries[j][l],
                        array_size,
                        task.client_id,
                        j);
            }
        }
        fprintf(stderr, "RUN %d\n", j);
        fprintf(stderr, "Total lock hold time(ms): %f\n", total_lock_hold);
        fprintf(stderr, "Total lock acquisitions: %llu\n\n", total_lock_acq);
    }
    return 0;
}

int main(int argc, char *argv[]) {
    // client = atoi(argv[6]);
    fprintf(stderr, "HI\n");
    int initialized;
    MPI_Initialized(&initialized);
    if (!initialized)
        MPI_Init(NULL, NULL);
    // int provided;
    // MPI_Init_thread(&argc, &argv, MPI_THREAD_MULTIPLE, &provided);
    // if (provided < MPI_THREAD_MULTIPLE) {
    //     fprintf(stderr, "Error: MPI does not provide required threading support\n");
    //     MPI_Abort(MPI_COMM_WORLD, 1);
    // }


    DEBUG("MPI INIT DONE\n");
    int rank, size;
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    MPI_Comm_size(MPI_COMM_WORLD, &size);
    client = rank;
    DEBUG("HI from client %d\n", client);

    srand(42);
    nthreads = atoi(argv[1]);
    int duration = atoi(argv[2]);
    double cs = atoi(argv[3]);
    char *server_ip = (argv[4]);
    int mode = argc < 6 ? 0 : atoi(argv[5]);
    num_clients = atoi(argv[7]);
    task_t *tasks = malloc(sizeof(task_t) * nthreads);

    pthread_attr_t attr;
    pthread_attr_init(&attr);

    volatile int stop __attribute__((aligned (CACHELINE_SIZE))) = 0;
    volatile ull global_its __attribute__((aligned (CACHELINE_SIZE))) = 0;
    double short_cs = duration * 1e6 / 100000.; //range in (us)
    double long_cs = duration * 1e6 / 100.;
    rlock_meta* rlock = NULL;
    int tcp_fd = 0;
    // int stop_warmup __attribute__((aligned (CACHELINE_SIZE))) = 0;
    for (int i = 0; i < num_clients;i++) {
        if (i == rank) {
            fprintf(stderr, "Client %d trying to connect\n", client);
        #ifdef RDMA
            fprintf(stderr, "RDMA\n");
            if (!(rlock = establish_rdma_connection(client, server_ip))) {
                _error("Client %d failed to eastablish rdma connection\n", client);
            }
        #endif
        #ifdef TCP_SPINLOCK
            fprintf(stderr, "TCP\n");
            if ((tcp_fd = establish_tcp_connection(client, server_ip)) == 0) {
                _error("Client %d failed to establish TCP_SPINLOCK connection\n", client);
            }
        #endif
        }
        MPI_Barrier(MPI_COMM_WORLD);
    }

    for (int i = 0; i < nthreads; i++) {
        tasks[i] = (task_t) {0};
        tasks[i].stop = &stop;
        tasks[i].disa = 'y';
        tasks[i].global_its = &global_its;
        tasks[i].cs = cs == 0 ? (i%2 == 0 ? short_cs : long_cs) : cs;
        tasks[i].priority = 1;
        tasks[i].id = i;
        tasks[i].server_ip = server_ip;
        tasks[i].rlock_meta = rlock;
        tasks[i].sockfd = tcp_fd;
        tasks[i].client_id = client;
        tasks[i].run = 0;
        tasks[i].snd_run = 0;
        for (int j = 0; j < NUM_RUNS; j++) {
            for (int k = 0; k < NUM_SND_RUNS; k++) {
                tasks[i].duration[j][k] = duration;
            }
        }
        // fprintf(stderr, "random cs: %f\n", tasks[i].cs);
        // int priority = atoi(argv[4+i*2]);
        // tasks[i].priority = priority;
    }
    lock.disa = 'y';
    lock_init((pthread_mutex_t *) &lock);
    pthread_barrier_init(&global_barrier, NULL, nthreads+1);
    pthread_barrier_init(&local_barrier, NULL, nthreads);


    void* worker; 
    int num_mem_runs = mode == 2 ? NUM_MEM_RUNS : 1;
    worker = mode == 0 ? empty_cs_worker : (mode == 1 ? lat_worker : mem_worker);
    duration = mode == 1 ? 0 : duration;
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
            fprintf(stderr, "CLIENT %d MEASUREMENTS RUN %d_%d\n", client, i, k);
            pthread_barrier_wait(&global_barrier);
            sleep(duration);
            stop = 1;
            pthread_barrier_wait(&global_barrier);
            // MPI_Barrier(MPI_COMM_WORLD);
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
    MPI_Finalize();
    // WAIT SO SERVER CAN SHUTDOWN
    sleep(2);
    fprintf(stderr, "CLIENT %d DONE\n", client);
    return 0;
}

// LD_PRELOAD=/home/mihi/Desktop/DAL/litl2/lib/original/libcbomcs_spinlock.so ./main 2 3 1000 8 
// LD_PRELOAD=/home/kumichae/DAL/litl2/lib/original/libcbomcs_spinlock.so ./main 2 3 1000 8 
