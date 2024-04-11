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

/* Timer definition */
typedef struct margo_timer {
    margo_instance_id       mid;
    margo_timer_callback_fn cb_fn;
    void*                   cb_dat;
    ABT_pool                pool;
    double                  expiration;

    /* finalization mechanism, to ensure that no ULT associated with
     * this timer remains to be executed. */
    ABT_mutex_memory mutex;
    ABT_cond_memory  cv;
    size_t           num_pending;
    _Atomic bool     canceled;
    _Atomic bool     destroy_requested;

    struct margo_timer_list* owner;

    struct margo_timer* next;
    struct margo_timer* prev;
} margo_timer;

/* List of timers sorted by expiration date */
struct margo_timer_list {
    margo_timer*     queue_head;
    ABT_mutex_memory mutex;
    /* finalization mechanism, to ensure that no ULT associated with timers
     * remain */
    ABT_cond_memory cv;
    size_t          num_pending;
    _Atomic bool    destroy_requested;
    /* Note: because ULTs associated with timers may still be pending
     * when finalization is requested, we keep track of the number of
     * pending ULTs and set destroy_requested to true upon finalizing.
     * destroy_requested being true will prevent the submission of
     * new timers. When num_pending reaches 0, __margo_timer_list_free
     * unblocks and frees the list. */
};

static inline struct margo_timer_list* get_timer_list(margo_instance_id mid)
{
    return mid->timer_list;
}

static inline void timer_list_cleanup(struct margo_timer_list* timer_lst)
{
    free(timer_lst);
}

static inline void timer_cleanup(margo_timer_t timer) { free(timer); }

static void timer_ult(void* args)
{
    margo_timer_t            timer     = (margo_timer_t)args;
    struct margo_timer_list* timer_lst = timer->owner;

    if (!timer->canceled) timer->cb_fn(timer->cb_dat);
    /* decrease the number of pending ULTs associated with the timer,
     * check if destruction of the timer was requested, and cleanup
     * if needed. */
    ABT_mutex_lock(ABT_MUTEX_MEMORY_GET_HANDLE(&timer->mutex));
    timer->num_pending -= 1;
    bool no_more_pending = timer->num_pending == 0;
    bool need_destroy    = no_more_pending && timer->destroy_requested;
    ABT_mutex_unlock(ABT_MUTEX_MEMORY_GET_HANDLE(&timer->mutex));
    if (no_more_pending)
        ABT_cond_signal(ABT_COND_MEMORY_GET_HANDLE(&timer->cv));
    if (need_destroy) timer_cleanup(timer);

    /* decrease the number of pending ULTs associated with any timer
     * belonging to the same list, and notify the condition variable
     * when reaching 0 to potentiallyunblock __margo_timer_list_free. */
    ABT_mutex_lock(ABT_MUTEX_MEMORY_GET_HANDLE(&timer_lst->mutex));
    timer_lst->num_pending -= 1;
    no_more_pending = timer_lst->num_pending == 0;
    ABT_mutex_unlock(ABT_MUTEX_MEMORY_GET_HANDLE(&timer_lst->mutex));
    if (no_more_pending)
        ABT_cond_signal(ABT_COND_MEMORY_GET_HANDLE(&timer_lst->cv));
}

struct margo_timer_list* __margo_timer_list_create()
{
    struct margo_timer_list* timer_lst;

    timer_lst = calloc(1, sizeof(*timer_lst));
    if (!timer_lst) return NULL;

    return timer_lst;
}

void __margo_timer_list_free(margo_instance_id mid)
{
    struct margo_timer_list* timer_lst = get_timer_list(mid);
    margo_timer*             cur;
    int                      ret;

    ABT_mutex_lock(ABT_MUTEX_MEMORY_GET_HANDLE(&timer_lst->mutex));
    timer_lst->destroy_requested = true;
    /* delete any remaining timers from the queue */
    while (timer_lst->queue_head) {
        cur = timer_lst->queue_head;
        DL_DELETE(timer_lst->queue_head, cur);
        cur->prev = cur->next = NULL;

        /* we must issue the callback now for any pending timers or else the
         * callers may hang indefinitely
         */
        if (cur->pool != ABT_POOL_NULL) {
            ABT_mutex_lock(ABT_MUTEX_MEMORY_GET_HANDLE(&cur->mutex));
            cur->num_pending += 1;
            ABT_mutex_unlock(ABT_MUTEX_MEMORY_GET_HANDLE(&cur->mutex));

            timer_lst->num_pending += 1;

            ret = ABT_thread_create(cur->pool, timer_ult, cur,
                                    ABT_THREAD_ATTR_NULL, NULL);
            assert(ret == ABT_SUCCESS);
        } else {
            cur->cb_fn(cur->cb_dat);
        }
    }

    /* check if we can cleanup the list or if cleanup will be done by
     * one of the submitted ULTs */
    while (timer_lst->num_pending != 0) {
        ABT_cond_wait(ABT_COND_MEMORY_GET_HANDLE(&timer_lst->cv),
                      ABT_MUTEX_MEMORY_GET_HANDLE(&timer_lst->mutex));
    }
    timer_list_cleanup(timer_lst);
    /* Note: no need to call ABT_mutex_unlock,
     * the timer_lst has been freed at this point */
}

