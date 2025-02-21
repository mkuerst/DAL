#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

using namespace std;
#include <string>
#include <cstdio>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

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
nodeID, duration, mode;
int pinning = 1;
uint64_t *lock_acqs;
uint64_t *lock_rels;

uint64_t dsmSize = 8;
uint64_t page_size = KB(1);
int chipSize = 128;
DSM *dsm;
DSMConfig config;
Tree *tree;

double zipfan = 0.99;
int use_zipfan = 0;

int kReadRatio = 50;
uint64_t kKeySpace = 60 * define::MB;
double kWarmRatio = 0.2;

extern uint64_t cache_miss[MAX_APP_THREAD][8];
extern uint64_t cache_hit[MAX_APP_THREAD][8];
extern uint64_t latency[MAX_APP_THREAD][LATENCY_WINDOWS];
uint64_t latency_th_all[LATENCY_WINDOWS];

extern Measurements measurements;


struct sigaction sa;

void cleanup() {
    printf("Cleaning up resources before exit...\n");
    dsm->free_dsm();
}

void signal_handler(int sig) {
    printf("Received signal %d (%s)\n", sig, strsignal(sig));
    cleanup();
    exit(EXIT_FAILURE);
}

void register_sighandler() {
    sa.sa_handler = signal_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    int signals[] = {SIGINT, SIGTERM, SIGQUIT, SIGHUP, SIGABRT, SIGSEGV, SIGBUS, SIGFPE, SIGILL};
    for (size_t i = 0; i < sizeof(signals) / sizeof(signals[0]); i++) {
        if (sigaction(signals[i], &sa, NULL) == -1) {
            perror("sigaction");
            exit(EXIT_FAILURE);
        }
    }
}

inline Key to_key(uint64_t k) {
  return (CityHash64((char *)&k, sizeof(k)) + 1) % kKeySpace;
}

void mn_worker() {
    DE("I AM A MN\n");
    char val[sizeof(uint64_t)];
    uint64_t num = 0;
    memcpy(val, &num, sizeof(uint64_t));
    dsm->get_DSMKeeper()->memSet(ck.c_str(), ck.size(), val, sizeof(uint64_t));

    for (uint64_t i = 1; i < 1024000; ++i) {
        tree->insert(to_key(i), i * 2);
        // DE("MN INSERTED KEY %ld / 1024000\n", i);
    }
    dsm->barrier("benchmark");
    dsm->resetThread();
    dsm->barrier("warm_finish");

    for (int n = 0; n < nodeNR; n++) {
        string writeResKey = "WRITE_RES_" + to_string(n);
        dsm->barrier(writeResKey);
        DE("[%d] MN WRITE BARRIER %d PASSED\n", nodeID, n);
    }

    fprintf(stderr, "MN [%d] finished\n", nodeID);
    dsm->barrier("fin");
}


class RequsetGenBench : public RequstGen {
public:
    RequsetGenBench(int coro_id, DSM *dsm, int id)
        : coro_id(coro_id), dsm(dsm), id(id) {
        seed = rdtsc();
        mehcached_zipf_init(&state, kKeySpace, zipfan,
                            (rdtsc() & (0x0000ffffffffffffull)) ^ id);
    }

    Request next() override {
    Request r;
    uint64_t dis = mehcached_zipf_next(&state);

    r.k = to_key(dis);
    r.v = 23;
    r.is_search = rand_r(&seed) % 100 < kReadRatio;

    // tp[id][0]++;

    return r;
    }

private:
    int coro_id;
    DSM *dsm;
    int id;

    unsigned int seed;
    struct zipf_gen_state state;
};


Timer bench_timer;
std::atomic<int64_t> warmup_cnt{0};
std::atomic_bool ready{false};
std::atomic_bool done{false};

