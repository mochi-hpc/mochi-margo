
/*
 * (C) 2015 The University of Chicago
 * 
 * See COPYRIGHT in top-level directory.
 */

#include <assert.h>
#include <unistd.h>
#include <errno.h>
#include <abt.h>
#include <stdlib.h>

#include <margo-config.h>
#include <time.h>
#include <math.h>

#include "margo.h"
#include "margo-timer.h"
#include "utlist.h"
#include "uthash.h"

#define DEFAULT_MERCURY_PROGRESS_TIMEOUT_UB 100 /* 100 milliseconds */
#define DEFAULT_MERCURY_HANDLE_CACHE_SIZE 32

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

struct margo_addr_cache_el
{
    char*          name; /* address as a string */
    hg_addr_t      addr; /* corresponding Mercury address */
    UT_hash_handle hh;   /* hash pointer */
};

struct margo_finalize_cb
{
    void(*callback)(void*);
    void* uargs;
    struct margo_finalize_cb* next;
};

struct margo_timer_list; /* defined in margo-timer.c */

struct margo_instance
{
    /* mercury/argobots state */
    hg_context_t *hg_context;
    hg_class_t *hg_class;
    ABT_pool handler_pool;
    ABT_pool progress_pool;

    /* internal to margo for this particular instance */
    int margo_init;
    int abt_init;
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
    struct margo_finalize_cb* finalize_cb;

    /* control logic to prevent margo_finalize from destroying
       the instance when some operations are pending */
    unsigned pending_operations;
    ABT_mutex pending_operations_mtx;
    int finalize_requested;

    /* control logic for shutting down */
    hg_id_t shutdown_rpc_id;
    int enable_remote_shutdown;

    /* timer data */
    struct margo_timer_list* timer_list;
    /* linked list of free hg handles and a hash of in-use handles */
    struct margo_handle_cache_el *free_handle_list;
    struct margo_handle_cache_el *used_handle_hash;
    ABT_mutex handle_cache_mtx; /* mutex protecting access to above caches */

