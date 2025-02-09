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
char *res_file_tp, *res_file_lat;
int threadNR, nodeNR, mnNR, lockNR, runNR,
nodeID, duration, mode;
uint64_t dsmSize;
uint64_t *lock_acqs;
uint64_t *lock_rels;

int page_size = KB(1);
DSM *dsm;
DSMConfig config;
Rlock *rlock;
pthread_barrier_t global_barrier;

extern Measurements measurements;

void mn_worker() {
    DE("I AM A MN\n");
    char val[sizeof(uint64_t)];
    uint64_t num = 0;
    memcpy(val, &num, sizeof(uint64_t));
    dsm->get_DSMKeeper()->memSet(ck.c_str(), ck.size(), val, sizeof(uint64_t));
    dsm->barrier("MB_BEGIN");

    for (int n = 0; n < nodeNR; n++) {
        string writeResKey = "WRITE_RES_" + to_string(n);
        dsm->barrier(writeResKey);
        DE("[%d] MN WRITE BARRIER %d PASSED\n", nodeID, n);
    }

    dsm->barrier("MB_END");
    #ifdef CORRECTNESS
        dsm->barrier("CORRECTNESS");
        DE("MN checking correctness\n");
        if (check_MN_correctness(dsm, dsmSize, mnNR, nodeNR, nodeID)) {
            __error("MN LOCK ACQS INCORRECT");
        }
        else {
            DE("MN CORRECTNESS PASSED!\n");
        }
    #endif
    fprintf(stderr, "MN [%d] finished\n", nodeID);
    dsm->barrier("fin");
}

void *correctness_worker(void *arg) {
    Task *task = (Task *) arg;
    dsm->registerThread();
    GlobalAddress baseAddr;
    baseAddr.nodeID = 0;
    baseAddr.offset = 0;
    uint64_t *long_data;
    int lock_idx = 0;
    uint64_t range = GB(config.dsmSize) / sizeof(uint64_t);

    pthread_barrier_wait(&global_barrier);

    while (!*task->stop) {
        int data_idx = uniform_rand_int(range);
        baseAddr.offset = data_idx * sizeof(uint64_t);
        rlock->lock_acquire(baseAddr, sizeof(uint64_t));
        lock_idx = rlock->getCurrLockAddr().offset / sizeof(uint64_t);
        lock_acqs[lock_idx]++;
        task->lock_acqs++;
        long_data = (uint64_t *) rlock->getCurrPB();
        long_data[0]++;
        rlock->lock_release(baseAddr, sizeof(uint64_t));
        lock_rels[lock_idx]++;
    }
    pthread_barrier_wait(&global_barrier);
    DE("[%d.%d] %lu ACQUISITIONS\n", dsm->getMyNodeID(), dsm->getMyThreadID(), task->lock_acqs);
    return 0;
}

void *empty_cs_worker(void *arg) {
    Task *task = (Task *) arg;
    bindCore(task->id);
    dsm->registerThread(page_size);
    set_id(dsm->getMyThreadID());
    GlobalAddress baseAddr;
    baseAddr.nodeID = 0;
    baseAddr.offset = 0;

    pthread_barrier_wait(&global_barrier);
    while (!*task->stop) {
        rlock->lock_acquire(baseAddr, 0);
        rlock->lock_release(baseAddr, 0);
    }
    pthread_barrier_wait(&global_barrier);
    DE("[%d.%d] %lu ACQUISITIONS\n", dsm->getMyNodeID(), dsm->getMyThreadID(), task->lock_acqs);
    return 0;
}