void *thread_run(void *arg) {
    Task *task = (Task *) arg;
    if (pinning == 1) {
        bindCore(thread_to_cpu_1n[task->id]);
    }
    else {
        bindCore(thread_to_cpu_2n[task->id]);
    }
    dsm->registerThread();
    int id = dsm->getMyThreadID();
    tree->set_threadID(id);

    uint64_t all_thread = threadNR * dsm->getClusterSize();
    uint64_t my_id = threadNR * dsm->getMyNodeID() + id;

    if (id == 0) {
        bench_timer.begin();
    }

    uint64_t end_warm_key = kWarmRatio * kKeySpace;
    for (uint64_t i = 1; i < end_warm_key; ++i) {
        if (i % all_thread == my_id) {
            tree->insert(to_key(i), i * 2);
            // DE("INSERTED WARMUP KEY %ld -> %ld\n", i, i*2);
        }
    }

    warmup_cnt.fetch_add(1);

    if (id == 0) {
        while (warmup_cnt.load() != threadNR);
        printf("node %d finish\n", dsm->getMyNodeID());
        dsm->barrier("warm_finish");

        uint64_t ns = bench_timer.end();
        printf("warmup time %lds\n", ns / 1000 / 1000 / 1000);

        tree->index_cache_statistics();
        tree->clear_statistics();
        clear_measurements();

        ready = true;

        warmup_cnt.store(0);
    }

    while (warmup_cnt.load() != 0);

    unsigned int seed = rdtsc();
    struct zipf_gen_state state;
    mehcached_zipf_init(&state, kKeySpace, zipfan,
                        (rdtsc() & (0x0000ffffffffffffull)) ^ id);

    Timer timer;
    while (!done.load()) {

        uint64_t dis = mehcached_zipf_next(&state);
        uint64_t key = to_key(dis);

        Value v;
        timer.begin();

        if (rand_r(&seed) % 100 < kReadRatio) { // GET
            tree->search(key, v);
        } else {
            v = 12;
            tree->insert(key, v);
        }

        auto us_10 = timer.end() / 100;
        if (us_10 >= LATENCY_WINDOWS) {
            us_10 = LATENCY_WINDOWS - 1;
        }
        measurements.tp[id]++;
        measurements.end_to_end[id*LATENCY_WINDOWS + us_10]++;
    }
    return 0;
}

int main(int argc, char *argv[]) {
    // register_sighandler();
    parse_cli_args(
    &threadNR, &nodeNR, &mnNR, &lockNR, &runNR,
    &nodeID, &duration, &mode, &use_zipfan, 
    &kReadRatio, &pinning, &chipSize, &dsmSize,
    &res_file_tp, &res_file_lat, &res_file_lock,
    argc, argv);
    mnNR = nodeNR;
    if (nodeID == 1) {
        if(system("sudo bash /nfs/DAL/restartMemc.sh"))
            _error("Failed to start MEMC server\n");
        DE("STARTED MEMC SERVER\n");
    }
    else {
        sleep(2);
    }

    config.dsmSize = dsmSize;
    config.mnNR = mnNR;
    config.machineNR = nodeNR;
    config.threadNR = threadNR;
    config.chipSize = chipSize;
    lockNR = chipSize * 1024 / sizeof(uint64_t);
    dsm = DSM::getInstance(config);
    nodeID = dsm->getMyNodeID();
    DE("DSM INIT DONE: DSM NODE %d\n", nodeID);

    dsm->registerThread();
    tree = new Tree(dsm, 0, lockNR, false);

    if (dsm->getMyNodeID() == 0) {
        for (uint64_t i = 1; i < 1024000 / 2; ++i) {
            tree->insert(to_key(i), i * 2);
        }
        fprintf(stderr, "inserted initial keys\n");
    }

    dsm->barrier("benchmark");
    dsm->resetThread();
    
    /*TASK INIT*/
    Task *tasks = new Task[threadNR];
    measurements.duration = duration;
    for (int i = 0; i < threadNR; i++) {
        tasks[i].id = i;
        tasks[i].disa = 'y';
        pthread_create(&tasks[i].thread, NULL, thread_run, &tasks[i]);
    }

    while (!ready.load());
    sleep(duration);
    done = true;
    for (int i = 0; i < threadNR; i++) {
        pthread_join(tasks[i].thread, NULL);
    }

    // TODO: CACHE HIT AND MISSES
    // TODO: END TO END LATENCY
    // uint64_t all = 0;
    // uint64_t hit = 0;
    // for (int i = 0; i < MAX_APP_THREAD; ++i) {
    //     all += (cache_hit[i][0] + cache_miss[i][0]);
    //     hit += cache_hit[i][0];
    // }

    for (int n = 0; n < nodeNR; n++) {
        if (n == nodeID) {
            write_tp(res_file_tp, res_file_lock, runNR, threadNR, lockNR, n, page_size);
            write_lat(res_file_lat, runNR, lockNR, n, page_size);
        }
        string writeResKey = "WRITE_RES_" + to_string(n);
        dsm->barrier(writeResKey);
        DE("[%d] WRITE BARRIER %d PASSED\n", nodeID, n);
    }

    fprintf(stderr, "DSM NODE %d DONE\n", nodeID);
    free_measurements();
    dsm->free_dsm();
    sleep(2);
    dsm->barrier("fin");
    return 0;
}