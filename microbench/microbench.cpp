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
#include "zipf.h"


#define gettid() syscall(SYS_gettid)

#ifndef CYCLE_PER_US
#error Must define CYCLE_PER_US for the current machine in the Makefile or elsewhere
#endif

char *res_file_tp, *res_file_lat, *res_file_lock;
int threadNR, nodeNR, mnNR, lockNR, runNR,
nodeID, duration, mode, kReadRatio, maxHandover, colocate;
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

double zipfian = 0.99;

// SINGLE MACHINE
int *shared_arr;
size_t shared_arr_sz = MAX_ARRAY_SIZE;
pthread_mutex_t* locks;
int chunk_byte_sz = KB(256);
int ints_per_chunk = chunk_byte_sz / sizeof(int);

extern Measurements measurements;
std::atomic_bool stop{false};

#include <iostream>
#include <random>
#include <cmath>
#include <vector>


class ZipfianGenerator {
    public:
        ZipfianGenerator(double alpha, int range, unsigned int seed = std::random_device{}())
            : alpha(alpha), range(range), rng(seed), dist(0.0, 1.0) {
            harmonic_numbers.reserve(range);
            double harmonic_sum = 0.0;
            for (int i = 0; i < range; ++i) {
                harmonic_sum += 1.0 / std::pow(i + 1, alpha);
                harmonic_numbers.push_back(harmonic_sum);
            }
            norm_factor = harmonic_sum;
        }
    
        int generate() {
            double rand_val = dist(rng) * norm_factor;
            
            auto it = std::lower_bound(harmonic_numbers.begin(), harmonic_numbers.end(), rand_val);
            return std::distance(harmonic_numbers.begin(), it);
        }
    
    private:
        double alpha; // Zipfian exponent
        int range; // Range of integers [0, range-1]
        double norm_factor; // Precomputed normalization factor
        std::vector<double> harmonic_numbers; // Precomputed harmonic sum
        std::mt19937 rng;
        std::uniform_real_distribution<> dist; // Uniform distribution between [0, 1)
    };


void *empty_cs_worker(void *arg) {
    Task *task = (Task *) arg;
    int cpu = 0;
    if (pinning == 1) {
        cpu = thread_to_cpu_1n[task->id];
        bindCore(cpu);
    }
    else {
        cpu = thread_to_cpu_2n[task->id];
        bindCore(cpu);
    }
    dsm->registerThread(page_size);
    int id = dsm->getMyThreadID();
    rlock->set_IDs(nodeID, id);

    GlobalAddress baseAddr;
    GlobalAddress lockAddr;
    baseAddr.nodeID = 0;
    baseAddr.offset = 0;
    lockAddr.nodeID = 0;
    lockAddr.offset = 0;

    int lock_idx = 0;
    uint64_t range = (lockNR * mnNR);

    struct zipf_gen_state state;
    mehcached_zipf_init(&state, range, zipfian,
                        (rdtsc() & (0x0000ffffffffffffull)) ^ id);
    
    int fd = setup_perf_event(cpu);
    start_perf_event(fd);

    pthread_barrier_wait(&global_barrier);
    while (!stop.load()) {
        lock_idx = mehcached_zipf_next(&state);

        lockAddr.nodeID = lock_idx / lockNR;
        lock_idx /= mnNR;
        lockAddr.offset = lock_idx * sizeof(uint64_t);

        rlock->mb_lock(baseAddr, lockAddr, 0);

        lock_acqs[lockAddr.nodeID * lockNR + lock_idx]++;
        task->lock_acqs++;
        measurements.tp[id]++;

        rlock->mb_unlock(baseAddr, 0);

        lock_rels[lockAddr.nodeID * lockNR + lock_idx]++;
    }
    measurements.cache_misses[id] = stop_perf_event(fd);
    // DE("[%d.%d] %lu ACQUISITIONS\n", dsm->getMyNodeID(), dsm->getMyThreadID(), task->lock_acqs);
    return 0;
}

