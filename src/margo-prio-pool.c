/*
 * (C) 2021 The University of Chicago
 *
 * See COPYRIGHT in top-level directory.
 */

#include <abt.h>
#include <pthread.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/epoll.h>
#include <sys/eventfd.h>

#include "margo-prio-pool.h"

/* ABT_POOL_PRIO_WAIT */

/* This is a custom Argobots pool, compatible with ABT_POOL_FIFO_WAIT, that
 * automatically splits work units into high priority and low priority bins.
 * Newly created threads (that have never been executed) are assigned a
 * low priority, while threads that have been executed at least once are
 * assigned a high priority.  This pool will therefore favor trying to
 * complete existing ULTs before starting new ones if both are possible.
 *
 * The SCHED_COUNTER_PRIORITY_LIMIT is a threshold for the number of times
 * that an existing ULT may be yielded before it is demoted to the low
 * priority bin.  This heuristic is mean to ensure that persistent
 * background threads do not get indefinitely favorable priority.
 */

/* Once a unit has yielded this many times, it no longer receives a
 * priority boost, on the assumption that it is a long-running background ULT
 */
#define SCHED_COUNTER_PRIORITY_LIMIT 25

typedef struct unit_t {
    ABT_thread     thread;
    struct unit_t* p_prev;
    struct unit_t* p_next;
    int            sched_counter;
} unit_t;

typedef struct queue_t {
    unit_t* p_tail;
    unit_t* p_head;
} queue_t;

static inline void queue_push(queue_t* p_queue, unit_t* p_unit)
{
    if (p_queue->p_head == NULL) {
        p_unit->p_next  = p_unit;
        p_unit->p_prev  = p_unit;
        p_queue->p_head = p_unit;
        p_queue->p_tail = p_unit;
    } else {
        unit_t* p_head  = p_queue->p_head;
        unit_t* p_tail  = p_queue->p_tail;
        p_tail->p_next  = p_unit;
        p_head->p_prev  = p_unit;
        p_unit->p_prev  = p_tail;
        p_unit->p_next  = p_head;
        p_queue->p_tail = p_unit;
    }
}

static inline unit_t* queue_pop(queue_t* p_queue)
{
    if (p_queue->p_head == NULL) {
        return NULL;
    } else {
        unit_t* p_unit = p_queue->p_head;
        if (p_queue->p_head == p_queue->p_tail) {
            /* one item */
            p_queue->p_head = NULL;
            p_queue->p_tail = NULL;
        } else {
            p_unit->p_prev->p_next = p_unit->p_next;
            p_unit->p_next->p_prev = p_unit->p_prev;
            p_queue->p_head        = p_unit->p_next;
        }
        p_unit->p_next = NULL;
        p_unit->p_prev = NULL;
        return p_unit;
    }
}

typedef struct pool_t {
    queue_t         high_prio_queue;
    queue_t         low_prio_queue;
    int             num;
    int             cnt;
    pthread_mutex_t mutex;
    pthread_cond_t  cond;
    int             efd;
    int             epfd;
} pool_t;

static ABT_unit pool_unit_create_from_thread(ABT_thread thread)
{
    unit_t* p_unit        = (unit_t*)calloc(1, sizeof(unit_t));
    p_unit->thread        = thread;
    p_unit->p_next        = NULL;
    p_unit->p_prev        = NULL;
    p_unit->sched_counter = 0;
    return (ABT_unit)p_unit;
}

static void pool_unit_free(ABT_unit* p_unit)
{
    free(*p_unit);
    *p_unit = ABT_UNIT_NULL;
}

