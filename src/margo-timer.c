/*
 * (C) 2016 The University of Chicago
 *
 * See COPYRIGHT in top-level directory.
 */
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <assert.h>

#include <abt.h>
#include "margo.h"
#include "margo-instance.h"
#include "margo-timer-private.h"
#include "utlist.h"

/* structure for mapping margo instance ids to corresponding timer instances */
struct margo_timer_list {
    ABT_mutex    mutex;
    margo_timer* queue_head;
};

static inline void timer_cleanup(margo_timer_t timer) { free(timer); }

static void timer_ult(void* args)
{
    margo_timer_t timer = (margo_timer_t)args;
    if (!timer->cancelled) timer->cb_fn(timer->cb_dat);
    ABT_mutex_lock(ABT_MUTEX_MEMORY_GET_HANDLE(&timer->mtx_mem));
    timer->num_pending -= 1;
    bool no_more_pending = timer->num_pending == 0;
    bool need_destroy    = no_more_pending && timer->destroy_requested;
    ABT_mutex_unlock(ABT_MUTEX_MEMORY_GET_HANDLE(&timer->mtx_mem));
    if (no_more_pending)
        ABT_cond_signal(ABT_COND_MEMORY_GET_HANDLE(&timer->cv_mem));
    if (need_destroy) timer_cleanup(timer);
}

static void __margo_timer_queue(struct margo_timer_list* timer_lst,
                                margo_timer*             timer);

struct margo_timer_list* __margo_timer_list_create()
{
    struct margo_timer_list* timer_lst;

    timer_lst = malloc(sizeof(*timer_lst));
    if (!timer_lst) return NULL;

    ABT_mutex_create(&(timer_lst->mutex));
    timer_lst->queue_head = NULL;

    return timer_lst;
}

void __margo_timer_list_free(margo_instance_id        mid,
                             struct margo_timer_list* timer_lst)
{
    margo_timer* cur;
    int          ret;

    ABT_mutex_lock(timer_lst->mutex);
    /* delete any remaining timers from the queue */
    while (timer_lst->queue_head) {
        cur = timer_lst->queue_head;
        DL_DELETE(timer_lst->queue_head, cur);
        cur->prev = cur->next = NULL;

        ABT_mutex_lock(ABT_MUTEX_MEMORY_GET_HANDLE(&cur->mtx_mem));
        cur->num_pending += 1;
        ABT_mutex_unlock(ABT_MUTEX_MEMORY_GET_HANDLE(&cur->mtx_mem));

        /* we must issue the callback now for any pending timers or else the
         * callers will hang indefinitely
         */
        ret = ABT_thread_create(cur->pool, timer_ult, cur, ABT_THREAD_ATTR_NULL,
                                NULL);
        assert(ret == ABT_SUCCESS);
    }
    ABT_mutex_unlock(timer_lst->mutex);
    ABT_mutex_free(&(timer_lst->mutex));

    free(timer_lst);

    return;
}

void __margo_timer_init(margo_instance_id       mid,
                        margo_timer*            timer,
                        margo_timer_callback_fn cb_fn,
                        void*                   cb_dat,
                        double                  timeout_ms)
{
    struct margo_timer_list* timer_lst;

    timer_lst = __margo_get_timer_list(mid);
    assert(timer_lst);
    assert(timer);

    memset(timer, 0, sizeof(*timer));
    timer->mid        = mid;
    timer->cb_fn      = cb_fn;
    timer->cb_dat     = cb_dat;
    timer->expiration = ABT_get_wtime() + (timeout_ms / 1000);
    timer->prev = timer->next = NULL;
    margo_get_handler_pool(mid, &timer->pool);

    __margo_timer_queue(timer_lst, timer);

    return;
}

void __margo_timer_destroy(margo_instance_id mid, margo_timer* timer)
{
    struct margo_timer_list* timer_lst;

    timer_lst = __margo_get_timer_list(mid);
    assert(timer_lst);
    assert(timer);

    ABT_mutex_lock(timer_lst->mutex);
    if (timer->prev || timer->next) DL_DELETE(timer_lst->queue_head, timer);
    ABT_mutex_unlock(timer_lst->mutex);

    return;
}

void __margo_check_timers(margo_instance_id mid)
{
    int                      ret;
    margo_timer*             cur;
    struct margo_timer_list* timer_lst;
    double                   now;

    timer_lst = __margo_get_timer_list(mid);
    assert(timer_lst);

    ABT_mutex_lock(timer_lst->mutex);

    if (timer_lst->queue_head) now = ABT_get_wtime();

    /* iterate through timer list, performing timeout action
     * for all elements which have passed expiration time
     */
    while (timer_lst->queue_head && (timer_lst->queue_head->expiration < now)) {
        cur = timer_lst->queue_head;
        DL_DELETE(timer_lst->queue_head, cur);
        cur->prev = cur->next = NULL;

        ABT_mutex_lock(ABT_MUTEX_MEMORY_GET_HANDLE(&cur->mtx_mem));
        cur->num_pending += 1;
        ABT_mutex_unlock(ABT_MUTEX_MEMORY_GET_HANDLE(&cur->mtx_mem));

        ret = ABT_thread_create(cur->pool, timer_ult, cur, ABT_THREAD_ATTR_NULL,
                                NULL);
        assert(ret == ABT_SUCCESS);
    }
    ABT_mutex_unlock(timer_lst->mutex);

    return;
}

