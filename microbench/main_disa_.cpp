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
uint64_t *lock_rels;

DSM *dsm;
DSMConfig config;
Rlock *rlock;
pthread_barrier_t global_barrier;

void mn_worker() {
    char val[sizeof(uint64_t)];
    uint64_t num = 0;
    memcpy(val, &num, sizeof(uint64_t));
    dsm->get_DSMKeeper()->memSet(ck.c_str(), ck.size(), val, sizeof(uint64_t));
    for (int i = 0; i < runNR; i++) {
        string barrierKey = "MB_RUN_" + to_string(i);
        dsm->barrier(barrierKey);
    }
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

    for (int i = 0; i < runNR; i++) {
        pthread_barrier_wait(&global_barrier);
        while (!*task->stop) {
            // TODO: Generate rand lock_idx/data_addr
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
    }
    DE("[%d.%d] %lu ACQUISITIONS\n", dsm->getMyNodeID(), dsm->getMyThreadID(), task->lock_acqs);
    return 0;
}

void *empty_cs_worker(void *arg) {
    Task *task = (Task *) arg;
    dsm->registerThread();
    GlobalAddress baseAddr;
    baseAddr.nodeID = 0;
    baseAddr.offset = 0;
    pthread_barrier_wait(&global_barrier);
    for (int i = 0; i < runNR; i++) {
        pthread_barrier_wait(&global_barrier);
        while (!*task->stop) {
            rlock->lock_acquire(baseAddr, 0);
            task->lock_acqs++;
            rlock->lock_release(baseAddr, 0);
        }
        pthread_barrier_wait(&global_barrier);
    }
    DE("[%d.%d] %lu ACQUISITIONS\n", dsm->getMyNodeID(), dsm->getMyThreadID(), task->lock_acqs);
    return 0;
}

void *mlocks_worker(void *arg) {
    Task *task = (Task *) arg;
    bindCore(task->id);
    dsm->registerThread();
    GlobalAddress baseAddr;
    baseAddr.nodeID = 0;
    baseAddr.offset = 0;
    int *private_int_array = task->private_int_array;
    uint64_t *long_data;
    int lock_idx = 0;
    uint64_t range = GB(config.dsmSize) / sizeof(uint64_t);
    volatile int sum = 0;
    int data_len = dsm->get_rbuf(0).getkPageSize() / sizeof(uint64_t);

    pthread_barrier_wait(&global_barrier);

    for (int i = 0; i < runNR; i++) {
        srand(node_id*threadNR + dsm->getMyThreadID() + 42);
        pthread_barrier_wait(&global_barrier);
        while (!*task->stop) {
            for (int j = 0; j < 400; j++) {
                int idx = uniform_rand_int(PRIVATE_ARRAY_SZ / sizeof(int));
                private_int_array[idx] += sum;
            }
            for (int j = 0; j < 100; j++) {
                int data_idx = uniform_rand_int(range);
                baseAddr.offset = data_idx * sizeof(uint64_t);
                rlock->lock_acquire(baseAddr, sizeof(uint64_t));
                lock_idx = rlock->getCurrLockAddr().offset / sizeof(uint64_t);
                lock_acqs[lock_idx]++;
                task->lock_acqs++;
                long_data = (uint64_t *) rlock->getCurrPB();
                long_data[0]++;
                for (int k = 0; k < data_len; k++) {
                    sum += long_data[k];
                }
                rlock->lock_release(baseAddr, sizeof(uint64_t));
                lock_rels[lock_idx]++;
            }
        }
        pthread_barrier_wait(&global_barrier);
    }
    DE("[%d.%d] %lu ACQUISITIONS\n", dsm->getMyNodeID(), dsm->getMyThreadID(), task->lock_acqs);
    return 0;
}

int main(int argc, char *argv[]) {
    parse_cli_args(
    &threadNR, &nodeNR, &mnNR, &lockNR, &runNR,
    &node_id, &duration, &mode,
    &res_file_cum, &res_file_single,
    argc, argv);
    dsmSize = 1;
    DE("HI\n");
    if (node_id == 1) {
        system("sudo bash /nfs/DAL/restartMemc.sh");
        DE("STARTED MEMC SERVER\n");
    }
    else {
        sleep(1);
    }

    config.dsmSize = dsmSize;
    config.mnNR = mnNR;
    config.machineNR = nodeNR;
    config.threadNR = threadNR;
    config.clusterID = node_id;
    dsm = DSM::getInstance(config);
    DE("DSM Init DONE\n");

    if (dsm->getMyNodeID() < mnNR) {
        DE("[%d] MN will busy spin\n", node_id);
        mn_worker();
        dsm->barrier("MB_END");
        #ifdef CORRECTNESS
        dsm->barrier("CORRECTNESS");
        DE("MN checking correctness\n");
        if (check_MN_correctness(dsm, dsmSize, mnNR, nodeNR, node_id))
            __error("MN LOCK ACQS INCORRECT");
        #endif
        fprintf(stderr, "MN [%d] finished\n", node_id);
        dsm->barrier("fin");
        return 0;
    }

    /*WORKER*/
    void* (*worker)(void*) = mlocks_worker;

    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_barrier_init(&global_barrier, NULL, threadNR+1);

    /*TASK INIT*/
    Task *tasks = new Task[threadNR];
    alignas(CACHELINE_SIZE) volatile int stop = 0;
    for (int i = 0; i < threadNR; i++) {
        tasks[i].id = i;
        tasks[i].stop = &stop;
        pthread_create(&tasks[i].thread, NULL, worker, &tasks[i]);
    }
    /*LOCK INIT*/
    rlock = new Rlock(dsm, lockNR, MB(32));
    lock_acqs = new uint64_t[lockNR];
    lock_rels = new uint64_t[lockNR];
    memset(lock_acqs, 0, lockNR*sizeof(uint64_t));
    memset(lock_rels, 0, lockNR*sizeof(uint64_t));
    /*RUNS*/
    pthread_barrier_wait(&global_barrier);
    for (int i = 0; i < runNR; i++) {
        stop = 0;
        fprintf(stderr, "[%d] RUN %d\n", node_id, i);

        string barrierKey = "MB_RUN_" + to_string(i);
        dsm->barrier(barrierKey);
        pthread_barrier_wait(&global_barrier);

        sleep(duration);
        stop = 1;

        pthread_barrier_wait(&global_barrier);
    }
    for (int i = 0; i < threadNR; i++) {
        pthread_join(tasks[i].thread, NULL);
    }
    dsm->barrier("MB_END");
    #ifdef CORRECTNESS
    DE("CN checking correctness\n");
    if (check_CN_correctness(tasks, lock_acqs, lock_rels, lockNR, threadNR, dsm, node_id))
        __error("CN LOCK_ACQS INCORRECT");
    dsm->barrier("CORRECTNESS");
    #endif
    fprintf(stderr, "NODE %d DONE\n", node_id);
    dsm->barrier("fin");
    return 0;
}