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
#include <time.h>
#include <math.h>
#include <json-c/json.h>

#include "margo.h"
#include "margo-abt-macros.h"
#include "margo-globals.h"
#include "margo-progress.h"
#include "margo-monitoring-internal.h"
#include "margo-handle-cache.h"
#include "margo-logging.h"
#include "margo-instance.h"
#include "margo-bulk-util.h"
#include "margo-timer-private.h"
#include "margo-serialization.h"
#include "margo-id.h"
#include "utlist.h"
#include "uthash.h"
#include "abtx_prof.h"

/* need endian macro shims for MacOS */
#ifdef __APPLE__
    #include <libkern/OSByteOrder.h>
    #define htobe16(x) OSSwapHostToBigInt16(x)
    #define htole16(x) OSSwapHostToLittleInt16(x)
    #define be16toh(x) OSSwapBigToHostInt16(x)
    #define le16toh(x) OSSwapLittleToHostInt16(x)

    #define htobe32(x) OSSwapHostToBigInt32(x)
    #define htole32(x) OSSwapHostToLittleInt32(x)
    #define be32toh(x) OSSwapBigToHostInt32(x)
    #define le32toh(x) OSSwapLittleToHostInt32(x)

    #define htobe64(x) OSSwapHostToBigInt64(x)
    #define htole64(x) OSSwapHostToLittleInt64(x)
    #define be64toh(x) OSSwapBigToHostInt64(x)
    #define le64toh(x) OSSwapLittleToHostInt64(x)
#endif /* __APPLE__ */

static void margo_rpc_data_free(void* ptr);

static hg_id_t margo_register_internal(margo_instance_id mid,
                                       const char*       name,
                                       hg_id_t           id,
                                       hg_proc_cb_t      in_proc_cb,
                                       hg_proc_cb_t      out_proc_cb,
                                       hg_rpc_cb_t       rpc_cb,
                                       ABT_pool          pool);

static hg_return_t check_error_in_output(hg_handle_t out);
static hg_return_t check_parent_id_in_input(hg_handle_t handle,
                                            hg_id_t*    parent_id);

margo_instance_id margo_init(const char* addr_str,
                             int         mode,
                             int         use_progress_thread,
                             int         rpc_thread_count)
{
    char config[1024];
    snprintf(config, 1024,
             "{ \"use_progress_thread\" : %s, \"rpc_thread_count\" : %d }",
             use_progress_thread ? "true" : "false", rpc_thread_count);

    struct margo_init_info args = {0};
    args.json_config            = config;

    return margo_init_ext(addr_str, mode, &args);
}

// LCOV_EXCL_START
margo_instance_id margo_init_opt(const char*                addr_str,
                                 int                        mode,
                                 const struct hg_init_info* hg_init_info,
                                 int                        use_progress_thread,
                                 int                        rpc_thread_count)
{
    char config[1024];
    snprintf(config, 1024,
             "{ \"use_progress_thread\" : %s, \"rpc_thread_count\" : %d }",
             use_progress_thread ? "true" : "false", rpc_thread_count);

    struct margo_init_info args = {0};
    args.json_config            = config;
    args.hg_init_info           = (struct hg_init_info*)hg_init_info;

    return margo_init_ext(addr_str, mode, &args);
}
// LCOV_EXCL_END

// LCOV_EXCL_START
margo_instance_id margo_init_pool(ABT_pool      progress_pool,
                                  ABT_pool      rpc_pool,
                                  hg_context_t* hg_context)
{
    struct margo_init_info args     = {0};
    hg_class_t*            hg_class = HG_Context_get_class(hg_context);
    args.hg_class                   = hg_class;
    args.hg_context                 = hg_context;
    args.progress_pool              = progress_pool;
    args.rpc_pool                   = rpc_pool;
    hg_bool_t listening             = HG_Class_is_listening(hg_class);

    return margo_init_ext(NULL, listening, &args);
}
// LCOV_EXCL_END

static void margo_call_finalization_callbacks(margo_instance_id mid)
{
    /* call finalize callbacks */
    MARGO_TRACE(mid, "Calling finalize callbacks");
    struct margo_finalize_cb* fcb = mid->finalize_cb;
    while (fcb) {
        mid->finalize_cb = fcb->next;
        (fcb->callback)(fcb->uargs);
        struct margo_finalize_cb* tmp = fcb;
        fcb                           = mid->finalize_cb;
        free(tmp);
    }
}

static void margo_cleanup(margo_instance_id mid)
{
    MARGO_TRACE(mid, "Entering margo_cleanup");
    struct margo_registered_rpc* next_rpc;

    /* monitoring */
    struct margo_monitor_finalize_args monitoring_args = {0};
    __MARGO_MONITOR(mid, FN_START, finalize, monitoring_args);

    margo_deregister(mid, mid->shutdown_rpc_id);
    margo_deregister(mid, mid->identity_rpc_id);

    /* Start with the handle cache, to clean up any Mercury-related
     * data */
    MARGO_TRACE(mid, "Destroying handle cache");
    __margo_handle_cache_destroy(mid);

    if (mid->abt_profiling_enabled) {
        MARGO_TRACE(mid, "Dumping ABT profile");
        margo_dump_abt_profiling(mid, "margo-profile", 1, NULL);
    }

    /* finalize Mercury before anything else because this
     * could trigger some margo_cb for forward operations that
     * have not completed yet (cancelling them) */
    MARGO_TRACE(mid, "Destroying Mercury environment");
    __margo_hg_destroy(&(mid->hg));

    MARGO_TRACE(mid, "Cleaning up RPC data");
    while (mid->registered_rpcs) {
        next_rpc = mid->registered_rpcs->next;
        free(mid->registered_rpcs);
        mid->registered_rpcs = next_rpc;
    }

    /* shut down pending timers */
    MARGO_TRACE(mid, "Cleaning up pending timers");
    __margo_timer_list_free(mid);

    MARGO_TRACE(mid, "Destroying mutex and condition variables");
    ABT_mutex_free(&mid->finalize_mutex);
    ABT_cond_free(&mid->finalize_cond);
    ABT_mutex_free(&mid->pending_operations_mtx);
    ABT_key_free(&(mid->current_rpc_id_key));

    /* monitoring (destroyed before Argobots since it contains mutexes) */
    __MARGO_MONITOR(mid, FN_END, finalize, monitoring_args);
    MARGO_TRACE(mid, "Destroying monitoring context");
    if (mid->monitor && mid->monitor->finalize)
        mid->monitor->finalize(mid->monitor->uargs);
    free(mid->monitor);

    free(mid->plumber_bucket_policy);
    free(mid->plumber_nic_policy);

    MARGO_TRACE(mid, "Destroying Argobots environment");
    __margo_abt_destroy(&(mid->abt));
    free(mid);

    MARGO_TRACE(0, "Completed margo_cleanup");
}

hg_return_t margo_instance_ref_incr(margo_instance_id mid)
{
    if (!mid) return HG_INVALID_ARG;
    mid->refcount++;
    return HG_SUCCESS;
}

hg_return_t margo_instance_ref_count(margo_instance_id mid, unsigned* refcount)
{
    if (!mid) return HG_INVALID_ARG;
    *refcount = mid->refcount;
    return HG_SUCCESS;
}

hg_return_t margo_instance_release(margo_instance_id mid)
{
    if (!mid) return HG_INVALID_ARG;
    if (!mid->refcount) return HG_OTHER_ERROR;
    unsigned refcount = --mid->refcount;
    if (refcount == 0) {
        if (!mid->finalize_flag) {
            ++mid->refcount; // needed because margo_finalize will itself
                             // decrease it back to 0
            margo_finalize(mid);
        } else {
            margo_cleanup(mid);
        }
    }
    return HG_SUCCESS;
}

hg_return_t margo_instance_is_finalized(margo_instance_id mid, bool* flag)
{
    if (!mid) return HG_INVALID_ARG;
    *flag = mid->finalize_flag;
    return HG_SUCCESS;
}

void margo_finalize(margo_instance_id mid)
{
    MARGO_TRACE(mid, "Calling margo_finalize");
    int do_cleanup;

    /* check if there are pending operations */
    int pending;
    ABT_mutex_lock(mid->pending_operations_mtx);
    pending = mid->pending_operations;
    if (pending) {
        mid->finalize_requested = 1;
        ABT_mutex_unlock(mid->pending_operations_mtx);
        MARGO_TRACE(mid, "Pending operations, exiting margo_finalize");
        return;
    }
    ABT_mutex_unlock(mid->pending_operations_mtx);

    MARGO_TRACE(mid, "Executing pre-finalize callbacks");
    /* before exiting the progress loop, pre-finalize callbacks need to be
     * called */

    /* monitoring */
    struct margo_monitor_prefinalize_args monitoring_args = {0};
    __MARGO_MONITOR(mid, FN_START, prefinalize, monitoring_args);

    struct margo_finalize_cb* fcb = mid->prefinalize_cb;
    while (fcb) {
        mid->prefinalize_cb = fcb->next;
        (fcb->callback)(fcb->uargs);
        struct margo_finalize_cb* tmp = fcb;
        fcb                           = mid->prefinalize_cb;
        free(tmp);
    }

    /* monitoring */
    __MARGO_MONITOR(mid, FN_END, prefinalize, monitoring_args);

    /* tell progress thread to wrap things up */
    mid->hg_progress_shutdown_flag = 1;
    PROGRESS_NEEDED_INCR(mid);

    /* wait for it to shutdown cleanly */
    MARGO_TRACE(mid, "Waiting for progress thread to complete");
    ABT_thread_join(mid->hg_progress_tid);
    ABT_thread_free(&mid->hg_progress_tid);
    PROGRESS_NEEDED_DECR(mid);
    mid->refcount--;

    ABT_mutex_lock(mid->finalize_mutex);
    mid->finalize_flag = true;
    margo_call_finalization_callbacks(mid);
    do_cleanup = mid->finalize_refcount == 0 && mid->refcount == 0;

    ABT_mutex_unlock(mid->finalize_mutex);
    ABT_cond_broadcast(mid->finalize_cond);

    /* if there was noone waiting on the finalize at the time of the finalize
     * broadcast, then we're safe to clean up. Otherwise, let the finalizer do
     * it */
    if (do_cleanup) margo_cleanup(mid);

    MARGO_TRACE(NULL, "Finalize completed");
    return;
}

void margo_finalize_and_wait(margo_instance_id mid)
{
    MARGO_TRACE(mid, "Start to finalize and wait");
    int do_cleanup;

    ABT_mutex_lock(mid->finalize_mutex);
    mid->finalize_requested = 1;
    mid->finalize_refcount++;
    ABT_mutex_unlock(mid->finalize_mutex);

    // try finalizing
    margo_finalize(mid);

    ABT_mutex_lock(mid->finalize_mutex);

    while (!mid->finalize_flag)
        ABT_cond_wait(mid->finalize_cond, mid->finalize_mutex);

    mid->finalize_refcount--;
    do_cleanup = mid->finalize_refcount == 0 && mid->refcount == 0;

    ABT_mutex_unlock(mid->finalize_mutex);

    if (do_cleanup) margo_cleanup(mid);

    MARGO_TRACE(NULL, "Done finalizing and waiting");
    return;
}

