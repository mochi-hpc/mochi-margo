
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
struct margo_timer_instance
{
    margo_instance_id mid;
    ABT_mutex mutex;
    margo_timer_t *queue_head;
};

#define MAX_TIMER_INSTANCES 8
static int timer_inst_table_size = 0;
static struct margo_timer_instance *timer_inst_table[MAX_TIMER_INSTANCES] = {NULL};

static struct margo_timer_instance *margo_get_timer_instance(
    margo_instance_id mid);
static void margo_timer_queue(
    struct margo_timer_instance *timer_inst,
    margo_timer_t *timer);


int margo_timer_instance_init(
    margo_instance_id mid)
{
    struct margo_timer_instance *timer_inst;

    if(timer_inst_table_size >= MAX_TIMER_INSTANCES)
        return(-1);

    timer_inst = malloc(sizeof(timer_inst));
    if(!timer_inst)
        return(-1);

    timer_inst->mid = mid;
    ABT_mutex_create(&(timer_inst->mutex));
    timer_inst->queue_head = NULL;

    /* add this instance to the table of active timer instances */
    timer_inst_table[timer_inst_table_size++] = timer_inst;

    return(0);
}

void margo_timer_instance_finalize(
    margo_instance_id mid)
{
    struct margo_timer_instance *timer_inst;
    margo_timer_t *cur;
    int i = 0;

    timer_inst = margo_get_timer_instance(mid);
    if(!timer_inst)
        return;

    ABT_mutex_lock(timer_inst->mutex);
    /* delete any remaining timers from the queue */
    while(timer_inst->queue_head)
    {
        cur = timer_inst->queue_head;
        DL_DELETE(timer_inst->queue_head, cur);
    }
    ABT_mutex_unlock(timer_inst->mutex);
    ABT_mutex_free(&(timer_inst->mutex));

    /* remove this timer instance from the active table */
    while(timer_inst_table[i] != timer_inst)
        i++;
    while(i < timer_inst_table_size - 1)
    {
        timer_inst_table[i] = timer_inst_table[i+1];
        i++;
    }
    timer_inst_table[i] = NULL;
    timer_inst_table_size--;
    free(timer_inst);

    return;
}

void margo_timer_init(
    margo_instance_id mid,
    margo_timer_t *timer,
    margo_timer_cb_fn cb_fn,
    void *cb_dat,
    double timeout_ms)
{
    struct margo_timer_instance *timer_inst;

    timer_inst = margo_get_timer_instance(mid);
    assert(timer_inst);
    assert(timer);

    memset(timer, 0, sizeof(*timer));
    timer->cb_fn = cb_fn;
    timer->cb_dat = cb_dat;
    timer->expiration = ABT_get_wtime() + (timeout_ms/1000);
    timer->prev = timer->next = NULL;

    margo_timer_queue(timer_inst, timer);

    return;
}

void margo_timer_destroy(
    margo_instance_id mid,
    margo_timer_t *timer)
{
    struct margo_timer_instance *timer_inst;

    timer_inst = margo_get_timer_instance(mid);
    assert(timer_inst);
    assert(timer);

    ABT_mutex_lock(timer_inst->mutex);
    if(timer->prev || timer->next)
        DL_DELETE(timer_inst->queue_head, timer);
    ABT_mutex_unlock(timer_inst->mutex);

    return;
}

void margo_check_timers(
    margo_instance_id mid)
{
    margo_timer_t *cur;
    struct margo_timer_instance *timer_inst;
    double now = ABT_get_wtime();

    timer_inst = margo_get_timer_instance(mid);
    assert(timer_inst);

    ABT_mutex_lock(timer_inst->mutex);

    /* iterate through timer list, performing timeout action
     * for all elements which have passed expiration time
     */
    while(timer_inst->queue_head && (timer_inst->queue_head->expiration < now))
    {
        cur = timer_inst->queue_head;
        DL_DELETE(timer_inst->queue_head, cur);
        cur->prev = cur->next = NULL;

        /* execute callback */
        cur->cb_fn(cur->cb_dat);
    }
    ABT_mutex_unlock(timer_inst->mutex);

    return;
}

static struct margo_timer_instance *margo_get_timer_instance(
    margo_instance_id mid)
{
    struct margo_timer_instance *timer_inst = NULL;
    int i = 0;

    /* find the timer instance using the given margo id */
    while(timer_inst_table[i] != NULL)
    {
        if(timer_inst_table[i]->mid == mid)
        {
            timer_inst = timer_inst_table[i];
            break;
        }
        i++;
    }

    if(timer_inst)
        assert(timer_inst->mutex != ABT_MUTEX_NULL);

    return(timer_inst);
}

static void margo_timer_queue(
    struct margo_timer_instance *timer_inst,
    margo_timer_t *timer)
{
    margo_timer_t *cur;

    ABT_mutex_lock(timer_inst->mutex);

    /* if list of timers is empty, put ourselves on it */
    if(!(timer_inst->queue_head))
    {
        DL_APPEND(timer_inst->queue_head, timer);
    }
    else
    {
        /* something else already in queue, keep it sorted in ascending order
         * of expiration time
         */
        cur = timer_inst->queue_head;
        do
        {
            /* walk backwards through queue */
            cur = cur->prev;
            /* as soon as we find an element that expires before this one, 
             * then we add ours after it
             */
            if(cur->expiration < timer->expiration)
            {
                DL_APPEND_ELEM(timer_inst->queue_head, cur, timer);
                break;
            }
        }while(cur != timer_inst->queue_head);

        /* if we never found one with an expiration before this one, then
         * this one is the new head
         */
        if(timer->prev == NULL && timer->next == NULL)
            DL_PREPEND(timer_inst->queue_head, timer);
    }
    ABT_mutex_unlock(timer_inst->mutex);

    return;
}