void *mlocks_worker(void *arg) {
    Task *task = (Task *) arg;
    Timer timer;
    int cpu = 0;
    if (pinning == 1) {
        cpu = thread_to_cpu_1n[task->id];
        bindCore(cpu);
    }
    else {
        cpu = thread_to_cpu_2n[task->id];
        bindCore(cpu);
    }
    dsm->registerThread(page_size);
    int id = dsm->getMyThreadID();
    rlock->set_IDs(nodeID, id);
    
    GlobalAddress baseAddr;
    GlobalAddress lockAddr;
    baseAddr.offset = 0;
    baseAddr.nodeID = 0;
    lockAddr.nodeID = 0;
    lockAddr.offset = 0;
    
    int *private_int_array = task->private_int_array;
    uint64_t *long_data;
    int lock_idx = 0;
    uint64_t range = (lockNR * mnNR);
    uint64_t chunk_size = GB(dsmSize) / rlock->getLockNR();
    int sum = 1;
    int data_len = page_size / sizeof(uint64_t);
    // int data_len = 1;
    uint64_t seed = nodeID*threadNR + id + 42;
    srand(seed);
    struct zipf_gen_state state;
    mehcached_zipf_init(&state, range, zipfian,
                        (rdtsc() & (0x0000ffffffffffffull)) ^ id);
    // int num = 0;
    // int cnt = 100;
    
    int fd = setup_perf_event(cpu);
    start_perf_event(fd);

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
            lock_idx = mehcached_zipf_next(&state);
            assert(lock_idx < lockNR * mnNR);

            baseAddr.nodeID = lock_idx / lockNR;
            lock_idx /= mnNR;
            baseAddr.offset = chunk_size * lock_idx;
            lockAddr.nodeID = baseAddr.nodeID;
            lockAddr.offset = lock_idx * sizeof(uint64_t);

            rlock->mb_lock(baseAddr, lockAddr, page_size);

            // num++;
            lock_acqs[lockAddr.nodeID * lockNR + lock_idx]++;
            task->lock_acqs++;
            measurements.tp[id]++;
            measurements.loop_in_cs[id]++;

            timer.begin();
            long_data = (uint64_t *) rlock->getCurrPB();
            // for (int x = 0; x < 100; x++) {
            for (int k = 0; k < data_len; k++) {
                long_data[k] += 1;
                task->inc++;
            }
            // }
            save_measurement(id, measurements.lock_hold);
            
            lock_rels[lockAddr.nodeID * lockNR + lock_idx]++;
            rlock->mb_unlock(baseAddr, page_size);
        }
    }
    measurements.cache_misses[id] = stop_perf_event(fd);
    // DE("[%d.%d] %lu ACQUISITIONS\n", dsm->getMyNodeID(), dsm->getMyThreadID(), task->lock_acqs);
    return 0;
}

void *single_machine(void *arg) {
    Task *task = (Task *) arg;
    Timer timer;
    int cpu = 0;
    int id = task->id;
    if (pinning == 1) {
        cpu = thread_to_cpu_1n[task->id];
        bindCore(cpu);
    }
    else {
        cpu = thread_to_cpu_2n[task->id];
        bindCore(cpu);
    }
    int *private_int_array = task->private_int_array;
    uint64_t lock_idx = 0;
    uint64_t data_idx = 0;
    int sum = 1;
    // int lock_chunk_sz = shared_arr_sz / sizeof(int) / lockNR;
    int data_len = ints_per_chunk;
    uint64_t range = (shared_arr_sz - data_len) / data_len;

    
    struct zipf_gen_state state;
    mehcached_zipf_init(&state, range, zipfian,
                        (rdtsc() & (0x0000ffffffffffffull)) ^ id);

    int fd = setup_perf_event(cpu);
    start_perf_event(fd);

    pthread_barrier_wait(&global_barrier);

    while (!stop.load()) {
        
        for (int j = 0; j < 400; j++) {
            int idx = uniform_rand_int(PRIVATE_ARRAY_SZ / sizeof(int));
            private_int_array[idx] += sum;
        }
        for (int j = 0; j < 100; j++) {
            if (stop.load())
                break;

            data_idx = mehcached_zipf_next(&state);
            assert(data_idx <= range);
            lock_idx = CityHash64((char *)&data_idx, sizeof(data_idx)) % lockNR;

            timer.begin();
            pthread_mutex_lock(&locks[lock_idx]);
            save_measurement(task->id, measurements.lwait_acq);

            lock_acqs[lock_idx]++;
            task->lock_acqs++;
            measurements.tp[task->id]++;

            timer.begin();
            // for (int x = 0; x < 100; x++) {
            for (size_t k = data_idx; k < data_idx+data_len; k++) {
                shared_arr[k] += 1;
                measurements.loop_in_cs[task->id]++;
                task->inc++;
            }
            // }
            save_measurement(task->id, measurements.lock_hold);
            
            timer.begin();
            pthread_mutex_unlock(&locks[lock_idx]);
            save_measurement(task->id, measurements.lwait_rel);
            lock_rels[lock_idx]++;
        }
    }
    measurements.cache_misses[task->id] = stop_perf_event(fd);
    return 0;
}