void margo_wait_for_finalize(margo_instance_id mid)
{
    MARGO_TRACE(mid, "Start waiting for finalize");
    int do_cleanup;

    ABT_mutex_lock(mid->finalize_mutex);

    mid->finalize_refcount++;

    while (!mid->finalize_flag)
        ABT_cond_wait(mid->finalize_cond, mid->finalize_mutex);

    mid->finalize_refcount--;
    do_cleanup = mid->finalize_refcount == 0 && mid->refcount == 0;

    ABT_mutex_unlock(mid->finalize_mutex);

    if (do_cleanup) margo_cleanup(mid);

    MARGO_TRACE(NULL, "Done waiting for finalize");
    return;
}

hg_bool_t margo_is_listening(margo_instance_id mid)
{
    if (!mid) return HG_FALSE;
    return HG_Class_is_listening(mid->hg.hg_class);
}

void margo_push_prefinalize_callback(margo_instance_id         mid,
                                     margo_finalize_callback_t cb,
                                     void*                     uargs)
{
    margo_provider_push_prefinalize_callback(mid, NULL, cb, uargs);
}

int margo_pop_prefinalize_callback(margo_instance_id mid)
{
    return margo_provider_pop_prefinalize_callback(mid, NULL);
}

int margo_top_prefinalize_callback(margo_instance_id          mid,
                                   margo_finalize_callback_t* cb,
                                   void**                     uargs)
{
    return margo_provider_top_prefinalize_callback(mid, NULL, cb, uargs);
}

void margo_provider_push_prefinalize_callback(margo_instance_id         mid,
                                              const void*               owner,
                                              margo_finalize_callback_t cb,
                                              void*                     uargs)
{
    if (cb == NULL) return;

    struct margo_finalize_cb* fcb
        = (struct margo_finalize_cb*)malloc(sizeof(*fcb));
    fcb->owner    = owner;
    fcb->callback = cb;
    fcb->uargs    = uargs;

    struct margo_finalize_cb* next = mid->prefinalize_cb;
    fcb->next                      = next;
    mid->prefinalize_cb            = fcb;
}

int margo_provider_top_prefinalize_callback(margo_instance_id          mid,
                                            const void*                owner,
                                            margo_finalize_callback_t* cb,
                                            void**                     uargs)
{
    struct margo_finalize_cb* fcb = mid->prefinalize_cb;
    while (fcb != NULL && fcb->owner != owner) { fcb = fcb->next; }
    if (fcb == NULL) return 0;
    if (cb) *cb = fcb->callback;
    if (uargs) *uargs = fcb->uargs;
    return 1;
}

int margo_provider_pop_prefinalize_callback(margo_instance_id mid,
                                            const void*       owner)
{
    struct margo_finalize_cb* prev = NULL;
    struct margo_finalize_cb* fcb  = mid->prefinalize_cb;
    while (fcb != NULL && fcb->owner != owner) {
        prev = fcb;
        fcb  = fcb->next;
    }
    if (fcb == NULL) return 0;
    if (prev == NULL) {
        mid->prefinalize_cb = fcb->next;
    } else {
        prev->next = fcb->next;
    }
    free(fcb);
    return 1;
}

void margo_push_finalize_callback(margo_instance_id         mid,
                                  margo_finalize_callback_t cb,
                                  void*                     uargs)
{
    margo_provider_push_finalize_callback(mid, NULL, cb, uargs);
}

int margo_pop_finalize_callback(margo_instance_id mid)
{
    return margo_provider_pop_finalize_callback(mid, NULL);
}

int margo_top_finalize_callback(margo_instance_id          mid,
                                margo_finalize_callback_t* cb,
                                void**                     uargs)
{
    return margo_provider_top_finalize_callback(mid, NULL, cb, uargs);
}

void margo_provider_push_finalize_callback(margo_instance_id         mid,
                                           const void*               owner,
                                           margo_finalize_callback_t cb,
                                           void*                     uargs)
{
    if (cb == NULL) return;

    struct margo_finalize_cb* fcb
        = (struct margo_finalize_cb*)malloc(sizeof(*fcb));
    fcb->owner    = owner;
    fcb->callback = cb;
    fcb->uargs    = uargs;

    struct margo_finalize_cb* next = mid->finalize_cb;
    fcb->next                      = next;
    mid->finalize_cb               = fcb;
}

int margo_provider_pop_finalize_callback(margo_instance_id mid,
                                         const void*       owner)
{
    struct margo_finalize_cb* prev = NULL;
    struct margo_finalize_cb* fcb  = mid->finalize_cb;
    while (fcb != NULL && fcb->owner != owner) {
        prev = fcb;
        fcb  = fcb->next;
    }
    if (fcb == NULL) return 0;
    if (prev == NULL) {
        mid->finalize_cb = fcb->next;
    } else {
        prev->next = fcb->next;
    }
    free(fcb);
    return 1;
}

int margo_provider_top_finalize_callback(margo_instance_id          mid,
                                         const void*                owner,
                                         margo_finalize_callback_t* cb,
                                         void**                     uargs)
{
    struct margo_finalize_cb* fcb = mid->finalize_cb;
    while (fcb != NULL && fcb->owner != owner) { fcb = fcb->next; }
    if (fcb == NULL) return 0;
    if (cb) *cb = fcb->callback;
    if (uargs) *uargs = fcb->uargs;
    return 1;
}

void margo_enable_remote_shutdown(margo_instance_id mid)
{
    mid->enable_remote_shutdown = 1;
}

int margo_shutdown_remote_instance(margo_instance_id mid, hg_addr_t remote_addr)
{
    hg_return_t hret;
    hg_handle_t handle;

    hret = margo_create(mid, remote_addr, mid->shutdown_rpc_id, &handle);
    if (hret != HG_SUCCESS) return -1;

    hret = margo_forward(handle, NULL);
    if (hret != HG_SUCCESS) {
        // LCOV_EXCL_START
        margo_destroy(handle);
        return -1;
        // LCOV_EXCL_END
    }

    margo_shutdown_out_t out;
    hret = margo_get_output(handle, &out);
    if (hret != HG_SUCCESS) {
        // LCOV_EXCL_START
        margo_destroy(handle);
        return -1;
        // LCOV_EXCL_END
    }

    margo_free_output(handle, &out);
    margo_destroy(handle);

    return out.ret;
}

hg_id_t margo_provider_register_name(margo_instance_id mid,
                                     const char*       func_name,
                                     hg_proc_cb_t      in_proc_cb,
                                     hg_proc_cb_t      out_proc_cb,
                                     hg_rpc_cb_t       rpc_cb,
                                     uint16_t          provider_id,
                                     ABT_pool          pool)
{
    hg_id_t                      id;
    struct margo_registered_rpc* tmp_rpc;

    if (!rpc_cb) rpc_cb = _handler_for_NULL;
    id = gen_id(func_name, provider_id);

    /* track information about this rpc registration for debugging and
     * profiling
     * NOTE: we do this even if profiling is currently disabled; it may be
     * enabled later on at run time.
     */
    tmp_rpc = calloc(1, sizeof(*tmp_rpc));
    if (!tmp_rpc) return (0);
    strncpy(tmp_rpc->func_name, func_name, 63);
    tmp_rpc->id          = id;
    tmp_rpc->next        = mid->registered_rpcs;
    mid->registered_rpcs = tmp_rpc;
    mid->num_registered_rpcs++;

    id = margo_register_internal(mid, func_name, id, in_proc_cb, out_proc_cb,
                                 rpc_cb, pool);
    if (id == 0) {
        mid->registered_rpcs = tmp_rpc->next;
        free(tmp_rpc);
        mid->num_registered_rpcs--;
        return (id);
    }

    return (id);
}

hg_return_t margo_deregister(margo_instance_id mid, hg_id_t rpc_id)
{
    if (!mid || !mid->hg.hg_class) return HG_SUCCESS;

    /* monitoring */
    struct margo_monitor_deregister_args monitoring_args
        = {.id = rpc_id, .ret = HG_SUCCESS};
    __MARGO_MONITOR(mid, FN_START, deregister, monitoring_args);

    /* get data */
    struct margo_rpc_data* data
        = (struct margo_rpc_data*)HG_Registered_data(mid->hg.hg_class, rpc_id);
    if (data) {
        /* decrement the numner of RPC id used by the pool */
        __margo_abt_lock(&mid->abt);
        int32_t index = __margo_abt_find_pool_by_handle(&mid->abt, data->pool);
        if (index >= 0) mid->abt.pools[index].refcount--;
        __margo_abt_unlock(&mid->abt);
    }

    /* deregister */
    hg_return_t hret = HG_Deregister(mid->hg.hg_class, rpc_id);

    /* monitoring */
    monitoring_args.ret = hret;
    __MARGO_MONITOR(mid, FN_END, deregister, monitoring_args);

    return hret;
}

hg_return_t margo_registered_name(margo_instance_id mid,
                                  const char*       func_name,
                                  hg_id_t*          id,
                                  hg_bool_t*        flag)
{
    *id = gen_id(func_name, 0);
    return (HG_Registered(mid->hg.hg_class, *id, flag));
}

hg_return_t margo_provider_registered_name(margo_instance_id mid,
                                           const char*       func_name,
                                           uint16_t          provider_id,
                                           hg_id_t*          id,
                                           hg_bool_t*        flag)
{
    *id = gen_id(func_name, provider_id);

    return HG_Registered(mid->hg.hg_class, *id, flag);
}

hg_return_t margo_register_data(margo_instance_id mid,
                                hg_id_t           id,
                                void*             data,
                                void (*free_callback)(void*))
{
    struct margo_rpc_data* margo_data
        = (struct margo_rpc_data*)HG_Registered_data(mid->hg.hg_class, id);
    if (!margo_data) return HG_OTHER_ERROR;
    if (margo_data->user_data && margo_data->user_free_callback) {
        (margo_data->user_free_callback)(margo_data->user_data);
    }
    margo_data->user_data          = data;
    margo_data->user_free_callback = free_callback;
    return HG_SUCCESS;
}

void* margo_registered_data(margo_instance_id mid, hg_id_t id)
{
    struct margo_rpc_data* data
        = (struct margo_rpc_data*)HG_Registered_data(margo_get_class(mid), id);
    if (!data)
        return NULL;
    else
        return data->user_data;
}

hg_return_t margo_registered_disable_response(margo_instance_id mid,
                                              hg_id_t           id,
                                              int               disable_flag)
{
    return (HG_Registered_disable_response(mid->hg.hg_class, id, disable_flag));
}

