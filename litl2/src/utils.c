/*
 * The MIT License (MIT)
 *
 * Copyright (c) 2016 Hugo Guiroux <hugo.guiroux at gmail dot com>
 *               2013 Tudor David
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of his software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */
#define _GNU_SOURCE
#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include "utils.h"

// r630-11: 8 MB, 16 MB, 128 MB
// r630-12: ???
size_t array_sizes[NUM_MEM_RUNS] = {KB(256), MB(16), MAX_ARRAY_SIZE};

inline void *alloc_cache_align(size_t n) {
    void *res = 0;
    if ((MEMALIGN(&res, L_CACHE_LINE_SIZE, cache_align(n)) < 0) || !res) {
        fprintf(stderr, "MEMALIGN(%llu, %llu)", (unsigned long long)n,
                (unsigned long long)cache_align(n));
        exit(-1);
    }
    return res;
}

inline int get_snd_runs(int mode) {
    switch(mode) {
        case(0): return 1;
        case(1): return 1;
        case(2): return NUM_LAT_RUNS;
        case(3): return NUM_MEM_RUNS;
        case(4): return NUM_MEM_RUNS;
        case(5): return NUM_MEM_RUNS;
        case(6): return NUM_MEM_RUNS;
        default: return 1;
    }
}

bool is_power_of_2(int n) {
    return (n > 0) && ((n & (n - 1)) == 0);
}

// HARDCODED FOR OUR HW
// CURRENT PINNING: always use 2 nodes
int pin_thread(unsigned int id, int nthreads, int use_nodes) {
    // int node = 0;
    int cpu_id = id;
    if (NUMA_NODES == 1) {
        return 0;
    }
    if (use_nodes == 2) {
        if (id < (nthreads / NUMA_NODES)) {
            cpu_id = 2*id;
        }
        else {
            if (id % 2 == 0) {
                cpu_id = id - (nthreads / 2) + 1;
            }
        }
    }
    else {
        if (id % 2 == 1) {
            cpu_id = (CPU_NUMBER / 2) + id - 1;
        }
    }
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(cpu_id, &cpuset);
    DEBUG("pinning thread %d to cpu %d\n", id, cpu_id);
    int ret = pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &cpuset);
    if (ret != 0) {
        _error("pthread_set_affinity_np failed for thread %d to cpu %d", id, cpu_id);
    }
    return 0;
}

inline int current_numa_node() {
    unsigned long a, d, c;
    int core;
    __asm__ volatile("rdtscp" : "=a"(a), "=d"(d), "=c"(c));
    core = c & 0xFFF;
    // return core / (CPU_NUMBER / NUMA_NODES);
    return core % 2;
}

int uniform_rand_int(int x) {
    return rand() % (x + 1);
}


int cs_result_to_out(task_t* tasks, int nthreads, int mode, char* res_file) {
    int client = tasks[0].client_id;
    FILE *file = fopen(res_file, "a");
    if (file == NULL) {
        _error("Client %d failed to open result file %s, errno %d\n", client, res_file, errno);
    }
    int snd_runs = get_snd_runs(mode);
    float cycle_to_ms = (float) (CYCLES_12 * 1e3);
    for (int j = 0; j < NUM_RUNS; j++) {
        float total_lock_hold = 0;
        ull total_lock_acq = 0;
        for (int i = 0; i < nthreads; i++) {
            task_t task = (task_t) tasks[i];
            for (int l = 0; l < snd_runs; l++) {
                float lock_hold = task.lock_hold[j][l] / cycle_to_ms;
                float wait_acq = task.wait_acq[j][l] / cycle_to_ms;
                float wait_rel = task.wait_rel[j][l] / cycle_to_ms;
                float lwait_acq = task.lwait_acq[j][l] / cycle_to_ms;
                float lwait_rel = task.lwait_rel[j][l] / cycle_to_ms;
                float gwait_acq = task.gwait_acq[j][l] / cycle_to_ms;
                float gwait_rel = task.gwait_rel[j][l] / cycle_to_ms;


                float total_duration = (float) task.duration[j][l];
                size_t array_size = task.array_size[j][l];
                total_lock_hold += lock_hold;
                total_lock_acq += task.lock_acquires[j][l];
                fprintf(file, "%03d,%10llu,%8llu,%12.6f,%12.6f,%12.6f,%12.6f,%12.6f,%12.6f,%12.6f,%12.6f,%10llu,%16lu,%03d,%03d\n",
                        task.id,
                        task.loop_in_cs[j][l],
                        task.lock_acquires[j][l],
                        lock_hold,
                        total_duration,
                        wait_acq,
                        wait_rel,
                        lwait_acq,
                        lwait_rel,
                        gwait_acq,
                        gwait_rel,
                        task.glock_tries[j][l],
                        array_size,
                        task.client_id,
                        j);
            }
        }
        fprintf(stderr, "RUN %d\n", j);
        fprintf(stderr, "Total lock hold time(ms): %f\n", total_lock_hold);
        fprintf(stderr, "Total lock acquisitions: %llu\n\n", total_lock_acq);
    }
    fclose(file);
    return 0;
}