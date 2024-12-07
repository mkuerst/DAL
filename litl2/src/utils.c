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
#include "utils.h"

size_t array_sizes[4] = {KB(256), MB(2), MB(16), MB(64)};

inline void *alloc_cache_align(size_t n) {
    void *res = 0;
    if ((MEMALIGN(&res, L_CACHE_LINE_SIZE, cache_align(n)) < 0) || !res) {
        fprintf(stderr, "MEMALIGN(%llu, %llu)", (unsigned long long)n,
                (unsigned long long)cache_align(n));
        exit(-1);
    }
    return res;
}

// HARDCODED FOR OUR HW
inline int pin_thread(unsigned int id) {
    // int node = 0;
    int cpu_id = id;
    if (NUMA_NODES == 1) {
        return 0;
    }
    else if (id < (CPU_NUMBER / NUMA_NODES)) {
        cpu_id = 2*id;
    }
    else {
        if (id % 2 == 0) {
            cpu_id = id - (CPU_NUMBER / 2) + 1;
        }
    }
    if (CPU_NUMBER != 0) {
        cpu_set_t cpuset;
        CPU_ZERO(&cpuset);
        CPU_SET(cpu_id, &cpuset);
        DEBUG(stderr, "pinning thread %d to cpu %d\n", id, cpu_id);
        int ret = pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &cpuset);
        if (ret != 0) {
            _error("pthread_set_affinity_np failed for thread %d to cpu %d", id, cpu_id);
        }
        // if (numa_run_on_node(node) != 0) {
        //     _error("numa_run_on_node %d failed", node);
        // }
        // fprintf(stderr, "pinning thread %d to numa node %d\n", id, node);
    }
    return 0;
}