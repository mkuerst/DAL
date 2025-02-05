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
char *array1;
char *res_file_cum, *res_file_single;
int threadNR, nodeNR, mnNR, lockNR, runNR,
node_id, duration, mode;


DSM *dsm;
pthread_barrier_t global_barrier;
pthread_barrier_t local_barrier;

 
void mn_func() {
    int r = 0;
    for (int i = 0; i < runNR; i++) {
        r++;
        string barrier_key = "MB_RUN_" + to_string(i);
        dsm->barrier(barrier_key);
    }
}
void *empty_cs_worker(void *arg) {
    Task *task = (Task *) arg;
    dsm->registerThread();
    // uint64_t all_thread = threadNR * dsm->getClusterSize();
    // int task_id = threadNR * dsm->getMyNodeID() + task->id;
    pthread_barrier_wait(&global_barrier);
    for (int i = 0; i < runNR; i++) {
        pthread_barrier_wait(&global_barrier);
        pthread_barrier_wait(&global_barrier);
    }
    return 0;
}


int main(int argc, char *argv[]) {
    parse_cli_args(
    &threadNR, &nodeNR, &mnNR, &lockNR, &runNR,
    &node_id, &duration, &mode,
    &res_file_cum, &res_file_single,
    argc, argv);
    DE("[%d] HI\n", node_id);
    if (node_id == 1) {
        system("sudo bash /nfs/DAL/restartMemc.sh");
        DE("[%d] STARTED MEMC SERVER\n", node_id);
    }
    else {
        sleep(1);
    }
    DSMConfig config;
    config.mnNR = mnNR;
    config.machineNR = nodeNR;
    config.threadNR = threadNR;
    config.clusterId = node_id;
    dsm = DSM::getInstance(config);
    DE("[%d] DSM Init DONE\n", node_id);
    if (dsm->getMyNodeID() < mnNR) {
        DE("[%d] MN will busy spin\n", node_id);
        mn_func();
        fprintf(stderr, "MN [%d] finished\n", node_id);
        dsm->barrier("fin");
        return 0;
    }
    Task *tasks = new Task[threadNR];

    /*WORKER*/
    void* (*worker)(void*) = empty_cs_worker;
    volatile int alignas(CACHELINE_SIZE) stop = 0;

    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_barrier_init(&global_barrier, NULL, threadNR+1);
    pthread_barrier_init(&local_barrier, NULL, threadNR);

    for (int i = 0; i < threadNR; i++) {
        tasks[i].id = i;
        tasks[i].stop = &stop;
        pthread_create(&tasks[i].thread, NULL, worker, &tasks[i]);
    }
    /*RUNS*/
    pthread_barrier_wait(&global_barrier);
    for (int i = 0; i < runNR; i++) {
        size_t array_sz = MAX_ARRAY_SIZE;
        stop = 0;
        fprintf(stderr, "[%d] RUN %d\n", node_id, i);

        string barrier_key = "MB_RUN_" + to_string(i);
        dsm->barrier(barrier_key);
        pthread_barrier_wait(&global_barrier);

        sleep(duration);
        stop = 1;

        pthread_barrier_wait(&global_barrier);
        if (mode == 3 || mode == 4) {
            numa_free(array0, array_sz);
            numa_free(array1, array_sz);
        }
    }
    for (int i = 0; i < threadNR; i++) {
        pthread_join(tasks[i].thread, NULL);
    }
    fprintf(stderr, "NODE %d DONE\n", node_id);
    dsm->barrier("fin");
    return 0;
}