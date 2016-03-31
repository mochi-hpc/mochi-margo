
/*
 * (C) 2015 The University of Chicago
 * 
 * See COPYRIGHT in top-level directory.
 */

#include <assert.h>
#include <unistd.h>
#include <errno.h>
#include <abt.h>
#include <abt-snoozer.h>
#include <time.h>
#include <math.h>

#include "margo.h"
#include "utlist.h"

/* TODO: including core.h for cancel definition, presumably this will be 
 * available in top level later?
 */
#include <mercury_core.h>

struct timed_element
{
    struct timespec expiration;
    hg_handle_t handle;
    struct timed_element *next;
    struct timed_element *prev;
};

#ifdef CLOCK_REALTIME_COARSE
clockid_t clk_id = CLOCK_REALTIME_COARSE;
#else
clockid_t clk_id = CLOCK_REALTIME;
#endif

struct margo_instance
{
    /* provided by caller */
    hg_context_t *hg_context;
    hg_class_t *hg_class;
    ABT_pool handler_pool;
    ABT_pool progress_pool;

    /* internal to margo for this particular instance */
    ABT_thread hg_progress_tid;
    int hg_progress_shutdown_flag;

    /* control logic for callers waiting on margo to be finalized */
    int finalize_flag;
    int finalize_waiters_in_progress_pool;
    ABT_mutex finalize_mutex;
    ABT_cond finalize_cond;

    /* pending operations with timeouts */
    ABT_mutex timer_mutex;
    struct timed_element *timer_head;

    int table_index;
};

struct margo_handler_mapping
{
    hg_class_t *class;
    margo_instance_id mid;
};

#define MAX_HANDLER_MAPPING 8
static int handler_mapping_table_size = 0;
static struct margo_handler_mapping handler_mapping_table[MAX_HANDLER_MAPPING] = {0};

static void hg_progress_fn(void* foo);


struct handler_entry
{
    void* fn;
    hg_handle_t handle;
    struct handler_entry *next; 
};

margo_instance_id margo_init(ABT_pool progress_pool, ABT_pool handler_pool,
    hg_context_t *hg_context, hg_class_t *hg_class)
{
    int ret;
    struct margo_instance *mid;

    if(handler_mapping_table_size >= MAX_HANDLER_MAPPING)
        return(MARGO_INSTANCE_NULL);

    mid = malloc(sizeof(*mid));
    if(!mid)
        return(MARGO_INSTANCE_NULL);
    memset(mid, 0, sizeof(*mid));

    ABT_mutex_create(&mid->finalize_mutex);
    ABT_cond_create(&mid->finalize_cond);

    ABT_mutex_create(&mid->timer_mutex);

    mid->progress_pool = progress_pool;
    mid->handler_pool = handler_pool;
    mid->hg_class = hg_class;
    mid->hg_context = hg_context;

    ret = ABT_thread_create(mid->progress_pool, hg_progress_fn, mid, 
        ABT_THREAD_ATTR_NULL, &mid->hg_progress_tid);
    if(ret != 0)
    {
        fprintf(stderr, "Error: ABT_thread_create()\n");
        free(mid);
        return(MARGO_INSTANCE_NULL);
    }

    handler_mapping_table[handler_mapping_table_size].mid = mid;
    handler_mapping_table[handler_mapping_table_size].class = mid->hg_class;
    mid->table_index = handler_mapping_table_size;
    handler_mapping_table_size++;

    return mid;
}

void margo_finalize(margo_instance_id mid)
{
    int i;

    /* tell progress thread to wrap things up */
    mid->hg_progress_shutdown_flag = 1;

    /* wait for it to shutdown cleanly */
    ABT_thread_join(mid->hg_progress_tid);
    ABT_thread_free(&mid->hg_progress_tid);

    for(i=mid->table_index; i<(handler_mapping_table_size-1); i++)
    {
        handler_mapping_table[i] = handler_mapping_table[i+1];
    }
    handler_mapping_table_size--;

    ABT_mutex_lock(mid->finalize_mutex);
    mid->finalize_flag = 1;
    ABT_cond_broadcast(mid->finalize_cond);
    ABT_mutex_unlock(mid->finalize_mutex);

    /* TODO: yuck, there is a race here if someone was really waiting for
     * finalize; we can't destroy the data structures out from under them.
     * We could fix this by reference counting so that the last caller
     * (whether a finalize() caller or wait_for_finalize() caller) knows it
     * is safe to turn off the lights on their way out.  For now we just leak 
     * a small amount of memory.
     */
#if 0
    ABT_mutex_free(&mid->finalize_mutex);
    ABT_cond_free(&mid->finalize_cond);
    ABT_mutex_free(&mid->timer_mutex);
    free(mid);
#endif

    return;
}

