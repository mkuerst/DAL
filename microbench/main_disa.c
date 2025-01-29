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
char *res_file_cum, *res_file_single;
char *mn_ip, peer_ips[MAX_CLIENTS][MAX_IP_LENGTH];
int nthreads, client, num_clients,
num_runs, num_mem_runs, use_nodes,
scope, mode, duration, nlocks;


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
    for (int i = 0; i < num_runs; i++) {
        task->run = i;
        lock_acquires = 0;
        lock_hold = 0;
        loop_in_cs = 0;
        wait_acq = 0;
        wait_rel = 0;

        pthread_barrier_wait(&global_barrier);
    #ifdef MPI
        if (task_id == 0)
            MPI_Barrier(MPI_COMM_WORLD);
    #endif

        pthread_barrier_wait(&global_barrier);
        
        task->cnt = 0;
        task->idx = 0;
        while (!*task->stop) {
            start = rdtscp();
            lock_acquire((pthread_mutex_t *)&lock);
            ull s = rdtscp();

            wait_acq += s - start;
            task->swait_acq[task->idx] = s - start;
            lock_acquires++;
            loop_in_cs++;

            start = rdtscp();
            lock_release((pthread_mutex_t *)&lock);
            ull end_rel = rdtscp();

            wait_rel += end_rel - start;
            lock_hold += start - s;

            task->slock_hold[task->idx] = start - s;
            task->swait_rel[task->idx] = end_rel - start;
            task->idx = (task->idx + 1) % MAX_MEASUREMENTS;
            task->cnt++;
        }

        task->lock_acquires[i] = lock_acquires;
        task->loop_in_cs[i] = loop_in_cs;
        task->lock_hold[i] = lock_hold;
        task->wait_acq[i] = wait_acq;
        task->wait_rel[i] = wait_rel;
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

    for (int i = 0; i < num_runs; i++) {
        for (int j = 0; j < num_mem_runs; j++)  {
            task->run = i*num_mem_runs + j;
            volatile char sum = 'a'; // Prevent compiler optimizations
            size_t repeat = array_sizes[num_mem_runs-1] / array_sizes[j];
            ull array_size = array_sizes[j];
            lock_acquires = 0;
            lock_hold = 0;
            loop_in_cs = 0;
            wait_acq = 0;
            wait_rel = 0;

            pthread_barrier_wait(&global_barrier);
        #ifdef MPI
            if (task_id == 0)
                MPI_Barrier(MPI_COMM_WORLD);
        #endif
            pthread_barrier_wait(&global_barrier);

            task->cnt = 0;
            task->idx = 0;
            while (!*task->stop) {
                for (int x = 0; x < repeat; x++) {
                    for (size_t k = 0; k < array_size; k += 1) {
                        int u = 0;
                        if(*task->stop)
                            break;

                        start = rdtscp();
                        lock_acquire((pthread_mutex_t *)&lock);
                        lock_start = rdtscp();

                        task->swait_acq[task->idx] = lock_start-start;
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

                        task->slock_hold[task->idx] = rel_start - lock_start;
                        task->swait_rel[task->idx] = rel_end - rel_start;
                        task->idx = (task->idx + 1) % MAX_MEASUREMENTS;
                        task->cnt++;

                        lock_hold += rel_start - lock_start;
                        wait_rel += rel_end - rel_start;
                    }
                }
            }
            int _idx = i*num_mem_runs + j;
            task->lock_acquires[_idx] = lock_acquires;
            task->loop_in_cs[_idx] = loop_in_cs;
            task->lock_hold[_idx] = lock_hold;
            task->array_size[_idx] = array_size;
            task->wait_acq[_idx] = wait_acq;
            task->wait_rel[_idx] = wait_rel;
            pthread_barrier_wait(&global_barrier);
    }
        }
    return 0;
}

void *debug_worker(void *arg) {
    task_t *task = (task_t *) arg;
    pthread_barrier_wait(&global_barrier);
    // for (int i = 0; i < 2 ; i++) {
    if (task->client_id == 0) {
        lock_acquire(&lock);
    }
    pthread_barrier_wait(&local_barrier);
    if (task->client_id == 1) {
        lock_release(&lock);
    }
    pthread_barrier_wait(&local_barrier);
    // }
    return 0;
}


