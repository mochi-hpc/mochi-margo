
/*
 * (C) 2015 The University of Chicago
 * 
 * See COPYRIGHT in top-level directory.
 */

#include <assert.h>
#include <unistd.h>
#include <errno.h>
#include <abt.h>

#include <margo-config.h>
#ifdef HAVE_ABT_SNOOZER
#include <abt-snoozer.h>
#endif
#include <time.h>
#include <math.h>

#include "margo.h"
#include "margo-timer.h"
#include "utlist.h"
#include "uthash.h"

#define DEFAULT_MERCURY_PROGRESS_TIMEOUT_UB 100 /* 100 milliseconds */
#define DEFAULT_MERCURY_HANDLE_CACHE_SIZE 32

struct mplex_key
{
    hg_id_t id;
    uint32_t mplex_id;
};

struct mplex_element
{
    struct mplex_key key;
    ABT_pool pool;
    UT_hash_handle hh;
};

struct diag_data
{
    double min;
    double max;
    double cumulative;
    int count;
};

#define __DIAG_UPDATE(__data, __time)\
do {\
    __data.count++; \
    __data.cumulative += (__time); \
    if((__time) > __data.max) __data.max = (__time); \
    if((__time) < __data.min) __data.min = (__time); \
} while(0)

struct margo_handle_cache_el
{
    hg_handle_t handle;
    UT_hash_handle hh; /* in-use hash link */
    struct margo_handle_cache_el *next; /* free list link */
};

struct margo_instance
{
    /* mercury/argobots state */
    hg_context_t *hg_context;
    hg_class_t *hg_class;
    ABT_pool handler_pool;
    ABT_pool progress_pool;

    /* internal to margo for this particular instance */
    int margo_init;
    ABT_thread hg_progress_tid;
    int hg_progress_shutdown_flag;
    ABT_xstream progress_xstream;
    int owns_progress_pool;
    ABT_xstream *rpc_xstreams;
    int num_handler_pool_threads;
    unsigned int hg_progress_timeout_ub;

    /* control logic for callers waiting on margo to be finalized */
    int finalize_flag;
    int refcount;
    ABT_mutex finalize_mutex;
    ABT_cond finalize_cond;

    /* hash table to track multiplexed rpcs registered with margo */
    struct mplex_element *mplex_table;

    /* linked list of free hg handles and a hash of in-use handles */
    struct margo_handle_cache_el *free_handle_list;
    struct margo_handle_cache_el *used_handle_hash;

    /* optional diagnostics data tracking */
    /* NOTE: technically the following fields are subject to races if they
     * are updated from more than one thread at a time.  We will be careful
     * to only update the counters from the progress_fn,
     * which will serialize access.
     */
    int diag_enabled;
    struct diag_data diag_trigger_elapsed;
    struct diag_data diag_progress_elapsed_zero_timeout;
    struct diag_data diag_progress_elapsed_nonzero_timeout;
    struct diag_data diag_progress_timeout_value;
};

struct margo_cb_arg
{
    ABT_eventual *eventual;
    margo_instance_id mid;
};

struct margo_rpc_data
{
	margo_instance_id mid;
	void* user_data;
	void (*user_free_callback)(void *);
};

static void hg_progress_fn(void* foo);
static void margo_rpc_data_free(void* ptr);

static hg_return_t margo_handle_cache_init(margo_instance_id mid);
static void margo_handle_cache_destroy(margo_instance_id mid);
static hg_return_t margo_handle_cache_get(margo_instance_id mid,
    hg_addr_t addr, hg_id_t id, hg_handle_t *handle);
static hg_return_t margo_handle_cache_put(margo_instance_id mid,
    hg_handle_t handle);