void margo_wait_for_finalize(margo_instance_id mid)
{
    ABT_xstream xstream;
    ABT_pool pool;
    int ret;
    int in_pool = 0;

    ret = ABT_xstream_self(&xstream);
    if(ret != 0)
        return;
    ret = ABT_xstream_get_main_pools(xstream, 1, &pool);
    if(ret != 0)
        return;

    /* Is this waiter in the same pool as the pool running the progress
     * thread?
     */
    if(pool == mid->progress_pool)
        in_pool = 1;

    ABT_mutex_lock(mid->finalize_mutex);

        mid->finalize_waiters_in_progress_pool += in_pool;
            
        while(!mid->finalize_flag)
            ABT_cond_wait(mid->finalize_cond, mid->finalize_mutex);

    ABT_mutex_unlock(mid->finalize_mutex);
    
    return;
}

/* dedicated thread function to drive Mercury progress */
static void hg_progress_fn(void* foo)
{
    int ret;
    unsigned int actual_count;
    struct margo_instance *mid = (struct margo_instance *)foo;
    size_t size;

    while(!mid->hg_progress_shutdown_flag)
    {
        do {
            ret = HG_Trigger(mid->hg_context, 0, 1, &actual_count);
        } while((ret == HG_SUCCESS) && actual_count && !mid->hg_progress_shutdown_flag);

        if(!mid->hg_progress_shutdown_flag)
        {
            ABT_pool_get_total_size(mid->progress_pool, &size);
            /* Are there any other threads executing in this pool that are *not*
             * blocked on margo_wait_for_finalize()?  If so then, we can't
             * sleep here or else those threads will not get a chance to
             * execute.
             */
            if(size > mid->finalize_waiters_in_progress_pool)
            {
                HG_Progress(mid->hg_context, 0);
                ABT_thread_yield();
            }
            else
            {
                HG_Progress(mid->hg_context, 100);
            }
        }

        /* TODO: check for timeouts here.  If timer_head not null, then check
         * current time and compare against first element.  Keep walking list
         * cancelling operations until we find non-expired element.
         */
    }

    return;
}

ABT_pool* margo_get_handler_pool(margo_instance_id mid)
{
    return(&mid->handler_pool);
}

static hg_return_t margo_cb(const struct hg_cb_info *info)
{
    hg_return_t hret = info->ret;

    ABT_eventual *eventual = info->arg;
    /* propagate return code out through eventual */
    ABT_eventual_set(*eventual, &hret, sizeof(hret));
    
    return(HG_SUCCESS);
}

hg_return_t margo_forward_timed(
    margo_instance_id mid,
    hg_handle_t handle,
    void *in_struct,
    double timeout_ms)
{
    hg_return_t hret = HG_TIMEOUT;
    ABT_eventual eventual;
    int ret;
    hg_return_t* waited_hret;
    struct timed_element el;
    struct timed_element *cur;

    /* calculate expiration time */
    el.handle = handle;
    el.prev = NULL;
    el.next = NULL;
    clock_gettime(clk_id, &el.expiration);
    el.expiration.tv_sec += timeout_ms/1000;
    el.expiration.tv_nsec += fmod(timeout_ms, 1000)*1000.0*1000.0;
    if(el.expiration.tv_nsec > 1000000000)
    {
        el.expiration.tv_nsec -= 1000000000;
        el.expiration.tv_sec++;
    }

    ret = ABT_eventual_create(sizeof(hret), &eventual);
    if(ret != 0)
    {
        return(HG_NOMEM_ERROR);        
    }

    /* TODO: split this out into a subroutine */
    /* track timer */
    ABT_mutex_lock(mid->timer_mutex);

    /* if queue of expiring ops is empty, put ourselves on it */
    if(!mid->timer_head)
        DL_APPEND(mid->timer_head, &el);
    else
    {
        /* something else already in queue, keep it sorted in ascending order
         * of expiration time
         */
        cur = mid->timer_head;
        do
        {
            /* walk backwards through queue */
            cur = cur->prev;
            /* as soon as we find an element that expires before this one, 
             * then we add ours after it
             */
            if(cur->expiration.tv_sec < el.expiration.tv_sec ||
                (cur->expiration.tv_sec == el.expiration.tv_sec &&
                 cur->expiration.tv_nsec < el.expiration.tv_nsec))
            {
                DL_APPEND_ELEM(mid->timer_head, cur, &el);
                break;
            }
        }while(cur != mid->timer_head);

        /* if we never found one with an expiration before this one, then
         * this one is the new head
         */
        if(el.prev == NULL && el.next == NULL)
            DL_PREPEND(mid->timer_head, &el);
    }
    ABT_mutex_unlock(mid->timer_mutex);

    hret = HG_Forward(handle, margo_cb, &eventual, in_struct);
    if(hret == 0)
    {
        ABT_eventual_wait(eventual, (void**)&waited_hret);
        hret = *waited_hret;
    }

    /* remove timer */
    ABT_mutex_lock(mid->timer_mutex);
    DL_DELETE(mid->timer_head, &el);
    ABT_mutex_unlock(mid->timer_mutex);

    ABT_eventual_free(&eventual);

    return(hret);

}