int main(int argc, char *argv[]) {
    parse_cli_args(
    &threadNR, &nodeNR, &mnNR, &lockNR, &runNR,
    &nodeID, &duration, &mode, &zipfian, 
    &kReadRatio, &pinning, &chipSize, &dsmSize,
    &maxHandover, &colocate,
    &res_file_tp, &res_file_lat, &res_file_lock,
    argc, argv);

    if (nodeID == 1 && mode < 2) {
        if(system("sudo bash /nfs/DAL/restartMemc.sh"))
            _error("Failed to start MEMC server\n");
    }
    else {
        sleep(1);
    }

    /*CONFIG*/
    config.dsmSize = dsmSize;
    config.mnNR = mnNR > nodeNR ? nodeNR : mnNR;
    mnNR = config.mnNR;
    config.machineNR = nodeNR;
    config.threadNR = threadNR;
    lockNR = lockNR / mnNR;
    config.chipSize = lockNR * sizeof(uint64_t);
    config.lockMetaSize = config.chipSize;
    config.lockNR = lockNR;
    // lockNR = chipSize * 1024 / sizeof(uint64_t);
    
    // register_sighandler(dsm);
    if (mode < 2) {
        dsm = DSM::getInstance(config);
        nodeID = dsm->getMyNodeID();
        if (nodeID == 0) {
            fprintf(stderr, "DSM INIT DONE\n");
        }
    
        if (nodeID == 0) {
            char val[sizeof(uint64_t)];
            uint64_t num = 0;
            memcpy(val, &num, sizeof(uint64_t));
            dsm->get_DSMKeeper()->memSet(ck.c_str(), ck.size(), val, sizeof(uint64_t));
        }

        dsm->registerThread();
        rlock = new Tree(dsm, 0, lockNR, true, maxHandover, true);
        dsm->resetThread();
    }
    else {
        rlock = new Tree(dsm, 0, lockNR, true, maxHandover, false);
        nodeID = 0;
    }

    /*WORKER*/
    void* (*worker)(void*);
    switch(mode) {
        case 0: worker = empty_cs_worker; break;
        case 1: worker = mlocks_worker; break;
        case 2: worker = single_machine; break;
        default: worker = empty_cs_worker; break;
    }


    /*LOCK INIT*/
    lock_acqs = (uint64_t *) malloc(nodeNR * lockNR * sizeof(uint64_t));
    lock_rels = (uint64_t *) malloc(nodeNR * lockNR * sizeof(uint64_t));
    memset(lock_acqs, 0, nodeNR*lockNR*sizeof(uint64_t));
    memset(lock_rels, 0, nodeNR*lockNR*sizeof(uint64_t));

    /*TASK INIT*/
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_barrier_init(&global_barrier, NULL, threadNR+1);
    if (mode > 1) {
        shared_arr = (int *) numa_alloc_onnode(shared_arr_sz, 0);
        locks = (pthread_mutex_t *) malloc(sizeof(pthread_mutex_t) * lockNR);
        for (int i = 0; i < lockNR; i++) {
            pthread_mutex_init(&locks[i], NULL);
        }
    }

    Task *tasks = new Task[threadNR];
    measurements.duration = duration;
    if (colocate || nodeNR >= mnNR) {
        for (int i = 0; i < threadNR; i++) {
            tasks[i].id = i;
            tasks[i].disa = 'y';
            pthread_create(&tasks[i].thread, NULL, worker, &tasks[i]);
        }
    }

    /*RUN*/
    if (mode < 2) {
        dsm->barrier("MB_BEGIN");
    }
    if (colocate || nodeID >= mnNR) {
        pthread_barrier_wait(&global_barrier);
    
        sleep(duration);
        stop.store(true);
    
        for (int i = 0; i < threadNR; i++) {
            pthread_join(tasks[i].thread, NULL);
        }
        for (int n = 0; n < nodeNR; n++) {
            if (n == nodeID) {
                write_tp(res_file_tp, res_file_lock, runNR,  lockNR*mnNR, n, page_size, pinning,
                        nodeNR, mnNR, threadNR, maxHandover, colocate, zipfian);
                write_lat(res_file_lat, runNR, lockNR*mnNR, n, page_size, pinning,
                        nodeNR, mnNR, threadNR, maxHandover, colocate, zipfian);
            }
            if (mode < 2) {
                string writeResKey = "WRITE_RES_" + to_string(n);
                dsm->barrier(writeResKey);
            }
        }
    } else {
        for (int n = 0; n < nodeNR; n++) {
            string writeResKey = "WRITE_RES_" + to_string(n);
            dsm->barrier(writeResKey);
        }
    }

    if (mode < 2) {
        dsm->barrier("MB_END");
    }
    if (nodeID == 0) {
        fprintf(stderr, "WRITTEN RESULTS\n");
    }

    __asm__ volatile("mfence" ::: "memory");

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

    free_measurements();
    if (mode < 2) {
        dsm->free_dsm();
        dsm->barrier("fin");
    } else {
        numa_free(shared_arr, shared_arr_sz);
        for (int i = 0; i < lockNR; i++) {
            pthread_mutex_destroy(&locks[i]);
        }
        free(locks);
    }
    if (nodeID == 0) {
        fprintf(stderr, "FIN\n");
    }
    return 0;
}