margo_instance_id margo_init(const char *addr_str, int mode,
    int use_progress_thread, int rpc_thread_count)
{
    ABT_xstream progress_xstream = ABT_XSTREAM_NULL;
    ABT_pool progress_pool = ABT_POOL_NULL;
    ABT_xstream *rpc_xstreams = NULL;
    ABT_xstream rpc_xstream = ABT_XSTREAM_NULL;
    ABT_pool rpc_pool = ABT_POOL_NULL;
    hg_class_t *hg_class = NULL;
    hg_context_t *hg_context = NULL;
    int listen_flag = (mode == MARGO_CLIENT_MODE) ? HG_FALSE : HG_TRUE;
    int i;
    int ret;
    struct margo_instance *mid = MARGO_INSTANCE_NULL;

    if(mode != MARGO_CLIENT_MODE && mode != MARGO_SERVER_MODE) goto err;

    ret = ABT_init(0, NULL); /* XXX: argc/argv not currently used by ABT ... */
    if(ret != 0) goto err;

    /* set caller (self) ES to idle without polling */
#ifdef HAVE_ABT_SNOOZER
    ret = ABT_snoozer_xstream_self_set();
    if(ret != 0) goto err;
#endif

    if (use_progress_thread)
    {
#ifdef HAVE_ABT_SNOOZER
        ret = ABT_snoozer_xstream_create(1, &progress_pool, &progress_xstream);
		if (ret != ABT_SUCCESS) goto err;
#else
		ret = ABT_xstream_create(ABT_SCHED_NULL, &progress_xstream);
		if (ret != ABT_SUCCESS) goto err;
		ret = ABT_xstream_get_main_pools(progress_xstream, 1, &progress_pool);
		if (ret != ABT_SUCCESS) goto err;
#endif
    }
    else
    {
        ret = ABT_xstream_self(&progress_xstream);
        if (ret != ABT_SUCCESS) goto err;
        ret = ABT_xstream_get_main_pools(progress_xstream, 1, &progress_pool);
        if (ret != ABT_SUCCESS) goto err;
    }

    if (mode == MARGO_SERVER_MODE)
    {
        if (rpc_thread_count > 0)
        {
            rpc_xstreams = calloc(rpc_thread_count, sizeof(*rpc_xstreams));
            if (rpc_xstreams == NULL) goto err;
#ifdef HAVE_ABT_SNOOZER
            ret = ABT_snoozer_xstream_create(rpc_thread_count, &rpc_pool,
                    rpc_xstreams);
            if (ret != ABT_SUCCESS) goto err;
#else
			int j;
			ret = ABT_pool_create_basic(ABT_POOL_FIFO, ABT_POOL_ACCESS_MPMC, ABT_TRUE, &rpc_pool);
			if (ret != ABT_SUCCESS) goto err;
			for(j=0; j<rpc_thread_count; j++) {
				ret = ABT_xstream_create(ABT_SCHED_NULL, rpc_xstreams+j);
				if (ret != ABT_SUCCESS) goto err;
			}
#endif
        }
        else if (rpc_thread_count == 0)
        {
            ret = ABT_xstream_self(&rpc_xstream);
            if (ret != ABT_SUCCESS) goto err;
            ret = ABT_xstream_get_main_pools(rpc_xstream, 1, &rpc_pool);
            if (ret != ABT_SUCCESS) goto err;
        }
        else
        {
            rpc_pool = progress_pool;
        }
    }

    hg_class = HG_Init(addr_str, listen_flag);
    if(!hg_class) goto err;

    hg_context = HG_Context_create(hg_class);
    if(!hg_context) goto err;

    mid = margo_init_pool(progress_pool, rpc_pool, hg_context);
    if (mid == MARGO_INSTANCE_NULL) goto err;

    mid->margo_init = 1;
    mid->owns_progress_pool = use_progress_thread;
    mid->progress_xstream = progress_xstream;
    mid->num_handler_pool_threads = rpc_thread_count < 0 ? 0 : rpc_thread_count;
    mid->rpc_xstreams = rpc_xstreams;

    return mid;

err:
    if(mid)
    {
        margo_timer_instance_finalize(mid);
        ABT_mutex_free(&mid->finalize_mutex);
        ABT_cond_free(&mid->finalize_cond);
        free(mid);
    }
    if (use_progress_thread && progress_xstream != ABT_XSTREAM_NULL)
    {
        ABT_xstream_join(progress_xstream);
        ABT_xstream_free(&progress_xstream);
    }
    if (rpc_thread_count > 0 && rpc_xstreams != NULL)
    {
        for (i = 0; i < rpc_thread_count; i++)
        {
            ABT_xstream_join(rpc_xstreams[i]);
            ABT_xstream_free(&rpc_xstreams[i]);
        }
        free(rpc_xstreams);
    }
    if(hg_context)
        HG_Context_destroy(hg_context);
    if(hg_class)
        HG_Finalize(hg_class);
    ABT_finalize();
    return MARGO_INSTANCE_NULL;
}

