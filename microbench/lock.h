#ifndef __LOCK_H__
#define __LOCK_H__

#ifdef MUTEX
#include <pthread.h>
#include <utils.h>
typedef disa_mutex_t disa_lock_t;
typedef pthread_mutex_t lock_t;
#define lock_init(plock) pthread_mutex_init((pthread_mutex_t *) plock, NULL)
#define lock_acquire(plock) pthread_mutex_lock((pthread_mutex_t *) plock)
#define lock_release(plock) pthread_mutex_unlock((pthread_mutex_t *) plock)

#elif SPIN
#include <pthread.h>
typedef pthread_spinlock_t lock_t;
#define lock_init(plock) pthread_spin_init((pthread_mutex_t *) plock, PTHREAD_PROCESS_PRIVATE)
#define lock_acquire(plock) pthread_spin_lock((pthread_mutex_t *) plock)
#define lock_release(plock) pthread_spin_unlock((pthread_mutex_t *) plock)

#endif

#endif // __LOCK_H__

