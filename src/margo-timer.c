
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
#include "margo-timer.h"
#include "utlist.h"


/* structure for mapping margo instance ids to corresponding timer instances */
struct margo_timer_list
{
    ABT_mutex mutex;
    margo_timer_t *queue_head;
};

static void margo_timer_queue(
    struct margo_timer_list *timer_lst,
    margo_timer_t *timer);


struct margo_timer_list* margo_timer_list_create()
{
    struct margo_timer_list *timer_lst;

    timer_lst = malloc(sizeof(*timer_lst));
    if(!timer_lst)
        return NULL;

    ABT_mutex_create(&(timer_lst->mutex));
    timer_lst->queue_head = NULL;

    return timer_lst;
}

void margo_timer_list_free(margo_instance_id mid, struct margo_timer_list* timer_lst)
{
    margo_timer_t *cur;
    ABT_pool handler_pool;
    int ret;

    ABT_mutex_lock(timer_lst->mutex);
    /* delete any remaining timers from the queue */
    while(timer_lst->queue_head)
    {
        cur = timer_lst->queue_head;
        DL_DELETE(timer_lst->queue_head, cur);
        cur->prev = cur->next = NULL;

        /* we must issue the callback now for any pending timers or else the
         * callers will hang indefinitely
         */
        margo_get_handler_pool(mid, &handler_pool);
        if(handler_pool != ABT_POOL_NULL)
        {
            /* if handler pool is present, run callback there */
            ret = ABT_thread_create(handler_pool, cur->cb_fn, cur->cb_dat,
                ABT_THREAD_ATTR_NULL, NULL);
            assert(ret == ABT_SUCCESS);
        }
        else
        {
            /* else run callback in place */
            cur->cb_fn(cur->cb_dat);
        }
    }
    ABT_mutex_unlock(timer_lst->mutex);
    ABT_mutex_free(&(timer_lst->mutex));

    free(timer_lst);

    return;
}

void margo_timer_init(
    margo_instance_id mid,
    margo_timer_t *timer,
    margo_timer_cb_fn cb_fn,
    void *cb_dat,
    double timeout_ms)
{
    struct margo_timer_list *timer_lst;

    timer_lst = margo_get_timer_list(mid);
    assert(timer_lst);
    assert(timer);

    memset(timer, 0, sizeof(*timer));
    timer->cb_fn = cb_fn;
    timer->cb_dat = cb_dat;
    timer->expiration = ABT_get_wtime() + (timeout_ms/1000);
    timer->prev = timer->next = NULL;

    margo_timer_queue(timer_lst, timer);

    return;
}

void margo_timer_destroy(
    margo_instance_id mid,
    margo_timer_t *timer)
{
    struct margo_timer_list *timer_lst;

    timer_lst = margo_get_timer_list(mid);
    assert(timer_lst);
    assert(timer);

    ABT_mutex_lock(timer_lst->mutex);
    if(timer->prev || timer->next)
        DL_DELETE(timer_lst->queue_head, timer);
    ABT_mutex_unlock(timer_lst->mutex);

    return;
}

void margo_check_timers(
    margo_instance_id mid)
{
    int ret;
    margo_timer_t *cur;
    struct margo_timer_list *timer_lst;
    ABT_pool handler_pool;
    double now;

    timer_lst = margo_get_timer_list(mid);
    assert(timer_lst);

    ABT_mutex_lock(timer_lst->mutex);

    if(timer_lst->queue_head)
        now = ABT_get_wtime();

    /* iterate through timer list, performing timeout action
     * for all elements which have passed expiration time
     */
    while(timer_lst->queue_head && (timer_lst->queue_head->expiration < now))
    {
        cur = timer_lst->queue_head;
        DL_DELETE(timer_lst->queue_head, cur);
        cur->prev = cur->next = NULL;

        /* schedule callback on the handler pool */
        margo_get_handler_pool(mid, &handler_pool);
        if(handler_pool != ABT_POOL_NULL)
        {
            ret = ABT_thread_create(handler_pool, cur->cb_fn, cur->cb_dat,
                ABT_THREAD_ATTR_NULL, NULL);
            assert(ret == ABT_SUCCESS);
        }
        else
        {
            cur->cb_fn(cur->cb_dat);
        }
    }
    ABT_mutex_unlock(timer_lst->mutex);

    return;
}

/* returns 0 and sets 'next_timer_exp' if the timer instance
 * has timers queued up, -1 otherwise
 */
int margo_timer_get_next_expiration(
    margo_instance_id mid,
    double *next_timer_exp)
{
    struct margo_timer_list *timer_lst;
    double now;
    int ret;

    timer_lst = margo_get_timer_list(mid);
    assert(timer_lst);

    ABT_mutex_lock(timer_lst->mutex);
    if(timer_lst->queue_head)
    {
        now = ABT_get_wtime();
        *next_timer_exp = timer_lst->queue_head->expiration - now;
        ret = 0;
    }
    else
    {
        ret = -1;
    }
    ABT_mutex_unlock(timer_lst->mutex);

    return(ret);
}

static void margo_timer_queue(
    struct margo_timer_list *timer_lst,
    margo_timer_t *timer)
{
    margo_timer_t *cur;

    ABT_mutex_lock(timer_lst->mutex);

    /* if list of timers is empty, put ourselves on it */
    if(!(timer_lst->queue_head))
    {
        DL_APPEND(timer_lst->queue_head, timer);
    }
    else
    {
        /* something else already in queue, keep it sorted in ascending order
         * of expiration time
         */
        cur = timer_lst->queue_head;
        do
        {
            /* walk backwards through queue */
            cur = cur->prev;
            /* as soon as we find an element that expires before this one, 
             * then we add ours after it
             */
            if(cur->expiration < timer->expiration)
            {
                DL_APPEND_ELEM(timer_lst->queue_head, cur, timer);
                break;
            }
        }while(cur != timer_lst->queue_head);

        /* if we never found one with an expiration before this one, then
         * this one is the new head
         */
        if(timer->prev == NULL && timer->next == NULL)
            DL_PREPEND(timer_lst->queue_head, timer);
    }
    ABT_mutex_unlock(timer_lst->mutex);

    return;
}
