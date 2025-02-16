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
#include utils.h>
#include <tcp_client.c>

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

ull *lock_acqs, *lock_rels;

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

void *empty_cs_worker(void *arg) {
    task_t *task = (task_t *) arg;
    int task_id = task->id;
    // fprintf(stderr,"RUNNING ON %d ndoes\n", NUMA_NODES);
    pin_thread(task_id, nthreads, use_nodes);
    ull start;
    ull lock_acquires;
    ull lock_hold;
    ull loop_in_cs;
    ull wait_rel;
    ull wait_acq;

    for (int i = 0; i < num_runs; i++) {
        lock_acquires = 0;
        lock_hold = 0;
        loop_in_cs = 0;
        wait_acq = 0;
        wait_rel = 0;
        pthread_barrier_wait(&global_barrier);
        while (!*task->stop) {
            start = rdtscp();
            lock_acquire(&lock);
            ull s = rdtscp();
            wait_acq += s - start;
            lock_acquires++;
            loop_in_cs++;
            start = rdtscp();
            lock_hold += start - s;
            lock_release(&lock);
            wait_rel += rdtscp() - start;
        }

        task->lock_acquires[i] = lock_acquires;
        task->loop_in_cs[i] = loop_in_cs;
        task->lock_hold[i] = lock_hold;
        task->wait_acq[i] = wait_acq;
        task->wait_rel[i] = wait_rel;
        pthread_barrier_wait(&global_barrier);
    }

    // fprintf(stderr,"FINISHED tid %d\n", task->id);
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
        for (int j = 0; j < num_mem_runs; j++) {
            volatile char sum = 'a'; // Prevent compiler optimizations
            size_t repeat = array_sizes[NUM_MEM_RUNS-1] / array_sizes[j];
            ull array_size = array_sizes[j];
            lock_acquires = 0;
            lock_hold = 0;
            loop_in_cs = 0;
            wait_acq = 0;
            wait_rel = 0;
            pthread_barrier_wait(&global_barrier);
                for (int x = 0; x < repeat; x++) {
            while (!*task->stop) {
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
                        lock_hold += rel_start - lock_start;
                        wait_rel += rel_end - rel_start;
                    }
                }
            }
            int _idx = i * num_mem_runs + j;
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

int main(int argc, char *argv[]) {
    parse_cli_args(&nthreads, &num_clients, &nlocks, &client, &duration,
    &mode, &num_runs, &num_mem_runs, &res_file_cum, &res_file_single,
    &mn_ip, peer_ips, argc, argv
    );
    void* worker; 
    switch (mode) {
        case 0:
            use_nodes = 2;
            worker = empty_cs_worker;
            break;
        case 1:
            use_nodes = 1;
            worker = empty_cs_worker;
            num_mem_runs = 1;
            break;
        case 3:
            use_nodes = 2;
            worker = mem_worker;
            break;
        case 4:
            use_nodes = 1;
            worker = mem_worker;
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
    for (int i = 0; i < nthreads; i++) {
        tasks[i] = (task_t) {0};
        tasks[i].client_id = client;
        tasks[i].nclients = num_clients;
        tasks[i].id = i;
        tasks[i].nlocks = nlocks;
        tasks[i].disa = 'y';
        tasks[i].stop = &stop;;
        tasks[i].server_ip = mn_ip;
        tasks[i].duration = duration;
        tasks[i].nthreads = nthreads;
    }
    allocate_task_mem(tasks, num_runs, num_mem_runs, nthreads);
    lock_init(&lock);
    pthread_barrier_init(&global_barrier, NULL, nthreads+1);
    pthread_barrier_init(&mem_barrier, NULL, nthreads);

    for (int i = 0; i < nthreads; i++) {
        pthread_create(&tasks[i].thread, NULL, worker, &tasks[i]);
    }
    for (int i = 0; i < num_runs; i++) {
        for (int k = 0; k < num_mem_runs; k++) {
            size_t array_sz = array_sizes[k];
            if (mode == 3 || 4) {
                array0 = (char *) numa_alloc_onnode(array_sz, 0);
                array1 = (char *) numa_alloc_onnode(array_sz, 1);
                if (!array0 || !array1) {
                    _error("Failed to allocate memory for size %lu bytes\n", array_sizes[k]);
                }
            }
            stop = 0;
            fprintf(stderr, "MEASUREMENTS RUN %d_%d\n", i, k);
            pthread_barrier_wait(&global_barrier);
            sleep(duration);
            stop = 1;
            pthread_barrier_wait(&global_barrier);
            if (mode == 3 || 4) {
                numa_free(array0, array_sz);
                numa_free(array1, array_sz);
            }
            write_res_single(tasks, nthreads, mode, res_file_single);
        }
    }

    for (int i = 0; i < nthreads; i++) {
        pthread_join(tasks[i].thread, NULL);
    }
    write_res_cum(tasks, nthreads, mode, res_file_cum, num_runs, num_mem_runs);
    fprintf(stderr, "DONE\n");
    return 0;
}