hg_return_t margo_registered_disabled_response(margo_instance_id mid,
                                               hg_id_t           id,
                                               int*              disabled_flag)
{
    hg_bool_t   b;
    hg_return_t ret = HG_Registered_disabled_response(mid->hg.hg_class, id, &b);
    if (ret != HG_SUCCESS) return ret;
    *disabled_flag = b;
    return HG_SUCCESS;
}

/* Mercury 2.x provides two versions of lookup (async and sync).  If a
 * synchronous lookup call is available then we do not need this callback.
 */
#ifndef HG_Addr_lookup
static hg_return_t margo_addr_lookup_cb(const struct hg_cb_info* info)
{
    struct lookup_cb_evt evt;
    evt.hret              = info->ret;
    evt.addr              = info->info.lookup.addr;
    ABT_eventual eventual = (ABT_eventual)(info->arg);

    /* propagate return code out through eventual */
    ABT_eventual_set(eventual, &evt, sizeof(evt));

    return (HG_SUCCESS);
}
#endif

hg_return_t
margo_addr_lookup(margo_instance_id mid, const char* name, hg_addr_t* addr)
{
    hg_return_t hret;

#ifdef HG_Addr_lookup

    /* monitoring */
    struct margo_monitor_lookup_args monitoring_args
        = {.name = name, .addr = HG_ADDR_NULL, .ret = HG_SUCCESS};
    __MARGO_MONITOR(mid, FN_START, lookup, monitoring_args);

    /* Mercury 2.x provides two versions of lookup (async and sync).  Choose the
     * former if available to avoid context switch
     */
    hret = HG_Addr_lookup2(mid->hg.hg_class, name, addr);

    /* monitoring */
    monitoring_args.addr = addr ? *addr : HG_ADDR_NULL;
    monitoring_args.ret  = hret;
    __MARGO_MONITOR(mid, FN_END, lookup, monitoring_args);

#else /* !defined HG_Addr_lookup */
    struct lookup_cb_evt* evt;
    ABT_eventual          eventual;
    int                   ret;

    ret = ABT_eventual_create(sizeof(*evt), &eventual);
    if (ret != 0) { return (HG_NOMEM_ERROR); }

    hret = HG_Addr_lookup(mid->hg_context, margo_addr_lookup_cb,
                          (void*)eventual, name, HG_OP_ID_IGNORE);
    PROGRESS_NEEDED_INCR(mid);
    if (hret == HG_SUCCESS) {
        ABT_eventual_wait(eventual, (void**)&evt);
        *addr = evt->addr;
        hret  = evt->hret;
    }
    PROGRESS_NEEDED_DECR(mid);

    ABT_eventual_free(&eventual);
#endif

    return (hret);
}

hg_return_t margo_addr_free(margo_instance_id mid, hg_addr_t addr)
{
    return (HG_Addr_free(mid->hg.hg_class, addr));
}

hg_return_t margo_addr_self(margo_instance_id mid, hg_addr_t* addr)
{
    hg_return_t hret = HG_SUCCESS;

    /* monitoring */
    struct margo_monitor_lookup_args monitoring_args
        = {.name = NULL, .addr = HG_ADDR_NULL, .ret = HG_SUCCESS};
    __MARGO_MONITOR(mid, FN_START, lookup, monitoring_args);

    hret = HG_Addr_self(mid->hg.hg_class, addr);

    /* monitoring */
    monitoring_args.addr = addr ? *addr : HG_ADDR_NULL;
    monitoring_args.ret  = hret;
    __MARGO_MONITOR(mid, FN_END, lookup, monitoring_args);

    return hret;
}

hg_return_t
margo_addr_dup(margo_instance_id mid, hg_addr_t addr, hg_addr_t* new_addr)
{
    return (HG_Addr_dup(mid->hg.hg_class, addr, new_addr));
}

hg_bool_t
margo_addr_cmp(margo_instance_id mid, hg_addr_t addr1, hg_addr_t addr2)
{
    return HG_Addr_cmp(mid->hg.hg_class, addr1, addr2);
}

hg_return_t margo_addr_set_remove(margo_instance_id mid, hg_addr_t addr)
{
    return HG_Addr_set_remove(mid->hg.hg_class, addr);
}

hg_return_t margo_addr_to_string(margo_instance_id mid,
                                 char*             buf,
                                 hg_size_t*        buf_size,
                                 hg_addr_t         addr)
{
    return (HG_Addr_to_string(mid->hg.hg_class, buf, buf_size, addr));
}

hg_return_t margo_create(margo_instance_id mid,
                         hg_addr_t         addr,
                         hg_id_t           id,
                         hg_handle_t*      handle)
{
    hg_return_t hret = HG_OTHER_ERROR;

    /* monitoring */
    struct margo_monitor_create_args monitoring_args
        = {.addr = addr, .id = id, .handle = HG_HANDLE_NULL, .ret = HG_SUCCESS};
    __MARGO_MONITOR(mid, FN_START, create, monitoring_args);

    /* look for a handle to reuse */
    hret = __margo_handle_cache_get(mid, addr, id, handle);
    if (hret != HG_SUCCESS) {
        /* else try creating a new handle */
        hret = HG_Create(mid->hg.hg_context, addr, id, handle);
    }
    if (hret != HG_SUCCESS) goto finish;

    hret = __margo_internal_set_handle_data(*handle);

finish:

    /* monitoring */
    monitoring_args.handle = handle ? *handle : HG_HANDLE_NULL;
    monitoring_args.ret    = hret;
    __MARGO_MONITOR(mid, FN_END, create, monitoring_args);

    return hret;
}

hg_return_t margo_destroy(hg_handle_t handle)
{
    if (handle == HG_HANDLE_NULL) return HG_SUCCESS;

    /* check if the reference count of the handle is 1 */
    int32_t refcount = HG_Ref_get(handle);
    if (refcount != 1) {
        /* if different from 1, then HG_Destroy will simply decrease it */
        return HG_Destroy(handle);
    }

    margo_instance_id mid;
    hg_return_t       hret = HG_OTHER_ERROR;

    /* use the handle to get the associated mid
     * Note: we need to do that before cleaning handle_data */
    mid = margo_hg_handle_get_instance(handle);

    /* monitoring */
    struct margo_monitor_destroy_args monitoring_args
        = {.handle = handle, .ret = HG_SUCCESS};
    __MARGO_MONITOR(mid, FN_START, destroy, monitoring_args);

    /* remove the margo_handle_data associated with the handle */
    struct margo_handle_data* handle_data = HG_Get_data(handle);
    if (handle_data) {
        if (handle_data->user_free_callback) {
            handle_data->user_free_callback(handle_data->user_data);
        }
        memset(handle_data, 0, sizeof(*handle_data));
    }

    if (mid) {
        /* recycle this handle if it came from the handle cache */
        hret = __margo_handle_cache_put(mid, handle);
        if (hret != HG_SUCCESS) {
            /* else destroy the handle manually and free the handle data */
            hret = HG_Destroy(handle);
        }
    } else {
        hret = HG_OTHER_ERROR;
    }

    /* monitoring */
    monitoring_args.ret = hret;
    __MARGO_MONITOR(mid, FN_END, destroy, monitoring_args);

    return hret;
}

static hg_return_t margo_cb(const struct hg_cb_info* info)
{
    hg_return_t       hret = info->ret;
    margo_request     req  = (margo_request)(info->arg);
    margo_instance_id mid  = req->mid;

    /* monitoring */
    struct margo_monitor_cb_args monitoring_args
        = {.info = info, .request = req, .ret = HG_SUCCESS};
    switch (info->type) {
    case HG_CB_FORWARD:
        __MARGO_MONITOR(mid, FN_START, forward_cb, monitoring_args);
        break;
    case HG_CB_RESPOND:
        __MARGO_MONITOR(mid, FN_START, respond_cb, monitoring_args);
        break;
    case HG_CB_BULK:
        __MARGO_MONITOR(mid, FN_START, bulk_transfer_cb, monitoring_args);
        break;
    default:
        break;
    };

    if (hret == HG_CANCELED && req->timer) { hret = HG_TIMEOUT; }

    /* remove timer if there is one and it is still in place */
    if (req->timer) {
        margo_timer_cancel(req->timer);
        margo_timer_destroy(req->timer);
    }

    if (req->kind == MARGO_REQ_CALLBACK) {
        if (req->callback.cb) req->callback.cb(req->callback.uargs, hret);
    } else {
        req->eventual.hret = hret;
        MARGO_EVENTUAL_SET(req->eventual.ev);
    }

    /* monitoring */
    monitoring_args.ret = hret;
    switch (info->type) {
    case HG_CB_FORWARD:
        __MARGO_MONITOR(mid, FN_END, forward_cb, monitoring_args);
        break;
    case HG_CB_RESPOND:
        __MARGO_MONITOR(mid, FN_END, respond_cb, monitoring_args);
        break;
    case HG_CB_BULK:
        __MARGO_MONITOR(mid, FN_END, bulk_transfer_cb, monitoring_args);
        break;
    default:
        break;
    };

    // a callback-based request is heap-allocated but is not
    // handed to the user, hence it has to be freed here.
    if (req->kind == MARGO_REQ_CALLBACK) free(req);

    PROGRESS_NEEDED_DECR(mid);

    return HG_SUCCESS;
}

static hg_return_t margo_wait_internal(margo_request req)
{
    hg_return_t hret = HG_SUCCESS;

    if (req->kind != MARGO_REQ_EVENTUAL) // should not happen
        return HG_INVALID_ARG;

    /* monitoring */
    struct margo_monitor_wait_args monitoring_args
        = {.request = req, .ret = HG_SUCCESS};
    __MARGO_MONITOR(req->mid, FN_START, wait, monitoring_args);

    MARGO_EVENTUAL_WAIT(req->eventual.ev);
    MARGO_EVENTUAL_FREE(&(req->eventual.ev));
    if (req->eventual.hret != HG_SUCCESS) {
        hret = req->eventual.hret;
        goto finish;
    }
    if (req->type == MARGO_FORWARD_REQUEST)
        hret = check_error_in_output(req->handle);

finish:

    /* monitoring */
    monitoring_args.ret = hret;
    __MARGO_MONITOR(req->mid, FN_END, wait, monitoring_args);

    return hret;
}

static void margo_timeout_cb(void* arg)
{
    margo_request req = (margo_request)arg;
    if(req->type == MARGO_FORWARD_REQUEST) {
        /* cancel the Mercury op if the forward timed out */
        HG_Cancel(req->handle);
    }
    if(req->type == MARGO_BULK_REQUEST) {
        /* cancel the Mercury op if the bulk transfer timed out */
        HG_Bulk_cancel(req->bulk_op);
    }
}

