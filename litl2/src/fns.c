/*
 * The MIT License (MIT)
 *
 * Copyright (c) 2016 Hugo Guiroux <hugo.guiroux at gmail dot com>
 *               UPMC, 2010-2011, Jean-Pierre Lozi <jean-pierre.lozi@lip6.fr>
 *                                Gaël Thomas <gael.thomas@lip6.fr>
 *                                Florian David <florian.david@lip6.fr>
 *                                Julia Lawall <julia.lawall@lip6.fr>
 *                                Gilles Muller <gilles.muller@lip6.fr>
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
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <sys/mman.h>
#include <pthread.h>
#include <assert.h>
#include <fns.h>
#include <papi.h>
#include <sched.h>
#include <sys/resource.h>
#include "waiting_policy.h"
#include "interpose.h"
#include "utils.h"

#include <assert.h>
/* #define BLOCKING_FAIRNESS */
/* #define VRUNTIME_FAIRNESS */

#define UNIT_MAX 4294967295
/* debugging */
#ifdef WAITER_DEBUG
typedef enum {
    RED,
    GREEN,
    BLUE,
    MAGENTA,
    YELLOW,
    CYAN,
    END,
} color_num;

static char colors[END][8] = {
    "\x1B[31m",
    "\x1B[32m",
    "\x1B[34m",
    "\x1B[35m",
    "\x1b[33m",
    "\x1b[36m",
};
static unsigned long counter = 0;

