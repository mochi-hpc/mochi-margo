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
#include "margo-internal.h"
#include "margo-bulk-util.h"
#include "margo-timer.h"
#include "utlist.h"
#include "uthash.h"

#define DEFAULT_MERCURY_PROGRESS_TIMEOUT_UB 100 /* 100 milliseconds */
#define DEFAULT_MERCURY_HANDLE_CACHE_SIZE 32

#define MARGO_SPARKLINE_TIMESLICE 1

/* If margo is initializing ABT, we need to track how many instances of margo
 * are being created, so that the last one can call ABT_finalize.
 * If margo initializes ABT, g_num_margo_instances_mtx will be created, so
 * in later calls and in margo_cleanup we can check for g_num_margo_instances_mtx != ABT_MUTEX_NULL
 * to know if we should do something to cleanup ABT as well.
 */
static int g_num_margo_instances = 0; // how many margo instances exist
static ABT_mutex g_num_margo_instances_mtx = ABT_MUTEX_NULL; // mutex for above global variable
static int g_margo_abt_init = 0;

/* key for Argobots thread-local storage to track RPC breadcrumbs across thread
 * execution
 */
static ABT_key rpc_breadcrumb_key = ABT_KEY_NULL;

/* ULT-local key to hold breadcrumb timing data on the target */
static ABT_key target_timing_key = ABT_KEY_NULL;

#define __DIAG_UPDATE(__data, __time)\
do {\
    __data.stats.count++; \
    __data.stats.cumulative += (__time); \
    if((__time) > __data.stats.max) __data.stats.max = (__time); \
    if(__data.stats.min == 0 || (__time) < __data.stats.min) __data.stats.min = (__time); \
} while(0)

MERCURY_GEN_PROC(margo_shutdown_out_t, ((int32_t)(ret)))

static void hg_progress_fn(void* foo);
static void sparkline_data_collection_fn(void* foo);

static void margo_rpc_data_free(void* ptr);
static uint64_t margo_breadcrumb_set(hg_id_t rpc_id);
static void margo_breadcrumb_measure(margo_instance_id mid, uint64_t rpc_breadcrumb, double start, breadcrumb_type type, uint16_t provider_id, uint64_t hash, hg_handle_t h);
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
        g_margo_abt_init = 1;
        ret = ABT_mutex_create(&g_num_margo_instances_mtx);
        if(ret != 0) goto err;
    }

    /* set caller (self) ES to sleep when idle by using sched_wait */
    ret = ABT_sched_create_basic(ABT_SCHED_BASIC_WAIT, 0, NULL, 
        ABT_SCHED_CONFIG_NULL, &self_sched);
    if(ret != ABT_SUCCESS) goto err;
    ret = ABT_xstream_self(&self_xstream);
    if(ret != ABT_SUCCESS) goto err;
    ret = ABT_xstream_set_main_sched(self_xstream, self_sched);
    if(ret != ABT_SUCCESS) {
        // best effort
        ABT_sched_free(&self_sched);
    }

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
    mid->owns_progress_pool = use_progress_thread;
    mid->progress_xstream = progress_xstream;
    mid->num_handler_pool_threads = rpc_thread_count < 0 ? 0 : rpc_thread_count;
    mid->rpc_xstreams = rpc_xstreams;
    mid->num_registered_rpcs = 0;

    /* start profiling if env variable MARGO_ENABLE_PROFILING is set */
    unsigned int profile = 0;
    mid->profile_enabled = 0;
    mid->previous_sparkline_data_collection_time = ABT_get_wtime();
    mid->sparkline_index = 0;

    if(getenv("MARGO_ENABLE_PROFILING")) {
      profile = (unsigned int)atoi(getenv("MARGO_ENABLE_PROFILING"));
      margo_set_param(mid, MARGO_PARAM_ENABLE_PROFILING, &profile);
    }

    if(profile) {
       char * name;
       margo_profile_start(mid);

       GET_SELF_ADDR_STR(mid, name);
       HASH_JEN(name, strlen(name), mid->self_addr_hash); /*record own address in cache to be used in breadcrumb generation */

       ret = ABT_thread_create(mid->progress_pool, sparkline_data_collection_fn, mid, 
       ABT_THREAD_ATTR_NULL, &mid->sparkline_data_collection_tid);
       if(ret != 0)
         fprintf(stderr, "MARGO_PROFILE: Failed to start sparkline data collection thread. Continuing to profile without sparkline data collection.\n");

    }

    /* start diagnostics if the variable MARGO_ENABLE_DIAGNOSTICS is set */
    unsigned int diag = 0;
    mid->diag_enabled = 0;

    if(getenv("MARGO_ENABLE_DIAGNOSTICS")) {
      diag = (unsigned int)atoi(getenv("MARGO_ENABLE_DIAGNOSTICS"));
      margo_set_param(mid, MARGO_PARAM_ENABLE_DIAGNOSTICS, &diag);
    }

    if(diag)
      margo_diag_start(mid);

    return mid;

err:
    if(mid)
    {
        margo_timer_list_free(mid, mid->timer_list);
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
    if(g_num_margo_instances_mtx != ABT_MUTEX_NULL && g_num_margo_instances == 0) {
        ABT_mutex_free(&g_num_margo_instances_mtx);
        g_num_margo_instances_mtx = ABT_MUTEX_NULL;
        if(g_margo_abt_init) ABT_finalize();
    }
    return MARGO_INSTANCE_NULL;
}