margo_instance_id margo_init_pool(ABT_pool progress_pool, ABT_pool handler_pool,
    hg_context_t *hg_context)
{
    int ret;
    hg_return_t hret;
    struct margo_instance *mid;

    mid = malloc(sizeof(*mid));
    if(!mid) goto err;
    memset(mid, 0, sizeof(*mid));

    ABT_mutex_create(&mid->finalize_mutex);
    ABT_cond_create(&mid->finalize_cond);

    mid->progress_pool = progress_pool;
    mid->handler_pool = handler_pool;
    mid->hg_class = HG_Context_get_class(hg_context);
    mid->hg_context = hg_context;
    mid->hg_progress_timeout_ub = DEFAULT_MERCURY_PROGRESS_TIMEOUT_UB;
    mid->refcount = 1;

    ret = margo_timer_instance_init(mid);
    if(ret != 0) goto err;

    /* initialize the handle cache */
    hret = margo_handle_cache_init(mid);
    if(hret != HG_SUCCESS) goto err;

    ret = ABT_thread_create(mid->progress_pool, hg_progress_fn, mid, 
        ABT_THREAD_ATTR_NULL, &mid->hg_progress_tid);
    if(ret != 0) goto err;

    return mid;

err:
    if(mid)
    {
        margo_handle_cache_destroy(mid);
        margo_timer_instance_finalize(mid);
        ABT_mutex_free(&mid->finalize_mutex);
        ABT_cond_free(&mid->finalize_cond);
        free(mid);
    }
    return MARGO_INSTANCE_NULL;
}

static void margo_cleanup(margo_instance_id mid)
{
    int i;

    margo_timer_instance_finalize(mid);

    ABT_mutex_free(&mid->finalize_mutex);
    ABT_cond_free(&mid->finalize_cond);

    if (mid->owns_progress_pool)
    {
        ABT_xstream_join(mid->progress_xstream);
        ABT_xstream_free(&mid->progress_xstream);
    }

    if (mid->num_handler_pool_threads > 0)
    {
        for (i = 0; i < mid->num_handler_pool_threads; i++)
        {
            ABT_xstream_join(mid->rpc_xstreams[i]);
            ABT_xstream_free(&mid->rpc_xstreams[i]);
        }
        free(mid->rpc_xstreams);
    }

    margo_handle_cache_destroy(mid);

    if (mid->margo_init)
    {
        if (mid->hg_context)
            HG_Context_destroy(mid->hg_context);
        if (mid->hg_class)
            HG_Finalize(mid->hg_class);
        ABT_finalize();
    }

    free(mid);
}

void margo_finalize(margo_instance_id mid)
{
    int do_cleanup;

    /* tell progress thread to wrap things up */
    mid->hg_progress_shutdown_flag = 1;

    /* wait for it to shutdown cleanly */
    ABT_thread_join(mid->hg_progress_tid);
    ABT_thread_free(&mid->hg_progress_tid);

    ABT_mutex_lock(mid->finalize_mutex);
    mid->finalize_flag = 1;
    ABT_cond_broadcast(mid->finalize_cond);

    mid->refcount--;
    do_cleanup = mid->refcount == 0;

    ABT_mutex_unlock(mid->finalize_mutex);

    /* if there was noone waiting on the finalize at the time of the finalize
     * broadcast, then we're safe to clean up. Otherwise, let the finalizer do
     * it */
    if (do_cleanup)
        margo_cleanup(mid);

    return;
}

void margo_wait_for_finalize(margo_instance_id mid)
{
    int do_cleanup;

    ABT_mutex_lock(mid->finalize_mutex);

        mid->refcount++;
            
        while(!mid->finalize_flag)
            ABT_cond_wait(mid->finalize_cond, mid->finalize_mutex);

        mid->refcount--;
        do_cleanup = mid->refcount == 0;

    ABT_mutex_unlock(mid->finalize_mutex);

    if (do_cleanup)
        margo_cleanup(mid);

    return;
}

hg_id_t margo_register_name(margo_instance_id mid, const char *func_name,
    hg_proc_cb_t in_proc_cb, hg_proc_cb_t out_proc_cb, hg_rpc_cb_t rpc_cb)
{
	struct margo_rpc_data* margo_data;
    hg_return_t hret;
    hg_id_t id;

    id = HG_Register_name(mid->hg_class, func_name, in_proc_cb, out_proc_cb, rpc_cb);
    if(id <= 0)
        return(0);

	/* register the margo data with the RPC */
    margo_data = (struct margo_rpc_data*)HG_Registered_data(mid->hg_class, id);
    if(!margo_data)
    {
        margo_data = (struct margo_rpc_data*)malloc(sizeof(struct margo_rpc_data));
        if(!margo_data)
            return(0);
        margo_data->mid = mid;
        margo_data->user_data = NULL;
        margo_data->user_free_callback = NULL;
        hret = HG_Register_data(mid->hg_class, id, margo_data, margo_rpc_data_free);
        if(hret != HG_SUCCESS)
        {
            free(margo_data);
            return(0);
        }
    }

	return(id);
}

