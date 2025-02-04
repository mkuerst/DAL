#define _GNU_SOURCE

#include <numa.h>

#include <DSM.h>

#define gettid() syscall(SYS_gettid)

#ifndef CYCLE_PER_US
#error Must define CYCLE_PER_US for the current machine in the Makefile or elsewhere
#endif

int kReadRatio;
int kThreadCount;
int kNodeCount = 1;
uint64_t kKeySpace = 64 * define::MB;
double kWarmRatio = 0.8;
double zipfan = 0;

char *array0;
char *array1;
char *res_file_cum, *res_file_single;
int nthreads, node_id, num_nodes,
num_runs, num_mem_runs, use_nodes,
scope, mode, duration, nlocks, num_mn;


DSM *dsm;
pthread_barrier_t global_barrier;
pthread_barrier_t local_barrier;

 
void mn_func() {
    for (int i = 0; i < num_runs; i++) {
        char barrier_key[32] = {0};
        sprintf(barrier_key, "MB_RUN_%d", i);
        dsm->barrier(barrier_key);
    }
}
void *empty_cs_worker(void *arg) {
    dsm->registerThread();
    uint64_t all_thread = nthreads * dsm->getClusterSize();
    int task_id = nthreads * dsm->getMyNodeID() + task->id;
    DEBUG("[%d.%d] HI\n", node_id, task->id)

    for (int i = 0; i < num_runs; i++) {
        pthread_barrier_wait(&global_barrier);
        pthread_barrier_wait(&global_barrier);
        pthread_barrier_wait(&global_barrier);
    }
    DEBUG("[%d.%d] BYE\n", node_id, task->id)
    return 0;
}


int main(int argc, char *argv[]) {
    if (node_id == 1) {
        system("sudo bash /nfs/DAL/restartMemc.sh");
    }
    else {
        sleep(3);
    }
    DSMConfig config;
    config.mnNR = 1;
    config.machineNR = num_nodes;
    dsm = DSM::getInstance(config);
    DEBUG("[%d] HI\n", node_id);
    if (dsm->getMyNodeID() <= num_mn) {
        mn_func();
        fprintf(stderr, "MN [%d] finished\n", node_id);
        dsm->barrier("fin");
        return 0;
    }

    scope = MAX_ARRAY_SIZE;

    /*WORKER*/
    void* worker; 
    worker = empty_cs_worker;

    pthread_attr_t attr;
    pthread_attr_init(&attr);

    volatile int stop __attribute__((aligned (CACHELINE_SIZE))) = 0;




    pthread_barrier_init(&global_barrier, NULL, nthreads+1);
    pthread_barrier_init(&local_barrier, NULL, nthreads);

    for (int i = 0; i < nthreads; i++) {
        pthread_create(&tasks[i].thread, NULL, worker, NULL);
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
            fprintf(stderr, "[%d] RUN %d_%d\n", node_id, i, k);

            pthread_barrier_wait(&global_barrier);
            char barrier_key[32] = {0};
            sprintf(barrier_key, "MB_RUN_%d", i);
            dsm->barrier(barrier_key);
            pthread_barrier_wait(&global_barrier);

            sleep(duration);
            stop = 1;

            pthread_barrier_wait(&global_barrier);
            if (mode == 3 || mode == 4) {
                numa_free(array0, array_sz);
                numa_free(array1, array_sz);
            }
            for (int i = 0; i < num_nodes; i++) {
                if (i == node_id) {
                    write_res_single(tasks, nthreads, mode, res_file_single);
                }
            } 
        }
    }

    for (int i = 0; i < nthreads; i++) {
        pthread_join(tasks[i].thread, NULL);
    }
    for (int i = 0; i < num_nodes; i++) {
        if (i == node_id) {
            ull total_acqs = write_res_cum(tasks, nthreads, mode, res_file_cum, num_runs, num_mem_runs);
            if (check_correctness(total_acqs, lock_acqs, lock_rels, nlocks)) {
                __error("INCORRECT ACQ NUMBERS");
            }
        }
    } 

    numa_free(locks, nlocks*sizeof(disa_mutex_t));
    sleep(2);
    fprintf(stderr, "CLIENT %d DONE\n", node_id);
    dsm->barrier("fin");
    return 0;
}