void *mlocks_worker(void *arg) {
    task_t *task = (task_t *) arg;
    int task_id = task->id;
    int nlocks = task->nlocks;
    int *private_int_array = task->private_int_array;
    int data_len = MAX_ARRAY_SIZE / sizeof(int);

    pin_thread(task_id, nthreads, use_nodes);
    ull start, lock_start;
    ull lock_acquires, lock_hold, loop_in_cs;
    ull wait_acq, wait_rel;

    volatile int sum = 1;

    for (int i = 0; i < num_runs; i++) {
        task->run = i;
        ull array_size = MAX_ARRAY_SIZE;
        lock_acquires = 0;
        lock_hold = 0;
        loop_in_cs = 0;
        wait_acq = 0;
        wait_rel = 0;
        srand(task->client_id*task->nthreads + task_id + 42);

        pthread_barrier_wait(&global_barrier);
    #ifdef MPI
        if (task_id == 0)
            MPI_Barrier(MPI_COMM_WORLD);
    #endif
        pthread_barrier_wait(&global_barrier);

        task->cnt = 0;
        task->idx = 0;
        while (!*task->stop) {
            for (int j = 0; j < 400; j++) {
                int idx = uniform_rand_int(PRIVATE_ARRAY_SZ / sizeof(int));
                private_int_array[idx] += sum;
            }

            for (int j = 0; j < 100; j++) {
                int lock_idx = uniform_rand_int(nlocks);
                disa_mutex_t *l = &locks[lock_idx];
                // fprintf(stderr,"[%d.%d] lock_idx: %d\n", client, task_id, lock_idx);

                start = rdtscp();
                lock_acquire((pthread_mutex_t *)l);
                lock_start = rdtscp();

                for (int idx = 0; idx < data_len; idx++) {
                    l->int_data[idx] += sum;
                    loop_in_cs++;
                }
                // data[lock_idx*data_len] += sum;
                // loop_in_cs++;

                ull rel_start = rdtscp();
                lock_release((pthread_mutex_t *)l);
                ull rel_end = rdtscp();

                if (*task->stop) {
                    break;
                }
                task->swait_acq[task->idx] = lock_start-start;
                task->slock_hold[task->idx] = rel_start - lock_start;
                task->swait_rel[task->idx] = rel_end - rel_start;
                task->idx = (task->idx + 1) % MAX_MEASUREMENTS;
                task->cnt++;

                wait_acq += lock_start-start;
                lock_hold += rel_start - lock_start;
                wait_rel += rel_end - rel_start;
                lock_acquires++;

            }
        }
        task->lock_acquires[i] = lock_acquires;
        task->loop_in_cs[i] = loop_in_cs; 
        task->lock_hold[i] = lock_hold;
        task->array_size[i] = array_size;
        task->wait_acq[i] = wait_acq;
        task->wait_rel[i] = wait_rel;
        pthread_barrier_wait(&global_barrier);
    }
    return 0;
}