margo_instance_id margo_init_pool(ABT_pool progress_pool, ABT_pool handler_pool,
    hg_context_t *hg_context)
{
    int ret;
    hg_return_t hret;
    struct margo_instance *mid;

    /* set input offset to include breadcrumb information in Mercury requests */
    hret = HG_Class_set_input_offset(HG_Context_get_class(hg_context), sizeof(uint64_t));
    /* this should not ever fail */
    assert(hret == HG_SUCCESS);

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
    mid->prefinalize_cb = NULL;
    mid->enable_remote_shutdown = 0;

    mid->pending_operations = 0;
    ABT_mutex_create(&mid->pending_operations_mtx);
    mid->finalize_requested = 0;

    ABT_mutex_create(&mid->diag_rpc_mutex);

    mid->timer_list = margo_timer_list_create();
    if(mid->timer_list == NULL) goto err;

    /* initialize the handle cache */
    hret = margo_handle_cache_init(mid);
    if(hret != HG_SUCCESS) goto err;

    /* register thread local key to track RPC breadcrumbs across threads */
    /* NOTE: we are registering a global key, even though init could be called
     * multiple times for different margo instances.  As of May 2019 this doesn't
     * seem to be a problem to call ABT_key_create() multiple times.
     */
    ret = ABT_key_create(free, &rpc_breadcrumb_key);
    if(ret != 0)
        goto err;

    ret = ABT_key_create(free, &target_timing_key);
    if(ret != 0)
        goto err;

    ret = ABT_thread_create(mid->progress_pool, hg_progress_fn, mid, 
        ABT_THREAD_ATTR_NULL, &mid->hg_progress_tid);
    if(ret != 0) goto err;

    mid->shutdown_rpc_id = MARGO_REGISTER(mid, "__shutdown__", 
            void, margo_shutdown_out_t, remote_shutdown_ult);

    /* increment the number of margo instances */
    if(g_num_margo_instances_mtx == ABT_MUTEX_NULL)
        ABT_mutex_create(&g_num_margo_instances_mtx);
    ABT_mutex_lock(g_num_margo_instances_mtx);
    g_num_margo_instances += 1;
    ABT_mutex_unlock(g_num_margo_instances_mtx);

    return mid;

err:
    if(mid)
    {
        margo_handle_cache_destroy(mid);
        margo_timer_list_free(mid, mid->timer_list);
        ABT_mutex_free(&mid->finalize_mutex);
        ABT_cond_free(&mid->finalize_cond);
        ABT_mutex_free(&mid->pending_operations_mtx);
        ABT_mutex_free(&mid->diag_rpc_mutex);
        free(mid);
    }
    return MARGO_INSTANCE_NULL;
}