/* returns 0 and sets 'next_timer_exp' if the timer instance
 * has timers queued up, -1 otherwise
 */
int __margo_timer_get_next_expiration(margo_instance_id mid,
                                      double*           next_timer_exp)
{
    struct margo_timer_list* timer_lst;
    double                   now;
    int                      ret;

    timer_lst = __margo_get_timer_list(mid);
    assert(timer_lst);

    ABT_mutex_lock(timer_lst->mutex);
    if (timer_lst->queue_head) {
        now             = ABT_get_wtime();
        *next_timer_exp = timer_lst->queue_head->expiration - now;
        ret             = 0;
    } else {
        ret = -1;
    }
    ABT_mutex_unlock(timer_lst->mutex);

    return (ret);
}

static void __margo_timer_queue(struct margo_timer_list* timer_lst,
                                margo_timer*             timer)
{
    margo_timer* cur;

    ABT_mutex_lock(timer_lst->mutex);

    /* if list of timers is empty, put ourselves on it */
    if (!(timer_lst->queue_head)) {
        DL_APPEND(timer_lst->queue_head, timer);
    } else {
        /* something else already in queue, keep it sorted in ascending order
         * of expiration time
         */
        cur = timer_lst->queue_head;
        do {
            /* walk backwards through queue */
            cur = cur->prev;
            /* as soon as we find an element that expires before this one,
             * then we add ours after it
             */
            if (cur->expiration < timer->expiration) {
                DL_APPEND_ELEM(timer_lst->queue_head, cur, timer);
                break;
            }
        } while (cur != timer_lst->queue_head);

        /* if we never found one with an expiration before this one, then
         * this one is the new head
         */
        if (timer->prev == NULL && timer->next == NULL)
            DL_PREPEND(timer_lst->queue_head, timer);
    }
    ABT_mutex_unlock(timer_lst->mutex);

    return;
}

struct margo_timer_list* __margo_get_timer_list(margo_instance_id mid)
{
    return mid->timer_list;
}

int margo_timer_create(margo_instance_id       mid,
                       margo_timer_callback_fn cb_fn,
                       void*                   cb_dat,
                       margo_timer_t*          timer)
{
    ABT_pool pool;
    int      ret = margo_get_handler_pool(mid, &pool);
    if (ret != 0) return ret;
    return margo_timer_create_with_pool(mid, cb_fn, cb_dat, pool, timer);
}

int margo_timer_create_with_pool(margo_instance_id       mid,
                                 margo_timer_callback_fn cb_fn,
                                 void*                   cb_dat,
                                 ABT_pool                pool,
                                 margo_timer_t*          timer)
{
    if (!pool || pool == ABT_POOL_NULL)
        return margo_timer_create(mid, cb_fn, cb_dat, timer);

    margo_timer_t tmp = (margo_timer_t)calloc(1, sizeof(*tmp));
    if (!tmp) return -1;
    tmp->mid    = mid;
    tmp->cb_fn  = cb_fn;
    tmp->cb_dat = cb_dat;
    tmp->pool   = pool;
    *timer      = tmp;
    return 0;
}

int margo_timer_start(margo_timer_t timer, double timeout_ms)
{
    if (timer->prev != NULL || timer->next != NULL) return -1;

    struct margo_timer_list* timer_lst = __margo_get_timer_list(timer->mid);
    timer->expiration                  = ABT_get_wtime() + (timeout_ms / 1000);
    __margo_timer_queue(timer_lst, timer);

    return 0;
}

int margo_timer_cancel(margo_timer_t timer)
{
    // Mark the timer as cancelled to prevent existing ULTs from calling the
    // callback
    timer->cancelled                   = true;
    struct margo_timer_list* timer_lst = __margo_get_timer_list(timer->mid);

    // Remove the timer from the list of pending timers
    ABT_mutex_lock(timer_lst->mutex);
    if (timer->prev || timer->next) DL_DELETE(timer_lst->queue_head, timer);
    ABT_mutex_unlock(timer_lst->mutex);

    timer->prev = timer->next = NULL;

    // Wait for any remaining ULTs
    ABT_mutex_lock(ABT_MUTEX_MEMORY_GET_HANDLE(&timer->mtx_mem));
    while (timer->num_pending != 0) {
        ABT_cond_wait(ABT_COND_MEMORY_GET_HANDLE(&timer->cv_mem),
                      ABT_MUTEX_MEMORY_GET_HANDLE(&timer->mtx_mem));
    }
    ABT_mutex_unlock(ABT_MUTEX_MEMORY_GET_HANDLE(&timer->mtx_mem));

    // Uncancel the timer
    timer->cancelled = false;

    return 0;
}

int margo_timer_destroy(margo_timer_t timer)
{
    timer->destroy_requested = true;
    ABT_mutex_lock(ABT_MUTEX_MEMORY_GET_HANDLE(&timer->mtx_mem));
    bool can_destroy = !timer->prev && !timer->next && !timer->num_pending;
    ABT_mutex_unlock(ABT_MUTEX_MEMORY_GET_HANDLE(&timer->mtx_mem));
    if (can_destroy) timer_cleanup(timer);
    return 0;
}
