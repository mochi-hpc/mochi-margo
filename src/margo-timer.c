
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
#include "margo-timer.h"
#include "utlist.h"


static void margo_queue_timer(margo_timed_element *el);


static ABT_mutex timer_mutex = ABT_MUTEX_NULL;
static margo_timed_element *timer_head = NULL;

void margo_timer_init()
{
    ABT_mutex_create(&timer_mutex);
    return;
}

void margo_timer_cleanup()
{
    margo_timed_element *cur;

    if(timer_mutex == ABT_MUTEX_NULL)
        return;

    ABT_mutex_lock(timer_mutex);
    /* free any remaining timers */
    while(timer_head)
    {
        cur = timer_head;
        DL_DELETE(timer_head, cur);
        free(cur);
    }
    ABT_mutex_unlock(timer_mutex);
    ABT_mutex_free(&timer_mutex);
    timer_mutex = ABT_MUTEX_NULL;
    return;
}

typedef struct
{
    ABT_mutex sleep_mutex;
    ABT_cond sleep_cond;
} margo_thread_sleep_cb_dat;

static void margo_thread_sleep_cb(void *arg)
{
    margo_thread_sleep_cb_dat *cb_dat =
        (margo_thread_sleep_cb_dat *)arg;

    /* wake up the sleeping thread */
    ABT_mutex_lock(cb_dat->sleep_mutex);
    ABT_cond_signal(cb_dat->sleep_cond);
    ABT_mutex_unlock(cb_dat->sleep_mutex);

    return;
}

void margo_thread_sleep(
    double timeout_ms)
{
    margo_thread_sleep_cb_dat *cb_dat;

    /* set data needed for callback */
    cb_dat = malloc(sizeof(margo_thread_sleep_cb_dat));
    assert(cb_dat);
    memset(cb_dat, 0 , sizeof(*cb_dat));
    ABT_mutex_create(&(cb_dat->sleep_mutex));
    ABT_cond_create(&(cb_dat->sleep_cond));

    /* create timer */
    margo_timer_create(margo_thread_sleep_cb, cb_dat, timeout_ms, NULL);

    /* yield thread for specified timeout */
    ABT_mutex_lock(cb_dat->sleep_mutex);
    ABT_cond_wait(cb_dat->sleep_cond, cb_dat->sleep_mutex);
    ABT_mutex_unlock(cb_dat->sleep_mutex);

    return;
}
 
void margo_timer_create(
    margo_timer_cb_fn cb_fn,
    void *cb_dat,
    double timeout_ms,
    margo_timer_handle *handle)
{
    margo_timed_element *el;

    el = malloc(sizeof(margo_timed_element));
    assert(el);
    memset(el, 0, sizeof(*el));
    el->cb_fn = cb_fn;
    el->cb_dat = cb_dat;
    el->expiration = ABT_get_wtime() + (timeout_ms/1000);
    el->prev = el->next = NULL;

    margo_queue_timer(el);

    if(handle)
        *handle = (margo_timer_handle)el;

    return;
}

void margo_timer_free(
    margo_timer_handle handle)
{
    assert(handle);
    assert(timer_mutex != ABT_MUTEX_NULL);

    margo_timed_element *el;
    el = (margo_timed_element *)handle;

    ABT_mutex_lock(timer_mutex);
    if(el->prev || el->next)
    {
        DL_DELETE(timer_head, el);
        if(el->cb_dat)
            free(el->cb_dat);
        free(el);
    }
    ABT_mutex_unlock(timer_mutex);

    return;
}

void margo_check_timers()
{
    margo_timed_element *cur;
    double now = ABT_get_wtime();

    assert(timer_mutex != ABT_MUTEX_NULL);

    ABT_mutex_lock(timer_mutex);

    /* iterate through timer list, performing timeout action
     * for all elements which have passed expiration time
     */
    while(timer_head && (timer_head->expiration < now))
    {
        cur = timer_head;
        DL_DELETE(timer_head, cur);

        /* execute callback */
        cur->cb_fn(cur->cb_dat);

        free(cur);
    }
    ABT_mutex_unlock(timer_mutex);

    return;
}

static void margo_queue_timer(margo_timed_element *el)
{
    margo_timed_element *cur;

    assert(timer_mutex != ABT_MUTEX_NULL);

    ABT_mutex_lock(timer_mutex);

    /* if list of timers is empty, put ourselves on it */
    if(!timer_head)
    {
        DL_APPEND(timer_head, el);
    }
    else
    {
        /* something else already in queue, keep it sorted in ascending order
         * of expiration time
         */
        cur = timer_head;
        do
        {
            /* walk backwards through queue */
            cur = cur->prev;
            /* as soon as we find an element that expires before this one, 
             * then we add ours after it
             */
            if(cur->expiration < el->expiration)
            {
                DL_APPEND_ELEM(timer_head, cur, el);
                break;
            }
        }while(cur != timer_head);

        /* if we never found one with an expiration before this one, then
         * this one is the new head
         */
        if(el->prev == NULL && el->next == NULL)
            DL_PREPEND(timer_head, el);
    }
    ABT_mutex_unlock(timer_mutex);

    return;
}