hg_id_t margo_register_name_mplex(margo_instance_id mid, const char *func_name,
    hg_proc_cb_t in_proc_cb, hg_proc_cb_t out_proc_cb, hg_rpc_cb_t rpc_cb,
    uint32_t mplex_id, ABT_pool pool)
{
    struct mplex_key key;
    struct mplex_element *element;
    hg_id_t id;

    id = margo_register_name(mid, func_name, in_proc_cb, out_proc_cb, rpc_cb);
    if(id <= 0)
        return(0);

    /* nothing to do, we'll let the handler pool take this directly */
    if(mplex_id == MARGO_DEFAULT_MPLEX_ID)
        return(id);

    memset(&key, 0, sizeof(key));
    key.id = id;
    key.mplex_id = mplex_id;

    HASH_FIND(hh, mid->mplex_table, &key, sizeof(key), element);
    if(element)
        return(id);

    element = malloc(sizeof(*element));
    if(!element)
        return(0);
    element->key = key;
    element->pool = pool;

    HASH_ADD(hh, mid->mplex_table, key, sizeof(key), element);

    return(id);
}

hg_return_t margo_registered_name(margo_instance_id mid, const char *func_name,
    hg_id_t *id, hg_bool_t *flag)
{
    return(HG_Registered_name(mid->hg_class, func_name, id, flag));
}

hg_return_t margo_register_data(
    margo_instance_id mid,
    hg_id_t id,
    void *data,
    void (*free_callback)(void *)) 
{
	struct margo_rpc_data* margo_data 
		= (struct margo_rpc_data*) HG_Registered_data(mid->hg_class, id);
	if(!margo_data) return HG_OTHER_ERROR;
	margo_data->user_data = data;
	margo_data->user_free_callback = free_callback;
	return HG_SUCCESS;
}

void* margo_registered_data(margo_instance_id mid, hg_id_t id)
{
	struct margo_rpc_data* data
		= (struct margo_rpc_data*) HG_Registered_data(margo_get_class(mid), id);
	if(!data) return NULL;
	else return data->user_data;
}

hg_return_t margo_registered_disable_response(
    margo_instance_id mid,
    hg_id_t id,
    int disable_flag)
{
    return(HG_Registered_disable_response(mid->hg_class, id, disable_flag));
}

struct lookup_cb_evt
{
    hg_return_t hret;
    hg_addr_t addr;
};

static hg_return_t margo_addr_lookup_cb(const struct hg_cb_info *info)
{
    struct lookup_cb_evt evt;
    evt.hret = info->ret;
    evt.addr = info->info.lookup.addr;
    struct margo_cb_arg* arg = info->arg;

    /* propagate return code out through eventual */
    ABT_eventual_set(*(arg->eventual), &evt, sizeof(evt));

    return(HG_SUCCESS);
}

hg_return_t margo_addr_lookup(
    margo_instance_id mid,
    const char   *name,
    hg_addr_t    *addr)
{
    hg_return_t hret;
    struct lookup_cb_evt *evt;
    ABT_eventual eventual;
    int ret;
    struct margo_cb_arg arg;

    ret = ABT_eventual_create(sizeof(*evt), &eventual);
    if(ret != 0)
    {
        return(HG_NOMEM_ERROR);        
    }

    arg.eventual = &eventual;
    arg.mid = mid;

    hret = HG_Addr_lookup(mid->hg_context, margo_addr_lookup_cb,
        &arg, name, HG_OP_ID_IGNORE);
    if(hret == HG_SUCCESS)
    {
        ABT_eventual_wait(eventual, (void**)&evt);
        *addr = evt->addr;
        hret = evt->hret;
    }

    ABT_eventual_free(&eventual);

    return(hret);
}

hg_return_t margo_addr_free(
    margo_instance_id mid,
    hg_addr_t addr)
{
    return(HG_Addr_free(mid->hg_class, addr));
}

hg_return_t margo_addr_self(
    margo_instance_id mid,
    hg_addr_t *addr)
{
    return(HG_Addr_self(mid->hg_class, addr));
}

hg_return_t margo_addr_dup(
    margo_instance_id mid,
    hg_addr_t addr,
    hg_addr_t *new_addr)
{
    return(HG_Addr_dup(mid->hg_class, addr, new_addr));
}

hg_return_t margo_addr_to_string(
    margo_instance_id mid,
    char *buf,
    hg_size_t *buf_size,
    hg_addr_t addr)
{
    return(HG_Addr_to_string(mid->hg_class, buf, buf_size, addr));
}