static hg_return_t margo_provider_iforward_internal(
    uint16_t      provider_id,
    hg_handle_t   handle,
    double        timeout_ms,
    void*         in_struct,
    margo_request req) /* the request should have been allocated */
{
    hg_return_t               hret = HG_TIMEOUT;
    int                       ret;
    const struct hg_info*     hgi;
    struct margo_handle_data* handle_data;
    hg_id_t                   client_id, server_id;
    hg_proc_cb_t              in_cb, out_cb;
    margo_instance_id         mid;

    hgi         = HG_Get_info(handle);
    handle_data = (struct margo_handle_data*)HG_Get_data(handle);

    if (!handle_data) {
        margo_error(MARGO_INSTANCE_NULL,
                    "in %s: HG_Get_data failed to return data", __func__);
        return HG_NO_MATCH;
    }

    mid       = handle_data->mid;
    in_cb     = handle_data->in_proc_cb;
    out_cb    = handle_data->out_proc_cb;
    client_id = hgi->id;
    server_id = mux_id(client_id, provider_id);

    if (!mid) {
        margo_error(MARGO_INSTANCE_NULL,
                    "in %s: handle is not associated"
                    " with a valid margo instance",
                    __func__);
        return HG_OTHER_ERROR;
    }

    /* monitoring */
    struct margo_monitor_forward_args monitoring_args
        = {.provider_id = provider_id,
           .handle      = handle,
           .data        = in_struct,
           .timeout_ms  = timeout_ms,
           .request     = req,
           .ret         = HG_SUCCESS};
    __MARGO_MONITOR(mid, FN_START, forward, monitoring_args);

    hg_bool_t is_registered;
    hret = HG_Registered(mid->hg.hg_class, server_id, &is_registered);
    if (hret != HG_SUCCESS) {
        // LCOV_EXCL_START
        margo_error(mid, "in %s HG_Registered failed: %s", __func__,
                    HG_Error_to_string(hret));
        goto finish;
        // LCOV_EXCL_END
    }

    if (!is_registered) {

        /* if Mercury does not recognize this ID (with provider id included)
         * then register it now
         */

        /* find out if disable_response was called for this RPC */
        // TODO this information could be added to margo_handle_data
        hg_bool_t response_disabled;
        hret = HG_Registered_disabled_response(mid->hg.hg_class, client_id,
                                               &response_disabled);
        if (hret != HG_SUCCESS) {
            // LCOV_EXCL_START
            margo_error(mid,
                        "in %s: HG_Registered_disabled_response failed: %s",
                        __func__, HG_Error_to_string(hret));
            goto finish;
            // LCOV_EXCL_END
        }

        /* register new ID that includes provider id */
        hg_id_t id = margo_register_internal(mid, handle_data->rpc_name,
                                             server_id, in_cb, out_cb,
                                             _handler_for_NULL, ABT_POOL_NULL);
        if (id == 0) {
            // LCOV_EXCL_START
            hret = HG_OTHER_ERROR;
            goto finish;
            // LCOV_EXCL_END
        }

        hret = HG_Registered_disable_response(hgi->hg_class, server_id,
                                              response_disabled);
        if (hret != HG_SUCCESS) {
            margo_error(mid, "in %s: HG_Registered_disable_response failed: %s",
                        __func__, HG_Error_to_string(hret));
            goto finish;
        }
    }

    hret = HG_Reset(handle, hgi->addr, server_id);
    if (hret != HG_SUCCESS) {
        margo_error(mid, "in %s: HG_Reset failed: %s", __func__,
                    HG_Error_to_string(hret));
        goto finish;
    }

    if (req->kind == MARGO_REQ_EVENTUAL) {
        ret = MARGO_EVENTUAL_CREATE(&req->eventual.ev);
        if (ret != 0) {
            // LCOV_EXCL_START
            margo_error(mid, "in %s: ABT_eventual_create failed: %d", __func__,
                        ret);
            hret = HG_NOMEM_ERROR;
            goto finish;
            // LCOV_EXCL_END
        }
    }

    req->type   = MARGO_FORWARD_REQUEST;
    req->timer  = NULL;
    req->handle = handle;
    req->mid    = mid;

    if (timeout_ms > 0) {
        /* set a timer object to expire when this forward times out */
        hret = margo_timer_create_with_pool(mid, margo_timeout_cb, req,
                                            ABT_POOL_NULL, &req->timer);
        if (hret != HG_SUCCESS) {
            // LCOV_EXCL_START
            margo_error(mid, "in %s: could not create timer", __func__);
            goto finish;
            // LCOV_EXCL_END
        }
        hret = margo_timer_start(req->timer, timeout_ms);
        if (hret != HG_SUCCESS) {
            // LCOV_EXCL_START
            margo_timer_destroy(req->timer);
            margo_error(mid, "in %s: could not start timer", __func__);
            goto finish;
            // LCOV_EXCL_END
        }
    }

    // get parent RPC id
    hg_id_t parent_rpc_id;
    margo_get_current_rpc_id(mid, &parent_rpc_id);

    // create the margo_forward_proc_args for the serializer
    struct margo_forward_proc_args forward_args
        = {.handle    = handle,
           .request   = req,
           .user_args = (void*)in_struct,
           .user_cb   = in_cb,
           .header    = {.parent_rpc_id = parent_rpc_id}};

    hret = HG_Forward(handle, margo_cb, (void*)req, (void*)&forward_args);

    if (hret != HG_SUCCESS) {
        margo_error(mid, "in %s: HG_Forward failed: %s", __func__,
                    HG_Error_to_string(hret));
    }
    /* remove timer if HG_Forward failed */
    if (hret != HG_SUCCESS && req->timer) {
        // LCOV_EXCL_START
        margo_timer_cancel(req->timer);
        margo_timer_destroy(req->timer);
        req->timer = NULL;
        // LCOV_EXCL_END
    }
    PROGRESS_NEEDED_INCR(mid);

finish:

    if(hret != HG_SUCCESS && req->kind == MARGO_REQ_EVENTUAL) {
        MARGO_EVENTUAL_FREE(&req->eventual.ev);
    }

    /* monitoring */
    monitoring_args.ret = hret;
    __MARGO_MONITOR(mid, FN_END, forward, monitoring_args);

    return hret;
}

hg_return_t margo_provider_forward_timed(uint16_t    provider_id,
                                         hg_handle_t handle,
                                         void*       in_struct,
                                         double      timeout_ms)
{
    hg_return_t                 hret;
    struct margo_request_struct reqs = {0};
    hret = margo_provider_iforward_internal(provider_id, handle, timeout_ms,
                                            in_struct, &reqs);
    if (hret != HG_SUCCESS) return hret;
    return margo_wait_internal(&reqs);
}

hg_return_t margo_provider_iforward_timed(uint16_t       provider_id,
                                          hg_handle_t    handle,
                                          void*          in_struct,
                                          double         timeout_ms,
                                          margo_request* req)
{
    hg_return_t   hret;
    margo_request tmp_req = calloc(1, sizeof(*tmp_req));
    if (!tmp_req) { return HG_NOMEM_ERROR; }
    hret = margo_provider_iforward_internal(provider_id, handle, timeout_ms,
                                            in_struct, tmp_req);
    if (hret != HG_SUCCESS) {
        free(tmp_req);
        return hret;
    }
    *req = tmp_req;
    return HG_SUCCESS;
}

hg_return_t margo_provider_cforward_timed(uint16_t    provider_id,
                                          hg_handle_t handle,
                                          void*       in_struct,
                                          double      timeout_ms,
                                          void (*on_complete)(void*,
                                                              hg_return_t),
                                          void* uargs)
{
    hg_return_t   hret;
    margo_request tmp_req = calloc(1, sizeof(*tmp_req));
    if (!tmp_req) { return HG_NOMEM_ERROR; }
    tmp_req->kind             = MARGO_REQ_CALLBACK;
    tmp_req->callback.cb    = on_complete;
    tmp_req->callback.uargs = uargs;

    hret = margo_provider_iforward_internal(provider_id, handle, timeout_ms,
                                            in_struct, tmp_req);
    if (hret != HG_SUCCESS) {
        free(tmp_req);
        return hret;
    }
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
    if (req->kind != MARGO_REQ_EVENTUAL) return -1;
    return MARGO_EVENTUAL_TEST(req->eventual.ev, flag);
}

hg_return_t margo_wait_any(size_t count, margo_request* req, size_t* index)
{
    // XXX this is an active loop, we should change it
    // when Argobots provide an ABT_eventual_wait_any
    size_t i;
    int    ret;
    int    flag = 0;
    int    has_pending_requests;
try_again:
    has_pending_requests = 0;
    for (i = 0; i < count; i++) {
        if (req[i] == MARGO_REQUEST_NULL)
            continue;
        else
            has_pending_requests = 1;
        ret = margo_test(req[i], &flag);
        if (ret != ABT_SUCCESS) {
            // LCOV_EXCL_START
            *index = i;
            return HG_OTHER_ERROR;
            // LCOV_EXCL_END
        }
        if (flag) {
            *index = i;
            return margo_wait(req[i]);
        }
    }
    ABT_thread_yield();
    if (has_pending_requests) goto try_again;
    *index = count;
    return HG_SUCCESS;
}

hg_handle_t margo_request_get_handle(margo_request req)
{
    if (!req) return NULL;
    return req->handle;
}

margo_request_type margo_request_get_type(margo_request req)
{
    if (!req) return MARGO_INVALID_REQUEST;
    return req->type;
}

margo_instance_id margo_request_get_instance(margo_request req)
{
    if (!req) return MARGO_INSTANCE_NULL;
    return req->mid;
}

static hg_return_t
margo_irespond_internal(hg_handle_t   handle,
                        void*         out_struct,
                        margo_request req) /* should have been allocated */
{
    int               ret;
    hg_return_t       hret;
    hg_proc_cb_t      out_cb = NULL;
    margo_instance_id mid    = MARGO_INSTANCE_NULL;

    struct margo_handle_data* handle_data
        = (struct margo_handle_data*)HG_Get_data(handle);
    if (!handle_data) return HG_NO_MATCH;

    mid         = handle_data->mid;

    req->type   = MARGO_RESPONSE_REQUEST;
    req->handle = handle;
    req->timer  = NULL;
    req->mid    = mid;

    /* monitoring */
    struct margo_monitor_respond_args monitoring_args = {.handle = handle,
                                                         .data   = out_struct,
                                                         .timeout_ms = 0.0,
                                                         .error      = false,
                                                         .request    = req,
                                                         .ret = HG_SUCCESS};
    __MARGO_MONITOR(mid, FN_START, respond, monitoring_args);

    out_cb = handle_data->out_proc_cb;
    if (req->kind == MARGO_REQ_EVENTUAL) {
        ret = MARGO_EVENTUAL_CREATE(&req->eventual.ev);
        if (ret != 0) {
            // LCOV_EXCL_START
            margo_error(mid, "in %s: ABT_eventual_create failed: %d", __func__,
                        ret);
            hret = HG_NOMEM_ERROR;
            goto finish;
            // LCOV_EXCL_END
        }
    }

    // create the margo_respond_proc_args for the serializer
    struct margo_respond_proc_args respond_args
        = {.handle    = handle,
           .request   = req,
           .user_args = (void*)out_struct,
           .user_cb   = out_cb,
           .header    = {.hg_ret = HG_SUCCESS}};

    hret = HG_Respond(handle, margo_cb, (void*)req, (void*)&respond_args);

    if (hret == HG_SUCCESS) { PROGRESS_NEEDED_INCR(mid); }

finish:

    if (hret != HG_SUCCESS && req->kind == MARGO_REQ_EVENTUAL) {
        MARGO_EVENTUAL_FREE(&req->eventual.ev);
    }

    /* monitoring */
    monitoring_args.ret = hret;
    __MARGO_MONITOR(mid, FN_END, respond, monitoring_args);

    return hret;
}

