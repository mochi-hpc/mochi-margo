
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
#include "margo-timer.h"
#include "utlist.h"

#define MERCURY_PROGRESS_TIMEOUT_UB 100 /* 100 milliseconds */

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
static int margo_xstream_is_in_progress_pool(margo_instance_id mid);

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

    mid->progress_pool = progress_pool;
    mid->handler_pool = handler_pool;
    mid->hg_class = hg_class;
    mid->hg_context = hg_context;

    ret = margo_timer_instance_init(mid);
    if(ret != 0)
    {
        fprintf(stderr, "Error: margo_timer_instance_init()\n");
        free(mid);
        return(MARGO_INSTANCE_NULL);
    }

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
    margo_timer_instance_finalize(mid);

    ABT_mutex_free(&mid->finalize_mutex);
    ABT_cond_free(&mid->finalize_cond);
    free(mid);
#endif

    return;
}

void margo_wait_for_finalize(margo_instance_id mid)
{
    int in_pool = 0;

    /* Is this waiter in the same pool as the pool running the progress
     * thread?
     */
    if(margo_xstream_is_in_progress_pool(mid))
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
    unsigned int hg_progress_timeout = MERCURY_PROGRESS_TIMEOUT_UB;
    double next_timer_exp;

    while(!mid->hg_progress_shutdown_flag)
    {
        do {
            ret = HG_Trigger(mid->hg_context, 0, 1, &actual_count);
        } while((ret == HG_SUCCESS) && actual_count && !mid->hg_progress_shutdown_flag);

        if(!mid->hg_progress_shutdown_flag)
        {
            ABT_mutex_lock(mid->finalize_mutex);

            ABT_pool_get_total_size(mid->progress_pool, &size);
            /* Are there any other threads executing in this pool that are *not*
             * blocked on margo_wait_for_finalize()?  If so then, we can't
             * sleep here or else those threads will not get a chance to
             * execute.
             */
            if(size > mid->finalize_waiters_in_progress_pool)
            {
                ABT_mutex_unlock(mid->finalize_mutex);
                HG_Progress(mid->hg_context, 0);
                ABT_thread_yield();
            }
            else
            {
                ABT_mutex_unlock(mid->finalize_mutex);

                ret = margo_timer_get_next_expiration(mid, &next_timer_exp);
                if(ret == 0)
                {
                    /* there is a queued timer, don't block long enough
                     * to keep this timer waiting
                     */
                    if(next_timer_exp >= 0.0)
                    {
                        next_timer_exp *= 1000; /* convert to milliseconds */
                        if(next_timer_exp < MERCURY_PROGRESS_TIMEOUT_UB)
                            hg_progress_timeout = (unsigned int)next_timer_exp;
                    }
                    else
                    {
                        hg_progress_timeout = 0;
                    }
                }
                HG_Progress(mid->hg_context, hg_progress_timeout);
            }
        }

        /* check for any expired timers */
        margo_check_timers(mid);
    }

    return;
}

ABT_pool* margo_get_handler_pool(margo_instance_id mid)
{
    return(&mid->handler_pool);
}

hg_context_t* margo_get_context(margo_instance_id mid)
{
    return(mid->hg_context);
}

hg_class_t* margo_get_class(margo_instance_id mid)
{
    return(mid->hg_class);
}


static hg_return_t margo_cb(const struct hg_cb_info *info)
{
    hg_return_t hret = info->ret;

    ABT_eventual *eventual = info->arg;
    /* propagate return code out through eventual */
    ABT_eventual_set(*eventual, &hret, sizeof(hret));
    
    return(HG_SUCCESS);
}

typedef struct
{
    hg_handle_t handle;
} margo_forward_timeout_cb_dat;

static void margo_forward_timeout_cb(void *arg)
{
    margo_forward_timeout_cb_dat *timeout_cb_dat =
        (margo_forward_timeout_cb_dat *)arg;

    /* cancel the Mercury op if the forward timed out */
    HG_Cancel(timeout_cb_dat->handle);
    return;
}