static void margo_cleanup(margo_instance_id mid)
{
    int i;
    struct margo_registered_rpc *next_rpc;

    /* call finalize callbacks */
    struct margo_finalize_cb* fcb = mid->finalize_cb;
    while(fcb) {
        mid->finalize_cb = fcb->next;
        (fcb->callback)(fcb->uargs);
        struct margo_finalize_cb* tmp = fcb;
        fcb = mid->finalize_cb;
        free(tmp);
    }

    ABT_mutex_free(&mid->finalize_mutex);
    ABT_cond_free(&mid->finalize_cond);
    ABT_mutex_free(&mid->pending_operations_mtx);
    ABT_mutex_free(&mid->diag_rpc_mutex);

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

    /* TODO: technically we could/should call ABT_key_free() for
     * rpc_breadcrumb_key.  We can't do that here, though, because the key is
     * global, not local to this mid.
     */

    if (mid->margo_init)
    {
        if (mid->hg_context)
            HG_Context_destroy(mid->hg_context);
        if (mid->hg_class)
            HG_Finalize(mid->hg_class);

        if(g_num_margo_instances_mtx != ABT_MUTEX_NULL) {
            ABT_mutex_lock(g_num_margo_instances_mtx);
            g_num_margo_instances -= 1;
            if(g_num_margo_instances > 0) {
                ABT_mutex_unlock(g_num_margo_instances_mtx);
            } else {
                ABT_mutex_unlock(g_num_margo_instances_mtx);
                ABT_mutex_free(&g_num_margo_instances_mtx);
                g_num_margo_instances_mtx = ABT_MUTEX_NULL;
                if(g_margo_abt_init) ABT_finalize();
            }
        }
    }

    while(mid->registered_rpcs)
    {
        next_rpc = mid->registered_rpcs->next;
        free(mid->registered_rpcs);
        mid->registered_rpcs = next_rpc;
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

    /* before exiting the progress loop, pre-finalize callbacks need to be called */
    struct margo_finalize_cb* fcb = mid->prefinalize_cb;
    while(fcb) {
        mid->prefinalize_cb = fcb->next;
        (fcb->callback)(fcb->uargs);
        struct margo_finalize_cb* tmp = fcb;
        fcb = mid->prefinalize_cb;
        free(tmp);
    }

    /* tell progress thread to wrap things up */
    mid->hg_progress_shutdown_flag = 1;

    /* wait for it to shutdown cleanly */
    ABT_thread_join(mid->hg_progress_tid);
    ABT_thread_free(&mid->hg_progress_tid);

    /* shut down pending timers */
    margo_timer_list_free(mid, mid->timer_list);

    if(mid->profile_enabled) {
      ABT_thread_join(mid->sparkline_data_collection_tid);
      ABT_thread_free(&mid->sparkline_data_collection_tid);
      margo_profile_dump(mid, "profile", 1);
    }
    
    if(mid->diag_enabled) 
      margo_diag_dump(mid, "profile", 1);

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

void margo_push_prefinalize_callback(
            margo_instance_id mid,
            margo_finalize_callback_t cb,
            void* uargs)
{
    margo_provider_push_prefinalize_callback(
            mid,
            NULL,
            cb,
            uargs);
}

int margo_pop_prefinalize_callback(
                    margo_instance_id mid)
{   
    return margo_provider_pop_prefinalize_callback(mid, NULL);
}

int margo_top_prefinalize_callback(
                    margo_instance_id mid,
                    margo_finalize_callback_t *cb,
                    void** uargs)
{   
    return margo_provider_top_prefinalize_callback(mid, NULL, cb, uargs);
}

void margo_provider_push_prefinalize_callback(
            margo_instance_id mid,
            const void* owner,
            margo_finalize_callback_t cb,
            void* uargs)
{
    if(cb == NULL) return;

    struct margo_finalize_cb* fcb = 
        (struct margo_finalize_cb*)malloc(sizeof(*fcb));
    fcb->owner    = owner;
    fcb->callback = cb;
    fcb->uargs    = uargs;

    struct margo_finalize_cb* next = mid->prefinalize_cb;
    fcb->next = next;
    mid->prefinalize_cb = fcb;
}

int margo_provider_top_prefinalize_callback(
            margo_instance_id mid,
            const void* owner,
            margo_finalize_callback_t *cb,
            void** uargs)
{
    struct margo_finalize_cb* prev = NULL;
    struct margo_finalize_cb* fcb  =  mid->prefinalize_cb;
    while(fcb != NULL && fcb->owner != owner) {
        prev = fcb;
        fcb = fcb->next;
    }
    if(fcb == NULL) return 0;
    if(prev == NULL) {
        mid->prefinalize_cb = fcb->next;
    } else {
        prev->next = fcb->next;
    }
    if(cb) *cb = fcb->callback;
    if(uargs) *uargs = fcb->uargs;
    return 1;
}

int margo_provider_pop_prefinalize_callback(
            margo_instance_id mid,
            const void* owner)
{
    struct margo_finalize_cb* prev = NULL;
    struct margo_finalize_cb* fcb  =  mid->prefinalize_cb;
    while(fcb != NULL && fcb->owner != owner) {
        prev = fcb;
        fcb = fcb->next;
    }
    if(fcb == NULL) return 0;
    if(prev == NULL) {
        mid->prefinalize_cb = fcb->next;
    } else {
        prev->next = fcb->next;
    }
    free(fcb);
    return 1;
}

void margo_push_finalize_callback(
            margo_instance_id mid,
            margo_finalize_callback_t cb,
            void* uargs)
{
    margo_provider_push_finalize_callback(
            mid,
            NULL,
            cb,
            uargs);
}

int margo_pop_finalize_callback(
                    margo_instance_id mid)
{   
    return margo_provider_pop_finalize_callback(mid, NULL);
}

int margo_top_finalize_callback(
                    margo_instance_id mid,
                    margo_finalize_callback_t *cb,
                    void** uargs)
{   
    return margo_provider_top_finalize_callback(mid, NULL, cb, uargs);
}

void margo_provider_push_finalize_callback(
            margo_instance_id mid,
            const void* owner,
            margo_finalize_callback_t cb,
            void* uargs)
{
    if(cb == NULL) return;

    struct margo_finalize_cb* fcb = 
        (struct margo_finalize_cb*)malloc(sizeof(*fcb));
    fcb->owner    = owner;
    fcb->callback = cb;
    fcb->uargs    = uargs;

    struct margo_finalize_cb* next = mid->finalize_cb;
    fcb->next = next;
    mid->finalize_cb = fcb;
}

int margo_provider_pop_finalize_callback(
            margo_instance_id mid,
            const void* owner)
{
    struct margo_finalize_cb* prev = NULL;
    struct margo_finalize_cb* fcb  =  mid->finalize_cb;
    while(fcb != NULL && fcb->owner != owner) {
        prev = fcb;
        fcb = fcb->next;
    }
    if(fcb == NULL) return 0;
    if(prev == NULL) {
        mid->finalize_cb = fcb->next;
    } else {
        prev->next = fcb->next;
    }
    free(fcb);
    return 1;
}

int margo_provider_top_finalize_callback(
            margo_instance_id mid,
            const void* owner,
            margo_finalize_callback_t *cb,
            void** uargs)
{
    struct margo_finalize_cb* prev = NULL;
    struct margo_finalize_cb* fcb  =  mid->finalize_cb;
    while(fcb != NULL && fcb->owner != owner) {
        prev = fcb;
        fcb = fcb->next;
    }
    if(fcb == NULL) return 0;
    if(prev == NULL) {
        mid->finalize_cb = fcb->next;
    } else {
        prev->next = fcb->next;
    }
    if(cb) *cb = fcb->callback;
    if(uargs) *uargs = fcb->uargs;
    return 1;
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
    struct margo_registered_rpc * tmp_rpc;

    assert(provider_id <= MARGO_MAX_PROVIDER_ID);
    
    id = gen_id(func_name, provider_id);

    if(mid->profile_enabled)
    {
        /* track information about this rpc registration for debugging and
         * profiling
         */
        tmp_rpc = calloc(1, sizeof(*tmp_rpc));
        if(!tmp_rpc)
            return(0);
        tmp_rpc->id = id;
        tmp_rpc->rpc_breadcrumb_fragment = id >> (__MARGO_PROVIDER_ID_SIZE*8);
        tmp_rpc->rpc_breadcrumb_fragment &= 0xffff;
        strncpy(tmp_rpc->func_name, func_name, 63);
        tmp_rpc->next = mid->registered_rpcs;
        mid->registered_rpcs = tmp_rpc;
        mid->num_registered_rpcs += 1;
    }

    ret = margo_register_internal(mid, id, in_proc_cb, out_proc_cb, rpc_cb, pool);
    if(ret == 0)
    {
        if(mid->profile_enabled)
        {
            mid->registered_rpcs = tmp_rpc->next;
            free(tmp_rpc);
        }
        return(0);
    }

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

/* Mercury 2.x provides two versions of lookup (async and sync).  If a
 * synchronous lookup call is available then we do not need this callback.
 */
#ifndef HG_Addr_lookup
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
#endif

hg_return_t margo_addr_lookup(
    margo_instance_id mid,
    const char   *name,
    hg_addr_t    *addr)
{
    hg_return_t hret;

#ifdef HG_Addr_lookup
    /* Mercury 2.x provides two versions of lookup (async and sync).  Choose the
     * former if available to avoid context switch
     */
    hret = HG_Addr_lookup2(mid->hg_class, name, addr);

#else /* !defined HG_Addr_lookup */
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
#endif

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
    margo_request req = (margo_request)(info->arg);
    margo_instance_id mid;

    if(hret == HG_CANCELED && req->timer) {
        hret = HG_TIMEOUT;
    }

    /* remove timer if there is one and it is still in place (i.e., not timed out) */
    if(hret != HG_TIMEOUT && req->timer && req->handle) {
        margo_instance_id mid = margo_hg_handle_get_instance(req->handle);
        margo_timer_destroy(mid, req->timer);
    }
    if(req->timer) {
        free(req->timer);
    }

    if(req->rpc_breadcrumb != 0)
    {
        /* This is the callback from an HG_Forward call.  Track RPC timing
         * information.
         */
        mid = margo_hg_handle_get_instance(req->handle);
        assert(mid);

        if(mid->profile_enabled) {
          /* 0 here indicates this is a origin-side call */
          margo_breadcrumb_measure(mid, req->rpc_breadcrumb, req->start_time, 0, req->provider_id, req->server_addr_hash, req->handle);
        }
    }

    /* propagate return code out through eventual */
    ABT_eventual_set(req->eventual, &hret, sizeof(hret));
    
    return(HG_SUCCESS);
}

static hg_return_t margo_wait_internal(margo_request req)
{
    hg_return_t* waited_hret;
    hg_return_t  hret;

    ABT_eventual_wait(req->eventual, (void**)&waited_hret);
    hret = *waited_hret;
    ABT_eventual_free(&(req->eventual));

    return(hret);
}

static void margo_forward_timeout_cb(void *arg)
{
    margo_request req = (margo_request)arg;
    /* cancel the Mercury op if the forward timed out */
    HG_Cancel(req->handle);
    return;
}

static hg_return_t margo_provider_iforward_internal(
    uint16_t provider_id,
    hg_handle_t handle,
    double timeout_ms,
    void *in_struct,
    margo_request req) /* the request should have been allocated */
{
    hg_return_t hret = HG_TIMEOUT;
    ABT_eventual eventual;
    int ret;
    const struct hg_info* hgi; 
    hg_id_t id;
    hg_proc_cb_t in_cb, out_cb;
    hg_bool_t flag;
    margo_instance_id mid = margo_hg_handle_get_instance(handle);
    uint64_t *rpc_breadcrumb;
    char addr_string[128];
    hg_size_t addr_string_sz = 128;

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
    
    req->timer = NULL;
    req->eventual = eventual;
    req->handle = handle;

    if(timeout_ms > 0) {
        /* set a timer object to expire when this forward times out */
        req->timer = calloc(1, sizeof(*(req->timer)));
        if(!(req->timer)) {
            ABT_eventual_free(&eventual);
            return(HG_NOMEM_ERROR);
        }
        margo_timer_init(mid, req->timer, margo_forward_timeout_cb,
                         req, timeout_ms);
    }

    /* add rpc breadcrumb to outbound request; this will be used to track
     * rpc statistics.
     */

    req->rpc_breadcrumb = 0;
    if(mid->profile_enabled) {
        ret = HG_Get_input_buf(handle, (void**)&rpc_breadcrumb, NULL);
        if(ret != HG_SUCCESS)
            return(ret);
        req->rpc_breadcrumb = margo_breadcrumb_set(hgi->id);
        /* LE encoding */
        *rpc_breadcrumb = htole64(req->rpc_breadcrumb);

        req->start_time = ABT_get_wtime();
    
       /* add information about the server and provider servicing the request */
        req->provider_id = provider_id; /*store id of provider servicing the request */
        const struct hg_info * inf = HG_Get_info(req->handle);
        margo_addr_to_string(mid, addr_string, &addr_string_sz, inf->addr);
        HASH_JEN(addr_string, strlen(addr_string), req->server_addr_hash); /*record server address in the breadcrumb */
    }

    return HG_Forward(handle, margo_cb, (void*)req, in_struct);
}

hg_return_t margo_provider_forward(
    uint16_t provider_id,
    hg_handle_t handle,
    void *in_struct)
{
    return margo_provider_forward_timed(provider_id, handle, in_struct, 0);
}

hg_return_t margo_provider_iforward(
    uint16_t provider_id,
    hg_handle_t handle,
    void *in_struct,
    margo_request* req)
{
    return margo_provider_iforward_timed(provider_id, handle, in_struct, 0, req);
}

hg_return_t margo_provider_forward_timed(
    uint16_t provider_id,
    hg_handle_t handle,
    void *in_struct,
    double timeout_ms)
{
    hg_return_t hret;
    struct margo_request_struct reqs;
    hret = margo_provider_iforward_internal(provider_id, handle, timeout_ms, in_struct, &reqs);
    if(hret != HG_SUCCESS)
        return hret;
    return margo_wait_internal(&reqs);
}

hg_return_t margo_provider_iforward_timed(
    uint16_t provider_id,
    hg_handle_t handle,
    void *in_struct,
    double timeout_ms,
    margo_request* req)
{
    hg_return_t hret;
    margo_request tmp_req = calloc(1, sizeof(*tmp_req));
    if(!tmp_req) {
        return HG_NOMEM_ERROR;
    }
    hret = margo_provider_iforward_internal(provider_id, handle, timeout_ms, in_struct, tmp_req);
    if(hret !=  HG_SUCCESS) {
        free(tmp_req);
        return hret;
    }
    *req = tmp_req;
    return HG_SUCCESS;
}

hg_return_t margo_wait(margo_request req)
{
    hg_return_t hret = margo_wait_internal(req);
    free(req);
    return hret;
}

int margo_test(margo_request req, int* flag)
{
    return ABT_eventual_test(req->eventual, NULL, flag);
}

hg_return_t margo_wait_any(
    size_t count, margo_request* req, size_t* index)
{
    // XXX this is an active loop, we should change it
    // when Argobots provide an ABT_eventual_wait_any
    size_t i;
    int ret;
    int flag = 0;
    int has_pending_requests;
try_again:
    has_pending_requests = 0;
    for(i = 0; i < count; i++) {
        if(req[i] == MARGO_REQUEST_NULL)
            continue;
        else
            has_pending_requests = 1;
        ret = margo_test(req[i], &flag);
        if(ret != ABT_SUCCESS) {
            *index = i;
            return HG_OTHER_ERROR;
        }
        if(flag) {
            *index = i;
            return margo_wait(req[i]);
        }
    }
    ABT_thread_yield();
    if(has_pending_requests)
        goto try_again;
    *index = count;
    return HG_SUCCESS;
}

static hg_return_t margo_irespond_internal(
    hg_handle_t handle,
    void *out_struct,
    margo_request req) /* should have been allocated */
{
    int ret;
    ret = ABT_eventual_create(sizeof(hg_return_t), &(req->eventual));
    if(ret != 0)
    {
        return(HG_NOMEM_ERROR);
    }
    req->handle = handle;
    req->timer = NULL;
    req->start_time = ABT_get_wtime();
    req->rpc_breadcrumb = 0;

    return HG_Respond(handle, margo_cb, (void*)req, out_struct);
}

hg_return_t margo_respond(
    hg_handle_t handle,
    void *out_struct)
{

    /* retrieve the ULT-local key for this breadcrumb and add measurement to profile */
    struct margo_request_struct* treq;
    margo_instance_id mid = margo_hg_handle_get_instance(handle);
    if(mid->profile_enabled) {
      ABT_key_get(target_timing_key, (void**)(&treq));
      assert(treq != NULL);
    
      /* the "1" indicates that this a target-side breadcrumb */
      margo_breadcrumb_measure(mid, treq->rpc_breadcrumb, treq->start_time, 1, treq->provider_id, treq->server_addr_hash, handle);
    }

    hg_return_t hret;
    struct margo_request_struct reqs;
    hret = margo_irespond_internal(handle, out_struct, &reqs);
    if(hret != HG_SUCCESS)
        return hret;
    return margo_wait_internal(&reqs);
}

hg_return_t margo_irespond(
    hg_handle_t handle,
    void *out_struct,
    margo_request* req)
{
    hg_return_t hret;
    margo_request tmp_req = calloc(1, sizeof(*tmp_req));
    if(!tmp_req) {
        return(HG_NOMEM_ERROR);
    }
    hret = margo_irespond_internal(handle, out_struct, tmp_req);
    if(hret != HG_SUCCESS) {
        free(req);
        return hret;
    }
    *req = tmp_req;
    return HG_SUCCESS;
}

hg_return_t margo_bulk_create(
    margo_instance_id mid,
    hg_uint32_t count,
    void **buf_ptrs,
    const hg_size_t *buf_sizes,
    hg_uint8_t flags,
    hg_bulk_t *handle)
{
    hg_return_t hret;
    double tm1, tm2;
    int diag_enabled = mid->diag_enabled;

    if(diag_enabled) tm1 = ABT_get_wtime();
    hret = HG_Bulk_create(mid->hg_class, count,
        buf_ptrs, buf_sizes, flags, handle);
    if(diag_enabled)
    {
        tm2 = ABT_get_wtime();
        __DIAG_UPDATE(mid->diag_bulk_create_elapsed, (tm2-tm1));
    }

    return(hret);
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

static hg_return_t margo_bulk_itransfer_internal(
    margo_instance_id mid,
    hg_bulk_op_t op,
    hg_addr_t origin_addr,
    hg_bulk_t origin_handle,
    size_t origin_offset,
    hg_bulk_t local_handle,
    size_t local_offset,
    size_t size,
    margo_request req) /* should have been allocated */
{
    hg_return_t hret = HG_TIMEOUT;
    int ret;

    ret = ABT_eventual_create(sizeof(hret), &(req->eventual));
    if(ret != 0)
    {
        return(HG_NOMEM_ERROR);        
    }
    req->timer = NULL;
    req->handle = HG_HANDLE_NULL;
    req->start_time = ABT_get_wtime();
    req->rpc_breadcrumb = 0;

    hret = HG_Bulk_transfer(mid->hg_context, margo_cb,
        (void*)req, op, origin_addr, origin_handle, origin_offset, local_handle,
        local_offset, size, HG_OP_ID_IGNORE);

    return(hret);
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
    struct margo_request_struct reqs;
    hg_return_t hret = margo_bulk_itransfer_internal(mid,op,origin_addr,
                          origin_handle, origin_offset, local_handle,
                          local_offset, size, &reqs);
    if(hret != HG_SUCCESS)
        return hret;
    return margo_wait_internal(&reqs);
}

hg_return_t margo_bulk_parallel_transfer(
    margo_instance_id mid,
    hg_bulk_op_t op,
    hg_addr_t origin_addr,
    hg_bulk_t origin_handle,
    size_t origin_offset,
    hg_bulk_t local_handle,
    size_t local_offset,
    size_t size,
    size_t chunk_size)
{  
    unsigned i, j;
    hg_return_t hret      = HG_SUCCESS;
    hg_return_t hret_wait = HG_SUCCESS;
    hg_return_t hret_xfer = HG_SUCCESS;
    size_t remaining_size = size;

    if(chunk_size == 0)
        return HG_INVALID_PARAM;

    size_t count = size/chunk_size;
    if(count*chunk_size < size) count += 1;
    struct margo_request_struct* reqs = calloc(count, sizeof(*reqs));

    for(i = 0; i < count; i++) {
        if(remaining_size < chunk_size) chunk_size = remaining_size;
        hret = margo_bulk_itransfer_internal(mid, op, origin_addr,
                          origin_handle, origin_offset, local_handle,
                          local_offset, chunk_size, reqs+i);
        if(hret_xfer != HG_SUCCESS) {
            hret = hret_xfer;
            goto wait;
        }
        origin_offset += chunk_size;
        local_offset += chunk_size;
    }

wait:
    for(j = 0; j < i; j++) {
         hret_wait = margo_wait_internal(reqs + j);
         if(hret == HG_SUCCESS && hret_wait != HG_SUCCESS) {
            hret = hret_wait;
            goto finish;
         }
    }
finish:
    free(reqs);
    return hret;
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
    margo_request tmp_req = calloc(1, sizeof(*tmp_req));
    if(!tmp_req) {
        return(HG_NOMEM_ERROR);
    }
    hg_return_t hret = margo_bulk_itransfer_internal(mid,op,origin_addr,
            origin_handle, origin_offset, local_handle,
            local_offset, size, tmp_req);
    if(hret != HG_SUCCESS) {
        free(tmp_req);
        return hret;
    }

    *req = tmp_req;

    return(hret);
}

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

/* dedicated thread function to collect sparkline data */
static void sparkline_data_collection_fn(void* foo)
{
    struct margo_instance *mid = (struct margo_instance *)foo;
    struct diag_data *stat, *tmp;

    /* double check that profile collection should run, else, close this ULT */
    if(!mid->profile_enabled) {
      ABT_thread_join(mid->sparkline_data_collection_tid);
      ABT_thread_free(&mid->sparkline_data_collection_tid);
    }

    while(!mid->hg_progress_shutdown_flag)
    {
        margo_thread_sleep(mid, MARGO_SPARKLINE_TIMESLICE*1000);
        HASH_ITER(hh, mid->diag_rpc, stat, tmp)
        {

          if(mid->sparkline_index > 0 && mid->sparkline_index < 100) {
            stat->sparkline_time[mid->sparkline_index] = stat->stats.cumulative - stat->sparkline_time[mid->sparkline_index - 1];
            stat->sparkline_count[mid->sparkline_index] = stat->stats.count - stat->sparkline_count[mid->sparkline_index - 1];
          } else if(mid->sparkline_index == 0) {
            stat->sparkline_time[mid->sparkline_index] = stat->stats.cumulative;
            stat->sparkline_count[mid->sparkline_index] = stat->stats.count;
          } else {
            //Drop!
          }
        }
        mid->sparkline_index++;
        mid->previous_sparkline_data_collection_time = ABT_get_wtime();
   }

   return;
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
    double tm1, tm2;
    int diag_enabled = 0;
    unsigned int pending;

    while(!mid->hg_progress_shutdown_flag)
    {
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
        } while((ret == HG_SUCCESS) && actual_count && !mid->hg_progress_shutdown_flag);

        /* Check to see if there are any runnable ULTs in the pool now.  If
         * so, then we yield here to allow them a chance to execute.  
         * We check here because new ULTs may now be elegible as a result of
         * being spawned by the trigger, but existing ones also may have been
         * activated by an external event.
         *
         * NOTE: the output size value does not count the calling ULT itself, 
         * because it is not technically in the pool as a runnable thread at 
         * the moment.
         */
        ABT_pool_get_size(mid->progress_pool, &size);
        if(size)
            ABT_thread_yield();

        /* Are there any other threads in this pool that *might* need to 
         * execute at some point in the future?  If so, then it's not
         * necessarily safe for Mercury to sleep here in progress.  It
         * doesn't matter whether they are runnable now or not, because an
         * external event could resume them.
         *
         * NOTE: we use ABT_pool_get_total_size() rather than
         * ABT_pool_get_size() in order to include suspended ULTs in our
         * count.  Note that this function *does* count the caller, so it
         * will always be at least one, unlike ABT_pool_get_size().
         */
        ABT_pool_get_total_size(mid->progress_pool, &size);

        /* Are there any RPCs in flight, regardless of what pool they were
         * issued to?  If so, then we also cannot block in Mercury, because
         * they may issue self forward() calls that cannot complete until we
         * get through this progress/trigger cycle
         */
        ABT_mutex_lock(mid->pending_operations_mtx);
        pending = mid->pending_operations;
        ABT_mutex_unlock(mid->pending_operations_mtx);

        /* Note that if profiling is enabled then there will be one extra
         * ULT in the progress pool.  We don't need to worry about that one;
         * a margo timer will wake the progress loop when it needs
         * attention.
         */
        if(pending || (mid->profile_enabled && size > 2) || (!mid->profile_enabled && size > 1))
        {
            /* TODO: a custom ABT scheduler could optimize this further by
             * delaying Mercury progress until all other runnable ULTs have
             * been given a chance to execute.  This will often happen
             * anyway, but not guaranteed.
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

void margo_profile_start(margo_instance_id mid)
{
    mid->profile_enabled = 1;
}

void margo_diag_stop(margo_instance_id mid)
{
    mid->diag_enabled = 0;
}

void margo_profile_stop(margo_instance_id mid)
{
    mid->profile_enabled = 0;
}

static void print_diag_data(margo_instance_id mid, FILE *file, const char* name, const char *description, struct diag_data *data)
{
    double avg;

    if(data->stats.count != 0)
        avg = data->stats.cumulative/data->stats.count;
    else
        avg = 0;

    fprintf(file, "%s,%.9f,%.9f,%.9f,%.9f,%lu\n", name, avg, data->stats.cumulative, data->stats.min, data->stats.max, data->stats.count);

    return;
}

static void print_profile_data(margo_instance_id mid, FILE *file, const char* name, const char *description, struct diag_data *data)
{
    double avg;
    int i;

    if(data->stats.count != 0)
        avg = data->stats.cumulative/data->stats.count;
    else
        avg = 0;

    /* first line is breadcrumb data */
    fprintf(file, "%s,%.9f,%lu,%lu,%d,%.9f,%.9f,%.9f,%lu,%lu,%lu,%lu,%lu,%lu,%lu\n", name, avg, data->key.rpc_breadcrumb, data->key.addr_hash, data->type, data->stats.cumulative, data->stats.min, data->stats.max, data->stats.count, data->stats.abt_pool_size_hwm, data->stats.abt_pool_size_lwm, data->stats.abt_pool_size_cumulative, data->stats.abt_pool_total_size_hwm, data->stats.abt_pool_total_size_lwm, data->stats.abt_pool_total_size_cumulative);

    /* second line is sparkline data for the given breadcrumb*/
    fprintf(file, "%s,%d;", name, data->type);
    for(i = 0; i < mid->sparkline_index; i++)
      fprintf(file, "%.9f,%.9f, %d;", data->sparkline_time[i], data->sparkline_count[i], i);
    fprintf(file,"\n");

    return;
}

/* copy out the entire list of breadcrumbs on this margo instance */
void margo_breadcrumb_snapshot(margo_instance_id mid, struct margo_breadcrumb_snapshot* snap)
{
  assert(mid->profile_enabled);
  struct diag_data *dd, *tmp;
  struct margo_breadcrumb *tmp_bc;

#if 0
  fprintf(stderr, "Taking a snapshot\n");
#endif
  
  snap->ptr = calloc(1, sizeof(struct margo_breadcrumb));
  tmp_bc = snap->ptr;

  HASH_ITER(hh, mid->diag_rpc, dd, tmp)
  {
#if 0
    fprintf(stderr, "Copying out RPC breadcrumb %d\n", dd->rpc_breadcrumb);
#endif
    tmp_bc->stats.min = dd->stats.min;
    tmp_bc->stats.max = dd->stats.max;
    tmp_bc->type = dd->type;
    tmp_bc->key = dd->key;
    tmp_bc->stats.count = dd->stats.count;
    tmp_bc->stats.cumulative = dd->stats.cumulative;

    tmp_bc->stats.abt_pool_total_size_hwm = dd->stats.abt_pool_total_size_hwm;
    tmp_bc->stats.abt_pool_total_size_lwm = dd->stats.abt_pool_total_size_lwm;
    tmp_bc->stats.abt_pool_total_size_cumulative = dd->stats.abt_pool_total_size_cumulative;
    tmp_bc->stats.abt_pool_size_hwm = dd->stats.abt_pool_size_hwm;
    tmp_bc->stats.abt_pool_size_lwm = dd->stats.abt_pool_size_lwm;
    tmp_bc->stats.abt_pool_size_cumulative = dd->stats.abt_pool_size_cumulative;

    tmp_bc->next = calloc(1, sizeof(struct margo_breadcrumb));
    tmp_bc = tmp_bc->next;
    tmp_bc->next = NULL;
  }

}
 
void margo_diag_dump(margo_instance_id mid, const char* file, int uniquify)
{
    FILE *outfile;
    time_t ltime;
    char revised_file_name[256] = {0};
    char * name;
    uint64_t hash;

    if (!mid->diag_enabled)
        return;

    if(uniquify)
    {
        char hostname[128] = {0};
        int pid;

        gethostname(hostname, 128);
        pid = getpid();

        sprintf(revised_file_name, "%s-%s-%d.diag", file, hostname, pid);
    }

    else
    {
        sprintf(revised_file_name, "%s.diag", file);
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
    GET_SELF_ADDR_STR(mid, name);
    HASH_JEN(name, strlen(name), hash); /*record own address in the breadcrumb */
    fprintf(outfile, "#Addr Hash and Address Name: %lu,%s\n", hash, name);
    fprintf(outfile, "# %s\n", ctime(&ltime));
    fprintf(outfile, "# Function Name, Average Time Per Call, Cumulative Time, Highwatermark, Lowwatermark, Call Count\n");
    
    print_diag_data(mid, outfile, "trigger_elapsed", 
        "Time consumed by HG_Trigger()", 
        &mid->diag_trigger_elapsed);
    print_diag_data(mid, outfile, "progress_elapsed_zero_timeout", 
        "Time consumed by HG_Progress() when called with timeout==0", 
        &mid->diag_progress_elapsed_zero_timeout);
    print_diag_data(mid, outfile, "progress_elapsed_nonzero_timeout", 
        "Time consumed by HG_Progress() when called with timeout!=0", 
        &mid->diag_progress_elapsed_nonzero_timeout);
    print_diag_data(mid, outfile, "bulk_create_elapsed",
        "Time consumed by HG_Bulk_create()",
        &mid->diag_bulk_create_elapsed);

    if(outfile != stdout)
        fclose(outfile);
    
    return;
}

void margo_profile_dump(margo_instance_id mid, const char* file, int uniquify)
{
    FILE *outfile;
    time_t ltime;
    char revised_file_name[256] = {0};
    struct diag_data *dd, *tmp;
    char rpc_breadcrumb_str[256] = {0};
    struct margo_registered_rpc *tmp_rpc;
    char * name;
    uint64_t hash;

    assert(mid->profile_enabled);

    if(uniquify)
    {
        char hostname[128] = {0};
        int pid;

        gethostname(hostname, 128);
        pid = getpid();

        sprintf(revised_file_name, "%s-%s-%d.csv", file, hostname, pid);
    }

    else
    {
        sprintf(revised_file_name, "%s.csv", file);
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

    fprintf(outfile, "%u\n", mid->num_registered_rpcs);
    GET_SELF_ADDR_STR(mid, name);
    HASH_JEN(name, strlen(name), hash); /*record own address in the breadcrumb */
    
    fprintf(outfile, "%lu,%s\n", hash, name);

    tmp_rpc = mid->registered_rpcs;
    while(tmp_rpc)
    {
        fprintf(outfile, "0x%.4lx,%s\n", tmp_rpc->rpc_breadcrumb_fragment, tmp_rpc->func_name);
        tmp_rpc = tmp_rpc->next;
    }
    
    HASH_ITER(hh, mid->diag_rpc, dd, tmp)
    {
        int i;
        uint64_t tmp_breadcrumb;
        for(i=0; i<4; i++)
        {
            tmp_breadcrumb = dd->rpc_breadcrumb;
            tmp_breadcrumb >>= (i*16);
            tmp_breadcrumb &= 0xffff;

            if(!tmp_breadcrumb) continue;

            if(i==3)
                sprintf(&rpc_breadcrumb_str[i*7], "0x%.4lx", tmp_breadcrumb);
            else
                sprintf(&rpc_breadcrumb_str[i*7], "0x%.4lx ", tmp_breadcrumb);
        }
        print_profile_data(mid, outfile, rpc_breadcrumb_str, "RPC statistics", dd);
    }

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
        case MARGO_PARAM_ENABLE_PROFILING:
            mid->profile_enabled = (*((const unsigned int*)param));
            break;
        case MARGO_PARAM_ENABLE_DIAGNOSTICS:
            mid->diag_enabled = (*((const unsigned int*)param));
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

/* sets the value of a breadcrumb, to be called just before issuing an RPC */
static uint64_t margo_breadcrumb_set(hg_id_t rpc_id)
{
    uint64_t *val;
    uint64_t tmp;

    ABT_key_get(rpc_breadcrumb_key, (void**)(&val));
    if(val == NULL)
    {
        /* key not set yet on this ULT; we need to allocate a new one
         * with all zeroes for initial value of breadcrumb and idx
         */
        /* NOTE: treating this as best effort; just return 0 if it fails */
        val = calloc(1, sizeof(*val));
        if(!val)
            return(0);
    }

    /* NOTE: an rpc_id (after mux'ing) has provider in low order bits and
     * base rpc_id in high order bits.  After demuxing, a base_id has zeroed
     * out low bits.  So regardless of whether the rpc_id is a base_id or a
     * mux'd id, either way we need to shift right to get either the
     * provider id (or the space reserved for it) out of the way, then mask
     * off 16 bits for use as a breadcrumb.
     */
    tmp = rpc_id >> (__MARGO_PROVIDER_ID_SIZE*8);
    tmp &= 0xffff;

    /* clear low 16 bits of breadcrumb */
    *val = (*val >> 16) << 16;

    /* combine them, so that we have low order 16 of rpc id and high order
     * bits of previous breadcrumb */
    *val |= tmp;

    ABT_key_set(rpc_breadcrumb_key, val);

    return *val;
}

/* records statistics for a breadcrumb, to be used after completion of an
 * RPC, both on the origin as well as on the target */
static void margo_breadcrumb_measure(margo_instance_id mid, uint64_t rpc_breadcrumb, double start, breadcrumb_type type, uint16_t provider_id, uint64_t hash, hg_handle_t h)
{
    struct diag_data *stat;
    double end, elapsed;
    uint16_t t = (type == origin) ? 2: 1;
    uint64_t hash_;

    __uint128_t x = 0;

    /* IMPT NOTE: presently not adding provider_id to the breadcrumb,
       thus, the breadcrumb represents cumulative information for all providers
       offering or making a certain RPC call on this Margo instance */

    /* Bake in information about whether or not this was an origin or target-side breadcrumb */
    hash_ = hash;
    hash_ = (hash_ >> 16) << 16;
    hash_ |= t;
  
    /* add in the server address */
    x = hash_;
    x = x << 64; 
    x |= rpc_breadcrumb;

    if(!mid->profile_enabled)
        return;

    end = ABT_get_wtime();
    elapsed = end-start;

    ABT_mutex_lock(mid->diag_rpc_mutex);

    HASH_FIND(hh, mid->diag_rpc, &x,
        sizeof(uint64_t)*2, stat);

    if(!stat)
    {
        /* we aren't tracking this breadcrumb yet; add it */
        stat = calloc(1, sizeof(*stat));
        if(!stat)
        {
            /* best effort; we return gracefully without recording stats if this
             * happens.
             */
            ABT_mutex_unlock(mid->diag_rpc_mutex);
            return;
        }

        stat->rpc_breadcrumb = rpc_breadcrumb;
        stat->type = type;
        stat->key.rpc_breadcrumb = rpc_breadcrumb;
        stat->key.addr_hash = hash;
        stat->key.provider_id = provider_id;
        stat->x = x;

        /* initialize pool stats for breadcrumb */
        stat->stats.abt_pool_size_lwm = 0x11111111; // Some high value
        stat->stats.abt_pool_size_cumulative = 0;
        stat->stats.abt_pool_size_hwm = -1;

        stat->stats.abt_pool_total_size_lwm = 0x11111111; // Some high value
        stat->stats.abt_pool_total_size_cumulative = 0;
        stat->stats.abt_pool_total_size_hwm = -1;
   
        /* initialize sparkline data */
        memset(stat->sparkline_time, 0.0, 100*sizeof(double));
        memset(stat->sparkline_count, 0.0, 100*sizeof(double));
 
        HASH_ADD(hh, mid->diag_rpc, x,
            sizeof(x), stat);
    }


    /* Argobots pool info */
    size_t s, s1;
    struct margo_rpc_data * margo_data;
    if(type) {
      const struct hg_info * info;
      info = HG_Get_info(h); 
      margo_data = (struct margo_rpc_data*)HG_Registered_data(mid->hg_class, info->id);
      if(margo_data && margo_data->pool != ABT_POOL_NULL) {
        ABT_pool_get_total_size(margo_data->pool, &s);
        ABT_pool_get_size(margo_data->pool, &s1);
      }
      else {
        ABT_pool_get_total_size(mid->handler_pool, &s);
        ABT_pool_get_size(mid->handler_pool, &s1);
      }

      stat->stats.abt_pool_size_hwm = stat->stats.abt_pool_size_hwm > (double)s1 ? stat->stats.abt_pool_size_hwm : s1;
      stat->stats.abt_pool_size_lwm = stat->stats.abt_pool_size_lwm < (double)s1 ? stat->stats.abt_pool_size_lwm : s1;
      stat->stats.abt_pool_size_cumulative += s1;

      stat->stats.abt_pool_total_size_hwm = stat->stats.abt_pool_total_size_hwm > (double)s ? stat->stats.abt_pool_total_size_hwm : s;
      stat->stats.abt_pool_total_size_lwm = stat->stats.abt_pool_total_size_lwm < (double)s ? stat->stats.abt_pool_total_size_lwm : s;
      stat->stats.abt_pool_total_size_cumulative += s;

    }
    /* Argobots pool info */

    stat->stats.count++;
    stat->stats.cumulative += elapsed;
    if(elapsed > stat->stats.max)
        stat->stats.max = elapsed;
    if(stat->stats.min == 0 || elapsed < stat->stats.min)
        stat->stats.min = elapsed;

    ABT_mutex_unlock(mid->diag_rpc_mutex);

    return;
}

static void margo_internal_breadcrumb_handler_set(uint64_t rpc_breadcrumb)
{
    uint64_t *val;

    ABT_key_get(rpc_breadcrumb_key, (void**)(&val));

    if(val == NULL)
    {
        /* key not set yet on this ULT; we need to allocate a new one */
        /* best effort; just return and don't set it if we can't allocate memory */
        val = malloc(sizeof(*val));
        if(!val)
            return;
    }
    *val = rpc_breadcrumb;

    ABT_key_set(rpc_breadcrumb_key, val);

    return;
}

void __margo_internal_pre_wrapper_hooks(margo_instance_id mid, hg_handle_t handle)
{
    hg_return_t ret;
    uint64_t *rpc_breadcrumb;
    const struct hg_info* info;
    struct margo_request_struct* req;

    ret = HG_Get_input_buf(handle, (void**)&rpc_breadcrumb, NULL);
    assert(ret == HG_SUCCESS);
    *rpc_breadcrumb = le64toh(*rpc_breadcrumb);
  
    /* add the incoming breadcrumb info to a ULT-local key if profiling is enabled */
    if(mid->profile_enabled) {

        ABT_key_get(target_timing_key, (void**)(&req));

        if(req == NULL)
        {
            req = calloc(1, sizeof(*req));
        }
    
        req->rpc_breadcrumb = *rpc_breadcrumb;

        req->timer = NULL;
        req->handle = handle;
        req->start_time = ABT_get_wtime(); /* measure start time */
        info = HG_Get_info(handle);
        req->provider_id = 0;
        req->provider_id += ((info->id) & (((1<<(__MARGO_PROVIDER_ID_SIZE*8))-1)));
        req->server_addr_hash = mid->self_addr_hash;
 
        /* Note: we use this opportunity to retrieve the incoming RPC
         * breadcrumb and put it in a thread-local argobots key.  It is
         * shifted down 16 bits so that if this handler in turn issues more
         * RPCs, there will be a stack showing the ancestry of RPC calls that
         * led to that point.
         */
        ABT_key_set(target_timing_key, req);
        margo_internal_breadcrumb_handler_set((*rpc_breadcrumb) << 16);
    }
}

void __margo_internal_post_wrapper_hooks(margo_instance_id mid)
{
    __margo_internal_decr_pending(mid);
    if(__margo_internal_finalize_requested(mid)) {
        margo_finalize(mid);
    }
}