void __margo_respond_with_error(hg_handle_t handle, hg_return_t hg_ret)
{
    const struct hg_info* hgi = HG_Get_info(handle);

    hg_bool_t   b;
    hg_return_t hret
        = HG_Registered_disabled_response(hgi->hg_class, hgi->id, &b);
    if (hret != HG_SUCCESS) return;
    if (b) return;

    struct margo_respond_proc_args respond_args
        = {.user_args = NULL, .user_cb = NULL, .header = {.hg_ret = hg_ret}};

    HG_Respond(handle, NULL, NULL, (void*)&respond_args);
}

hg_return_t margo_respond(hg_handle_t handle, void* out_struct)
{

    margo_instance_id mid = margo_hg_handle_get_instance(handle);
    if (mid == NULL) {
        margo_error(NULL, "Could not get margo instance in margo_respond()");
        return (HG_OTHER_ERROR);
    }

    hg_return_t                 hret;
    struct margo_request_struct reqs = {0};
    hret = margo_irespond_internal(handle, out_struct, &reqs);
    if (hret != HG_SUCCESS) return hret;
    return margo_wait_internal(&reqs);
}

hg_return_t
margo_irespond(hg_handle_t handle, void* out_struct, margo_request* req)
{
    hg_return_t   hret;
    margo_request tmp_req = calloc(1, sizeof(*tmp_req));
    if (!tmp_req) { return (HG_NOMEM_ERROR); }
    hret = margo_irespond_internal(handle, out_struct, tmp_req);
    if (hret != HG_SUCCESS) {
        free(tmp_req);
        return hret;
    }
    *req = tmp_req;
    return HG_SUCCESS;
}

hg_return_t margo_crespond(hg_handle_t handle,
                           void* out_struct,
                           void (*on_complete)(void*, hg_return_t),
                           void* uargs)
{
    hg_return_t   hret;
    margo_request tmp_req = calloc(1, sizeof(*tmp_req));
    if (!tmp_req) { return (HG_NOMEM_ERROR); }
    tmp_req->kind             = MARGO_REQ_CALLBACK;
    tmp_req->callback.cb    = on_complete;
    tmp_req->callback.uargs = uargs;
    hret = margo_irespond_internal(handle, out_struct, tmp_req);
    if (hret != HG_SUCCESS) {
        free(tmp_req);
        return hret;
    }
    return HG_SUCCESS;
}

hg_return_t margo_get_input(hg_handle_t handle, void* in_struct)
{
    hg_proc_cb_t      in_cb = NULL;
    margo_instance_id mid   = MARGO_INSTANCE_NULL;

    struct margo_handle_data* handle_data
        = (struct margo_handle_data*)HG_Get_data(handle);
    if (!handle_data) return HG_NO_MATCH;
    in_cb = handle_data->in_proc_cb;
    mid   = handle_data->mid;

    /* monitoring */
    struct margo_monitor_get_input_args monitoring_args
        = {.handle = handle, .data = in_struct, .ret = HG_SUCCESS};
    __MARGO_MONITOR(mid, FN_START, get_input, monitoring_args);

    // create the margo_forward_proc_args for the serializer
    struct margo_forward_proc_args forward_args
        = {.handle    = handle,
           .request   = NULL,
           .user_args = (void*)in_struct,
           .user_cb   = in_cb,
           .header    = {0}};

    hg_return_t hret = HG_Get_input(handle, (void*)&forward_args);

    /* monitoring */
    monitoring_args.ret = hret;
    __MARGO_MONITOR(mid, FN_END, get_input, monitoring_args);

    return hret;
}

hg_return_t margo_free_input(hg_handle_t handle, void* in_struct)
{
    hg_proc_cb_t      in_cb = NULL;
    margo_instance_id mid   = MARGO_INSTANCE_NULL;

    struct margo_handle_data* handle_data
        = (struct margo_handle_data*)HG_Get_data(handle);
    if (!handle_data) return HG_NO_MATCH;
    in_cb = handle_data->in_proc_cb;
    mid   = handle_data->mid;

    /* monitoring */
    struct margo_monitor_free_input_args monitoring_args
        = {.handle = handle, .data = in_struct, .ret = HG_SUCCESS};
    __MARGO_MONITOR(mid, FN_START, free_input, monitoring_args);

    // create the margo_forward_proc_args for the serializer
    struct margo_forward_proc_args forward_args
        = {.handle    = handle,
           .request   = NULL,
           .user_args = (void*)in_struct,
           .user_cb   = in_cb,
           .header    = {0}};

    hg_return_t hret = HG_Free_input(handle, (void*)&forward_args);

    /* monitoring */
    monitoring_args.ret = hret;
    __MARGO_MONITOR(mid, FN_END, free_input, monitoring_args);

    return hret;
}

hg_return_t margo_get_output(hg_handle_t handle, void* out_struct)
{
    hg_proc_cb_t      out_cb = NULL;
    margo_instance_id mid    = MARGO_INSTANCE_NULL;

    struct margo_handle_data* handle_data
        = (struct margo_handle_data*)HG_Get_data(handle);
    if (!handle_data) return HG_NO_MATCH;
    out_cb = handle_data->out_proc_cb;
    mid    = handle_data->mid;

    /* monitoring */
    struct margo_monitor_get_output_args monitoring_args
        = {.handle = handle, .data = out_struct, .ret = HG_SUCCESS};
    __MARGO_MONITOR(mid, FN_START, get_output, monitoring_args);

    // create the margo_respond_proc_args for the serializer
    struct margo_respond_proc_args respond_args
        = {.handle    = handle,
           .request   = NULL,
           .user_args = (void*)out_struct,
           .user_cb   = out_cb,
           .header    = {.hg_ret = HG_SUCCESS}};

    hg_return_t hret = HG_Get_output(handle, (void*)&respond_args);
    if (hret != HG_SUCCESS) goto finish;
    hret = respond_args.header.hg_ret;
    if (hret != HG_SUCCESS) HG_Free_output(handle, (void*)&respond_args);

finish:

    /* monitoring */
    monitoring_args.ret = hret;
    __MARGO_MONITOR(mid, FN_END, get_output, monitoring_args);

    return hret;
}

hg_return_t margo_free_output(hg_handle_t handle, void* out_struct)
{
    hg_proc_cb_t      out_cb = NULL;
    margo_instance_id mid    = MARGO_INSTANCE_NULL;

    struct margo_handle_data* handle_data
        = (struct margo_handle_data*)HG_Get_data(handle);
    if (!handle_data) return HG_NO_MATCH;
    out_cb = handle_data->out_proc_cb;
    mid    = handle_data->mid;

    /* monitoring */
    struct margo_monitor_free_output_args monitoring_args
        = {.handle = handle, .ret = HG_SUCCESS};
    __MARGO_MONITOR(mid, FN_START, free_output, monitoring_args);

    // create the margo_respond_proc_args for the serializer
    struct margo_respond_proc_args respond_args
        = {.handle    = handle,
           .request   = NULL,
           .user_args = (void*)out_struct,
           .user_cb   = out_cb,
           .header    = {.hg_ret = HG_SUCCESS}};

    hg_return_t hret = HG_Free_output(handle, (void*)&respond_args);

    /* monitoring */
    monitoring_args.ret = hret;
    __MARGO_MONITOR(mid, FN_END, free_output, monitoring_args);

    return hret;
}

void* margo_get_data(hg_handle_t h)
{
    struct margo_handle_data* handle_data
        = (struct margo_handle_data*)HG_Get_data(h);
    if (!handle_data) return NULL;

    return handle_data->user_data;
}

hg_return_t
margo_set_data(hg_handle_t h, void* data, void (*free_callback)(void*))
{
    struct margo_handle_data* handle_data
        = (struct margo_handle_data*)HG_Get_data(h);
    if (!handle_data) return HG_NO_MATCH;

    if (handle_data->user_free_callback) {
        handle_data->user_free_callback(handle_data->user_data);
    }

    handle_data->user_data          = data;
    handle_data->user_free_callback = free_callback;

    return HG_SUCCESS;
}

hg_return_t margo_bulk_create(margo_instance_id mid,
                              hg_uint32_t       count,
                              void**            buf_ptrs,
                              const hg_size_t*  buf_sizes,
                              hg_uint8_t        flags,
                              hg_bulk_t*        handle)
{
    hg_return_t hret;

    /* monitoring */
    struct margo_monitor_bulk_create_args monitoring_args
        = {.count  = count,
           .ptrs   = (const void* const*)buf_ptrs,
           .sizes  = buf_sizes,
           .flags  = flags,
           .attrs  = NULL,
           .handle = HG_BULK_NULL,
           .ret    = HG_SUCCESS};
    __MARGO_MONITOR(mid, FN_START, bulk_create, monitoring_args);

    hret = HG_Bulk_create(mid->hg.hg_class, count, buf_ptrs, buf_sizes, flags,
                          handle);
    /* monitoring */
    monitoring_args.handle = handle ? *handle : HG_BULK_NULL;
    monitoring_args.ret    = hret;
    __MARGO_MONITOR(mid, FN_END, bulk_create, monitoring_args);

    return hret;
}

#if (HG_VERSION_MAJOR > 2) || (HG_VERSION_MAJOR == 2 && HG_VERSION_MINOR > 1) \
    || (HG_VERSION_MAJOR == 2 && HG_VERSION_MINOR == 1                        \
        && HG_VERSION_PATCH > 0)
hg_return_t margo_bulk_create_attr(margo_instance_id          mid,
                                   hg_uint32_t                count,
                                   void**                     buf_ptrs,
                                   const hg_size_t*           buf_sizes,
                                   hg_uint8_t                 flags,
                                   const struct hg_bulk_attr* attrs,
                                   hg_bulk_t*                 handle)
{
    hg_return_t hret;

    /* monitoring */
    struct margo_monitor_bulk_create_args monitoring_args
        = {.count  = count,
           .ptrs   = (const void* const*)buf_ptrs,
           .sizes  = buf_sizes,
           .flags  = flags,
           .attrs  = attrs,
           .handle = HG_BULK_NULL,
           .ret    = HG_SUCCESS};
    __MARGO_MONITOR(mid, FN_START, bulk_create, monitoring_args);

    hret = HG_Bulk_create_attr(mid->hg.hg_class, count, buf_ptrs, buf_sizes,
                               flags, attrs, handle);
    /* monitoring */
    monitoring_args.handle = handle ? *handle : HG_BULK_NULL;
    monitoring_args.ret    = hret;
    __MARGO_MONITOR(mid, FN_END, bulk_create, monitoring_args);

    return hret;
}
#endif

