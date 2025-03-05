#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

using namespace std;
#include <string>
#include <cstdio>
#include <pthread.h>

#include <numa.h>
#include "Tree.h"
#include "mb_utils.h"
#include <DSM.h>
// #include "zipf.h"


#define gettid() syscall(SYS_gettid)

#ifndef CYCLE_PER_US
#error Must define CYCLE_PER_US for the current machine in the Makefile or elsewhere
#endif

char *res_file_tp, *res_file_lat, *res_file_lock;
int threadNR, nodeNR, mnNR, lockNR, runNR,
nodeID, duration, mode, kReadRatio;
int pinning = 1;
uint64_t *lock_acqs;
uint64_t *lock_rels;

uint64_t dsmSize = 4;
uint64_t page_size = KB(1);
int chipSize = 128;
DSM *dsm;
DSMConfig config;
Tree *rlock;
pthread_barrier_t global_barrier;

double zipfian = 0;
int use_zipfian = 0;

extern Measurements measurements;
std::atomic_bool stop{false};

#include <iostream>
#include <random>
#include <cmath>
#include <vector>
class ZipfianGenerator {
    public:
        // Constructor now accepts a seed for the random number generator
        ZipfianGenerator(double alpha, int range, unsigned int seed = std::random_device{}())
            : alpha(alpha), range(range), harmonic_sum(0), rng(seed), dist(0.0, 1.0) {
            // Precompute the harmonic numbers and their sum
            harmonic_numbers.resize(range);
            for (int i = 1; i <= range; ++i) {
                harmonic_sum += 1.0 / std::pow(i, alpha);
                harmonic_numbers[i - 1] = harmonic_sum;
            }
        }
    
        int generate() {
            // Generate a random number between 0 and harmonic_sum
            double rand_val = dist(rng) * harmonic_sum;
            
            // Find the index corresponding to this random value (inverse transform sampling)
            for (int i = 0; i < range; ++i) {
                if (rand_val <= harmonic_numbers[i]) {
                    return i + 1; // Returning the index (1-based)
                }
            }
            
            return range; // If no match, return the last element
        }
    
    private:
        double alpha; // Zipfian exponent
        int range; // Range of integers
        double harmonic_sum; // The sum of the harmonic numbers for normalization
        std::vector<double> harmonic_numbers; // Precomputed harmonic numbers
        std::mt19937 rng; // Random number generator with custom seed
        std::uniform_real_distribution<> dist; // Uniform distribution between [0, 1)
};

void mn_worker() {
    DE("I AM A MN\n");
    // dsm->resetThread();
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
        if (check_MN_correctness(dsm, dsmSize, mnNR, nodeNR, nodeID, page_size)) {
            __error("MN LOCK ACQS INCORRECT");
        }
        else {
            DE("MN CORRECTNESS PASSED!\n");
        }
    #endif
    // dsm->stopDirThread();
    fprintf(stderr, "MN [%d] finished\n", nodeID);
    free_measurements();
    dsm->free_dsm();
    dsm->barrier("fin");
}

void *empty_cs_worker(void *arg) {
    Task *task = (Task *) arg;
    if (pinning == 1) {
        bindCore(thread_to_cpu_1n[task->id]);
    }
    else {
        bindCore(thread_to_cpu_2n[task->id]);
    }
    dsm->registerThread(page_size);
    rlock->set_threadID(dsm->getMyThreadID());
    GlobalAddress baseAddr;
    GlobalAddress lockAddr;
    baseAddr.nodeID = 0;
    baseAddr.offset = 0;
    lockAddr.nodeID = 0;
    lockAddr.offset = 0;

    pthread_barrier_wait(&global_barrier);
    while (!stop.load()) {
        baseAddr.nodeID = uniform_rand_int(mnNR);
        rlock->mb_lock(baseAddr, lockAddr, 0);
        task->lock_acqs++;
        rlock->mb_unlock(baseAddr, 0);
    }
    DE("[%d.%d] %lu ACQUISITIONS\n", dsm->getMyNodeID(), dsm->getMyThreadID(), task->lock_acqs);
    return 0;
}