    /* cache of addresses */
    int addr_cache_enabled;
    struct margo_addr_cache_el *addr_cache;
    ABT_mutex addr_cache_mtx; /* mutex protecting access to above cache */

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

struct margo_rpc_data
{
    margo_instance_id mid;
    ABT_pool pool;
    void* user_data;
    void (*user_free_callback)(void *);
};

MERCURY_GEN_PROC(margo_shutdown_out_t, ((int32_t)(ret)))

static void hg_progress_fn(void* foo);
static void margo_rpc_data_free(void* ptr);
static void remote_shutdown_ult(hg_handle_t handle);
DECLARE_MARGO_RPC_HANDLER(remote_shutdown_ult);

static inline void demux_id(hg_id_t in, hg_id_t* base_id, uint16_t *provider_id)
{
    /* retrieve low bits for provider */
    *provider_id = 0;
    *provider_id += (in & (((1<<(__MARGO_PROVIDER_ID_SIZE*8))-1)));

    /* clear low order bits */
    *base_id = (in >> (__MARGO_PROVIDER_ID_SIZE*8)) <<
        (__MARGO_PROVIDER_ID_SIZE*8);

    return;
}

static inline hg_id_t mux_id(hg_id_t base_id, uint16_t provider_id)
{
    hg_id_t id;

    id = (base_id >> (__MARGO_PROVIDER_ID_SIZE*8)) <<
       (__MARGO_PROVIDER_ID_SIZE*8);
    id |= provider_id;

    return id;
}

static inline hg_id_t gen_id(const char* func_name, uint16_t provider_id)
{
    hg_id_t id;
    unsigned hashval;

    HASH_JEN(func_name, strlen(func_name), hashval);
    id = hashval << (__MARGO_PROVIDER_ID_SIZE*8);
    id |= provider_id;

    return id;
}

static hg_return_t margo_handle_cache_init(margo_instance_id mid);
static void margo_handle_cache_destroy(margo_instance_id mid);
static hg_return_t margo_handle_cache_get(margo_instance_id mid,
    hg_addr_t addr, hg_id_t id, hg_handle_t *handle);
static hg_return_t margo_handle_cache_put(margo_instance_id mid,
    hg_handle_t handle);
static hg_return_t margo_addr_cache_init(margo_instance_id mid);
static void margo_addr_cache_destroy(margo_instance_id mid);
static hg_return_t margo_addr_cache_get_or_lookup(margo_instance_id mid,
    const char* name, hg_addr_t* addr);
static hg_id_t margo_register_internal(margo_instance_id mid, hg_id_t id,
    hg_proc_cb_t in_proc_cb, hg_proc_cb_t out_proc_cb, hg_rpc_cb_t rpc_cb, ABT_pool pool);
static void set_argobots_tunables(void);

/* Set tunable parameters in Argobots to be more friendly to typical Margo
 * use cases.  No return value, this is a best-effort advisory function. It
 * also will (where possible) defer to any pre-existing explicit environment
 * variable settings.  We only override if the user has not specified yet.
 */
static void set_argobots_tunables(void)
{

    /* Rationale: Margo is very likely to create a single producer (the
     * progress function), multiple consumer usage pattern that
     * causes excess memory consumption in some versions of
     * Argobots.  See
     * https://xgitlab.cels.anl.gov/sds/margo/issues/40 for details.
     * We therefore set the ABT_MEM_MAX_NUM_STACKS parameter 
     * for Argobots to a low value so that RPC handler threads do not
     * queue large numbers of stacks for reuse in per-ES data 
     * structures.
     */
    if(!getenv("ABT_MEM_MAX_NUM_STACKS"))
        putenv("ABT_MEM_MAX_NUM_STACKS=8");

    /* Rationale: the default stack size in Argobots (as of February 2019)
     * is 16K, but this is likely to be too small for Margo as it traverses
     * a Mercury -> communications library call stack, and the potential 
     * stack corruptions are very hard to debug.  We therefore pick a much
     * higher default stack size.  See this mailing list thread for
     * discussion:
     * https://lists.argobots.org/pipermail/discuss/2019-February/000039.html
     */
    if(!getenv("ABT_THREAD_STACKSIZE"))
        putenv("ABT_THREAD_STACKSIZE=2097152");

    return;
}

margo_instance_id margo_init_opt(const char *addr_str, int mode, const struct hg_init_info *hg_init_info,
    int use_progress_thread, int rpc_thread_count)
{
    ABT_xstream progress_xstream = ABT_XSTREAM_NULL;
    ABT_pool progress_pool = ABT_POOL_NULL;
    ABT_sched progress_sched;
    ABT_sched self_sched;
    ABT_xstream self_xstream;
    ABT_xstream *rpc_xstreams = NULL;
    ABT_sched *rpc_scheds = NULL;
    ABT_xstream rpc_xstream = ABT_XSTREAM_NULL;
    ABT_pool rpc_pool = ABT_POOL_NULL;
    hg_class_t *hg_class = NULL;
    hg_context_t *hg_context = NULL;
    int listen_flag = (mode == MARGO_CLIENT_MODE) ? HG_FALSE : HG_TRUE;
    int abt_init = 0;
    int i;
    int ret;
    struct margo_instance *mid = MARGO_INSTANCE_NULL;

    if(mode != MARGO_CLIENT_MODE && mode != MARGO_SERVER_MODE) goto err;

    /* adjust argobots settings to suit Margo */
    set_argobots_tunables();

    if (ABT_initialized() == ABT_ERR_UNINITIALIZED)
    {
        ret = ABT_init(0, NULL); /* XXX: argc/argv not currently used by ABT ... */
        if(ret != 0) goto err;
        abt_init = 1;
    }

    /* set caller (self) ES to sleep when idle by using sched_wait */
    ret = ABT_sched_create_basic(ABT_SCHED_BASIC_WAIT, 0, NULL, 
        ABT_SCHED_CONFIG_NULL, &self_sched);
    if(ret != ABT_SUCCESS) goto err;
    ret = ABT_xstream_self(&self_xstream);
    if(ret != ABT_SUCCESS) goto err;
    ret = ABT_xstream_set_main_sched(self_xstream, self_sched);
    if(ret != ABT_SUCCESS) goto err;

    if (use_progress_thread)
    {
        /* create an xstream to run progress engine */
        ret = ABT_sched_create_basic(ABT_SCHED_BASIC_WAIT, 0, NULL,
           ABT_SCHED_CONFIG_NULL, &progress_sched);
        if (ret != ABT_SUCCESS) goto err;
        ret = ABT_xstream_create(progress_sched, &progress_xstream);
        if (ret != ABT_SUCCESS) goto err;
    }
    else
    {
        ret = ABT_xstream_self(&progress_xstream);
        if (ret != ABT_SUCCESS) goto err;
    }
    ret = ABT_xstream_get_main_pools(progress_xstream, 1, &progress_pool);
    if (ret != ABT_SUCCESS) goto err;

    if (rpc_thread_count > 0)
    {
        /* create a collection of xstreams to run RPCs */
        rpc_xstreams = calloc(rpc_thread_count, sizeof(*rpc_xstreams));
        if (rpc_xstreams == NULL) goto err;
        rpc_scheds = calloc(rpc_thread_count, sizeof(*rpc_scheds));
        if (rpc_scheds == NULL) goto err;
        ret = ABT_pool_create_basic(ABT_POOL_FIFO_WAIT, ABT_POOL_ACCESS_MPMC, ABT_TRUE, &rpc_pool);
        if (ret != ABT_SUCCESS) goto err;
        for(i=0; i<rpc_thread_count; i++) 
        {
            ret = ABT_sched_create_basic(ABT_SCHED_BASIC_WAIT, 1, &rpc_pool,
               ABT_SCHED_CONFIG_NULL, &rpc_scheds[i]);
            if (ret != ABT_SUCCESS) goto err;
            ret = ABT_xstream_create(rpc_scheds[i], rpc_xstreams+i);
            if (ret != ABT_SUCCESS) goto err;
        }
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

    hg_class = HG_Init_opt(addr_str, listen_flag, hg_init_info);
    if(!hg_class) goto err;

    hg_context = HG_Context_create(hg_class);
    if(!hg_context) goto err;

    mid = margo_init_pool(progress_pool, rpc_pool, hg_context);
    if (mid == MARGO_INSTANCE_NULL) goto err;

    mid->margo_init = 1;
    mid->abt_init = abt_init;
    mid->owns_progress_pool = use_progress_thread;
    mid->progress_xstream = progress_xstream;
    mid->num_handler_pool_threads = rpc_thread_count < 0 ? 0 : rpc_thread_count;
    mid->rpc_xstreams = rpc_xstreams;

    return mid;

err:
    if(mid)
    {
        margo_timer_list_free(mid->timer_list);
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
        free(rpc_scheds);
    }
    if(hg_context)
        HG_Context_destroy(hg_context);
    if(hg_class)
        HG_Finalize(hg_class);
    if(abt_init)
        ABT_finalize();
    return MARGO_INSTANCE_NULL;
}

margo_instance_id margo_init_pool(ABT_pool progress_pool, ABT_pool handler_pool,
    hg_context_t *hg_context)
{
    int ret;
    hg_return_t hret;
    struct margo_instance *mid;

    mid = calloc(1,sizeof(*mid));
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
    mid->finalize_cb = NULL;
    mid->enable_remote_shutdown = 0;

    mid->pending_operations = 0;
    ABT_mutex_create(&mid->pending_operations_mtx);
    mid->finalize_requested = 0;

    mid->timer_list = margo_timer_list_create();
    if(mid->timer_list == NULL) goto err;

    /* initialize the handle cache */
    hret = margo_handle_cache_init(mid);
    if(hret != HG_SUCCESS) goto err;

    /* initialize the addr cache */
    hret = margo_addr_cache_init(mid);
    if(hret != HG_SUCCESS) goto err;

    ret = ABT_thread_create(mid->progress_pool, hg_progress_fn, mid, 
        ABT_THREAD_ATTR_NULL, &mid->hg_progress_tid);
    if(ret != 0) goto err;

    mid->shutdown_rpc_id = MARGO_REGISTER(mid, "__shutdown__", 
            void, margo_shutdown_out_t, remote_shutdown_ult);

    return mid;

err:
    if(mid)
    {
        margo_handle_cache_destroy(mid);
        margo_addr_cache_destroy(mid);
        margo_timer_list_free(mid->timer_list);
        ABT_mutex_free(&mid->finalize_mutex);
        ABT_cond_free(&mid->finalize_cond);
        ABT_mutex_free(&mid->pending_operations_mtx);
        free(mid);
    }
    return MARGO_INSTANCE_NULL;
}

static void margo_cleanup(margo_instance_id mid)
{
    int i;

    /* call finalize callbacks */
    struct margo_finalize_cb* fcb = mid->finalize_cb;
    while(fcb) {
        (fcb->callback)(fcb->uargs);
        struct margo_finalize_cb* tmp = fcb;
        fcb = fcb->next;
        free(tmp);
    }

    margo_timer_list_free(mid->timer_list);

    ABT_mutex_free(&mid->finalize_mutex);
    ABT_cond_free(&mid->finalize_cond);
    ABT_mutex_free(&mid->pending_operations_mtx);

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
        if (mid->abt_init)
            ABT_finalize();
    }

    free(mid);
}

void margo_finalize(margo_instance_id mid)
{
    int do_cleanup;

    /* check if there are pending operations */
    int pending;
    ABT_mutex_lock(mid->pending_operations_mtx);
    pending = mid->pending_operations;
    ABT_mutex_unlock(mid->pending_operations_mtx);
    if(pending) {
        mid->finalize_requested = 1;
        return;
    }

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

hg_bool_t margo_is_listening(
            margo_instance_id mid)
{
    if(!mid) return HG_FALSE;
    return HG_Class_is_listening(mid->hg_class);
}

void margo_push_finalize_callback(
            margo_instance_id mid,
            void(*cb)(void*),                  
            void* uargs)
{
    if(cb == NULL) return;

    struct margo_finalize_cb* fcb = 
        (struct margo_finalize_cb*)malloc(sizeof(*fcb));
    fcb->callback = cb;
    fcb->uargs = uargs;

    struct margo_finalize_cb* next = mid->finalize_cb;
    fcb->next = next;
    mid->finalize_cb = fcb;
}

void margo_enable_remote_shutdown(margo_instance_id mid)
{
    mid->enable_remote_shutdown = 1;
}

int margo_shutdown_remote_instance(
        margo_instance_id mid,
        hg_addr_t remote_addr)
{
    hg_return_t hret;
    hg_handle_t handle;

    hret = margo_create(mid, remote_addr,
                        mid->shutdown_rpc_id, &handle);
    if(hret != HG_SUCCESS) return -1;

    hret = margo_forward(handle, NULL);
    if(hret != HG_SUCCESS)
    {
        margo_destroy(handle);
        return -1;
    }

    margo_shutdown_out_t out;
    hret = margo_get_output(handle, &out);
    if(hret != HG_SUCCESS)
    {
        margo_destroy(handle);
        return -1;
    }

    margo_free_output(handle, &out);
    margo_destroy(handle);

    return out.ret;
}

hg_id_t margo_provider_register_name(margo_instance_id mid, const char *func_name,
    hg_proc_cb_t in_proc_cb, hg_proc_cb_t out_proc_cb, hg_rpc_cb_t rpc_cb,
    uint16_t provider_id, ABT_pool pool)
{
    hg_id_t id;
    int ret;

    assert(provider_id <= MARGO_MAX_PROVIDER_ID);
    
    id = gen_id(func_name, provider_id);

    ret = margo_register_internal(mid, id, in_proc_cb, out_proc_cb, rpc_cb, pool);
    if(ret == 0)
        return(0);

    return(id);
}

hg_return_t margo_deregister(
        margo_instance_id mid,
        hg_id_t rpc_id)
{
    return HG_Deregister(mid->hg_class, rpc_id);
}

hg_return_t margo_registered_name(margo_instance_id mid, const char *func_name,
    hg_id_t *id, hg_bool_t *flag)
{
    *id = gen_id(func_name, 0);
    return(HG_Registered(mid->hg_class, *id, flag));
}

hg_return_t margo_provider_registered_name(margo_instance_id mid, const char *func_name,
    uint16_t provider_id, hg_id_t *id, hg_bool_t *flag)
{
    *id = gen_id(func_name, provider_id);

    return HG_Registered(mid->hg_class, *id, flag);
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
    if(margo_data->user_data && margo_data->user_free_callback) {
        (margo_data->user_free_callback)(margo_data->user_data);
    }
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

hg_return_t margo_registered_disabled_response(
    margo_instance_id mid,
    hg_id_t id,
    int* disabled_flag)
{
    hg_bool_t b;
    hg_return_t ret = HG_Registered_disabled_response(mid->hg_class, id, &b);
    if(ret != HG_SUCCESS) return ret;
    *disabled_flag = b;
    return HG_SUCCESS;
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
    ABT_eventual eventual = (ABT_eventual)(info->arg);

    /* propagate return code out through eventual */
    ABT_eventual_set(eventual, &evt, sizeof(evt));

    return(HG_SUCCESS);
}

static hg_return_t margo_addr_lookup_internal(
    margo_instance_id mid,
    const char   *name,
    hg_addr_t    *addr)
{
    hg_return_t hret;
    struct lookup_cb_evt *evt;
    ABT_eventual eventual;
    int ret;

    ret = ABT_eventual_create(sizeof(*evt), &eventual);
    if(ret != 0)
    {
        return(HG_NOMEM_ERROR);        
    }

    hret = HG_Addr_lookup(mid->hg_context, margo_addr_lookup_cb,
        (void*)eventual, name, HG_OP_ID_IGNORE);
    if(hret == HG_SUCCESS)
    {
        ABT_eventual_wait(eventual, (void**)&evt);
        *addr = evt->addr;
        hret = evt->hret;
    }

    ABT_eventual_free(&eventual);

    return(hret);
}

hg_return_t margo_addr_lookup(
    margo_instance_id mid,
    const char   *name,
    hg_addr_t    *addr)
{
    return margo_addr_cache_get_or_lookup(mid, name, addr);
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
    hg_return_t hret = HG_OTHER_ERROR;

    /* look for a handle to reuse */
    hret = margo_handle_cache_get(mid, addr, id, handle);
    if(hret != HG_SUCCESS)
    {
        /* else try creating a new handle */
        hret = HG_Create(mid->hg_context, addr, id, handle);
    }

    return hret;
}

hg_return_t margo_destroy(hg_handle_t handle)
{
    if(handle == HG_HANDLE_NULL)
        return HG_SUCCESS;

    /* check if the reference count of the handle is 1 */
    int32_t refcount = HG_Ref_get(handle);
    if(refcount != 1) {
        /* if different from 1, then HG_Destroy will simply decrease it */
        return HG_Destroy(handle);
    }

    margo_instance_id mid;
    hg_return_t hret = HG_OTHER_ERROR;

    /* use the handle to get the associated mid */
    mid = margo_hg_handle_get_instance(handle);

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
    ABT_eventual eventual = (ABT_eventual)(info->arg);

    /* propagate return code out through eventual */
    ABT_eventual_set(eventual, &hret, sizeof(hret));
    
    return(HG_SUCCESS);
}

hg_return_t margo_provider_forward(
    uint16_t provider_id,
    hg_handle_t handle,
    void *in_struct)
{
	hg_return_t hret;
	margo_request req;
	hret = margo_provider_iforward(provider_id, handle, in_struct, &req);
	if(hret != HG_SUCCESS) 
		return hret;
	return margo_wait(req);
}

hg_return_t margo_provider_iforward(
    uint16_t provider_id,
    hg_handle_t handle,
    void *in_struct,
    margo_request* req)
{
    hg_return_t hret = HG_TIMEOUT;
    ABT_eventual eventual;
    int ret;
    const struct hg_info* hgi; 
    hg_id_t id;
    hg_proc_cb_t in_cb, out_cb;
    hg_bool_t flag;

    assert(provider_id <= MARGO_MAX_PROVIDER_ID);

    hgi = HG_Get_info(handle);
    id = mux_id(hgi->id, provider_id);

    hg_bool_t is_registered;
    ret = HG_Registered(hgi->hg_class, id, &is_registered);
    if(ret != HG_SUCCESS)
        return(ret);
    if(!is_registered)
    {
        /* if Mercury does not recognize this ID (with provider id included)
         * then register it now
         */
        /* find encoders for base ID */
        ret = HG_Registered_proc_cb(hgi->hg_class, hgi->id, &flag, &in_cb, &out_cb);
        if(ret != HG_SUCCESS)
            return(ret);
        if(!flag)
            return(HG_NO_MATCH);

        /* find out if disable_response was called for this RPC */
        hg_bool_t response_disabled;
        ret = HG_Registered_disabled_response(hgi->hg_class, hgi->id, &response_disabled);
        if(ret != HG_SUCCESS)
            return(ret);

        /* register new ID that includes provider id */
        ret = margo_register_internal(margo_hg_info_get_instance(hgi), 
            id, in_cb, out_cb, NULL, ABT_POOL_NULL);
        if(ret == 0)
            return(HG_OTHER_ERROR);
        ret = HG_Registered_disable_response(hgi->hg_class, id, response_disabled);
        if(ret != HG_SUCCESS)
            return(ret);
    }
    ret = HG_Reset(handle, hgi->addr, id);
    if(ret != HG_SUCCESS)
        return(ret);

    ret = ABT_eventual_create(sizeof(hret), &eventual);
    if(ret != 0)
    {
        return(HG_NOMEM_ERROR);        
    }

    *req = eventual;

    return HG_Forward(handle, margo_cb, (void*)eventual, in_struct);
}

hg_return_t margo_wait(margo_request req)
{
	hg_return_t* waited_hret;
	hg_return_t  hret;

    ABT_eventual_wait(req, (void**)&waited_hret);
	hret = *waited_hret;
    ABT_eventual_free(&req);
	
    return(hret);
}

int margo_test(margo_request req, int* flag)
{
    return ABT_eventual_test(req, NULL, flag);
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
    hg_handle_t handle,
    void *in_struct,
    double timeout_ms)
{
    int ret;
    hg_return_t hret;
    margo_instance_id mid;
    ABT_eventual eventual;
    hg_return_t* waited_hret;
    margo_timer_t forward_timer;
    margo_forward_timeout_cb_dat timeout_cb_dat;

    ret = ABT_eventual_create(sizeof(hret), &eventual);
    if(ret != 0)
    {
        return(HG_NOMEM_ERROR);        
    }

    /* use the handle to get the associated mid */
    mid = margo_hg_handle_get_instance(handle);

    /* set a timer object to expire when this forward times out */
    timeout_cb_dat.handle = handle;
    margo_timer_init(mid, &forward_timer, margo_forward_timeout_cb,
        &timeout_cb_dat, timeout_ms);

    hret = HG_Forward(handle, margo_cb, (void*)eventual, in_struct);
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
    hg_handle_t handle,
    void *out_struct)
{
    hg_return_t hret;
    margo_request req;
    hret = margo_irespond(handle,out_struct,&req);
    if(hret != HG_SUCCESS)
        return hret;
    return margo_wait(req);
}

hg_return_t margo_irespond(
    hg_handle_t handle,
    void *out_struct,
    margo_request* req)
{
    ABT_eventual eventual;
    int ret;

    ret = ABT_eventual_create(sizeof(hg_return_t), &eventual);
    if(ret != 0)
    {
        return(HG_NOMEM_ERROR);
    }

    *req = eventual;

    return HG_Respond(handle, margo_cb, (void*)eventual, out_struct);
}

hg_return_t margo_bulk_create(
    margo_instance_id mid,
    hg_uint32_t count,
    void **buf_ptrs,
    const hg_size_t *buf_sizes,
    hg_uint8_t flags,
    hg_bulk_t *handle)
{
    return(HG_Bulk_create(mid->hg_class, count,
        buf_ptrs, buf_sizes, flags, handle));
}

hg_return_t margo_bulk_free(
    hg_bulk_t handle)
{
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
    margo_request req;
    hg_return_t hret = margo_bulk_itransfer(mid,op,origin_addr,
                          origin_handle, origin_offset, local_handle,
                          local_offset, size, &req);
    if(hret != HG_SUCCESS)
        return hret;
    return margo_wait(req);
}

hg_return_t margo_bulk_itransfer(
    margo_instance_id mid,
    hg_bulk_op_t op,
    hg_addr_t origin_addr,
    hg_bulk_t origin_handle,
    size_t origin_offset,
    hg_bulk_t local_handle,
    size_t local_offset,
    size_t size,
    margo_request* req)
{
    hg_return_t hret = HG_TIMEOUT;
    ABT_eventual eventual;
    int ret;

    ret = ABT_eventual_create(sizeof(hret), &eventual);
    if(ret != 0)
    {
        return(HG_NOMEM_ERROR);        
    }

    *req = eventual;

    hret = HG_Bulk_transfer(mid->hg_context, margo_cb,
        (void*)eventual, op, origin_addr, origin_handle, origin_offset, local_handle,
        local_offset, size, HG_OP_ID_IGNORE);

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

int margo_get_handler_pool(margo_instance_id mid, ABT_pool* pool)
{
    if(mid) {
        *pool = mid->handler_pool;
        return 0;
    } else {
        return -1;
    }
}

hg_context_t* margo_get_context(margo_instance_id mid)
{
    return(mid->hg_context);
}

hg_class_t* margo_get_class(margo_instance_id mid)
{
    return(mid->hg_class);
}

ABT_pool margo_hg_handle_get_handler_pool(hg_handle_t h)
{
    struct margo_rpc_data* data;
    const struct hg_info* info;
    ABT_pool pool;
    
    info = HG_Get_info(h);
    if(!info) return ABT_POOL_NULL;

    data = (struct margo_rpc_data*) HG_Registered_data(info->hg_class, info->id);
    if(!data) return ABT_POOL_NULL;

    pool = data->pool;
    if(pool == ABT_POOL_NULL)
        margo_get_handler_pool(data->mid, &pool);

    return pool;
}

margo_instance_id margo_hg_info_get_instance(const struct hg_info *info)
{
    struct margo_rpc_data* data = 
        (struct margo_rpc_data*) HG_Registered_data(info->hg_class, info->id);
    if(!data) return MARGO_INSTANCE_NULL;
    return data->mid;
}

margo_instance_id margo_hg_handle_get_instance(hg_handle_t h)
{
    struct margo_rpc_data* data;
    const struct hg_info* info;
    
    info = HG_Get_info(h);
    if(!info) return MARGO_INSTANCE_NULL;

    data = (struct margo_rpc_data*) HG_Registered_data(info->hg_class, info->id);
    if(!data) return MARGO_INSTANCE_NULL;

    return data->mid;
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

    ABT_mutex_create(&(mid->handle_cache_mtx));

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

    ABT_mutex_free(&mid->handle_cache_mtx);

    return;
}

static hg_return_t margo_handle_cache_get(margo_instance_id mid,
    hg_addr_t addr, hg_id_t id, hg_handle_t *handle)
{
    struct margo_handle_cache_el *el;
    hg_return_t hret = HG_SUCCESS;

    ABT_mutex_lock(mid->handle_cache_mtx);

    if(!mid->free_handle_list)
    {
        /* if no available handles, just fall through */
        hret = HG_OTHER_ERROR;
        goto finish;
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

finish:
    ABT_mutex_unlock(mid->handle_cache_mtx);
    return hret;
}

static hg_return_t margo_handle_cache_put(margo_instance_id mid,
    hg_handle_t handle)
{
    struct margo_handle_cache_el *el;
    hg_return_t hret = HG_SUCCESS;

    ABT_mutex_lock(mid->handle_cache_mtx);

    /* look for handle in the in-use hash */
    HASH_FIND(hh, mid->used_handle_hash, &handle, sizeof(hg_handle_t), el);
    if(!el)
    {
        /* this handle was manually allocated -- just fall through */
        hret = HG_OTHER_ERROR;
        goto finish;
    }

    /* remove from the in-use hash */
    HASH_DELETE(hh, mid->used_handle_hash, el);

    /* add to the tail of the free handle list */
    LL_APPEND(mid->free_handle_list, el);

finish:
    ABT_mutex_unlock(mid->handle_cache_mtx);
    return hret;
}


static hg_return_t margo_addr_cache_init(margo_instance_id mid)
{
    int i;
    struct margo_addr_cache_el *el;
    hg_return_t hret = HG_SUCCESS;

    ABT_mutex_create(&(mid->addr_cache_mtx));

    return hret;
}

static void margo_addr_cache_destroy(margo_instance_id mid)
{
    struct margo_addr_cache_el *el, *tmp;

    HASH_ITER(hh, mid->addr_cache, el, tmp) {
        HASH_DEL(mid->addr_cache, el);
        margo_addr_free(mid, el->addr);
        free(el->name);
        free(el);
    }

    ABT_mutex_free(&mid->addr_cache_mtx);

    return;
}

static hg_return_t margo_addr_cache_get_or_lookup(margo_instance_id mid,
    const char* name, hg_addr_t* addr)
{
    hg_return_t hret = HG_SUCCESS;
    ABT_mutex_lock(mid->addr_cache_mtx);

    struct margo_addr_cache_el *el = NULL;
    HASH_FIND_STR(mid->addr_cache, name, el);
    if(el != NULL) {
        hret = margo_addr_dup(mid, el->addr, addr);
        ABT_mutex_unlock(mid->addr_cache_mtx);
        return hret;
    }

    ABT_mutex_unlock(mid->addr_cache_mtx);

    hret = margo_addr_lookup_internal(mid, name, addr);
    if(hret != HG_SUCCESS)
        return hret;

    ABT_mutex_lock(mid->addr_cache_mtx);

    HASH_FIND_STR(mid->addr_cache, name, el);
    if(el == NULL) {
        el = (struct margo_addr_cache_el*)calloc(1, sizeof(*el));
        hret = margo_addr_dup(mid, *addr, &(el->addr));
        if(hret != HG_SUCCESS) {
            ABT_mutex_unlock(mid->addr_cache_mtx);
            free(el);
            return hret;
        }
        el->name = strdup(name);
        HASH_ADD_KEYPTR(hh, mid->addr_cache, el->name, strlen(el->name), el);
    }

    ABT_mutex_unlock(mid->addr_cache_mtx);

    return hret;
}

struct margo_timer_list *margo_get_timer_list(margo_instance_id mid)
{
        return mid->timer_list;
}

static void remote_shutdown_ult(hg_handle_t handle)
{
    margo_instance_id mid = margo_hg_handle_get_instance(handle);
    margo_shutdown_out_t out;
    if(!(mid->enable_remote_shutdown)) {
        out.ret = -1;
    } else {
        out.ret = 0;
    }
    margo_respond(handle, &out);
    margo_destroy(handle);
    if(mid->enable_remote_shutdown) {
        margo_finalize(mid);
    }
}
DEFINE_MARGO_RPC_HANDLER(remote_shutdown_ult)

static hg_id_t margo_register_internal(margo_instance_id mid, hg_id_t id,
    hg_proc_cb_t in_proc_cb, hg_proc_cb_t out_proc_cb, hg_rpc_cb_t rpc_cb,
    ABT_pool pool)
{
    struct margo_rpc_data* margo_data;
    hg_return_t hret;
    
    hret = HG_Register(mid->hg_class, id, in_proc_cb, out_proc_cb, rpc_cb);
    if(hret != HG_SUCCESS)
        return(hret);

    /* register the margo data with the RPC */
    margo_data = (struct margo_rpc_data*)HG_Registered_data(mid->hg_class, id);
    if(!margo_data)
    {
        margo_data = (struct margo_rpc_data*)malloc(sizeof(struct margo_rpc_data));
        if(!margo_data)
            return(0);
        margo_data->mid = mid;
        margo_data->pool = pool;
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

int __margo_internal_finalize_requested(margo_instance_id mid)
{
    if(!mid) return 0;
    return mid->finalize_requested;
}

void __margo_internal_incr_pending(margo_instance_id mid)
{
    if(!mid) return;
    ABT_mutex_lock(mid->pending_operations_mtx);
    mid->pending_operations += 1;
    ABT_mutex_unlock(mid->pending_operations_mtx);
}

void __margo_internal_decr_pending(margo_instance_id mid)
{
    if(!mid) return;
    ABT_mutex_lock(mid->pending_operations_mtx);
    mid->pending_operations -= 1;
    ABT_mutex_unlock(mid->pending_operations_mtx);
}