hg_return_t margo_bulk_free(hg_bulk_t handle)
{
    hg_return_t       hret = HG_SUCCESS;
    margo_instance_id mid  = MARGO_INSTANCE_NULL;

    /* Note: until Mercury provides us with a way of attaching
     * data to hg_bulk_t handles, we cannot retrieve the
     * margo_instance_id that was used to create the bulk handle.
     * The monitoring bellow will therefore not work.
     */

    /* monitoring */
    struct margo_monitor_bulk_free_args monitoring_args
        = {.handle = handle, .ret = HG_SUCCESS};
    __MARGO_MONITOR(mid, FN_START, bulk_free, monitoring_args);

    hret = HG_Bulk_free(handle);

    /* monitoring */
    monitoring_args.ret = hret;
    __MARGO_MONITOR(mid, FN_END, bulk_free, monitoring_args);

    return hret;
}

hg_return_t margo_bulk_deserialize(margo_instance_id mid,
                                   hg_bulk_t*        handle,
                                   const void*       buf,
                                   hg_size_t         buf_size)
{
    return (HG_Bulk_deserialize(mid->hg.hg_class, handle, buf, buf_size));
}

static hg_return_t margo_bulk_itransfer_internal(
    margo_instance_id mid,
    hg_bulk_op_t      op,
    hg_addr_t         origin_addr,
    hg_bulk_t         origin_handle,
    size_t            origin_offset,
    hg_bulk_t         local_handle,
    size_t            local_offset,
    size_t            size,
    double            timeout_ms,
    margo_request     req) /* should have been allocated */
{
    hg_return_t hret = HG_TIMEOUT;
    int         ret;

    req->type   = MARGO_BULK_REQUEST;
    req->timer  = NULL;
    req->handle = HG_HANDLE_NULL;
    req->mid    = mid;

    /* monitoring */
    struct margo_monitor_bulk_transfer_args monitoring_args
        = {.op            = op,
           .origin_addr   = origin_addr,
           .origin_handle = origin_handle,
           .origin_offset = origin_offset,
           .local_handle  = local_handle,
           .local_offset  = local_offset,
           .size          = size,
           .timeout_ms    = 0.0,
           .request       = req,
           .ret           = HG_SUCCESS};
    __MARGO_MONITOR(mid, FN_START, bulk_transfer, monitoring_args);

    if (req->kind == MARGO_REQ_EVENTUAL) {
        ret = MARGO_EVENTUAL_CREATE(&req->eventual.ev);
        if (ret != 0) {
            // LCOV_EXCL_START
            margo_error(mid, "in %s: ABT_eventual_create failed: %d", __func__,
                        ret);
            hret = HG_NOMEM_ERROR;
            goto finish;
            // LCOV_EXCL_END
        }
    }

    if (timeout_ms > 0) {
        /* set a timer object to expire when this forward times out */
        hret = margo_timer_create_with_pool(mid, margo_timeout_cb, req,
                ABT_POOL_NULL, &req->timer);
        if (hret != HG_SUCCESS) {
            // LCOV_EXCL_START
            margo_error(mid, "in %s: could not create timer", __func__);
            goto finish;
            // LCOV_EXCL_END
        }
        hret = margo_timer_start(req->timer, timeout_ms);
        if (hret != HG_SUCCESS) {
            // LCOV_EXCL_START
            margo_timer_destroy(req->timer);
            margo_error(mid, "in %s: could not start timer", __func__);
            goto finish;
            // LCOV_EXCL_END
        }
    }

    hret = HG_Bulk_transfer(mid->hg.hg_context, margo_cb, (void*)req, op,
                            origin_addr, origin_handle, origin_offset,
                            local_handle, local_offset, size, &req->bulk_op);
    if (hret == HG_SUCCESS) { PROGRESS_NEEDED_INCR(mid); }

    if (hret != HG_SUCCESS && req->timer) {
        // LCOV_EXCL_START
        margo_timer_cancel(req->timer);
        margo_timer_destroy(req->timer);
        req->timer = NULL;
        // LCOV_EXCL_END
    }

finish:

    monitoring_args.ret = hret;
    __MARGO_MONITOR(mid, FN_END, bulk_transfer, monitoring_args);

    return hret;
}

hg_return_t margo_bulk_transfer_timed(margo_instance_id mid,
                                      hg_bulk_op_t      op,
                                      hg_addr_t         origin_addr,
                                      hg_bulk_t         origin_handle,
                                      size_t            origin_offset,
                                      hg_bulk_t         local_handle,
                                      size_t            local_offset,
                                      size_t            size,
                                      double            timeout_ms)
{
    struct margo_request_struct reqs = {0};
    hg_return_t                 hret = margo_bulk_itransfer_internal(
        mid, op, origin_addr, origin_handle, origin_offset, local_handle,
        local_offset, size, timeout_ms, &reqs);
    if (hret != HG_SUCCESS) return hret;
    return margo_wait_internal(&reqs);
}

hg_return_t margo_bulk_itransfer_timed(margo_instance_id mid,
                                       hg_bulk_op_t      op,
                                       hg_addr_t         origin_addr,
                                       hg_bulk_t         origin_handle,
                                       size_t            origin_offset,
                                       hg_bulk_t         local_handle,
                                       size_t            local_offset,
                                       size_t            size,
                                       double            timeout_ms,
                                       margo_request*    req)
{
    margo_request tmp_req = calloc(1, sizeof(*tmp_req));
    if (!tmp_req) { return (HG_NOMEM_ERROR); }
    hg_return_t hret = margo_bulk_itransfer_internal(
        mid, op, origin_addr, origin_handle, origin_offset, local_handle,
        local_offset, size, timeout_ms, tmp_req);
    if (hret != HG_SUCCESS) {
        free(tmp_req);
        return hret;
    }

    *req = tmp_req;

    return (hret);
}

hg_return_t margo_bulk_ctransfer_timed(margo_instance_id mid,
                                       hg_bulk_op_t      op,
                                       hg_addr_t         origin_addr,
                                       hg_bulk_t         origin_handle,
                                       size_t            origin_offset,
                                       hg_bulk_t         local_handle,
                                       size_t            local_offset,
                                       size_t            size,
                                       double            timeout_ms,
                                       void (*on_complete)(void*, hg_return_t),
                                       void* uargs)
{
    margo_request tmp_req = calloc(1, sizeof(*tmp_req));
    if (!tmp_req) { return (HG_NOMEM_ERROR); }
    tmp_req->kind             = MARGO_REQ_CALLBACK;
    tmp_req->callback.cb    = on_complete;
    tmp_req->callback.uargs = uargs;

    hg_return_t hret = margo_bulk_itransfer_internal(
        mid, op, origin_addr, origin_handle, origin_offset, local_handle,
        local_offset, size, timeout_ms, tmp_req);
    if (hret != HG_SUCCESS) {
        free(tmp_req);
        return hret;
    }

    return (hret);
}

hg_return_t margo_bulk_parallel_transfer(margo_instance_id mid,
                                         hg_bulk_op_t      op,
                                         hg_addr_t         origin_addr,
                                         hg_bulk_t         origin_handle,
                                         size_t            origin_offset,
                                         hg_bulk_t         local_handle,
                                         size_t            local_offset,
                                         size_t            size,
                                         size_t            chunk_size)
{
    unsigned    i, j;
    hg_return_t hret           = HG_SUCCESS;
    hg_return_t hret_wait      = HG_SUCCESS;
    hg_return_t hret_xfer      = HG_SUCCESS;
    size_t      remaining_size = size;

    if (chunk_size == 0) return HG_INVALID_PARAM;

    size_t count = size / chunk_size;
    if (count * chunk_size < size) count += 1;
    struct margo_request_struct* reqs = calloc(count, sizeof(*reqs));

    for (i = 0; i < count; i++) {
        if (remaining_size < chunk_size) chunk_size = remaining_size;
        hret_xfer = margo_bulk_itransfer_internal(
            mid, op, origin_addr, origin_handle, origin_offset, local_handle,
            local_offset, chunk_size, 0, reqs + i);
        if (hret_xfer != HG_SUCCESS) {
            // LCOV_EXCL_START
            hret = hret_xfer;
            goto wait;
            // LCOV_EXCL_END
        }
        origin_offset += chunk_size;
        local_offset += chunk_size;
    }

wait:
    for (j = 0; j < i; j++) {
        hret_wait = margo_wait_internal(reqs + j);
        if (hret == HG_SUCCESS && hret_wait != HG_SUCCESS) {
            // LCOV_EXCL_START
            hret = hret_wait;
            goto finish;
            // LCOV_EXCL_END
        }
    }
finish:
    free(reqs);
    return hret;
}

static void margo_thread_sleep_cb(void* arg)
{
    margo_thread_sleep_cb_dat* sleep_cb_dat = (margo_thread_sleep_cb_dat*)arg;

    /* wake up the sleeping thread */
    ABT_mutex_lock(sleep_cb_dat->mutex);
    sleep_cb_dat->is_asleep = 0;
    ABT_cond_signal(sleep_cb_dat->cond);
    ABT_mutex_unlock(sleep_cb_dat->mutex);

    return;
}

void margo_thread_sleep(margo_instance_id mid, double timeout_ms)
{
    margo_timer_t             sleep_timer;
    margo_thread_sleep_cb_dat sleep_cb_dat;

    /* monitoring */
    struct margo_monitor_sleep_args monitoring_args = {
        .timeout_ms = timeout_ms,
    };
    __MARGO_MONITOR(mid, FN_START, sleep, monitoring_args);

    // TODO: the mechanism bellow would be better off using an ABT_eventual

    /* set data needed for sleep callback */
    ABT_mutex_create(&(sleep_cb_dat.mutex));
    ABT_cond_create(&(sleep_cb_dat.cond));
    sleep_cb_dat.is_asleep = 1;

    /* initialize the sleep timer */
    margo_timer_create_with_pool(mid, margo_thread_sleep_cb, &sleep_cb_dat,
                                 ABT_POOL_NULL, &sleep_timer);
    margo_timer_start(sleep_timer, timeout_ms);

    PROGRESS_NEEDED_INCR(mid);

    /* yield thread for specified timeout */
    ABT_mutex_lock(sleep_cb_dat.mutex);
    while (sleep_cb_dat.is_asleep)
        ABT_cond_wait(sleep_cb_dat.cond, sleep_cb_dat.mutex);
    ABT_mutex_unlock(sleep_cb_dat.mutex);

    /* clean up */
    ABT_mutex_free(&sleep_cb_dat.mutex);
    ABT_cond_free(&sleep_cb_dat.cond);

    margo_timer_destroy(sleep_timer);

    PROGRESS_NEEDED_DECR(mid);

    /* monitoring */
    __MARGO_MONITOR(mid, FN_END, sleep, monitoring_args);
}

