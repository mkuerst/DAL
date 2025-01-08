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
#include <dirent.h>
#include <assert.h>

#include <sys/stat.h>
#include <sys/types.h>
#include <errno.h>

#define gettid() syscall(SYS_gettid)

#ifndef CYCLE_PER_US
#error Must define CYCLE_PER_US for the current machine in the Makefile or elsewhere
#endif

char *array0;
char *array1;
char *res_file;
int nthreads;
int client;
int num_clients;
int use_nodes;
int scope;

disa_lock_t lock;
disa_lock_t *locks;
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
 
void client_barrier(int run) {
    struct dirent *entry;
    char dir_name[256] = {0};
    char filename[256] = {0};

    snprintf(dir_name, sizeof(dir_name), "/home/kumichae/DAL/microbench/barrier_files/run%d", run);
    sprintf(filename, "/home/kumichae/DAL/microbench/barrier_files/run%d/%d", run, client);
    if (mkdir(dir_name, 0777) != 0 && errno != EEXIST) {
        _error("Failed to create directory");
    }
    FILE *file = fopen(filename, "w");
    if (file == NULL) {
        _error("Client %d failed to create barrier file\n", client);
    }
    fclose(file);

    while (1) {
        DIR *dir = opendir(dir_name);
        if (dir == NULL) {
            _error("Client %d failed to open %s\n", client, dir_name);
        }
        int count = 0;
        while ((entry = readdir(dir)) != NULL) {

            if (entry->d_name[0] == '.')
                continue;

            count++;
        }
        // fprintf(stderr, "COUNT IS: %d\n", count);
        closedir(dir);
        if (count == num_clients)
            break;
    }
    DEBUG("Client %d passing barrier for run %d\n", client, run);
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
    pin_thread(task_id, nthreads, use_nodes);
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
    pin_thread(task_id, nthreads, use_nodes);
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
        #ifdef MPI
        if (task_id == 0)
            MPI_Barrier(MPI_COMM_WORLD);
            // client_barrier(i);
        #endif
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

    pin_thread(task_id, nthreads, use_nodes);
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
                        lock_hold += rel_start - lock_start;
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

void *mlocks_worker(void *arg) {
    task_t *task = (task_t *) arg;
    int task_id = task->id;
    char* data = task->client_meta->data;

    pin_thread(task_id, nthreads, use_nodes);
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
        #ifdef MPI
            if (task_id == 0)
                MPI_Barrier(MPI_COMM_WORLD);
        #endif
            pthread_barrier_wait(&global_barrier);

            while (!*task->stop) {
                for (size_t k = 0; k < array_size; k += 1024) {
                    int u = 0;
                    // int lock_idx = k / scope;
                    int lock_idx = 1;
                    if(*task->stop)
                        break;
                    start = rdtscp();
                    lock_acquire((pthread_mutex_t *)&locks[lock_idx]);
                    lock_start = rdtscp();
                    wait_acq += lock_start-start;
                    lock_acquires++;
                    while (u < CACHELINE_SIZE) {
                        data[k] += sum;
                        u++;
                        loop_in_cs++;
                    }
                    ull rel_start = rdtscp();
                    lock_release((pthread_mutex_t *)&locks[lock_idx]);
                    ull rel_end = rdtscp();
                    lock_hold += rel_start - lock_start;
                    wait_rel += rel_end - rel_start;
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


int main(int argc, char *argv[]) {
    client = atoi(argv[6]);
#ifdef MPI
    int initialized;
    MPI_Initialized(&initialized);
    if (!initialized)
        MPI_Init(NULL, NULL);


    DEBUG("MPI INIT DONE\n");
    int rank, size;
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    MPI_Comm_size(MPI_COMM_WORLD, &size);
    client = rank;
#endif

    DEBUG("HI from client [%d]\n", client);

    srand(42);
    nthreads = atoi(argv[1]);
    int duration = atoi(argv[2]);
    double cs = atoi(argv[3]);
    char *server_ip = argv[4];
    int mode = argc < 6 ? 0 : atoi(argv[5]);
    num_clients = atoi(argv[7]);
    res_file = argv[8];
    int nlocks = atoi(argv[9]);
    scope = MAX_ARRAY_SIZE;
    void* worker; 
    int num_mem_runs;
    switch (mode) {
        case 0:
            use_nodes = 2;
            worker = empty_cs_worker;
            num_mem_runs = 1;
            break;
        case 1:
            use_nodes = 1;
            worker = empty_cs_worker;
            num_mem_runs = 1;
            duration = 0;
            break;
        case 2:
            use_nodes = 2;
            worker = lat_worker;
            num_mem_runs = 1;
            break;
        case 3:
            use_nodes = 2;
            worker = mem_worker;
            num_mem_runs = NUM_MEM_RUNS;
            break;
        case 4:
            use_nodes = 1;
            worker = mem_worker;
            num_mem_runs = NUM_MEM_RUNS;
            break;
        case 5:
            use_nodes = 2;
            worker = mlocks_worker;
            num_mem_runs = NUM_MEM_RUNS;
            break;
        case 6:
            use_nodes = 1;
            worker = mlocks_worker;
            num_mem_runs = NUM_MEM_RUNS;
            break;
        default:
            use_nodes = 2;
            worker = empty_cs_worker;
            num_mem_runs = 1;
            break;
    }
    task_t *tasks = malloc(sizeof(task_t) * nthreads);

    pthread_attr_t attr;
    pthread_attr_init(&attr);

    volatile int stop __attribute__((aligned (CACHELINE_SIZE))) = 0;
    volatile ull global_its __attribute__((aligned (CACHELINE_SIZE))) = 0;
    double short_cs = duration * 1e6 / 100000.; //range in (us)
    double long_cs = duration * 1e6 / 100.;
    rdma_client_meta* client_meta = NULL;
    int tcp_fd = 0;
    // int stop_warmup __attribute__((aligned (CACHELINE_SIZE))) = 0;
#ifdef RDMA
    for (int i = 0; i < num_clients;i++) {
        if (i == client) {
            if (!(client_meta = establish_rdma_connection(client, server_ip, nthreads, nlocks))) {
                _error("Client %d failed to eastablish rdma connection\n", client);
            }
        }
    #ifdef MPI
        MPI_Barrier(MPI_COMM_WORLD);
    #endif
    }
#endif
    #ifdef TCP_SPINLOCK
        if ((tcp_fd = establish_tcp_connection(client, server_ip)) == 0) {
            _error("Client %d failed to establish TCP_SPINLOCK connection\n", client);
        }
    #endif

    for (int i = 0; i < nthreads; i++) {
        tasks[i] = (task_t) {0};
        tasks[i].nlocks = nlocks;
        tasks[i].disa = 'y';
        tasks[i].stop = &stop;
        tasks[i].global_its = &global_its;
        tasks[i].cs = cs == 0 ? (i%2 == 0 ? short_cs : long_cs) : cs;
        tasks[i].priority = 1;
        tasks[i].id = i;
        tasks[i].server_ip = server_ip;
        tasks[i].client_meta = client_meta;
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

    locks = (disa_mutex_t *) malloc(nlocks * sizeof(disa_mutex_t));
    for (int l = 0; l < nlocks; l++) {
        locks[l].disa = 'y';
        locks[l].id = l;
        locks[l].offset = -1;
        locks[l].len = 0;
        lock_init(&locks[l]);
    }
    lock = locks[0];

    pthread_barrier_init(&global_barrier, NULL, nthreads+1);
    pthread_barrier_init(&local_barrier, NULL, nthreads);

    for (int i = 0; i < nthreads; i++) {
        pthread_create(&tasks[i].thread, NULL, worker, &tasks[i]);
    }
    for (int i = 0; i < NUM_RUNS; i++) {
        for (int k = 0; k < num_mem_runs; k++) {
            size_t array_sz = array_sizes[k];
            if (mode == 3 || mode == 4) {
                array0 = (char *) numa_alloc_onnode(array_sz, 0);
                array1 = (char *) numa_alloc_onnode(array_sz, 1);
                if (!array0 || !array1) {
                    _error("Failed to allocate memory for size %lu bytes\n", array_sizes[k]);
                }
            }
            if (mode > 4) {
                assert(array_sz / CACHELINE_SIZE >= nlocks);
                assert(array_sz % nlocks == 0);
                scope = array_sz / nlocks;
                for (int l = 0; l < nlocks; l++) {
                    locks[l].offset = l*scope;
                    locks[l].len = scope;
                }
            }
            stop = 0;
            fprintf(stderr, "CLIENT %d MEASUREMENTS RUN %d_%d\n", client, i, k);
            pthread_barrier_wait(&global_barrier);
            sleep(duration);
            stop = 1;
            pthread_barrier_wait(&global_barrier);
            if (mode == 3 || mode == 4) {
                numa_free(array0, array_sz);
                numa_free(array1, array_sz);
            }
        }
    }

    for (int i = 0; i < nthreads; i++) {
        pthread_join(tasks[i].thread, NULL);
    }
    for (int i = 0; i < num_clients; i++) {
        if (i == client) {
            cs_result_to_out(tasks, nthreads, mode, res_file);
        }
    #ifdef MPI
        MPI_Barrier(MPI_COMM_WORLD);
    #endif
    } 
#ifdef MPI
    MPI_Finalize();
#endif
    // WAIT SO SERVER CAN SHUTDOWN
    sleep(2);
    fprintf(stderr, "CLIENT %d DONE\n", client);
    return 0;
}

// LD_PRELOAD=/home/mihi/Desktop/DAL/litl2/lib/original/libcbomcs_spinlock.so ./main 2 3 1000 8 
// LD_PRELOAD=/home/kumichae/DAL/litl2/lib/original/libcbomcs_spinlock.so ./main 2 3 1000 8 