static int pool_init(ABT_pool pool, ABT_pool_config config)
{
    struct epoll_event epev        = {0};
    pool_t*            p_pool      = (pool_t*)calloc(1, sizeof(pool_t));
    p_pool->high_prio_queue.p_tail = NULL;
    p_pool->high_prio_queue.p_head = NULL;
    p_pool->low_prio_queue.p_tail  = NULL;
    p_pool->low_prio_queue.p_head  = NULL;
    p_pool->num                    = 0;
    p_pool->cnt                    = 0;
    p_pool->efd                    = -1;
    p_pool->epfd                   = -1;
    pthread_mutex_init(&p_pool->mutex, NULL);
    pthread_cond_init(&p_pool->cond, NULL);
    /* NOTE: we don't check return code here because efd will still be -1 if
     * it fails.
     */
    ABT_pool_config_get(config, MARGO_PRIO_POOL_CONFIG_KEY_EFD, NULL,
                        &p_pool->efd);
    // fprintf(stderr, "DBG: prio_pool got efd %d\n", p_pool->efd);

    /* if we have been asked to trigger an eventfd, then configure an epoll
     * set so that the pool can also monitor it.
     */
    /* TODO: error handling */
    if (p_pool->efd > -1) {
        p_pool->epfd = epoll_create(1);
        epev.events  = EPOLLIN;
        epev.data.fd = p_pool->efd;
        epoll_ctl(p_pool->epfd, EPOLL_CTL_ADD, p_pool->efd, &epev);
    }

    ABT_pool_set_data(pool, (void*)p_pool);
    return ABT_SUCCESS;
}

static size_t pool_get_size(ABT_pool pool)
{
    pool_t* p_pool;
    ABT_pool_get_data(pool, (void**)&p_pool);
    return p_pool->num;
}

static void pool_push(ABT_pool pool, ABT_unit unit)
{
    pool_t* p_pool;
    ABT_pool_get_data(pool, (void**)&p_pool);
    unit_t* p_unit = (unit_t*)unit;
    int     sched_counter;

#if 0
    ABT_thread_state state;
    /* If it is a thread, look at state */
    if(p_unit->thread != ABT_THREAD_NULL) {
        if(ABT_thread_get_state(p_unit->thread, &state) == ABT_SUCCESS) {
            fprintf(stderr, "DBG: %p state on push: %d\n", p_unit->thread, state);
        }
    }
#endif

    /* save incoming value of push counter, then increment */
    sched_counter = p_unit->sched_counter;
    if (p_unit->sched_counter < SCHED_COUNTER_PRIORITY_LIMIT)
        p_unit->sched_counter++;

    // fprintf(stderr, "DBG: looking at sched_counter %d\n", sched_counter);
    pthread_mutex_lock(&p_pool->mutex);
    if (sched_counter == 0 || sched_counter >= SCHED_COUNTER_PRIORITY_LIMIT) {
        /* The first push or long-running ULT, so put it to the low-priority
         * pool. */
        queue_push(&p_pool->low_prio_queue, p_unit);
    } else {
        /* high-priority pool, for ULTs that have been suspended more than
         * once but not excessively
         */
        queue_push(&p_pool->high_prio_queue, p_unit);
    }
    p_pool->num++;
    /* only signal on the transition from empty to non-empty; in other cases
     * there will be no one waiting on the condition.
     */
    if (p_pool->num == 1) {
        if (p_pool->epfd)
            eventfd_write(p_pool->efd, 1);
        else
            pthread_cond_signal(&p_pool->cond);
    }
    pthread_mutex_unlock(&p_pool->mutex);
}

static ABT_unit pool_pop(ABT_pool pool)
{
    pool_t*  p_pool;
    uint64_t tmp_val;
    ABT_pool_get_data(pool, (void**)&p_pool);

    /* Sometimes it should pop from low_prio_queue to avoid a deadlock. */
    pthread_mutex_lock(&p_pool->mutex);
    unit_t* p_unit = NULL;
    do {
        if ((p_pool->cnt++ & 0xFF) != 0) {
            p_unit = queue_pop(&p_pool->high_prio_queue);
            if (p_unit) {
                // fprintf(stderr, "DBG: found high.\n");
                break;
            }
            p_unit = queue_pop(&p_pool->low_prio_queue);
            if (p_unit) {
                // fprintf(stderr, "DBG: found low.\n");
                break;
            }
        } else {
            p_unit = queue_pop(&p_pool->low_prio_queue);
            if (p_unit) {
                // fprintf(stderr, "DBG: found low.\n");
                break;
            }
            p_unit = queue_pop(&p_pool->high_prio_queue);
            if (p_unit) {
                // fprintf(stderr, "DBG: found high.\n");
                break;
            }
        }
    } while (0);
    if (p_unit) p_pool->num--;
    if (p_pool->num == 0) {
        /* clear eventfd state when we transition to empty */
        eventfd_read(p_pool->efd, &tmp_val);
    }
    pthread_mutex_unlock(&p_pool->mutex);
    return p_unit ? (ABT_unit)p_unit : ABT_UNIT_NULL;
}

