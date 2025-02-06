#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

using namespace std;
#include <string>
#include <cstdio>
#include <pthread.h>

#include <numa.h>
#include "mb_utils.h"

#include <DSM.h>

#define gettid() syscall(SYS_gettid)

#ifndef CYCLE_PER_US
#error Must define CYCLE_PER_US for the current machine in the Makefile or elsewhere
#endif

char *array0;
char *res_file_cum, *res_file_single;
int threadNR, nodeNR, mnNR, lockNR, runNR,
node_id, duration, mode;
uint64_t dsmSize;
uint64_t *lock_acqs;

DSM *dsm;
Rlock *rlock;
pthread_barrier_t global_barrier;
 

void mn_worker() {
    for (int i = 0; i < runNR; i++) {
        string barrier_key = "MB_RUN_" + to_string(i);
        dsm->barrier(barrier_key);
    }
}

void *correctness_worker(void *arg) {
    Task *task = (Task *) arg;
    dsm->registerThread();
    GlobalAddress baseAddr;
    baseAddr.nodeID = 0;
    baseAddr.offset = 0;
    int *int_data;
    // uint64_t all_thread = threadNR * dsm->getClusterSize();
    // int task_id = threadNR * dsm->getMyNodeID() + dsm->getMyThreadID();
    uint64_t acq_tp = 0;
    pthread_barrier_wait(&global_barrier);
    for (int i = 0; i < runNR; i++) {
        pthread_barrier_wait(&global_barrier);
        while (!*task->stop) {
            rlock->lock_acquire(baseAddr, 0);
            acq_tp++;
            int_data = (int *) rlock->getCurrPB();

            rlock->lock_release(baseAddr, 0);
        }
        pthread_barrier_wait(&global_barrier);
    }
    DE("[%d.%d] %lu ACQUISITIONS\n", dsm->getMyNodeID(), dsm->getMyThreadID(), acq_tp);
    return 0;
}

void *empty_cs_worker(void *arg) {
    Task *task = (Task *) arg;
    dsm->registerThread();
    GlobalAddress baseAddr;
    baseAddr.nodeID = 0;
    baseAddr.offset = 0;
    // uint64_t all_thread = threadNR * dsm->getClusterSize();
    // int task_id = threadNR * dsm->getMyNodeID() + dsm->getMyThreadID();
    uint64_t acq_tp = 0;
    pthread_barrier_wait(&global_barrier);
    for (int i = 0; i < runNR; i++) {
        pthread_barrier_wait(&global_barrier);
        while (!*task->stop) {
            rlock->lock_acquire(baseAddr, 0);
            acq_tp++;
            rlock->lock_release(baseAddr, 0);
        }
        pthread_barrier_wait(&global_barrier);
    }
    DE("[%d.%d] %lu ACQUISITIONS\n", dsm->getMyNodeID(), dsm->getMyThreadID(), acq_tp);
    return 0;
}


int main(int argc, char *argv[]) {
    parse_cli_args(
    &threadNR, &nodeNR, &mnNR, &lockNR, &runNR,
    &node_id, &duration, &mode,
    &res_file_cum, &res_file_single,
    argc, argv);
    dsmSize = 1;
    DE("[%d] HI\n", node_id);
    if (node_id == 1) {
        system("sudo bash /nfs/DAL/restartMemc.sh");
        DE("[%d] STARTED MEMC SERVER\n", node_id);
    }
    else {
        sleep(1);
    }
    DSMConfig config;
    config.dsmSize = dsmSize;
    config.mnNR = mnNR;
    config.machineNR = nodeNR;
    config.threadNR = threadNR;
    config.clusterID = node_id;
    dsm = DSM::getInstance(config);
    DE("[%d] DSM Init DONE\n", node_id);
    if (dsm->getMyNodeID() < mnNR) {
        DE("[%d] MN will busy spin\n", node_id);
        mn_worker();
        fprintf(stderr, "MN [%d] finished\n", node_id);
        dsm->barrier("fin");
        return 0;
    }

    /*WORKER*/
    void* (*worker)(void*) = empty_cs_worker;

    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_barrier_init(&global_barrier, NULL, threadNR+1);

    /*TASK INIT*/
    Task *tasks = new Task[threadNR];
    volatile int alignas(CACHELINE_SIZE) stop = 0;
    for (int i = 0; i < threadNR; i++) {
        tasks[i].id = i;
        tasks[i].stop = &stop;
        pthread_create(&tasks[i].thread, NULL, worker, &tasks[i]);
    }
    rlock = new Rlock(dsm, lockNR);
    lock_acqs = new uint64_t[lockNR];
    /*RUNS*/
    pthread_barrier_wait(&global_barrier);
    for (int i = 0; i < runNR; i++) {
        stop = 0;
        fprintf(stderr, "[%d] RUN %d\n", node_id, i);

        string barrier_key = "MB_RUN_" + to_string(i);
        dsm->barrier(barrier_key);
        pthread_barrier_wait(&global_barrier);

        sleep(duration);
        stop = 1;

        pthread_barrier_wait(&global_barrier);
    }
    for (int i = 0; i < threadNR; i++) {
        pthread_join(tasks[i].thread, NULL);
    }
    fprintf(stderr, "NODE %d DONE\n", node_id);
    dsm->barrier("fin");
    return 0;
}