void *mlocks_worker(void *arg) {
    Task *task = (Task *) arg;
    Timer timer;
    if (pinning == 1) {
        bindCore(thread_to_cpu_1n[task->id]);
    }
    else {
        bindCore(thread_to_cpu_2n[task->id]);
    }
    dsm->registerThread(page_size);
    int id = dsm->getMyThreadID();
    rlock->set_threadID(id);

    GlobalAddress baseAddr;
    GlobalAddress lockAddr;
    baseAddr.nodeID = 0;
    baseAddr.offset = 0;
    lockAddr.nodeID = 0;
    lockAddr.offset = 0;

    int *private_int_array = task->private_int_array;
    uint64_t *long_data;
    int lock_idx = 0;
    uint64_t range = rlock->getLockNR()-1;
    uint64_t chunk_size = GB(dsmSize) / rlock->getLockNR();
    int sum = 1;
    int data_len = page_size / sizeof(uint64_t);
    // int data_len = 1;
    uint64_t seed = nodeID*threadNR + id + 42;
    srand(seed);
    ZipfianGenerator zipfian(0.99, range, seed);
    // int num = 0;
    // int cnt = 10;

    pthread_barrier_wait(&global_barrier);

    while (!stop.load()) {
    // while (num < cnt) {
        for (int j = 0; j < 400; j++) {
            int idx = uniform_rand_int(PRIVATE_ARRAY_SZ / sizeof(int));
            private_int_array[idx] += sum;
        }
        for (int j = 0; j < 100; j++) {
            if (stop.load())
                break;
            // if (num >= cnt)
                break;
            if (use_zipfian) {
                lock_idx = zipfian.generate();
            }
            else {
                lock_idx = (uint64_t) uniform_rand_int(range+1);
            }
            // cerr << "mnNR: " << mnNR << endl;
            // baseAddr.nodeID = uniform_rand_int(mnNR);
            baseAddr.nodeID = uniform_rand_int(1);
            baseAddr.offset = chunk_size * lock_idx;
            lockAddr.nodeID = baseAddr.nodeID;
            lockAddr.offset = lock_idx * sizeof(uint64_t);
            // cerr << "lockAddr: " << lockAddr << endl <<
            // "baseAdrr: " << baseAddr << endl;
            rlock->mb_lock(baseAddr, lockAddr, page_size);
            // num++;
            lock_acqs[lockAddr.nodeID * lockNR + lock_idx]++;
            task->lock_acqs++;
            measurements.tp[id]++;
            measurements.loop_in_cs[id]++;

            timer.begin();
            long_data = (uint64_t *) rlock->getCurrPB();
            for (int k = 0; k < data_len; k++) {
                long_data[k] += 1;
                task->inc++;
            }
            save_measurement(id, measurements.lock_hold);
            
            lock_rels[lockAddr.nodeID * lockNR + lock_idx]++;
            rlock->mb_unlock(baseAddr, page_size);
        }
    }
    // DE("[%d.%d] %lu ACQUISITIONS\n", dsm->getMyNodeID(), dsm->getMyThreadID(), task->lock_acqs);
    return 0;
}