hg_return_t margo_create(margo_instance_id mid, hg_addr_t addr,
    hg_id_t id, hg_handle_t *handle)
{
    hg_return_t hret;

    /* look for a handle to reuse */
    hret = margo_handle_cache_get(mid, addr, id, handle);
    if(hret != HG_SUCCESS)
    {
        /* else try creating a new handle */
        hret = HG_Create(mid->hg_context, addr, id, handle);
    }

    return hret;
}

hg_return_t margo_destroy(margo_instance_id mid, hg_handle_t handle)
{
    hg_return_t hret;

    /* recycle this handle if it came from the handle cache */
    hret = margo_handle_cache_put(mid, handle);
    if(hret != HG_SUCCESS)
    {
        /* else destroy the handle manually */
        hret = HG_Destroy(handle);
    }

    return hret;
}

static hg_return_t margo_cb(const struct hg_cb_info *info)
{
    hg_return_t hret = info->ret;
    struct margo_cb_arg* arg = info->arg;

    /* propagate return code out through eventual */
    ABT_eventual_set(*(arg->eventual), &hret, sizeof(hret));
    
    return(HG_SUCCESS);
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
    struct margo_cb_arg arg;

    ret = ABT_eventual_create(sizeof(hret), &eventual);
    if(ret != 0)
    {
        return(HG_NOMEM_ERROR);        
    }

    arg.eventual = &eventual;
    arg.mid = mid;

    hret = HG_Forward(handle, margo_cb, &arg, in_struct);
    if(hret == HG_SUCCESS)
    {
        ABT_eventual_wait(eventual, (void**)&waited_hret);
        hret = *waited_hret;
    }

    ABT_eventual_free(&eventual);

    return(hret);
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
    struct margo_cb_arg arg;

    ret = ABT_eventual_create(sizeof(hret), &eventual);
    if(ret != 0)
    {
        return(HG_NOMEM_ERROR);        
    }

    /* set a timer object to expire when this forward times out */
    timeout_cb_dat.handle = handle;
    margo_timer_init(mid, &forward_timer, margo_forward_timeout_cb,
        &timeout_cb_dat, timeout_ms);

    arg.eventual = &eventual;
    arg.mid = mid;

    hret = HG_Forward(handle, margo_cb, &arg, in_struct);
    if(hret == HG_SUCCESS)
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

hg_return_t margo_respond(
    margo_instance_id mid,
    hg_handle_t handle,
    void *out_struct)
{
    hg_return_t hret = HG_TIMEOUT;
    ABT_eventual eventual;
    int ret;
    hg_return_t* waited_hret;
    struct margo_cb_arg arg;

    ret = ABT_eventual_create(sizeof(hret), &eventual);
    if(ret != 0)
    {
        return(HG_NOMEM_ERROR);
    }

    arg.eventual = &eventual;
    arg.mid = mid;

    hret = HG_Respond(handle, margo_cb, &arg, out_struct);
    if(hret == HG_SUCCESS)
    {
        ABT_eventual_wait(eventual, (void**)&waited_hret);
        hret = *waited_hret;
    }

    ABT_eventual_free(&eventual);

    return(hret);
}

hg_return_t margo_bulk_create(
    margo_instance_id mid,
    hg_uint32_t count,
    void **buf_ptrs,
    const hg_size_t *buf_sizes,
    hg_uint8_t flags,
    hg_bulk_t *handle)
{
    /* XXX: handle caching logic? */

    return(HG_Bulk_create(mid->hg_class, count,
        buf_ptrs, buf_sizes, flags, handle));
}

hg_return_t margo_bulk_free(
    hg_bulk_t handle)
{
    /* XXX: handle caching logic? */

    return(HG_Bulk_free(handle));
}

hg_return_t margo_bulk_deserialize(
    margo_instance_id mid,
    hg_bulk_t *handle,
    const void *buf,
    hg_size_t buf_size)
{
    return(HG_Bulk_deserialize(mid->hg_class, handle, buf, buf_size));
}

static hg_return_t margo_bulk_transfer_cb(const struct hg_cb_info *info)
{
    hg_return_t hret = info->ret;
    struct margo_cb_arg* arg = info->arg;

    /* propagate return code out through eventual */
    ABT_eventual_set(*(arg->eventual), &hret, sizeof(hret));
    
    return(HG_SUCCESS);
}

hg_return_t margo_bulk_transfer(
    margo_instance_id mid,
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
    struct margo_cb_arg arg;

    ret = ABT_eventual_create(sizeof(hret), &eventual);
    if(ret != 0)
    {
        return(HG_NOMEM_ERROR);        
    }

    arg.eventual = &eventual;
    arg.mid = mid;

    hret = HG_Bulk_transfer(mid->hg_context, margo_bulk_transfer_cb,
        &arg, op, origin_addr, origin_handle, origin_offset, local_handle,
        local_offset, size, HG_OP_ID_IGNORE);
    if(hret == HG_SUCCESS)
    {
        ABT_eventual_wait(eventual, (void**)&waited_hret);
        hret = *waited_hret;
    }

    ABT_eventual_free(&eventual);

    return(hret);
}

typedef struct
{
    ABT_mutex mutex;
    ABT_cond cond;
    char is_asleep;
} margo_thread_sleep_cb_dat;

static void margo_thread_sleep_cb(void *arg)
{
    margo_thread_sleep_cb_dat *sleep_cb_dat =
        (margo_thread_sleep_cb_dat *)arg;

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
    margo_timer_t sleep_timer;
    margo_thread_sleep_cb_dat sleep_cb_dat;

    /* set data needed for sleep callback */
    ABT_mutex_create(&(sleep_cb_dat.mutex));
    ABT_cond_create(&(sleep_cb_dat.cond));
    sleep_cb_dat.is_asleep = 1;

    /* initialize the sleep timer */
    margo_timer_init(mid, &sleep_timer, margo_thread_sleep_cb,
        &sleep_cb_dat, timeout_ms);

    /* yield thread for specified timeout */
    ABT_mutex_lock(sleep_cb_dat.mutex);
    while(sleep_cb_dat.is_asleep)
        ABT_cond_wait(sleep_cb_dat.cond, sleep_cb_dat.mutex);
    ABT_mutex_unlock(sleep_cb_dat.mutex);

    /* clean up */
    ABT_mutex_free(&sleep_cb_dat.mutex);
    ABT_cond_free(&sleep_cb_dat.cond);

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

margo_instance_id margo_hg_handle_get_instance(hg_handle_t h)
{
	const struct hg_info* info = HG_Get_info(h);
	if(!info) return MARGO_INSTANCE_NULL;
    return margo_hg_info_get_instance(info);
}

margo_instance_id margo_hg_info_get_instance(const struct hg_info *info)
{
	struct margo_rpc_data* data = 
		(struct margo_rpc_data*) HG_Registered_data(info->hg_class, info->id);
	if(!data) return MARGO_INSTANCE_NULL;
	return data->mid;
}

int margo_lookup_mplex(margo_instance_id mid, hg_id_t id, uint32_t mplex_id, ABT_pool *pool)
{
    struct mplex_key key;
    struct mplex_element *element;

    if(!mplex_id)
    {
        *pool = mid->handler_pool;
        return(0);
    }

    memset(&key, 0, sizeof(key));
    key.id = id;
    key.mplex_id = mplex_id;

    HASH_FIND(hh, mid->mplex_table, &key, sizeof(key), element);
    if(!element)
        return(-1);

    assert(element->key.id == id && element->key.mplex_id == mplex_id);

    *pool = element->pool;

    return(0);
}

static void margo_rpc_data_free(void* ptr)
{
	struct margo_rpc_data* data = (struct margo_rpc_data*) ptr;
	if(data->user_data && data->user_free_callback) {
		data->user_free_callback(data->user_data);
	}
	free(ptr);
}

/* dedicated thread function to drive Mercury progress */
static void hg_progress_fn(void* foo)
{
    int ret;
    unsigned int actual_count;
    struct margo_instance *mid = (struct margo_instance *)foo;
    size_t size;
    unsigned int hg_progress_timeout = mid->hg_progress_timeout_ub;
    double next_timer_exp;
    int trigger_happened;
    double tm1, tm2;
    int diag_enabled = 0;

    while(!mid->hg_progress_shutdown_flag)
    {
        trigger_happened = 0;
        do {
            /* save value of instance diag variable, in case it is modified
             * while we are in loop 
             */
            diag_enabled = mid->diag_enabled;

            if(diag_enabled) tm1 = ABT_get_wtime();
            ret = HG_Trigger(mid->hg_context, 0, 1, &actual_count);
            if(diag_enabled)
            {
                tm2 = ABT_get_wtime();
                __DIAG_UPDATE(mid->diag_trigger_elapsed, (tm2-tm1));
            }

            if(ret == HG_SUCCESS && actual_count > 0)
                trigger_happened = 1;
        } while((ret == HG_SUCCESS) && actual_count && !mid->hg_progress_shutdown_flag);

        if(trigger_happened)
            ABT_thread_yield();

        ABT_pool_get_size(mid->progress_pool, &size);
        /* Are there any other threads executing in this pool that are *not*
         * blocked ?  If so then, we can't sleep here or else those threads 
         * will not get a chance to execute.
         * TODO: check is ABT_pool_get_size returns the number of ULT/tasks
         * that can be executed including this one, or not including this one.
         */
        if(size > 0)
        {
            /* TODO: this is being executed more than is necessary (i.e.
             * in cases where there are other legitimate ULTs eligible
             * for execution that are not blocking on any events, Margo
             * or otherwise). Maybe we need an abt scheduling tweak here
             * to make sure that this ULT is the lowest priority in that
             * scenario.
             */
            if(diag_enabled) tm1 = ABT_get_wtime();
            ret = HG_Progress(mid->hg_context, 0);
            if(diag_enabled)
            {
                tm2 = ABT_get_wtime();
                __DIAG_UPDATE(mid->diag_progress_elapsed_zero_timeout, (tm2-tm1));
                __DIAG_UPDATE(mid->diag_progress_timeout_value, 0);
            }
            if(ret == HG_SUCCESS)
            {
                /* Mercury completed something; loop around to trigger
                 * callbacks 
                 */
            }
            else if(ret == HG_TIMEOUT)
            {
                /* No completion; yield here to allow other ULTs to run */
                ABT_thread_yield();
            }
            else
            {
                /* TODO: error handling */
                fprintf(stderr, "WARNING: unexpected return code (%d) from HG_Progress()\n", ret);
            }
        }
        else
        {
            hg_progress_timeout = mid->hg_progress_timeout_ub;
            ret = margo_timer_get_next_expiration(mid, &next_timer_exp);
            if(ret == 0)
            {
                /* there is a queued timer, don't block long enough
                 * to keep this timer waiting
                 */
                if(next_timer_exp >= 0.0)
                {
                    next_timer_exp *= 1000; /* convert to milliseconds */
                    if(next_timer_exp < mid->hg_progress_timeout_ub)
                        hg_progress_timeout = (unsigned int)next_timer_exp;
                }
                else
                {
                    hg_progress_timeout = 0;
                }
            }
            if(diag_enabled) tm1 = ABT_get_wtime();
            ret = HG_Progress(mid->hg_context, hg_progress_timeout);
            if(diag_enabled)
            {
                tm2 = ABT_get_wtime();
                if(hg_progress_timeout == 0)
                    __DIAG_UPDATE(mid->diag_progress_elapsed_zero_timeout, (tm2-tm1));
                else
                    __DIAG_UPDATE(mid->diag_progress_elapsed_nonzero_timeout, (tm2-tm1));
                    
                __DIAG_UPDATE(mid->diag_progress_timeout_value, hg_progress_timeout);
            }
            if(ret != HG_SUCCESS && ret != HG_TIMEOUT)
            {
                /* TODO: error handling */
                fprintf(stderr, "WARNING: unexpected return code (%d) from HG_Progress()\n", ret);
            }
        }

        /* check for any expired timers */
        margo_check_timers(mid);
    }

    return;
}


void margo_diag_start(margo_instance_id mid)
{
    mid->diag_enabled = 1;
}

static void print_diag_data(FILE *file, const char* name, const char *description, struct diag_data *data)
{
    double avg;

    fprintf(file, "# %s\n", description);
    if(data->count != 0)
        avg = data->cumulative/data->count;
    else
        avg = 0;
    fprintf(file, "%s\t%.9f\t%.9f\t%.9f\t%d\n", name, avg, data->min, data->max, data->count);
    return;
}

void margo_diag_dump(margo_instance_id mid, const char* file, int uniquify)
{
    FILE *outfile;
    time_t ltime;
    char revised_file_name[256] = {0};

    assert(mid->diag_enabled);

    if(uniquify)
    {
        char hostname[128] = {0};
        int pid;

        gethostname(hostname, 128);
        pid = getpid();

        sprintf(revised_file_name, "%s-%s-%d", file, hostname, pid);
    }
    else
    {
        sprintf(revised_file_name, "%s", file);
    }

    if(strcmp("-", file) == 0)
    {
        outfile = stdout;
    }
    else
    {
        outfile = fopen(revised_file_name, "a");
        if(!outfile)
        {
            perror("fopen");
            return;
        }
    }

    /* TODO: retrieve self addr and include in output */
    /* TODO: support pattern substitution in file name to create unique
     * output files per process
     */

    time(&ltime);
    fprintf(outfile, "# Margo diagnostics\n");
    fprintf(outfile, "# %s\n", ctime(&ltime));
    fprintf(outfile, "# <stat>\t<avg>\t<min>\t<max>\t<count>\n");
    print_diag_data(outfile, "trigger_elapsed", 
        "Time consumed by HG_Trigger()", 
        &mid->diag_trigger_elapsed);
    print_diag_data(outfile, "progress_elapsed_zero_timeout", 
        "Time consumed by HG_Progress() when called with timeout==0", 
        &mid->diag_progress_elapsed_zero_timeout);
    print_diag_data(outfile, "progress_elapsed_nonzero_timeout", 
        "Time consumed by HG_Progress() when called with timeout!=0", 
        &mid->diag_progress_elapsed_nonzero_timeout);
    print_diag_data(outfile, "progress_timeout_value", 
        "Timeout values passed to HG_Progress()", 
        &mid->diag_progress_timeout_value);

    if(outfile != stdout)
        fclose(outfile);
    
    return;
}

void margo_set_param(margo_instance_id mid, int option, const void *param)
{
    switch(option)
    {
        case MARGO_PARAM_PROGRESS_TIMEOUT_UB:
            mid->hg_progress_timeout_ub = (*((const unsigned int*)param));
            break;
    }

    return;
}

void margo_get_param(margo_instance_id mid, int option, void *param)
{

    switch(option)
    {
        case MARGO_PARAM_PROGRESS_TIMEOUT_UB:
            (*((unsigned int*)param)) = mid->hg_progress_timeout_ub;
            break;
    }

    return;
}

static hg_return_t margo_handle_cache_init(margo_instance_id mid)
{
    int i;
    struct margo_handle_cache_el *el;
    hg_return_t hret = HG_SUCCESS;

    for(i = 0; i < DEFAULT_MERCURY_HANDLE_CACHE_SIZE; i++)
    {
        el = malloc(sizeof(*el));
        if(!el)
        {
            hret = HG_NOMEM_ERROR;
            margo_handle_cache_destroy(mid);
            break;
        }

        /* create handle with NULL_ADDRs, we will reset later to valid addrs */
        hret = HG_Create(mid->hg_context, HG_ADDR_NULL, 0, &el->handle);
        if(hret != HG_SUCCESS)
        {
            free(el);
            margo_handle_cache_destroy(mid);
            break;
        }

        /* add to the free list */
        LL_PREPEND(mid->free_handle_list, el);
    }

    return hret;
}

static void margo_handle_cache_destroy(margo_instance_id mid)
{
    struct margo_handle_cache_el *el, *tmp;

    /* only free handle list elements -- handles in hash are still in use */
    LL_FOREACH_SAFE(mid->free_handle_list, el, tmp)
    {
        LL_DELETE(mid->free_handle_list, el);
        HG_Destroy(el->handle);
        free(el);
    }

    return;
}

static hg_return_t margo_handle_cache_get(margo_instance_id mid,
    hg_addr_t addr, hg_id_t id, hg_handle_t *handle)
{
    struct margo_handle_cache_el *el;
    hg_return_t hret;

    if(!mid->free_handle_list)
    {
        /* if no available handles, just fall through */
        return HG_OTHER_ERROR;
    }

    /* pop first element from the free handle list */
    el = mid->free_handle_list;
    LL_DELETE(mid->free_handle_list, el);

    /* reset handle */
    hret = HG_Reset(el->handle, addr, id);
    if(hret == HG_SUCCESS)
    {
        /* put on in-use list and pass back handle */
        HASH_ADD(hh, mid->used_handle_hash, handle, sizeof(hg_handle_t), el);
        *handle = el->handle;
    }
    else
    {
        /* reset failed, add handle back to the free list */
        LL_APPEND(mid->free_handle_list, el);
    }

    return hret;
}

static hg_return_t margo_handle_cache_put(margo_instance_id mid,
    hg_handle_t handle)
{
    struct margo_handle_cache_el *el;

    /* look for handle in the in-use hash */
    HASH_FIND(hh, mid->used_handle_hash, &handle, sizeof(hg_handle_t), el);
    if(!el)
    {
        /* this handle was manually allocated -- just fall through */
        return HG_OTHER_ERROR;
    }

    /* remove from the in-use hash */
    HASH_DELETE(hh, mid->used_handle_hash, el);

    /* add to the tail of the free handle list */
    LL_APPEND(mid->free_handle_list, el);

    return HG_SUCCESS;
}