int main(int argc, char *argv[]) {
    parse_cli_args(&nthreads, &num_clients, &nlocks, &client, &duration,
    &mode, &num_runs, &num_mem_runs, &res_file_cum, &res_file_single,
    &mn_ip, peer_ips, argc, argv
    );
#ifdef MPI
    int provided;
    MPI_Init_thread(&argc, &argv, MPI_THREAD_MULTIPLE, &provided);
    if (provided < MPI_THREAD_MULTIPLE) {
        fprintf(stderr, "MPI does not provide required threading support\n");
        MPI_Abort(MPI_COMM_WORLD, 1);
        exit(EXIT_FAILURE);
    }
    DEBUG("MPI INIT DONE\n");
    int rank, size;
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    MPI_Comm_size(MPI_COMM_WORLD, &size);
    client = rank;
#endif
    DEBUG("[%d] HI\n", client);
    scope = MAX_ARRAY_SIZE;
    void* worker; 
    switch (mode) {
        case 0:
            use_nodes = 2;
            worker = empty_cs_worker;
            break;
        case 1:
            use_nodes = 1;
            worker = empty_cs_worker;
            break;
        case 2:
            use_nodes = 1;
            worker = debug_worker;
            break;
        case 3:
            use_nodes = 2;
            worker = mem_worker;
            break;
        case 4:
            use_nodes = 1;
            worker = mem_worker;
            break;
        case 5:
            use_nodes = 2;
            worker = mlocks_worker;
            break;
        case 6:
            use_nodes = 1;
            worker = mlocks_worker;
            break;
        default:
            use_nodes = 2;
            worker = empty_cs_worker;
            break;
    }
    task_t *tasks = malloc(sizeof(task_t) * nthreads);

    pthread_attr_t attr;
    pthread_attr_init(&attr);

    volatile int stop __attribute__((aligned (CACHELINE_SIZE))) = 0;
    int tcp_fd = 0;
    /*CONNECTION*/
#ifdef RDMA
    pthread_t listener;
    rdma_client_meta* client_meta = malloc(sizeof(rdma_client_meta));
    for (int i = 0; i < num_clients; i++) {
        if (i == client) {
            if (establish_rdma_mn_connection(client, mn_ip, nthreads, num_clients, nlocks, use_nodes, client_meta)) {
                _error("Client %d failed to establish rdma mn connection\n", client);
            }
            if (start_peer_listener(client, peer_ips, num_clients, nthreads, &listener)) {
                _error("Client %d failed to start peer listener\n", client);
            }
            sleep(1);
        }
        #ifdef MPI
        MPI_Barrier(MPI_COMM_WORLD);
        #endif
    }
    for (int i = 0; i < num_clients; i++) {
        if (i == client) {
            if (establish_rdma_peer_connections(client, peer_ips, nthreads, num_clients, nlocks, client_meta)) {
                _error("Client %d failed to establish rdma peer connections\n", client);
            }
        }
        #ifdef MPI
        MPI_Barrier(MPI_COMM_WORLD);
        #endif
    }
    pthread_join(listener, NULL);
#endif

#ifdef TCP_SPINLOCK
    if ((tcp_fd = establish_tcp_connection(client, server_ip)) == 0) {
        _error("Client %d failed to establish TCP_SPINLOCK connection\n", client);
    }
#endif

    /*TASK INIT*/
    for (int i = 0; i < nthreads; i++) {
        tasks[i] = (task_t) {0};
        tasks[i].sockfd = tcp_fd;
        tasks[i].client_id = client;
        tasks[i].nclients = num_clients;
        tasks[i].id = i;
        tasks[i].nlocks = nlocks;
        tasks[i].disa = 'y';
        tasks[i].stop = &stop;;
        // TODO: adapt tcp to use struct sockaddr_in
        tasks[i].server_ip = mn_ip;
        tasks[i].client_meta = client_meta;
        tasks[i].duration = duration;
        tasks[i].nthreads = nthreads;
    }
    allocate_task_mem(tasks, num_runs, num_mem_runs, nthreads);

    /*LOCK INIT*/
    locks = (disa_mutex_t *) numa_alloc_onnode(nlocks * sizeof(disa_mutex_t), 0);
    // locks = (disa_mutex_t *) malloc(nlocks * sizeof(disa_mutex_t));
    for (int l = 0; l < nlocks; l++) {
        // lock_init(&locks[l].mutex);
        locks[l].disa = 'y';
        locks[l].id = l;
        locks[l].offset = -1;
        locks[l].elem_sz = 1;
        locks[l].data_len = 0;
        locks[l].turns = 0;
        locks[l].rlock_addr = client_meta->rlock_addr + l*RLOCK_SIZE;
        locks[l].data_addr = client_meta->data_addr + l*MAX_ARRAY_SIZE;
    }
    lock = locks[0];

    pthread_barrier_init(&global_barrier, NULL, nthreads+1);
    pthread_barrier_init(&local_barrier, NULL, nthreads);

    for (int i = 0; i < nthreads; i++) {
        pthread_create(&tasks[i].thread, NULL, worker, &tasks[i]);
    }
    /*RUNS*/
    for (int i = 0; i < num_runs; i++) {
        for (int k = 0; k < num_mem_runs; k++) {
            size_t array_sz = MAX_ARRAY_SIZE;
            for (int l = 0; l < nlocks; l++) {
                locks[l].turns = nthreads;
            }
            if (mode == 3 || mode == 4) {
                array0 = (char *) numa_alloc_onnode(array_sz, 0);
                array1 = (char *) numa_alloc_onnode(array_sz, 1);
                if (!array0 || !array1) {
                    _error("Failed to allocate memory for size %lu bytes\n", array_sizes[k]);
                }
            }
            if (mode > 4) {
                // assert(array_sz / CACHELINE_SIZE >= nlocks);
                assert(array_sz % nlocks == 0);
                for (int l = 0; l < nlocks; l++) {
                    locks[l].offset = l*array_sz;
                    locks[l].data_len = array_sz;
                    locks[l].elem_sz = sizeof(int);
                    locks[l].other = 0;
                    locks[l].turns = 0;
                }
            }
            stop = 0;
            fprintf(stderr, "[%d] RUN %d_%d\n", client, i, k);
            pthread_barrier_wait(&global_barrier);
            pthread_barrier_wait(&global_barrier);
            sleep(duration);
            stop = 1;
            pthread_barrier_wait(&global_barrier);
            if (mode == 3 || mode == 4) {
                numa_free(array0, array_sz);
                numa_free(array1, array_sz);
            }
            for (int i = 0; i < num_clients; i++) {
                if (i == client) {
                    write_res_single(tasks, nthreads, mode, res_file_single);
                }
                #ifdef MPI
                MPI_Barrier(MPI_COMM_WORLD);
                #endif
            } 
        }
    }

    for (int i = 0; i < nthreads; i++) {
        pthread_join(tasks[i].thread, NULL);
    }
    for (int i = 0; i < num_clients; i++) {
        if (i == client) {
            write_res_cum(tasks, nthreads, mode, res_file_cum, num_runs, num_mem_runs);
        }
        #ifdef MPI
        MPI_Barrier(MPI_COMM_WORLD);
        #endif
    } 
    // #ifdef RDMA
    // if (client == 0) {
    //     shutdown_server(client_meta);
    // }
    // MPI_Barrier(MPI_COMM_WORLD);
    // #endif

    #ifdef MPI
    MPI_Finalize();
    #endif
    numa_free(locks, nlocks*sizeof(disa_mutex_t));
    sleep(2);
    fprintf(stderr, "CLIENT %d DONE\n", client);
    return 0;
}