int main(int argc, char *argv[]) {
    parse_cli_args(
    &threadNR, &nodeNR, &mnNR, &lockNR, &runNR,
    &nodeID, &duration, &mode, &use_zipfian, 
    &kReadRatio, &pinning, &chipSize, &dsmSize,
    &res_file_tp, &res_file_lat, &res_file_lock,
    argc, argv);
    // dsmSize = 64 / mnNR;
    // DE("HI\n");
    if (nodeID == 1) {
        if(system("sudo bash /nfs/DAL/restartMemc.sh"))
            _error("Failed to start MEMC server\n");
        DE("STARTED MEMC SERVER\n");
    }
    else {
        sleep(1);
    }

    config.dsmSize = dsmSize;
    config.mnNR = mnNR > nodeNR ? nodeNR : mnNR;
    mnNR = config.mnNR;
    config.machineNR = nodeNR;
    config.threadNR = threadNR;
    config.chipSize = chipSize;
    // lockNR = chipSize * 1024 / sizeof(uint64_t);
    dsm = DSM::getInstance(config);
    nodeID = dsm->getMyNodeID();
    // register_sighandler(dsm);
    DE("DSM INIT DONE: %d\n", nodeID);

    if (nodeID == 0) {
        char val[sizeof(uint64_t)];
        uint64_t num = 0;
        memcpy(val, &num, sizeof(uint64_t));
        dsm->get_DSMKeeper()->memSet(ck.c_str(), ck.size(), val, sizeof(uint64_t));
    }

    /*WORKER*/
    void* (*worker)(void*);
    switch(mode) {
        case 0: worker = empty_cs_worker; break;
        case 1: worker = mlocks_worker; break;
        default: worker = empty_cs_worker; break;
    }

    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_barrier_init(&global_barrier, NULL, threadNR+1);

    // lockNR = chipSize * 1024 / sizeof(uint64_t);
    /*LOCK INIT*/
    dsm->registerThread();
    rlock = new Tree(dsm, 0, lockNR, true);
    dsm->resetThread();
    lock_acqs = (uint64_t *) malloc(nodeNR * lockNR * sizeof(uint64_t));
    lock_rels = (uint64_t *) malloc(nodeNR * lockNR * sizeof(uint64_t));
    memset(lock_acqs, 0, nodeNR*lockNR*sizeof(uint64_t));
    memset(lock_rels, 0, nodeNR*lockNR*sizeof(uint64_t));

    /*TASK INIT*/
    Task *tasks = new Task[threadNR];
    measurements.duration = duration;
    for (int i = 0; i < threadNR; i++) {
        tasks[i].id = i;
        tasks[i].disa = 'y';
        pthread_create(&tasks[i].thread, NULL, worker, &tasks[i]);
    }
    

    /*RUN*/
    dsm->barrier("MB_BEGIN");
    pthread_barrier_wait(&global_barrier);
    if (nodeID == 0) {
        DE("RUN %d\n", runNR);
    }

    sleep(duration);
    stop.store(true);

    for (int i = 0; i < threadNR; i++) {
        pthread_join(tasks[i].thread, NULL);
    }
    for (int n = 0; n < nodeNR; n++) {
        if (n == nodeID) {
            write_tp(res_file_tp, res_file_lock, runNR, threadNR, lockNR, n, page_size);
            write_lat(res_file_lat, runNR, lockNR, n, page_size);
        }
        string writeResKey = "WRITE_RES_" + to_string(n);
        dsm->barrier(writeResKey);
    }
    dsm->barrier("MB_END");
    if (nodeID == 0) {
        DE("WRITTEN RESULTS\n");
    }

    #ifdef CORRECTNESS
        DE("CN checking correctness\n");
        if (check_CN_correctness(tasks, lock_acqs, lock_rels, lockNR, threadNR, dsm, nodeID)) {
            __error("CN LOCK_ACQS INCORRECT");
        }
        else {
            DE("CN CORRECTNESS PASSED!\n");
        }
        dsm->barrier("CN_CORRECTNESS");

        if(nodeID == 0) {
            if (check_MN_correctness(dsm, dsmSize, mnNR, nodeNR, nodeID, page_size)) {
                __error("MN CORRECTNESS CHECK FAILED\n");
            }
            else {
                DE("MN CORRECTNESS PASSED!\n");
            }
        }
    #endif

    fprintf(stderr, "DSM NODE %d DONE\n", nodeID);
    free_measurements();
    dsm->free_dsm();
    dsm->barrier("fin");
    return 0;
}