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
#include <x86intrin.h>
#include <math.h>
#include <assert.h>
#include <getopt.h>
#include <arpa/inet.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>

// r630-11: 8 MB, 16 MB, 128 MB
// r630-12: ???
// size_t array_sizes[NUM_MEM_RUNS] = {KB(256), MB(16), MAX_ARRAY_SIZE};
size_t array_sizes[NUM_MEM_RUNS] = {MAX_ARRAY_SIZE};
float cycle_to_ms = (float) (CYCLES_12 * 1e3);

inline void *alloc_cache_align(size_t n) {
    void *res = 0;
    if ((MEMALIGN(&res, L_CACHE_LINE_SIZE, cache_align(n)) < 0) || !res) {
        fprintf(stderr, "MEMALIGN(%llu, %llu)", (unsigned long long)n,
                (unsigned long long)cache_align(n));
        exit(-1);
    }
    return res;
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

// r6525
inline int current_numa_node() {
    unsigned long a, d, c;
    int core;
    __asm__ volatile("rdtscp" : "=a"(a), "=d"(d), "=c"(c));
    core = c & 0xFFF;
    // return core / (CPU_NUMBER / NUMA_NODES);
    // return core % 2;
    if (core < 32) {
        return 0;
    }
    else if (core < 64) {
        return 1;
    }
    else if (core < 96) {
        return 0;
    }
    return 1;
}

int uniform_rand_int(int x) {
    return rand() % x;
}

void flush_cache(void *ptr, size_t size) {
    char *data = (char *)ptr;
    for (size_t i = 0; i < size; i += 64) {
        _mm_clflush(&data[i]);
    }
    _mm_clflush(&data[size - 1]);
}

// double random_double(double min, double max) {
//     if (min > max) {
//         fprintf(stderr, "Invalid range: min must be <= max\n");
//         exit(EXIT_FAILURE);
//     }
//     // Generate a random double in [0, 1)
//     double scale = rand() / (double) RAND_MAX;

//     // Scale and shift to [min, max)
//     return min + scale * (max - min);
// }

int compare_desc(const unsigned long long *a, const unsigned long long *b) {
    if (*a < *b) return 1;
    if (*a > *b) return -1;
    return 0;
}
// SORTS IN DESCENDING ORDER
void sort_desc(unsigned long long *latencies, size_t size) {
    assert(size >= 0);
    qsort(latencies, size, sizeof(unsigned long long),
        (int (*)(const void *, const void *))compare_desc);
}

ull _median(unsigned long long *sorted_array, size_t size) {
    assert(size >= 0);

    if (size % 2 == 1) {
        return sorted_array[size / 2];
    } else {
        return (sorted_array[(size / 2) - 1] + sorted_array[size / 2]) / 2.0;
    }
}

// TAKES COMPLEMENT % PERCENTILE
ull percentile(unsigned long long *latencies, size_t size, double p) {
    assert(size >= 0);
    assert(p >= 0);
    assert(p <= 1);
    p = 1-p;
    // size_t index = (size_t)ceil(p * size) - 1;
    return latencies[0];
}

void log_single(task_t * task, int num_runs) {
    size_t array_size = task->array_size[0];
    fprintf(stderr, "tid, slock_hold, swait_acq, swait_rel, slwait_acq, slwait_rel, sgwait_acq, sgwait_rel, sdata_read, sdata_write, sglock_tries, array_sz, cid\n");
    fprintf(stderr, "---------------------------------------------------------------------\n");
    for (int c = task->idx-1; c >= ((int)task->idx-num_runs); c--) {
        fprintf(stderr, "%03d,", task->id);
        ull *base_ptr = task->slock_hold;
        float x[NUM_MEASUREMENTS];
        for (int l = 0; l < NUM_MEASUREMENTS; l++) {
            float factor = l >= IDX_NONCYCLE_MEASURES ? 1 : cycle_to_ms;
            x[l] = (float) base_ptr[c] / factor;
            base_ptr += MAX_MEASUREMENTS;
        }
        for (int l = 0; l < NUM_MEASUREMENTS; l++) {
            fprintf(stderr, "%12.6f,", x[l]);
        }
        fprintf(stderr, "%16lu,%03d\n", array_size, task->client_id);
    }
}

ull write_res_cum(task_t* tasks, int nthreads, int mode, char* res_file, int num_runs, int snd_runs) {
    int client = tasks[0].client_id;
    FILE *file = fopen(res_file, "a");
    if (file == NULL) {
        _error("Client %d failed to open result file %s, errno %d\n", client, res_file, errno);
    }
    // int snd_runs = get_snd_runs(mode);
    ull total_lock_acq = 0;
    for (int j = 0; j < num_runs; j++) {
        float total_lock_hold = 0;
        ull run_lock_acq = 0;
        for (int i = 0; i < nthreads; i++) {
            task_t task = (task_t) tasks[i];
            for (int l = 0; l < snd_runs; l++) {
                int idx = j*snd_runs + l;
                float lock_hold = task.lock_hold[idx] / cycle_to_ms;
                float wait_acq = task.wait_acq[idx] / cycle_to_ms;
                float wait_rel = task.wait_rel[idx] / cycle_to_ms;
                float lwait_acq = task.lwait_acq[idx] / cycle_to_ms;
                float lwait_rel = task.lwait_rel[idx] / cycle_to_ms;
                float gwait_acq = task.gwait_acq[idx] / cycle_to_ms;
                float gwait_rel = task.gwait_rel[idx] / cycle_to_ms;
                float data_read = task.data_read[idx] / cycle_to_ms;
                float data_write = task.data_write[idx] / cycle_to_ms;


                size_t array_size = task.array_size[idx];
                total_lock_hold += lock_hold;
                run_lock_acq += task.lock_acquires[idx];
                total_lock_acq += task.lock_acquires[idx];
                fprintf(file, "%03d,%10llu,%8llu,%12.6f,%12.6f,%12.6f,%12.6f,%12.6f,%12.6f,%12.6f,%12.6f,%12.6f,%12.6f,%10llu,%16lu,%03d,%03d,%06d\n",
                        task.id,
                        task.loop_in_cs[idx],
                        task.lock_acquires[idx],
                        lock_hold,
                        (float) task.duration,
                        wait_acq,
                        wait_rel,
                        lwait_acq,
                        lwait_rel,
                        gwait_acq,
                        gwait_rel,
                        data_read,
                        data_write,
                        task.glock_tries[idx],
                        array_size,
                        task.client_id,
                        j,
                        task.nlocks);
            }
        }
        fprintf(stderr, "RUN %d\n", j);
        fprintf(stderr, "Total lock hold time(ms): %f\n", total_lock_hold);
        fprintf(stderr, "Total lock acquisitions: %llu\n\n", total_lock_acq);
    }
    fclose(file);
    return total_lock_acq;
}

int write_res_single(task_t* tasks, int nthreads, int mode, char* res_file) {
    int client = tasks[0].client_id;
    FILE *file = fopen(res_file, "a");
    if (file == NULL) {
        _error("Client %d failed to open result file %s, errno %d\n", client, res_file, errno);
    }
    // double p = 0.999;
    size_t array_size = tasks[0].array_size[0];
    float duration = (float) tasks[0].duration;
    for (int i = 0; i < nthreads; i++) {
        task_t task = tasks[i];
        size_t sz = task.cnt == 0 ? 1 : (task.cnt >= MAX_MEASUREMENTS ? MAX_MEASUREMENTS : task.cnt);

        fprintf(file, "%03d,%3.1f,", task.id, duration);

        ull *base_ptr = task.slock_hold;
        int idx = 0;
        float x[NUM_MEASUREMENTS*NUM_STATS];
        for (int l = 0; l < NUM_MEASUREMENTS; l++) {
            sort_desc(base_ptr, sz);
            float factor = l >= IDX_NONCYCLE_MEASURES ? 1 : cycle_to_ms;
            x[idx++] = (float) base_ptr[sz-1] / factor;
            x[idx++] = (float) _median(base_ptr, sz) / factor;
            // x[idx++] = (float) percentile(base_ptr, sz, p) / factor;
            x[idx++] = (float) base_ptr[0] / factor;
            base_ptr += MAX_MEASUREMENTS;
        }
        for (int l = 0; l < NUM_MEASUREMENTS*NUM_STATS; l++) {
            fprintf(file, "%12.9f,", x[l]);
        }
        fprintf(file, "%16lu,%03d,%06d\n", array_size, task.client_id, task.nlocks);
    }
    fclose(file);
    return 0;
}

void allocate_task_mem(task_t *tasks, int num_runs, int num_mem_runs, int nthreads) {
    for (int i = 0; i < nthreads; i++) {
        tasks[i].loop_in_cs = aligned_alloc(CACHELINE_SIZE, sizeof(ull) * num_runs * num_mem_runs);
        memset(tasks[i].loop_in_cs, 0, sizeof(ull) * num_runs * num_mem_runs);

        tasks[i].lock_acquires = aligned_alloc(CACHELINE_SIZE, sizeof(ull) * num_runs * num_mem_runs);
        memset(tasks[i].lock_acquires, 0, sizeof(ull) * num_runs * num_mem_runs);

        tasks[i].lock_hold = aligned_alloc(CACHELINE_SIZE, sizeof(ull) * num_runs * num_mem_runs);
        memset(tasks[i].lock_hold, 0, sizeof(ull) * num_runs * num_mem_runs);

        tasks[i].wait_acq = aligned_alloc(CACHELINE_SIZE, sizeof(ull) * num_runs * num_mem_runs);
        memset(tasks[i].wait_acq, 0, sizeof(ull) * num_runs * num_mem_runs);

        tasks[i].wait_rel = aligned_alloc(CACHELINE_SIZE, sizeof(ull) * num_runs * num_mem_runs);
        memset(tasks[i].wait_rel, 0, sizeof(ull) * num_runs * num_mem_runs);

        tasks[i].lwait_acq = aligned_alloc(CACHELINE_SIZE, sizeof(ull) * num_runs * num_mem_runs);
        memset(tasks[i].lwait_acq, 0, sizeof(ull) * num_runs * num_mem_runs);

        tasks[i].lwait_rel = aligned_alloc(CACHELINE_SIZE, sizeof(ull) * num_runs * num_mem_runs);
        memset(tasks[i].lwait_rel, 0, sizeof(ull) * num_runs * num_mem_runs);

        tasks[i].gwait_acq = aligned_alloc(CACHELINE_SIZE, sizeof(ull) * num_runs * num_mem_runs);
        memset(tasks[i].gwait_acq, 0, sizeof(ull) * num_runs * num_mem_runs);

        tasks[i].gwait_rel = aligned_alloc(CACHELINE_SIZE, sizeof(ull) * num_runs * num_mem_runs);
        memset(tasks[i].gwait_rel, 0, sizeof(ull) * num_runs * num_mem_runs);

        tasks[i].glock_tries = aligned_alloc(CACHELINE_SIZE, sizeof(ull) * num_runs * num_mem_runs);
        memset(tasks[i].glock_tries, 0, sizeof(ull) * num_runs * num_mem_runs);

        tasks[i].data_read = aligned_alloc(CACHELINE_SIZE, sizeof(ull) * num_runs * num_mem_runs);
        memset(tasks[i].data_read, 0, sizeof(ull) * num_runs * num_mem_runs);

        tasks[i].data_write = aligned_alloc(CACHELINE_SIZE, sizeof(ull) * num_runs * num_mem_runs);
        memset(tasks[i].data_write, 0, sizeof(ull) * num_runs * num_mem_runs);

        tasks[i].array_size = aligned_alloc(CACHELINE_SIZE, sizeof(ull) * num_runs * num_mem_runs);
        memset(tasks[i].array_size, 0, sizeof(ull) * num_runs * num_mem_runs);
    }
} 

/* Code acknowledgment: rping.c from librdmacm/examples */
int get_addr(char *dst, struct sockaddr *addr)
{
	struct addrinfo *res;
	if (getaddrinfo(dst, NULL, NULL, &res)) {
		__error("getaddrinfo failed - invalid hostname or IP address, -errno: %d\n", -errno);
        return -errno;
	}
	memcpy(addr, res->ai_addr, sizeof(struct sockaddr_in));
	freeaddrinfo(res);
	return 0;
}

int create_sockaddr(char *addr, struct sockaddr_in *sa, uint16_t port)
{
    bzero(sa, sizeof(struct sockaddr_in));
	sa->sin_family = AF_INET;
	sa->sin_addr.s_addr = htonl(INADDR_LOOPBACK);
	sa->sin_addr.s_addr = htonl(INADDR_ANY); 

	if (get_addr(addr, (struct sockaddr*) sa)) {
		__error("Invalid IP, -errno: %d\n", -errno);
        return -errno;
	}
    sa->sin_port = htons(port);
    DEBUG("created sockaddr: %s:%d\n", 
			inet_ntoa(sa->sin_addr),
			ntohs(sa->sin_port));
	return 0;
}

void parse_cli_args(
    int *nthreads, int *num_nodes, int *num_mn,
    int *nlocks, int *node_id, int* duration,
    int* mode, int* num_runs, int *num_mem_runs,
    char **res_file_cum, char **res_file_single,
    char **mn_ip, char peer_ips[MAX_CLIENTS][MAX_IP_LENGTH],
    int argc, char **argv) 
{
    char hostname[256];
    char *ptr;
    *num_mn = 1;

    if (gethostname(hostname, sizeof(hostname)) == 0) {
        DEBUG("Hostname: %s\n", hostname);
        ptr = strstr(hostname, "node");
        if (ptr != NULL) {
            *node_id = atoi(ptr + 4) - 1;  // 4 is the length of "node"
            DEBUG("Extracted node number: %d\n", *node_id);
        } else {
            DEBUG("No node found in the hostname.\n");
        }
    } else {
        _error("gethostname failed");
    }

    int option;
    int i = 0;
    char *addresses;
    char *token;
	while ((option = getopt(argc, argv,
    "p:o:c:t:l:i:d:s:m:r:e:f:g:")) != -1) 
    {
		switch (option) {
			case 's':
                *mn_ip = optarg; 
				break;
			case 'f':
                *res_file_cum = optarg; 
				break;
			case 'g':
                *res_file_single = optarg; 
				break;
            case 'p':
                addresses = strdup(optarg);
                token = strtok(addresses, ",");
                while(token) {
                    strncpy(peer_ips[i], token, MAX_IP_LENGTH - 1);
                    token = strtok(NULL, ",");
                    i++;
                }
                free(addresses);
                break;
			// case 'o':
			// 	mn_sockaddr->sin_port = htons(strtol(optarg, NULL, 0)); 
			// 	break;
			case 'c':
				*num_nodes = atoi(optarg);
				break;
			case 'r':
				*num_runs = atoi(optarg);
				break;
			case 'e':
				*num_mem_runs = atoi(optarg);
				break;
			case 'm':
				*mode = atoi(optarg);
				break;
			case 'd':
				*duration = atoi(optarg);
				break;
            case 'i':
                *node_id = atoi(optarg);
                break;
			case 't':
				*nthreads = atoi(optarg);
				break;
			case 'l':
				*nlocks = atoi(optarg);
				break;
			default:
				break;
		}
	}
    if (i >= 1) {
        DEBUG("PEER_IPs: \n");
        for (int x = 0; x < i; x++) {
            DEBUG("%s\n", peer_ips[x]);
        }
    }
}

int check_correctness(ull total_acq, ull* lock_acqs, ull* lock_rels, int nlocks) 
{
    ull sum_acqs = 0;
    ull sum_rels = 0;
    for (int i = 0; i < nlocks; i++) {
        sum_acqs += lock_acqs[i];
        sum_rels += lock_rels[i];
    }
    DEBUG("sum_acqs = %llu\ntotal_acq = %llu\n", sum_acqs, total_acq);
    assert(sum_acqs == sum_rels);
    assert(sum_acqs == total_acq);
    return 0;

}