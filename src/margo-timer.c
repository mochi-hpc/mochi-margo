
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


static void margo_timer_queue(margo_timer_t *timer);


static ABT_mutex timer_mutex = ABT_MUTEX_NULL;
static margo_timer_t *timer_head = NULL;

void margo_timer_sys_init()
{
    ABT_mutex_create(&timer_mutex);
    return;
}

void margo_timer_sys_shutdown()
{
    margo_timer_t *cur;

    if(timer_mutex == ABT_MUTEX_NULL)
        return;

    ABT_mutex_lock(timer_mutex);
    /* delete any remaining timers from the queue */
    while(timer_head)
    {
        cur = timer_head;
        DL_DELETE(timer_head, cur);
    }
    ABT_mutex_unlock(timer_mutex);
    ABT_mutex_free(&timer_mutex);
    timer_mutex = ABT_MUTEX_NULL;

    return;
}

typedef struct
{
    ABT_mutex mutex;
    ABT_cond cond;
} margo_thread_sleep_cb_dat;

static void margo_thread_sleep_cb(void *arg)
{
    margo_thread_sleep_cb_dat *sleep_cb_dat =
        (margo_thread_sleep_cb_dat *)arg;

    /* wake up the sleeping thread */
    ABT_mutex_lock(sleep_cb_dat->mutex);
    ABT_cond_signal(sleep_cb_dat->cond);
    ABT_mutex_unlock(sleep_cb_dat->mutex);

    return;
}

void margo_thread_sleep(
    double timeout_ms)
{
    margo_timer_t sleep_timer;
    margo_thread_sleep_cb_dat sleep_cb_dat;

    /* set data needed for sleep callback */
    ABT_mutex_create(&(sleep_cb_dat.mutex));
    ABT_cond_create(&(sleep_cb_dat.cond));

    /* initialize the sleep timer */
    margo_timer_init(&sleep_timer, margo_thread_sleep_cb,
        &sleep_cb_dat, timeout_ms);

    /* yield thread for specified timeout */
    ABT_mutex_lock(sleep_cb_dat.mutex);
    ABT_cond_wait(sleep_cb_dat.cond, sleep_cb_dat.mutex);
    ABT_mutex_unlock(sleep_cb_dat.mutex);

    return;
}
 
void margo_timer_init(
    margo_timer_t *timer,
    margo_timer_cb_fn cb_fn,
    void *cb_dat,
    double timeout_ms)
{
    assert(timer_mutex != ABT_MUTEX_NULL);
    assert(timer);

    memset(timer, 0, sizeof(*timer));
    timer->cb_fn = cb_fn;
    timer->cb_dat = cb_dat;
    timer->expiration = ABT_get_wtime() + (timeout_ms/1000);
    timer->prev = timer->next = NULL;

    margo_timer_queue(timer);

    return;
}

void margo_timer_destroy(
    margo_timer_t *timer)
{
    assert(timer_mutex != ABT_MUTEX_NULL);
    assert(timer);

    ABT_mutex_lock(timer_mutex);
    if(timer->prev || timer->next)
        DL_DELETE(timer_head, timer);
    ABT_mutex_unlock(timer_mutex);

    return;
}

void margo_check_timers()
{
    margo_timer_t *cur;
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
        cur->prev = cur->next = NULL;

        /* execute callback */
        cur->cb_fn(cur->cb_dat);
    }
    ABT_mutex_unlock(timer_mutex);

    return;
}

static void margo_timer_queue(margo_timer_t *timer)
{
    margo_timer_t *cur;

    ABT_mutex_lock(timer_mutex);

    /* if list of timers is empty, put ourselves on it */
    if(!timer_head)
    {
        DL_APPEND(timer_head, timer);
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
            if(cur->expiration < timer->expiration)
            {
                DL_APPEND_ELEM(timer_head, cur, timer);
                break;
            }
        }while(cur != timer_head);

        /* if we never found one with an expiration before this one, then
         * this one is the new head
         */
        if(timer->prev == NULL && timer->next == NULL)
            DL_PREPEND(timer_head, timer);
    }
    ABT_mutex_unlock(timer_mutex);

    return;
}