void __margo_check_timers(margo_instance_id mid)
{
    int                      ret;
    margo_timer*             cur;
    struct margo_timer_list* timer_lst;
    double                   now;

    timer_lst = get_timer_list(mid);
    assert(timer_lst);

    ABT_mutex_lock(ABT_MUTEX_MEMORY_GET_HANDLE(&timer_lst->mutex));

    if (timer_lst->queue_head) now = ABT_get_wtime();

    /* iterate through timer list, performing timeout action
     * for all elements which have passed expiration time
     */
    while (timer_lst->queue_head && (timer_lst->queue_head->expiration < now)) {
        cur = timer_lst->queue_head;
        DL_DELETE(timer_lst->queue_head, cur);
        cur->prev = cur->next = NULL;

        if (cur->pool != ABT_POOL_NULL) {
            ABT_mutex_lock(ABT_MUTEX_MEMORY_GET_HANDLE(&cur->mutex));
            cur->num_pending += 1;
            ABT_mutex_unlock(ABT_MUTEX_MEMORY_GET_HANDLE(&cur->mutex));

            timer_lst->num_pending += 1;

            ret = ABT_thread_create(cur->pool, timer_ult, cur,
                                    ABT_THREAD_ATTR_NULL, NULL);
            assert(ret == ABT_SUCCESS);
        } else {
            cur->cb_fn(cur->cb_dat);
        }
    }
    ABT_mutex_unlock(ABT_MUTEX_MEMORY_GET_HANDLE(&timer_lst->mutex));

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

    timer_lst = get_timer_list(mid);
    assert(timer_lst);

    ABT_mutex_lock(ABT_MUTEX_MEMORY_GET_HANDLE(&timer_lst->mutex));
    if (timer_lst->queue_head) {
        now             = ABT_get_wtime();
        *next_timer_exp = timer_lst->queue_head->expiration - now;
        ret             = 0;
    } else {
        ret = -1;
    }
    ABT_mutex_unlock(ABT_MUTEX_MEMORY_GET_HANDLE(&timer_lst->mutex));

    return ret;
}

static void __margo_timer_queue(struct margo_timer_list* timer_lst,
                                margo_timer*             timer)
{
    margo_timer* cur;

    ABT_mutex_lock(ABT_MUTEX_MEMORY_GET_HANDLE(&timer_lst->mutex));

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
    ABT_mutex_unlock(ABT_MUTEX_MEMORY_GET_HANDLE(&timer_lst->mutex));

    return;
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
    if (!pool) pool = ABT_POOL_NULL;

    struct margo_timer_list* timer_lst = get_timer_list(mid);
    if (timer_lst->destroy_requested) return -1;

    margo_timer_t tmp = (margo_timer_t)calloc(1, sizeof(*tmp));
    if (!tmp) return -1;
    tmp->mid    = mid;
    tmp->cb_fn  = cb_fn;
    tmp->cb_dat = cb_dat;
    tmp->pool   = pool;
    tmp->owner  = timer_lst;
    *timer      = tmp;
    return 0;
}

int margo_timer_start(margo_timer_t timer, double timeout_ms)
{
    struct margo_timer_list* timer_lst = get_timer_list(timer->mid);
    bool already_started = timer->prev != NULL || timer->next != NULL;

    if (already_started || timer->canceled || timer_lst->destroy_requested)
        return -1;

    timer->expiration = ABT_get_wtime() + (timeout_ms / 1000);
    __margo_timer_queue(timer_lst, timer);

    return 0;
}

int margo_timer_cancel(margo_timer_t timer)
{
    // Mark the timer as canceled to prevent existing ULTs that have been
    // submitted but haven't started from calling the callback and to prevent
    // calls to margo_timer_start on this timer from succeeding.
    timer->canceled                    = true;
    struct margo_timer_list* timer_lst = get_timer_list(timer->mid);

    // Remove the timer from the list of pending timers
    ABT_mutex_lock(ABT_MUTEX_MEMORY_GET_HANDLE(&timer_lst->mutex));
    if (timer->prev || timer->next) DL_DELETE(timer_lst->queue_head, timer);
    ABT_mutex_unlock(ABT_MUTEX_MEMORY_GET_HANDLE(&timer_lst->mutex));

    timer->prev = timer->next = NULL;

    // Wait for any remaining ULTs
    ABT_mutex_lock(ABT_MUTEX_MEMORY_GET_HANDLE(&timer->mutex));
    while (timer->num_pending != 0) {
        ABT_cond_wait(ABT_COND_MEMORY_GET_HANDLE(&timer->cv),
                      ABT_MUTEX_MEMORY_GET_HANDLE(&timer->mutex));
    }
    ABT_mutex_unlock(ABT_MUTEX_MEMORY_GET_HANDLE(&timer->mutex));

    // Uncancel the timer (so we can call margo_timer_start again)
    timer->canceled = false;

    return 0;
}

int margo_timer_destroy(margo_timer_t timer)
{
    ABT_mutex_lock(ABT_MUTEX_MEMORY_GET_HANDLE(&timer->mutex));
    bool can_destroy = !timer->prev && !timer->next && !timer->num_pending;
    timer->destroy_requested = !can_destroy;
    ABT_mutex_unlock(ABT_MUTEX_MEMORY_GET_HANDLE(&timer->mutex));
    if (can_destroy) timer_cleanup(timer);
    return 0;
}