hg_return_t margo_forward_timed(
    margo_instance_id mid,
    hg_handle_t handle,
    void *in_struct,
    double timeout_ms)
{
    int ret;
    hg_return_t hret;
    ABT_eventual eventual;
    hg_return_t* waited_hret;
    margo_timer_t forward_timer;
    margo_forward_timeout_cb_dat timeout_cb_dat;

    ret = ABT_eventual_create(sizeof(hret), &eventual);
    if(ret != 0)
    {
        return(HG_NOMEM_ERROR);        
    }

    /* set a timer object to expire when this forward times out */
    timeout_cb_dat.handle = handle;
    margo_timer_init(mid, &forward_timer, margo_forward_timeout_cb,
        &timeout_cb_dat, timeout_ms);

    hret = HG_Forward(handle, margo_cb, &eventual, in_struct);
    if(hret == 0)
    {
        ABT_eventual_wait(eventual, (void**)&waited_hret);
        hret = *waited_hret;
    }

    /* convert HG_CANCELED to HG_TIMEOUT to indicate op timed out */
    if(hret == HG_CANCELED)
        hret = HG_TIMEOUT;

    /* remove timer if it is still in place (i.e., not timed out) */
    if(hret != HG_TIMEOUT)
        margo_timer_destroy(mid, &forward_timer);

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


static hg_return_t margo_bulk_transfer_cb(const struct hg_cb_info *info)
{
    hg_return_t hret = info->ret;
    ABT_eventual *eventual = info->arg;

    /* propagate return code out through eventual */
    ABT_eventual_set(*eventual, &hret, sizeof(hret));
    
    return(HG_SUCCESS);
}

struct lookup_cb_evt
{
    hg_return_t nret;
    hg_addr_t addr;
};

static hg_return_t margo_addr_lookup_cb(const struct hg_cb_info *info)
{
    struct lookup_cb_evt evt;
    evt.nret = info->ret;
    evt.addr = info->info.lookup.addr;

    ABT_eventual *eventual = info->arg;

    /* propagate return code out through eventual */
    ABT_eventual_set(*eventual, &evt, sizeof(evt));
    
    return(HG_SUCCESS);
}


hg_return_t margo_addr_lookup(
    margo_instance_id mid,
    hg_context_t *context,
    const char   *name,
    hg_addr_t    *addr)
{
    hg_return_t nret;
    struct lookup_cb_evt *evt;
    ABT_eventual eventual;
    int ret;

    ret = ABT_eventual_create(sizeof(*evt), &eventual);
    if(ret != 0)
    {
        return(HG_NOMEM_ERROR);        
    }

    nret = HG_Addr_lookup(context, margo_addr_lookup_cb,
        &eventual, name, HG_OP_ID_IGNORE);
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
    hg_addr_t origin_addr,
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

typedef struct
{
    margo_instance_id mid;
    ABT_mutex mutex;
    ABT_cond cond;
    char is_asleep;
    char in_pool;
} margo_thread_sleep_cb_dat;

static void margo_thread_sleep_cb(void *arg)
{
    margo_thread_sleep_cb_dat *sleep_cb_dat =
        (margo_thread_sleep_cb_dat *)arg;

    /* decrement number of waiting threads */
    ABT_mutex_lock(sleep_cb_dat->mid->finalize_mutex);
    sleep_cb_dat->mid->finalize_waiters_in_progress_pool -=
        sleep_cb_dat->in_pool;
    ABT_mutex_unlock(sleep_cb_dat->mid->finalize_mutex);

    /* wake up the sleeping thread */
    ABT_mutex_lock(sleep_cb_dat->mutex);
    sleep_cb_dat->is_asleep = 0;
    ABT_cond_signal(sleep_cb_dat->cond);
    ABT_mutex_unlock(sleep_cb_dat->mutex);

    return;
}

void margo_thread_sleep(
    margo_instance_id mid,
    double timeout_ms)
{
    int in_pool = 0;
    margo_timer_t sleep_timer;
    margo_thread_sleep_cb_dat sleep_cb_dat;

    if(margo_xstream_is_in_progress_pool(mid))
        in_pool = 1;

    /* set data needed for sleep callback */
    sleep_cb_dat.mid = mid;
    ABT_mutex_create(&(sleep_cb_dat.mutex));
    ABT_cond_create(&(sleep_cb_dat.cond));
    sleep_cb_dat.is_asleep = 1;
    sleep_cb_dat.in_pool = in_pool;

    /* initialize the sleep timer */
    margo_timer_init(mid, &sleep_timer, margo_thread_sleep_cb,
        &sleep_cb_dat, timeout_ms);

    /* increment number of waiting threads */
    ABT_mutex_lock(mid->finalize_mutex);
    mid->finalize_waiters_in_progress_pool += in_pool;
    ABT_mutex_unlock(mid->finalize_mutex);

    /* yield thread for specified timeout */
    ABT_mutex_lock(sleep_cb_dat.mutex);
    while(sleep_cb_dat.is_asleep)
        ABT_cond_wait(sleep_cb_dat.cond, sleep_cb_dat.mutex);
    ABT_mutex_unlock(sleep_cb_dat.mutex);

    return;
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

/* returns 1 if current xstream is in the progress pool, 0 if not */
static int margo_xstream_is_in_progress_pool(margo_instance_id mid)
{
    int ret;
    ABT_xstream xstream;
    ABT_pool pool;

    ret = ABT_xstream_self(&xstream);
    assert(ret == ABT_SUCCESS);
    ret = ABT_xstream_get_main_pools(xstream, 1, &pool);
    assert(ret == ABT_SUCCESS);

    if(pool == mid->progress_pool)
        return(1);
    else
        return(0);
}