void *mlocks_worker(void *arg) {
    Task *task = (Task *) arg;
    Timer timer = task->timer;
    bindCore(task->id);
    dsm->registerThread(page_size);
    set_id(dsm->getMyThreadID());
    GlobalAddress baseAddr;
    baseAddr.nodeID = 0;
    baseAddr.offset = 0;
    int *private_int_array = task->private_int_array;
    uint64_t *long_data;
    int lock_idx = 0;
    uint64_t range = (GB(config.dsmSize) - page_size) / page_size;
    volatile int sum = 0;
    int data_len = dsm->get_rbuf(0).getkPageSize() / sizeof(uint64_t);
    srand(nodeID*threadNR + dsm->getMyThreadID() + 42);

    pthread_barrier_wait(&global_barrier);

    while (!*task->stop) {
        for (int j = 0; j < 400; j++) {
            int idx = uniform_rand_int(PRIVATE_ARRAY_SZ / sizeof(int));
            private_int_array[idx] += sum;
        }
        for (int j = 0; j < 100; j++) {
            if (*task->stop)
                break;
            int data_idx = uniform_rand_int(range);
            baseAddr.offset = data_idx * page_size;
            rlock->lock_acquire(baseAddr, data_len * sizeof(uint64_t));
            lock_idx = rlock->getCurrLockAddr().offset / sizeof(uint64_t);
            lock_acqs[lock_idx]++;
            task->lock_acqs++;

            timer.begin();
            long_data = (uint64_t *) rlock->getCurrPB();
            long_data[0]++;
            for (int k = 0; k < data_len; k++) {
                sum += long_data[k];
            }
            save_measurement(measurements.lock_hold);
            
            rlock->lock_release(baseAddr, data_len);
            lock_rels[lock_idx]++;
        }
    }
    pthread_barrier_wait(&global_barrier);
    DE("[%d.%d] %lu ACQUISITIONS\n", dsm->getMyNodeID(), dsm->getMyThreadID(), task->lock_acqs);
    return 0;
}

int main(int argc, char *argv[]) {
    parse_cli_args(
    &threadNR, &nodeNR, &mnNR, &lockNR, &runNR,
    &nodeID, &duration, &mode,
    &res_file_tp, &res_file_lat,
    argc, argv);
    dsmSize = 1;
    DE("HI\n");
    if (nodeID == 1) {
        if(system("sudo bash /nfs/DAL/restartMemc.sh"))
            _error("Failed to start MEMC server\n");
        DE("STARTED MEMC SERVER\n");
    }

    config.dsmSize = dsmSize;
    config.mnNR = mnNR;
    config.machineNR = nodeNR;
    config.threadNR = threadNR;
    // config.clusterID = nodeID;
    dsm = DSM::getInstance(config);
    nodeID = dsm->getMyNodeID();
    DE("DSM INIT DONE: DSM NODE %d\n", nodeID);

    /*MN*/
    if (nodeID < mnNR) {
        mn_worker();
        return 0;
    }

    /*WORKER*/
    void* (*worker)(void*);
    switch(mode) {
        case 0: worker = empty_cs_worker; break;
        case 1: worker = mlocks_worker; break;
        case 2: worker = correctness_worker; break;
        default: worker = empty_cs_worker; break;
    }

    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_barrier_init(&global_barrier, NULL, threadNR+1);

    /*TASK INIT*/
    Task *tasks = new Task[threadNR];
    measurements.duration = duration;
    alignas(CACHELINE_SIZE) volatile int stop = 0;
    for (int i = 0; i < threadNR; i++) {
        tasks[i].id = i;
        tasks[i].stop = &stop;
        pthread_create(&tasks[i].thread, NULL, worker, &tasks[i]);
    }
    
    /*LOCK INIT*/
    rlock = new Rlock(dsm, lockNR);
    lock_acqs = new uint64_t[lockNR];
    lock_rels = new uint64_t[lockNR];
    memset(lock_acqs, 0, lockNR*sizeof(uint64_t));
    memset(lock_rels, 0, lockNR*sizeof(uint64_t));

    /*RUNS*/
    stop = 0;
    dsm->barrier("MB_BEGIN");
    pthread_barrier_wait(&global_barrier);
    DE("RUN %d\n", runNR);

    sleep(duration);
    stop = 1;

    pthread_barrier_wait(&global_barrier);
    for (int n = 0; n < nodeNR; n++) {
        if (n == nodeID) {
            write_tp(res_file_tp, runNR, threadNR, lockNR, n, page_size);
            write_lat(res_file_lat, runNR, lockNR, n, page_size);
        }
        string writeResKey = "WRITE_RES_" + to_string(n);
        dsm->barrier(writeResKey);
        DE("[%d] WRITE BARRIER %d PASSED\n", nodeID, n);
    }
    for (int i = 0; i < threadNR; i++) {
        pthread_join(tasks[i].thread, NULL);
    }
    dsm->barrier("MB_END");

    #ifdef CORRECTNESS
        DE("CN checking correctness\n");
        if (check_CN_correctness(tasks, lock_acqs, lock_rels, lockNR, threadNR, dsm, nodeID)) {
            __error("CN LOCK_ACQS INCORRECT");
        }
        else {
            DE("CN CORRECTNESS PASSED!\n");
        }
        dsm->barrier("CORRECTNESS");
    #endif

    fprintf(stderr, "DSM NODE %d DONE\n", nodeID);
    dsm->barrier("fin");
    return 0;
}