static inline void convert_double_sec_to_timespec(struct timespec* ts_out,
                                                  double           seconds)
{
    ts_out->tv_sec  = (time_t)seconds;
    ts_out->tv_nsec = (long)((seconds - ts_out->tv_sec) * 1000000000.0);
}

static ABT_unit pool_pop_timedwait(ABT_pool pool, double abstime_secs)
{
    pool_t*  p_pool;
    uint64_t tmp_val;
    ABT_pool_get_data(pool, (void**)&p_pool);
    /* Sometimes it should pop from low_prio_queue to avoid a deadlock. */
    pthread_mutex_lock(&p_pool->mutex);
    unit_t* p_unit = NULL;
    if (p_pool->num == 0) {
        if (p_pool->efd > -1) {
            struct epoll_event event;
            int                timeout_ms;
            int                ret;
            /* if the pool is configured to signal an event file descriptor
             * when it transitions to non-idle, then go ahead and reuse it for
             * internal signalling as well.
             */
            if (!abstime_secs) {
                timeout_ms = 0;
            } else {
                /* TODO: if we used the new pop_wait() interface we could
                 * avoid two time calls per iteration.
                 */
                timeout_ms = (int)((ABT_get_wtime() - abstime_secs) * 1000.0);
                if (timeout_ms < 0) timeout_ms = 100;
            }
            // fprintf(stderr,
            //         "DBG: calling epoll_wait with timeout %d (abstime_secs
            //         was "
            //        "%f)\n",
            //        timeout_ms, abstime_secs);
            /* TODO: error handling */
            /* NOTE: we have to drop mutex while blocking in epoll to avoid
             * deadlock.  Note that we only set and clear events with the
             * lock held.
             */
            pthread_mutex_unlock(&p_pool->mutex);
            ret = epoll_wait(p_pool->epfd, &event, 1, timeout_ms);
            pthread_mutex_lock(&p_pool->mutex);
            // fprintf(stderr, "DBG: epoll_wait returned %d, num %d\n", ret,
            //        p_pool->num);
        } else {
            struct timespec ts;
            convert_double_sec_to_timespec(&ts, abstime_secs);
            pthread_cond_timedwait(&p_pool->cond, &p_pool->mutex, &ts);
        }
    }
    do {
        if ((p_pool->cnt++ & 0xFF) != 0) {
            p_unit = queue_pop(&p_pool->high_prio_queue);
            if (p_unit) break;
            p_unit = queue_pop(&p_pool->low_prio_queue);
            if (p_unit) break;
        } else {
            p_unit = queue_pop(&p_pool->low_prio_queue);
            if (p_unit) break;
            p_unit = queue_pop(&p_pool->high_prio_queue);
            if (p_unit) break;
        }
    } while (0);
    if (p_unit) p_pool->num--;
    if (p_pool->num == 0) {
        /* clear eventfd state when we transition to empty */
        eventfd_read(p_pool->efd, &tmp_val);
    }
    pthread_mutex_unlock(&p_pool->mutex);
    return p_unit ? (ABT_unit)p_unit : ABT_UNIT_NULL;
}

static int pool_free(ABT_pool pool)
{
    pool_t* p_pool;
    ABT_pool_get_data(pool, (void**)&p_pool);
    pthread_mutex_destroy(&p_pool->mutex);
    pthread_cond_destroy(&p_pool->cond);
    if (p_pool->epfd > -1) close(p_pool->epfd);
    free(p_pool);

    return ABT_SUCCESS;
}

void margo_create_prio_pool_def(ABT_pool_def* p_def)
{
    p_def->access               = ABT_POOL_ACCESS_MPMC;
    p_def->u_create_from_thread = pool_unit_create_from_thread;
    p_def->u_free               = pool_unit_free;
    p_def->p_init               = pool_init;
    p_def->p_get_size           = pool_get_size;
    p_def->p_push               = pool_push;
    p_def->p_pop                = pool_pop;
    p_def->p_pop_timedwait      = pool_pop_timedwait;
    p_def->p_free               = pool_free;
    p_def->p_print_all          = NULL; /* Optional. */
}