#define dprintf(__fmt, ...)                                                    \
    do {                                                                       \
        smp_faa(&counter, 1);                                                  \
        fprintf(stderr, "%s [DBG:%010lu: %d (%s: %d)]: " __fmt,                \
                colors[__my_cpu_id % END], counter, __my_cpu_id,                         \
                __func__, __LINE__, ##__VA_ARGS__);                            \
    } while(0);
#define dassert(v)      assert((v))
#else
#define dprintf(__fmt, ...) do { } while(0)
#define dassert(v) do { } while(0)
#endif

#define GET_TIME()                                              \
((long long)PAPI_get_real_cyc() / (CPU_FREQ * 1000))

#define AQS_MIN_WAIT_TIME	50LL
#define AQS_MAX_WAIT_TIME	10000000LL
extern __thread unsigned int cur_thread_id;
// extern __thread struct t_info tinfo;
extern unsigned int last_thread_id;

//unsigned long long runtime_checker_core[64];
//unsigned long long runtime_checker_node[4];
int allowed_node;

static inline void traverse(aqs_node_t *node) {
	int i;
	i = 0;
	aqs_node_t *iter;
	iter = node;
	while (iter)
	{
		i++;
//		fprintf(stderr, "%d: runtime %llu, cid = %d, nid = %d \n", i, runtime_checker_core[iter->cid], iter->cid, iter->nid);
		iter = READ_ONCE(iter->next);
	}

}


typedef enum {
    RED,
    GREEN,
    BLUE,
    MAGENTA,
    YELLOW,
    CYAN,
    END,
} color_num;

static char colors[END][8] = {
    "\x1B[31m",
    "\x1B[32m",
    "\x1B[34m",
    "\x1B[35m",
    "\x1b[33m",
    "\x1b[36m",
};

#define eprintf(__fmt, ...)                                                    \
    do {                                                                       \
        fprintf(stderr, "%s [%d (%s: %d)]: " __fmt,                \
                colors[cur_thread_id % END], cur_thread_id,                         \
                __func__, __LINE__, ##__VA_ARGS__);                            \
    } while(0);

/* debugging */
/* #ifdef WAITER_DEBUG */
static inline void traverse_nodes(aqs_mutex_t *lock, aqs_node_t *node)
{
    aqs_node_t *curr = node;
    int count = 0;
    eprintf("prev shuffler: ");
    if (node->last_visited) {
        eprintf("%d\n", node->last_visited->cid);
    } else {
        eprintf("no one!\n");
    }

    printf("#coreid[lock-status:shuffle-leader:waiter-count]\n");
    for (;;) {
        if (++count > 200)
            assert(0);
        if (!curr)
            break;

        printf("%d[%d:%d:%d]->", curr->cid, curr->lstatus,
               curr->sleader, curr->wcount);
        curr = READ_ONCE(curr->next);
    }
    printf("\n");
}
/* #endif */

#define THRESHOLD (0xffff)
#ifndef UNLOCK_COUNT_THRESHOLD
#define UNLOCK_COUNT_THRESHOLD 1024
#endif

static inline uint32_t xor_random() {
    static __thread uint32_t rv = 0;

    if (rv == 0)
        rv = cur_thread_id + 1;

    uint32_t v = rv;
    v ^= v << 6;
    v ^= (uint32_t)(v) >> 21;
    v ^= v << 7;
    rv = v;

    return v & (UNLOCK_COUNT_THRESHOLD - 1);
    /* return v; */
}
/*
static inline int keep_lock_local(void)
{
    return xor_random() & THRESHOLD;
}
*/
// static inline int current_numa_node() {
//     unsigned long a, d, c;
//     int core;
//     __asm__ volatile("rdtscp" : "=a"(a), "=d"(d), "=c"(c));
//     core = c & 0xFFF;
// //    return core / (CPU_NUMBER / NUMA_NODES);
// 	return core % NUMA_NODES;
// }
static inline int current_numa_core() {
    unsigned long a, d, c;
    int core;
    __asm__ volatile("rdtscp" : "=a"(a), "=d"(d), "=c"(c));
    core = c & 0xFFF;
//    return core / (CPU_NUMBER / NUMA_NODES);
	return core;
}

#define false 0
#define true  1

#define atomic_andnot(val, ptr) \
    __sync_fetch_and_and((ptr), ~(val));
#define atomic_fetch_or_acquire(val, ptr) \
    __sync_fetch_and_or((ptr), (val));

#define _AQS_NOSTEAL_VAL        (1U << (_AQS_LOCKED_OFFSET + _AQS_LOCKED_BITS))
#define AQS_MAX_PATIENCE_COUNT  2
#define MAX_CONT_SHFLD_COUNT    2

static inline void smp_wmb(void)
{
    __asm __volatile("sfence":::"memory");
}

static inline void enable_stealing(aqs_mutex_t *lock)
{
        atomic_andnot(_AQS_NOSTEAL_VAL, &lock->val);
}

static inline void disable_stealing(aqs_mutex_t *lock)
{
        atomic_fetch_or_acquire(_AQS_NOSTEAL_VAL, &lock->val);
}

static inline uint8_t is_stealing_disabled(aqs_mutex_t *lock)
{
        return READ_ONCE(lock->no_stealing);
}

static inline void set_sleader(struct aqs_node *node, struct aqs_node *qend)
{
        WRITE_ONCE(node->sleader, 1);
        if (qend != node)
                WRITE_ONCE(node->last_visited, qend);
}

static inline void clear_sleader(struct aqs_node *node)
{
        node->sleader = 0;
}

static inline void set_waitcount(struct aqs_node *node, int count)
{
        WRITE_ONCE(node->wcount, count);
}
/*
static inline int need_switch()
{
	int i, minid;
	unsigned long max, min, threshold, value;
	max = 0;
	min = runtime_checker_node[0];
	minid = 0;
	threshold = 100000;
	value = 9;
	for (i = 0; i < 4; i++)
	{
		value = READ_ONCE(runtime_checker_node[i]);
		if (max < value) {
			max = value;
		}
		if (min > value) {
			min = value;
			minid = i;
		}
	}
	if (max - min <= threshold) {
		WRITE_ONCE(allowed_node, 100);
		return 100;
	}
	else {
		WRITE_ONCE(allowed_node, minid);
		return minid;
	}

}*/
/* #define USE_COUNTER */
static void shuffle_waiters(aqs_mutex_t *lock, struct aqs_node *node,
                            int is_next_waiter)
{
    aqs_node_t *curr, *prev, *next, *last, *sleader, *qend, *iter;
    int nid = node->nid;
    int curr_locked_count = node->wcount;
    int one_shuffle = 0;
	int minid, i;
	unsigned long long min;
	
	unsigned long long node_runtime[4];
	unsigned long long node_numthreads[4];
	unsigned long long avg;

	int total_threads = 0;
    uint32_t lock_ready;

	unsigned long long standard;

	for (i = 0; i < 4; i++)
	{
		node_runtime[i] = 0;
		node_numthreads[i] = 0;
	}

	prev = READ_ONCE(node->last_visited);
    if (!prev) {
	    prev = node;
		//fprintf(stderr,"noprev!\n");
	}
    sleader = NULL;
//	prev = node;
    last = node;
    curr = NULL;
    next = NULL;
    qend = NULL;

	standard = 0;
	

	iter = NULL;

    if (curr_locked_count == 0)
	    set_waitcount(node, ++curr_locked_count);

    clear_sleader(node);

	nid = lock->allowed_nid;
	if (nid == 100) {
		nid = node->nid;
		standard = UINT_MAX;
	} else {
		standard = lock->avg_runtime;
	}

	for (;;) {
        curr = READ_ONCE(prev->next);

        if (!curr) {
            sleader = last;
            qend = prev;
            break;
        }

        if (curr == READ_ONCE(lock->tail)) {
            sleader = last;
            qend = prev;
            break;
        }

		/* update statistics */
	node_runtime[curr->nid] += curr->runtime;
	node_numthreads[curr->nid]++;
        /* got the current for sure */
	if (curr->runtime > standard) {
		prev = curr;
		goto check;
	}
        /* Check if curr->nid is same as nid */
        if (curr->nid == nid) {

            if (prev == node && prev->nid == nid) {
#ifdef USE_COUNTER
			    set_waitcount(curr, ++curr_locked_count);
#else
                set_waitcount(curr, curr_locked_count);
#endif
                last = curr;
                prev = curr;
                one_shuffle = 1;
            }
            else {
                next = READ_ONCE(curr->next);
                if (!next) {
                    sleader = last;
                    qend = prev;

                    goto out;
                }
			
				iter = last;
				
#ifdef USE_COUNTER
				set_waitcount(curr, ++curr_locked_count);
#else
                set_waitcount(curr, curr_locked_count);
#endif

				if (iter != prev) {
					prev->next = next;
					next->prev = prev;

					curr->next = iter->next;
					iter->next->prev = curr;
					iter->next = curr;
					curr->prev = iter;
				}
				else
					prev = curr;

				if (iter == last) {
					last = curr;
				}

				one_shuffle = 1;
            }
        } else
            prev = curr;

check:
        lock_ready = !READ_ONCE(lock->locked);
        if (one_shuffle && ((is_next_waiter && lock_ready) ||
			    (!is_next_waiter && READ_ONCE(node->lstatus)))) {
            sleader = last;
		    qend = prev;
			goto out2;
        }
    }
	
out:
	barrier();
	min = UINT_MAX;
	minid = 0;
	avg=0;
	total_threads = 0;
	curr = READ_ONCE(node->last_visited);
	if (curr == NULL)
		curr = node;
	for(;;) {
		if (curr == NULL)
			break;
		node_runtime[curr->nid] += curr->runtime;
		node_numthreads[curr->nid]++;
		curr = READ_ONCE(curr->prev);
	}

	for (i = 0; i < 4; i++)
	{
		if (node_numthreads[i] == 0){
			continue;
		}
		avg +=node_runtime[i];
		total_threads += node_numthreads[i];

		node_runtime[i] /= node_numthreads[i];
		if (min > node_runtime[i]) {
			min = node_runtime[i];
			minid = i;
		}

	//	avg +=node_runtime[i];
	}
	if (total_threads != 0)
		avg /= total_threads;
//		avg /= 4;
//	if (avg == 0)
//		minid = 100;
	if (lock->allowed_nid != 100 && minid != lock->allowed_nid)
		qend = NULL;
	WRITE_ONCE(lock->allowed_nid, minid);
	WRITE_ONCE(lock->avg_runtime, avg);

out2:	
    if (sleader) {
		set_sleader(sleader, qend);
    }
}

/* Interpose */
aqs_mutex_t *aqs_mutex_create(const pthread_mutexattr_t *attr) {
    aqs_mutex_t *impl = (aqs_mutex_t *)alloc_cache_align(sizeof(aqs_mutex_t));
    impl->tail = NULL;
    impl->val = 0;
	impl->avg_runtime = 0;
	impl->allowed_nid= 100;
#ifdef WAITER_CORRECTNESS
    impl->slocked = 0;
#endif
#if COND_VAR
    REAL(pthread_mutex_init)(&impl->posix_lock, attr);
    DEBUG("Mutex init lock=%p posix_lock=%p\n", impl, &impl->posix_lock);
#endif

    barrier();
    return impl;
}

static int __aqs_mutex_lock(aqs_mutex_t *impl, aqs_node_t *me)
{
	aqs_node_t *prev;

	me->cid = cur_thread_id;
    me->nid = current_numa_node();
	me->prev = NULL;

	// me->runtime = tinfo.vcs_runtime;
/*
	if (impl->allowed_nid == 100 || (me->nid == impl->allowed_nid && me->runtime < impl->avg_runtime)) {
   		if (smp_cas(&impl->locked_no_stealing, 0, 1) == 0) {
    	    goto release;
    	}
	}
*/
    
    me->locked = AQS_STATUS_WAIT;
    me->next = NULL;
    me->last_visited = NULL;

    /*
     * Publish the updated tail.
     */
    prev = smp_swap(&impl->tail, me);
	WRITE_ONCE(me->prev, prev);

    if (prev) {

        WRITE_ONCE(prev->next, me);
        
        for (;;) {

            if (READ_ONCE(me->lstatus) == AQS_STATUS_LOCKED)
                break;

            if (READ_ONCE(me->sleader)) {
                shuffle_waiters(impl, me, 0);
            }

            CPU_PAUSE();
        }
    } else
	    disable_stealing(impl);

    for (;;) {
        int wcount;

        if (!READ_ONCE(impl->locked))
            break;

        wcount = me->wcount;
        if (!wcount ||
            (wcount && me->sleader)) {
            shuffle_waiters(impl, me, 1);
        }

//         CPU_PAUSE();
    }

    for (;;) {
        /*
         * If someone has already disable stealing,
         * change locked and proceed forward
         */
        if(smp_cas(&impl->locked, 0, 1) == 0)
            break;

        while (READ_ONCE(impl->locked))
            CPU_PAUSE();

    }

    if (!READ_ONCE(me->next)) {
        if (smp_cas(&impl->tail, me, NULL) == me) {
			enable_stealing(impl);
            goto release;
        }

        while (!READ_ONCE(me->next))
            CPU_PAUSE();
    }

	prev = smp_swap(&me->next->prev, NULL);
    barrier();
    WRITE_ONCE(me->next->lstatus, 1);
    WRITE_ONCE(me->prev, NULL);
	
 release:
    return 0;
}

int aqs_mutex_lock(aqs_mutex_t *impl, aqs_node_t *me) {

    // tinfo.start_ticks2 = rdtsc();
    int ret = __aqs_mutex_lock(impl, me);
    // tinfo.waittime += rdtsc() - tinfo.start_ticks2;
    // tinfo.count++;
    assert(ret == 0);
#if COND_VAR
    if (ret == 0) {
        DEBUG_PTHREAD("[%d] Lock posix=%p\n", cur_thread_id, &impl->posix_lock);
        assert(REAL(pthread_mutex_lock)(&impl->posix_lock) == 0);
    }
#endif
    DEBUG("[%d] Lock acquired posix=%p\n", cur_thread_id, &impl->posix_lock);
	// tinfo.start_ticks = rdtsc();
    return ret;
}

int aqs_mutex_trylock(aqs_mutex_t *impl, aqs_node_t *me) {

    if ((smp_cas(&impl->locked, 0, 1) == 0)) {
#if COND_VAR
        DEBUG_PTHREAD("[%d] Lock posix=%p\n", cur_thread_id, &impl->posix_lock);
        int ret = 0;
        while ((ret = REAL(pthread_mutex_trylock)(&impl->posix_lock)) == EBUSY)
            ;
        assert(ret == 0);
#endif
        return 0;
    }
    return EBUSY;
}

static inline void __aqs_mutex_unlock(aqs_mutex_t *impl, aqs_node_t *me) {
#ifdef BLOCKING_FAIRNESS
    unsigned long cslength, now;

    now = rdtsc();
    smp_cmb();
    // cslength = now - tinfo.start_ticks;
//    tinfo.banned_until += (cslength * (last_thread_id - 1) * 1);
 //   tinfo.banned = tinfo.banned_until > now;
#endif
    dprintf("releasing the lock\n");
	
//	unsigned long cslength, now;

    WRITE_ONCE(impl->locked, 0);
	smp_cmb();
	
		// tinfo.priority = getpriority(PRIO_PROCESS, 0);
//	fprintf(stderr,"prio = %d\n", tinfo.priority);
	// if (tinfo.priority == 16) {
	// 	tinfo.vcs_runtime += (rdtsc() - tinfo.start_ticks) / 2;
	// }
	// else
	// 	tinfo.vcs_runtime += rdtsc() - tinfo.start_ticks;
	
//		fprintf(stderr, "allowed_id = %d, me=%d, nid=%d, runtime=%llu\n", impl->allowed_nid, me->cid, me->nid, runtime_checker_core[me->cid]);

}

void aqs_mutex_unlock(aqs_mutex_t *impl, aqs_node_t *me) {
#if COND_VAR
    DEBUG_PTHREAD("[%d] Unlock posix=%p\n", cur_thread_id, &impl->posix_lock);
    assert(REAL(pthread_mutex_unlock)(&impl->posix_lock) == 0);
#endif
    __aqs_mutex_unlock(impl, me);
}

int aqs_mutex_destroy(aqs_mutex_t *lock) {
#if COND_VAR
    REAL(pthread_mutex_destroy)(&lock->posix_lock);
#endif
    free(lock);
    lock = NULL;

    return 0;
}

int aqs_cond_init(aqs_cond_t *cond, const pthread_condattr_t *attr) {
#if COND_VAR
    return REAL(pthread_cond_init)(cond, attr);
#else
    fprintf(stderr, "Error cond_var not supported.");
    assert(0);
#endif
}

int aqs_cond_timedwait(aqs_cond_t *cond, aqs_mutex_t *lock, aqs_node_t *me,
                       const struct timespec *ts) {
#if COND_VAR
    int res;

    __aqs_mutex_unlock(lock, me);
    DEBUG("[%d] Sleep cond=%p lock=%p posix_lock=%p\n", cur_thread_id, cond,
          lock, &(lock->posix_lock));
    DEBUG_PTHREAD("[%d] Cond posix = %p lock = %p\n", cur_thread_id, cond,
                  &lock->posix_lock);

    if (ts)
        res = REAL(pthread_cond_timedwait)(cond, &lock->posix_lock, ts);
    else
        res = REAL(pthread_cond_wait)(cond, &lock->posix_lock);

    if (res != 0 && res != ETIMEDOUT) {
        fprintf(stderr, "Error on cond_{timed,}wait %d\n", res);
        assert(0);
    }

    int ret = 0;
    if ((ret = REAL(pthread_mutex_unlock)(&lock->posix_lock)) != 0) {
        fprintf(stderr, "Error on mutex_unlock %d\n", ret == EPERM);
        assert(0);
    }

    aqs_mutex_lock(lock, me);

    return res;
#else
    fprintf(stderr, "Error cond_var not supported.");
    assert(0);
#endif
}

int aqs_cond_wait(aqs_cond_t *cond, aqs_mutex_t *lock, aqs_node_t *me) {
    return aqs_cond_timedwait(cond, lock, me, 0);
}

int aqs_cond_signal(aqs_cond_t *cond) {
#if COND_VAR
    return REAL(pthread_cond_signal)(cond);
#else
    fprintf(stderr, "Error cond_var not supported.");
    assert(0);
#endif
}

int aqs_cond_broadcast(aqs_cond_t *cond) {
#if COND_VAR
    DEBUG("[%d] Broadcast cond=%p\n", cur_thread_id, cond);
    return REAL(pthread_cond_broadcast)(cond);
#else
    fprintf(stderr, "Error cond_var not supported.");
    assert(0);
#endif
}

int aqs_cond_destroy(aqs_cond_t *cond) {
#if COND_VAR
    return REAL(pthread_cond_destroy)(cond);
#else
    fprintf(stderr, "Error cond_var not supported.");
    assert(0);
#endif
}

void aqs_thread_start(void) {
//	tinfo.priority = -1;//getpriority(PRIO_PROCESS, 0);
//	fprintf(stderr, "prio = %d\n", tinfo.priority);
}

void aqs_thread_exit(void) {

	// fprintf(stderr, "prio = %d runtime = %lf waittime = %lf count = %lu\n", tinfo.priority, tinfo.vcs_runtime/CYCLESTOSEC,tinfo.waittime/CYCLESTOSEC, tinfo.count);
}

void aqs_application_init(void) {
}

void aqs_application_exit(void) {
}
void aqs_init_context(lock_mutex_t *UNUSED(impl),
                      lock_context_t *UNUSED(context), int UNUSED(number)) {
}