hg_return_t margo_forward(
    margo_instance_id mid,
    hg_handle_t handle,
    void *in_struct)
{
    hg_return_t hret = HG_TIMEOUT;
    ABT_eventual eventual;
    int ret;
    hg_return_t* waited_hret;

    ret = ABT_eventual_create(sizeof(hret), &eventual);
    if(ret != 0)
    {
        return(HG_NOMEM_ERROR);        
    }

    hret = HG_Forward(handle, margo_cb, &eventual, in_struct);
    if(hret == 0)
    {
        ABT_eventual_wait(eventual, (void**)&waited_hret);
        hret = *waited_hret;
    }

    ABT_eventual_free(&eventual);

    return(hret);
}

hg_return_t margo_respond(
    margo_instance_id mid,
    hg_handle_t handle,
    void *out_struct)
{
    hg_return_t hret = HG_TIMEOUT;
    ABT_eventual eventual;
    int ret;
    hg_return_t* waited_hret;

    ret = ABT_eventual_create(sizeof(hret), &eventual);
    if(ret != 0)
    {
        return(HG_NOMEM_ERROR);
    }

    hret = HG_Respond(handle, margo_cb, &eventual, out_struct);
    if(hret == 0)
    {
        ABT_eventual_wait(eventual, (void**)&waited_hret);
        hret = *waited_hret;
    }

    ABT_eventual_free(&eventual);

    return(hret);
}


static hg_return_t margo_bulk_transfer_cb(const struct hg_bulk_cb_info *hg_bulk_cb_info)
{
    hg_return_t hret = hg_bulk_cb_info->ret;
    ABT_eventual *eventual = hg_bulk_cb_info->arg;

    /* propagate return code out through eventual */
    ABT_eventual_set(*eventual, &hret, sizeof(hret));
    
    return(HG_SUCCESS);
}

struct lookup_cb_evt
{
    na_return_t nret;
    na_addr_t addr;
};

static na_return_t margo_na_addr_lookup_cb(const struct na_cb_info *callback_info)
{
    struct lookup_cb_evt evt;
    evt.nret = callback_info->ret;
    evt.addr = callback_info->info.lookup.addr;

    ABT_eventual *eventual = callback_info->arg;

    /* propagate return code out through eventual */
    ABT_eventual_set(*eventual, &evt, sizeof(evt));
    
    return(NA_SUCCESS);
}


na_return_t margo_na_addr_lookup(
    margo_instance_id mid,
    na_class_t   *na_class,
    na_context_t *context,
    const char   *name,
    na_addr_t    *addr)
{
    na_return_t nret;
    struct lookup_cb_evt *evt;
    ABT_eventual eventual;
    int ret;

    ret = ABT_eventual_create(sizeof(*evt), &eventual);
    if(ret != 0)
    {
        return(HG_NOMEM_ERROR);        
    }

    nret = NA_Addr_lookup(na_class, context, margo_na_addr_lookup_cb,
        &eventual, name, NA_OP_ID_IGNORE);
    if(nret == 0)
    {
        ABT_eventual_wait(eventual, (void**)&evt);
        *addr = evt->addr;
        nret = evt->nret;
    }

    ABT_eventual_free(&eventual);

    return(nret);
}

hg_return_t margo_bulk_transfer(
    margo_instance_id mid,
    hg_context_t *context,
    hg_bulk_op_t op,
    na_addr_t origin_addr,
    hg_bulk_t origin_handle,
    size_t origin_offset,
    hg_bulk_t local_handle,
    size_t local_offset,
    size_t size)
{
    hg_return_t hret = HG_TIMEOUT;
    hg_return_t *waited_hret;
    ABT_eventual eventual;
    int ret;

    ret = ABT_eventual_create(sizeof(hret), &eventual);
    if(ret != 0)
    {
        return(HG_NOMEM_ERROR);        
    }

    hret = HG_Bulk_transfer(context, margo_bulk_transfer_cb, &eventual, op, 
        origin_addr, origin_handle, origin_offset, local_handle, local_offset,
        size, HG_OP_ID_IGNORE);
    if(hret == 0)
    {
        ABT_eventual_wait(eventual, (void**)&waited_hret);
        hret = *waited_hret;
    }

    ABT_eventual_free(&eventual);

    return(hret);
}

margo_instance_id margo_hg_class_to_instance(hg_class_t *cl)
{
    int i;

    for(i=0; i<handler_mapping_table_size; i++)
    {
        if(handler_mapping_table[i].class == cl)
            return(handler_mapping_table[i].mid);
    }
    return(NULL);
}