int margo_get_handler_pool(margo_instance_id mid, ABT_pool* pool)
{
    if (mid) {
        *pool = MARGO_RPC_POOL(mid);
        return 0;
    } else {
        return -1;
    }
}

int margo_get_progress_pool(margo_instance_id mid, ABT_pool* pool)
{
    if (mid) {
        *pool = MARGO_PROGRESS_POOL(mid);
        return 0;
    } else {
        return -1;
    }
}

hg_context_t* margo_get_context(margo_instance_id mid)
{
    return (mid->hg.hg_context);
}

hg_class_t* margo_get_class(margo_instance_id mid)
{
    return (mid->hg.hg_class);
}

ABT_pool margo_hg_handle_get_handler_pool(hg_handle_t h)
{
    struct margo_handle_data* data;
    ABT_pool                  pool;

    data = (struct margo_handle_data*)HG_Get_data(h);
    if (!data) return ABT_POOL_NULL;

    pool = data->pool;
    if (pool == ABT_POOL_NULL) margo_get_handler_pool(data->mid, &pool);

    return pool;
}

margo_instance_id margo_hg_info_get_instance(const struct hg_info* info)
{
    struct margo_rpc_data* data
        = (struct margo_rpc_data*)HG_Registered_data(info->hg_class, info->id);
    if (!data) return MARGO_INSTANCE_NULL;
    return data->mid;
}

margo_instance_id margo_hg_handle_get_instance(hg_handle_t h)
{
    struct margo_handle_data* data = (struct margo_handle_data*)HG_Get_data(h);
    if (!data) return MARGO_INSTANCE_NULL;
    return data->mid;
}

static void margo_rpc_data_free(void* ptr)
{
    struct margo_rpc_data* data = (struct margo_rpc_data*)ptr;
    if (data->user_data && data->user_free_callback) {
        data->user_free_callback(data->user_data);
    }
    free(data->rpc_name);
    free(data);
}

static inline hg_return_t margo_internal_progress(margo_instance_id mid,
                                                  unsigned int      timeout_ms)
{
    /* monitoring */
    struct margo_monitor_progress_args monitoring_args
        = {.timeout_ms = timeout_ms, .ret = HG_SUCCESS};
    __MARGO_MONITOR(mid, FN_START, progress, monitoring_args);

    hg_return_t hret = HG_Progress(mid->hg.hg_context, timeout_ms);
    mid->num_progress_calls++;

    /* monitoring */
    monitoring_args.ret = hret;
    __MARGO_MONITOR(mid, FN_END, progress, monitoring_args);

    return hret;
}

static inline hg_return_t margo_internal_trigger(margo_instance_id mid,
                                                 unsigned int      timeout_ms,
                                                 unsigned int      max_count,
                                                 unsigned int*     actual_count)
{
    /* monitoring */
    struct margo_monitor_trigger_args monitoring_args
        = {.timeout_ms   = timeout_ms,
           .max_count    = max_count,
           .actual_count = 0,
           .ret          = HG_SUCCESS};
    __MARGO_MONITOR(mid, FN_START, trigger, monitoring_args);

    unsigned int count = 0;
    hg_return_t  hret
        = HG_Trigger(mid->hg.hg_context, timeout_ms, max_count, &count);
    mid->num_trigger_calls++;
    if (hret == HG_SUCCESS && actual_count) *actual_count = count;

    /* monitoring */
    monitoring_args.ret          = hret;
    monitoring_args.actual_count = count;
    __MARGO_MONITOR(mid, FN_END, trigger, monitoring_args);

    return hret;
}

/* dedicated thread function to drive Mercury progress */
void __margo_hg_progress_fn(void* foo)
{
    int                    ret;
    unsigned int           actual_count;
    struct margo_instance* mid = (struct margo_instance*)foo;
    size_t                 size;
    unsigned int           hg_progress_timeout;
    double                 next_timer_exp;
    unsigned int           pending;
    int                    spin_flag     = 0;
    double                 spin_start_ts = 0;

    while (!mid->hg_progress_shutdown_flag) {

        /* Wait for progress to actually be needed */
        WAIT_FOR_PROGRESS_TO_BE_NEEDED(mid);

        do {
            ret = margo_internal_trigger(mid, 0, 1, &actual_count);
        } while ((ret == HG_SUCCESS) && actual_count
                 && !mid->hg_progress_shutdown_flag);

        /* Yield now to give an opportunity for this ES to either a) run other
         * ULTs that are eligible in this pool or b) check for runnable ULTs
         * in other pools that the ES is associated with.
         */
        ABT_thread_yield();

        if (spin_flag) {
            /* We used a zero progress timeout (busy spinning) on the last
             * iteration.  See if spindown time has elapsed yet.
             */
            if (((ABT_get_wtime() - spin_start_ts) * 1000)
                < (double)mid->hg_progress_spindown_msec) {
                /* We are still in the spindown window; continue spinning
                 * regardless of current conditions.
                 */
                spin_flag = 1;
            } else {
                /* This spindown window has elapsed; clear flag and timestep
                 * so that we can make a new policy decision.
                 */
                spin_flag     = 0;
                spin_start_ts = 0;
            }
        }

        if (mid->hg_progress_spindown_msec && !spin_flag) {
            /* Determine if it is reasonably safe to briefly block on
             * Mercury progress or if we should enter spin mode.  We check
             * two conditions: are there any RPCs currently being processed
             * (i.e. pending_operations) or are there any other threads
             * assicated with the current pool that might become runnable
             * while this thread is blocked?  If either condition is met,
             * then we use a zero timeout to Mercury to avoid blocking this
             * ULT for too long.
             *
             * Note that there is no easy way to determine if this ES is
             * expected to also execute work in other pools, so we may
             * still introduce hg_progress_timeout_ub of latency in that
             * configuration scenario.  Latency-sensitive use cases
             * should avoid running the Margo progress function in pools
             * that share execution streams with other pools.
             */
            ABT_mutex_lock(mid->pending_operations_mtx);
            pending = mid->pending_operations;
            ABT_mutex_unlock(mid->pending_operations_mtx);

            /*
             * Note that we intentionally use get_total_size() rather
             * than get_size() to make sure that we count suspended
             * ULTs, not just currently runnable ULTs.  The resulting
             * count includes this ULT so we look for a count > 1
             * instead of a count > 0.
             */
            ABT_pool_get_total_size(MARGO_PROGRESS_POOL(mid), &size);

            if (pending || size > 1) {
                /* entering spin mode; record timestamp so that we can
                 * track how long we have been in this mode
                 */
                spin_flag     = 1;
                spin_start_ts = ABT_get_wtime();
            } else {
                /* Block on Mercury progress to release CPU */
                spin_flag     = 0;
                spin_start_ts = 0;
            }
        }

        if (spin_flag) {
            hg_progress_timeout = 0;
        } else {
            hg_progress_timeout = mid->hg_progress_timeout_ub;
            ret = __margo_timer_get_next_expiration(mid, &next_timer_exp);
            if (ret == 0) {
                /* there is a queued timer, don't block long enough
                 * to keep this timer waiting
                 */
                if (next_timer_exp >= 0.0) {
                    next_timer_exp *= 1000; /* convert to milliseconds */
                    if (next_timer_exp < mid->hg_progress_timeout_ub)
                        hg_progress_timeout = (unsigned int)next_timer_exp;
                } else {
                    hg_progress_timeout = 0;
                }
            }
        }

        ret = margo_internal_progress(mid, hg_progress_timeout);
        if (ret != HG_SUCCESS && ret != HG_TIMEOUT) {
            /* TODO: error handling */
            MARGO_CRITICAL(mid,
                           "unexpected return code (%d: %s) from HG_Progress()",
                           ret, HG_Error_to_string(ret));
            assert(0);
        }

        /* check for any expired timers */
        __margo_check_timers(mid);
    }

    return;
}

int margo_set_progress_timeout_ub_msec(margo_instance_id mid, unsigned timeout)
{
    if (!mid) return -1;
    mid->hg_progress_timeout_ub = timeout;
    return 0;
}

int margo_get_progress_timeout_ub_msec(margo_instance_id mid, unsigned* timeout)
{
    if (!mid) return -1;
    if (timeout) *timeout = mid->hg_progress_timeout_ub;
    return 0;
}

uint64_t margo_get_num_progress_calls(margo_instance_id mid)
{
    if (!mid) return 0;
    return mid->num_progress_calls;
}

uint64_t margo_get_num_trigger_calls(margo_instance_id mid)
{
    if (!mid) return 0;
    return mid->num_trigger_calls;
}

int margo_set_param(margo_instance_id mid, const char* key, const char* value)
{
    if (strcmp(key, "progress_timeout_ub_msecs") == 0) {
        MARGO_TRACE(0, "Setting progress_timeout_ub_msecs to %s", value);
        int progress_timeout_ub_msecs = atoi(value);
        mid->hg_progress_timeout_ub   = progress_timeout_ub_msecs;
        return 0;
    }

    /* unknown key, or at least one that cannot be modified at runtime */
    return -1;
}

static hg_id_t margo_register_internal(margo_instance_id mid,
                                       const char*       name,
                                       hg_id_t           id,
                                       hg_proc_cb_t      in_proc_cb,
                                       hg_proc_cb_t      out_proc_cb,
                                       hg_rpc_cb_t       rpc_cb,
                                       ABT_pool          pool)
{
    struct margo_rpc_data* margo_data;
    hg_return_t            hret;

    /* check pool */
    if (pool == ABT_POOL_NULL) { margo_get_handler_pool(mid, &pool); }

    /* monitoring */
    struct margo_monitor_register_args monitoring_args
        = {.name = name, .pool = pool, .id = id, .ret = HG_SUCCESS};
    __MARGO_MONITOR(mid, FN_START, register, monitoring_args);

    /* register the RPC with Mercury */
    hret = HG_Register(mid->hg.hg_class, id, margo_forward_proc,
                       margo_respond_proc, rpc_cb);
    if (hret != HG_SUCCESS) {
        id = 0;
        margo_error(mid, "HG_Register failed for RPC %s with id %lu",
                    name ? name : "???", id);
        goto finish;
    }

    /* register the margo data with the RPC */
    margo_data
        = (struct margo_rpc_data*)HG_Registered_data(mid->hg.hg_class, id);
    if (!margo_data) {
        margo_data
            = (struct margo_rpc_data*)malloc(sizeof(struct margo_rpc_data));
        if (!margo_data) {
            // LCOV_EXCL_START
            margo_error(
                mid,
                "margo_rpc_data allocation failed in margo_register_internal");
            id = 0;
            goto finish;
            // LCOV_EXCL_END
        }
        margo_data->mid                = mid;
        margo_data->pool               = pool;
        margo_data->rpc_name           = name ? strdup(name) : NULL;
        margo_data->in_proc_cb         = in_proc_cb;
        margo_data->out_proc_cb        = out_proc_cb;
        margo_data->user_data          = NULL;
        margo_data->user_free_callback = NULL;
        hret = HG_Register_data(mid->hg.hg_class, id, margo_data,
                                margo_rpc_data_free);
        if (hret != HG_SUCCESS) {
            // LCOV_EXCL_START
            margo_error(mid, "HG_Register_data failed for RPC %s with id %lu",
                        name ? name : "???", id);
            id = 0;
            free(margo_data);
            goto finish;
            // LCOV_EXCL_END
        }
    }

    /* increment the number of RPC ids using the pool */
    struct margo_pool_info pool_info;
    if (margo_find_pool_by_handle(mid, pool, &pool_info) == HG_SUCCESS) {
        mid->abt.pools[pool_info.index].refcount++;
    }

finish:

    /* monitoring */
    monitoring_args.ret = hret;
    __MARGO_MONITOR(mid, FN_END, register, monitoring_args);

    return id;
}

int __margo_internal_finalize_requested(margo_instance_id mid)
{
    if (!mid) return 0;
    return mid->finalize_requested;
}

int __margo_internal_incr_pending(margo_instance_id mid)
{
    if (!mid) return 0;
    int ret = 1;
    ABT_mutex_lock(mid->pending_operations_mtx);
    if (mid->finalize_requested)
        ret = 0;
    else
        mid->pending_operations += 1;
    ABT_mutex_unlock(mid->pending_operations_mtx);
    return ret;
}

void __margo_internal_decr_pending(margo_instance_id mid)
{
    if (!mid) return;
    ABT_mutex_lock(mid->pending_operations_mtx);
    mid->pending_operations -= 1;
    ABT_mutex_unlock(mid->pending_operations_mtx);
}

hg_return_t margo_set_current_rpc_id(margo_instance_id mid, hg_id_t parent_id)
{
    if (mid == MARGO_INSTANCE_NULL) return HG_INVALID_ARG;
    // rely on the fact that sizeof(void*) == sizeof(hg_id_t)
    if (parent_id == 0) parent_id = mux_id(0, MARGO_DEFAULT_PROVIDER_ID);
    int ret = ABT_key_set(mid->current_rpc_id_key, (void*)parent_id);
    if (ret != ABT_SUCCESS) return HG_OTHER_ERROR;
    return HG_SUCCESS;
}

hg_return_t margo_get_current_rpc_id(margo_instance_id mid, hg_id_t* parent_id)
{
    if (mid == MARGO_INSTANCE_NULL) return HG_INVALID_ARG;
    int ret = ABT_key_get(mid->current_rpc_id_key, (void**)parent_id);
    if (ret != ABT_SUCCESS || *parent_id == 0) {
        *parent_id = mux_id(0, MARGO_DEFAULT_PROVIDER_ID);
        return HG_OTHER_ERROR;
    }
    return HG_SUCCESS;
}

void __margo_internal_pre_handler_hooks(
    margo_instance_id                      mid,
    hg_handle_t                            handle,
    struct margo_monitor_rpc_handler_args* monitoring_args)
{
    hg_id_t parent_id = 0;
    check_parent_id_in_input(handle, &parent_id);
    monitoring_args->parent_rpc_id = parent_id;

    /* monitoring */
    __MARGO_MONITOR(mid, FN_START, rpc_handler, (*monitoring_args));
}

void __margo_internal_post_handler_hooks(
    margo_instance_id                      mid,
    struct margo_monitor_rpc_handler_args* monitoring_args)
{
    /* monitoring */
    __MARGO_MONITOR(mid, FN_END, rpc_handler, (*monitoring_args));
}

void __margo_internal_pre_wrapper_hooks(
    margo_instance_id                  mid,
    hg_handle_t                        handle,
    struct margo_monitor_rpc_ult_args* monitoring_args)
{
    const struct hg_info* info = margo_get_info(handle);
    if (!info) return;
    margo_set_current_rpc_id(mid, info->id);

    /* monitoring */
    __MARGO_MONITOR(mid, FN_START, rpc_ult, (*monitoring_args));

    margo_ref_incr(handle);
}

void __margo_internal_post_wrapper_hooks(
    margo_instance_id mid, struct margo_monitor_rpc_ult_args* monitoring_args)
{
    /* monitoring */
    __MARGO_MONITOR(mid, FN_END, rpc_ult, (*monitoring_args));

    margo_destroy(monitoring_args->handle);

    __margo_internal_decr_pending(mid);
    if (__margo_internal_finalize_requested(mid)) { margo_finalize(mid); }
}

static void margo_handle_data_free(void* args)
{
    /* Note: normally this function should not be called by Mercury
     * because we are manually freeing the handle's data and calling
     * HG_Set_data(handle, NULL, NULL) in margo_destroy.
     */
    struct margo_handle_data* handle_data = (struct margo_handle_data*)args;
    if (!handle_data) return;
    if (handle_data->user_free_callback)
        handle_data->user_free_callback(handle_data->user_data);
    free(handle_data);
}

hg_return_t __margo_internal_set_handle_data(hg_handle_t handle)
{
    struct margo_rpc_data* rpc_data;
    const struct hg_info*  info = HG_Get_info(handle);
    if (!info) return HG_OTHER_ERROR;
    rpc_data
        = (struct margo_rpc_data*)HG_Registered_data(info->hg_class, info->id);
    if (!rpc_data) return HG_OTHER_ERROR;
    struct margo_handle_data* handle_data;
    handle_data               = HG_Get_data(handle);
    bool handle_data_attached = handle_data != NULL;
    if (!handle_data) handle_data = calloc(1, sizeof(*handle_data));
    handle_data->mid         = rpc_data->mid;
    handle_data->pool        = rpc_data->pool;
    handle_data->rpc_name    = rpc_data->rpc_name;
    handle_data->in_proc_cb  = rpc_data->in_proc_cb;
    handle_data->out_proc_cb = rpc_data->out_proc_cb;
    if (!handle_data_attached)
        return HG_Set_data(handle, handle_data, margo_handle_data_free);
    else
        return HG_SUCCESS;
}

const char* margo_rpc_get_name(margo_instance_id mid, hg_id_t id)
{
    struct margo_rpc_data* data
        = (struct margo_rpc_data*)HG_Registered_data(margo_get_class(mid), id);
    if (!data)
        return NULL;
    else
        return data->rpc_name;
}

hg_return_t
margo_rpc_get_pool(margo_instance_id mid, hg_id_t id, ABT_pool* pool)
{
    if (mid == MARGO_INSTANCE_NULL) return HG_INVALID_ARG;
    struct margo_rpc_data* data
        = (struct margo_rpc_data*)HG_Registered_data(margo_get_class(mid), id);
    if (!data) return HG_NOENTRY;
    if (pool) *pool = data->pool;
    return HG_SUCCESS;
}

hg_return_t margo_rpc_set_pool(margo_instance_id mid, hg_id_t id, ABT_pool pool)
{
    if (mid == MARGO_INSTANCE_NULL) return HG_INVALID_ARG;
    struct margo_rpc_data* data
        = (struct margo_rpc_data*)HG_Registered_data(margo_get_class(mid), id);
    if (!data) return HG_NOENTRY;
    __margo_abt_lock(&mid->abt);
    if (pool == ABT_POOL_NULL) margo_get_handler_pool(mid, &pool);
    int old_pool_entry_idx
        = __margo_abt_find_pool_by_handle(&mid->abt, data->pool);
    int new_pool_entry_idx = __margo_abt_find_pool_by_handle(&mid->abt, pool);
    if (old_pool_entry_idx >= 0) mid->abt.pools[old_pool_entry_idx].refcount--;
    if (new_pool_entry_idx >= 0)
        mid->abt.pools[new_pool_entry_idx].refcount++;
    else
        margo_warning(mid, "Associating RPC with a pool not know to Margo");
    __margo_abt_unlock(&mid->abt);
    data->pool = pool;
    return HG_SUCCESS;
}

const char* margo_handle_get_name(hg_handle_t handle)
{
    struct margo_handle_data* handle_data = HG_Get_data(handle);
    return handle_data ? handle_data->rpc_name : NULL;
}

hg_return_t check_error_in_output(hg_handle_t handle)
{
    const struct hg_info* info = HG_Get_info(handle);
    hg_bool_t             disabled;
    hg_return_t           hret
        = HG_Registered_disabled_response(info->hg_class, info->id, &disabled);
    if (hret != HG_SUCCESS) return hret;
    if (disabled) return HG_SUCCESS;

    struct margo_respond_proc_args respond_args = {
        .user_args = NULL, .user_cb = NULL, .header = {.hg_ret = HG_SUCCESS}};

    hret = HG_Get_output(handle, (void*)&respond_args);
    // note: if mercury was compiled with +checksum, the call above
    // will return HG_CHECKSUM_ERROR because we are not reading the
    // whole output.
    if (hret != HG_SUCCESS && hret != HG_CHECKSUM_ERROR)
        return hret;
    else if (hret == HG_CHECKSUM_ERROR)
        return respond_args.header.hg_ret;
    hret = respond_args.header.hg_ret;
    HG_Free_output(handle, (void*)&respond_args);
    return hret;
}

hg_return_t check_parent_id_in_input(hg_handle_t handle, hg_id_t* parent_id)
{
    struct margo_forward_proc_args forward_args
        = {.user_args = NULL, .user_cb = NULL};

    hg_return_t hret = HG_Get_input(handle, (void*)&forward_args);
    // note: if mercury was compiled with +checksum, the call above
    // will return HG_CHECKSUM_ERROR because we are not reading the
    // whole input.
    if (hret != HG_SUCCESS && hret != HG_CHECKSUM_ERROR) return hret;
    *parent_id = forward_args.header.parent_rpc_id;
    if (hret == HG_CHECKSUM_ERROR) return HG_SUCCESS;
    HG_Free_input(handle, (void*)&forward_args);
    return HG_SUCCESS;
}

hg_return_t _handler_for_NULL(hg_handle_t handle)
{
    __margo_respond_with_error(handle, HG_NOENTRY);
    margo_destroy(handle);
    return HG_SUCCESS;
}

int margo_set_progress_when_needed(margo_instance_id mid, bool when_needed)
{
    if (mid == MARGO_INSTANCE_NULL) return -1;
    mid->progress_when_needed.flag = when_needed;
    if (!when_needed) {
        ABT_cond_signal(
            ABT_COND_MEMORY_GET_HANDLE(&mid->progress_when_needed.cond));
    }
    return 0;
}

int margo_migrate_progress_loop(margo_instance_id mid, unsigned pool_idx)
{
    if (mid == MARGO_INSTANCE_NULL) return ABT_ERR_INV_ARG;
    if (pool_idx >= mid->abt.pools_len) return ABT_ERR_INV_ARG;
    if (pool_idx == mid->progress_pool_idx) return 0;
    mid->progress_pool_idx = pool_idx;
    ABT_pool target_pool = mid->abt.pools[pool_idx].pool;
    return ABT_thread_migrate_to_pool(mid->hg_progress_tid, target_pool);
}
