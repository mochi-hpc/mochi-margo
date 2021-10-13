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
#include "margo-diag-internal.h"
#include "margo-handle-cache.h"
#include "margo-logging.h"
#include "margo-instance.h"
#include "margo-bulk-util.h"
#include "margo-timer.h"
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

static inline void demux_id(hg_id_t in, hg_id_t* base_id, uint16_t* provider_id)
{
    /* retrieve low bits for provider */
    *provider_id = 0;
    *provider_id += (in & (((1 << (__MARGO_PROVIDER_ID_SIZE * 8)) - 1)));

    /* clear low order bits */
    *base_id = (in >> (__MARGO_PROVIDER_ID_SIZE * 8))
            << (__MARGO_PROVIDER_ID_SIZE * 8);

    return;
}

static inline hg_id_t mux_id(hg_id_t base_id, uint16_t provider_id)
{
    hg_id_t id;

    id = (base_id >> (__MARGO_PROVIDER_ID_SIZE * 8))
      << (__MARGO_PROVIDER_ID_SIZE * 8);
    id |= provider_id;

    return id;
}

static inline hg_id_t gen_id(const char* func_name, uint16_t provider_id)
{
    hg_id_t  id;
    unsigned hashval;

    HASH_JEN(func_name, strlen(func_name), hashval);
    id = hashval << (__MARGO_PROVIDER_ID_SIZE * 8);
    id |= provider_id;

    return id;
}

static hg_id_t margo_register_internal(margo_instance_id mid,
                                       hg_id_t           id,
                                       hg_proc_cb_t      in_proc_cb,
                                       hg_proc_cb_t      out_proc_cb,
                                       hg_rpc_cb_t       rpc_cb,
                                       ABT_pool          pool);

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

static void margo_cleanup(margo_instance_id mid)
{
    MARGO_TRACE(mid, "Entering margo_cleanup");
    struct margo_registered_rpc* next_rpc;

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

    MARGO_TRACE(mid, "Destroying mutex and condition variables");
    ABT_mutex_free(&mid->finalize_mutex);
    ABT_cond_free(&mid->finalize_cond);
    ABT_mutex_free(&mid->pending_operations_mtx);
    ABT_mutex_free(&mid->diag_rpc_mutex);

    MARGO_TRACE(mid, "Joining and destroying xstreams");
    for (unsigned i = 0; i < mid->num_abt_xstreams; i++) {
        if (mid->owns_abt_xstream[i]) {
            ABT_xstream_join(mid->abt_xstreams[i]);
            ABT_xstream_free(&mid->abt_xstreams[i]);
        }
    }

    MARGO_TRACE(mid, "Destroying handle cache");
    __margo_handle_cache_destroy(mid);

    if (mid->hg_ownership & MARGO_OWNS_HG_CONTEXT) {
        MARGO_TRACE(mid, "Destroying mercury context");
        HG_Context_destroy(mid->hg_context);
    }

    if (mid->hg_ownership & MARGO_OWNS_HG_CLASS) {
        MARGO_TRACE(mid, "Destroying mercury class");
        HG_Finalize(mid->hg_class);
    }

    MARGO_TRACE(mid, "Checking if Argobots should be finalized");
    if (g_margo_num_instances_mtx != ABT_MUTEX_NULL) {
        ABT_mutex_lock(g_margo_num_instances_mtx);
        g_margo_num_instances -= 1;
        if (g_margo_num_instances > 0) {
            ABT_mutex_unlock(g_margo_num_instances_mtx);
        } else {
            /* this is the last margo instance */
            ABT_mutex_unlock(g_margo_num_instances_mtx);
            ABT_mutex_free(&g_margo_num_instances_mtx);
            g_margo_num_instances_mtx = ABT_MUTEX_NULL;

            /* free global keys used for profiling */
            ABT_key_free(&g_margo_rpc_breadcrumb_key);
            ABT_key_free(&g_margo_target_timing_key);

            /* shut down global abt profiling if needed */
            if (g_margo_abt_prof_init) {
                if (g_margo_abt_prof_started) {
                    ABTX_prof_stop(g_margo_abt_prof_context);
                    g_margo_abt_prof_started = 0;
                }
                ABTX_prof_finalize(g_margo_abt_prof_context);
                g_margo_abt_prof_init = 0;
            }

            /* shut down argobots itself if needed */
            if (g_margo_abt_init) {
                MARGO_TRACE(mid, "Finalizing argobots");
                ABT_finalize();
            }
        }
    }

    MARGO_TRACE(mid, "Cleaning up RPC data");
    while (mid->registered_rpcs) {
        next_rpc = mid->registered_rpcs->next;
        free(mid->registered_rpcs);
        mid->registered_rpcs = next_rpc;
    }

    MARGO_TRACE(mid, "Destroying JSON configuration");
    json_object_put(mid->json_cfg);

    MARGO_TRACE(mid, "Cleaning up margo instance");
    /* free any pools that Margo itself is reponsible for */
    for (unsigned i = 0; i < mid->num_abt_pools; i++) {
        if (mid->abt_pools[i].margo_free_flag
            && mid->abt_pools[i].pool != ABT_POOL_NULL)
            ABT_pool_free(&mid->abt_pools[i].pool);
    }
    free(mid->abt_pools);
    free(mid->abt_xstreams);
    free(mid->owns_abt_xstream);
    free(mid);

    MARGO_TRACE(0, "Completed margo_cleanup");
}

void margo_finalize(margo_instance_id mid)
{
    MARGO_TRACE(mid, "Calling margo_finalize");
    int do_cleanup;

    /* check if there are pending operations */
    int pending;
    ABT_mutex_lock(mid->pending_operations_mtx);
    pending = mid->pending_operations;
    ABT_mutex_unlock(mid->pending_operations_mtx);
    if (pending) {
        MARGO_TRACE(mid, "Pending operations, exiting margo_finalize");
        mid->finalize_requested = 1;
        return;
    }

    MARGO_TRACE(mid, "Executing pre-finalize callbacks");
    /* before exiting the progress loop, pre-finalize callbacks need to be
     * called */
    struct margo_finalize_cb* fcb = mid->prefinalize_cb;
    while (fcb) {
        mid->prefinalize_cb = fcb->next;
        (fcb->callback)(fcb->uargs);
        struct margo_finalize_cb* tmp = fcb;
        fcb                           = mid->prefinalize_cb;
        free(tmp);
    }

    /* tell progress thread to wrap things up */
    mid->hg_progress_shutdown_flag = 1;

    /* wait for it to shutdown cleanly */
    MARGO_TRACE(mid, "Waiting for progress thread to complete");
    ABT_thread_join(mid->hg_progress_tid);
    ABT_thread_free(&mid->hg_progress_tid);

    /* shut down pending timers */
    MARGO_TRACE(mid, "Cleaning up pending timers");
    __margo_timer_list_free(mid, mid->timer_list);

    if (mid->profile_enabled) {
        __margo_sparkline_thread_stop(mid);

        MARGO_TRACE(mid, "Dumping profile");
        margo_profile_dump(mid, "profile", 1);
    }

    if (mid->diag_enabled) {
        MARGO_TRACE(mid, "Dumping diagnostics");
        margo_diag_dump(mid, "profile", 1);
    }

    ABT_mutex_lock(mid->finalize_mutex);
    mid->finalize_flag = 1;
    mid->refcount--;
    do_cleanup = mid->refcount == 0;

    ABT_mutex_unlock(mid->finalize_mutex);
    ABT_cond_broadcast(mid->finalize_cond);

    /* if there was noone waiting on the finalize at the time of the finalize
     * broadcast, then we're safe to clean up. Otherwise, let the finalizer do
     * it */
    if (do_cleanup) margo_cleanup(mid);

    MARGO_TRACE(NULL, "Finalize completed");
    return;
}

void margo_wait_for_finalize(margo_instance_id mid)
{
    MARGO_TRACE(mid, "Start waiting for finalize");
    int do_cleanup;

    ABT_mutex_lock(mid->finalize_mutex);

    mid->refcount++;

    while (!mid->finalize_flag)
        ABT_cond_wait(mid->finalize_cond, mid->finalize_mutex);

    mid->refcount--;
    do_cleanup = mid->refcount == 0;

    ABT_mutex_unlock(mid->finalize_mutex);

    if (do_cleanup) margo_cleanup(mid);

    MARGO_TRACE(NULL, "Done waiting for finalize");
    return;
}

hg_bool_t margo_is_listening(margo_instance_id mid)
{
    if (!mid) return HG_FALSE;
    return HG_Class_is_listening(mid->hg_class);
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
    int                          ret;
    struct margo_registered_rpc* tmp_rpc;

    id = gen_id(func_name, provider_id);

    /* track information about this rpc registration for debugging and
     * profiling
     * NOTE: we do this even if profiling is currently disabled; it may be
     * enabled later on at run time.
     */
    tmp_rpc = calloc(1, sizeof(*tmp_rpc));
    if (!tmp_rpc) return (0);
    tmp_rpc->id                      = id;
    tmp_rpc->rpc_breadcrumb_fragment = id >> (__MARGO_PROVIDER_ID_SIZE * 8);
    tmp_rpc->rpc_breadcrumb_fragment &= 0xffff;
    strncpy(tmp_rpc->func_name, func_name, 63);
    tmp_rpc->next        = mid->registered_rpcs;
    mid->registered_rpcs = tmp_rpc;
    mid->num_registered_rpcs++;

    ret = margo_register_internal(mid, id, in_proc_cb, out_proc_cb, rpc_cb,
                                  pool);
    if (ret == 0) {
        mid->registered_rpcs = tmp_rpc->next;
        free(tmp_rpc);
        mid->num_registered_rpcs--;
        return (id);
    }

    return (id);
}

hg_return_t margo_deregister(margo_instance_id mid, hg_id_t rpc_id)
{
    return HG_Deregister(mid->hg_class, rpc_id);
}

hg_return_t margo_registered_name(margo_instance_id mid,
                                  const char*       func_name,
                                  hg_id_t*          id,
                                  hg_bool_t*        flag)
{
    *id = gen_id(func_name, 0);
    return (HG_Registered(mid->hg_class, *id, flag));
}

hg_return_t margo_provider_registered_name(margo_instance_id mid,
                                           const char*       func_name,
                                           uint16_t          provider_id,
                                           hg_id_t*          id,
                                           hg_bool_t*        flag)
{
    *id = gen_id(func_name, provider_id);

    return HG_Registered(mid->hg_class, *id, flag);
}

hg_return_t margo_register_data(margo_instance_id mid,
                                hg_id_t           id,
                                void*             data,
                                void (*free_callback)(void*))
{
    struct margo_rpc_data* margo_data
        = (struct margo_rpc_data*)HG_Registered_data(mid->hg_class, id);
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
    return (HG_Registered_disable_response(mid->hg_class, id, disable_flag));
}

hg_return_t margo_registered_disabled_response(margo_instance_id mid,
                                               hg_id_t           id,
                                               int*              disabled_flag)
{
    hg_bool_t   b;
    hg_return_t ret = HG_Registered_disabled_response(mid->hg_class, id, &b);
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
    /* Mercury 2.x provides two versions of lookup (async and sync).  Choose the
     * former if available to avoid context switch
     */
    hret = HG_Addr_lookup2(mid->hg_class, name, addr);

#else /* !defined HG_Addr_lookup */
    struct lookup_cb_evt* evt;
    ABT_eventual          eventual;
    int                   ret;

    ret = ABT_eventual_create(sizeof(*evt), &eventual);
    if (ret != 0) { return (HG_NOMEM_ERROR); }

    hret = HG_Addr_lookup(mid->hg_context, margo_addr_lookup_cb,
                          (void*)eventual, name, HG_OP_ID_IGNORE);
    if (hret == HG_SUCCESS) {
        ABT_eventual_wait(eventual, (void**)&evt);
        *addr = evt->addr;
        hret  = evt->hret;
    }

    ABT_eventual_free(&eventual);
#endif

    return (hret);
}

hg_return_t margo_addr_free(margo_instance_id mid, hg_addr_t addr)
{
    return (HG_Addr_free(mid->hg_class, addr));
}

hg_return_t margo_addr_self(margo_instance_id mid, hg_addr_t* addr)
{
    return (HG_Addr_self(mid->hg_class, addr));
}

hg_return_t
margo_addr_dup(margo_instance_id mid, hg_addr_t addr, hg_addr_t* new_addr)
{
    return (HG_Addr_dup(mid->hg_class, addr, new_addr));
}

hg_bool_t
margo_addr_cmp(margo_instance_id mid, hg_addr_t addr1, hg_addr_t addr2)
{
    return HG_Addr_cmp(mid->hg_class, addr1, addr2);
}

hg_return_t margo_addr_set_remove(margo_instance_id mid, hg_addr_t addr)
{
    return HG_Addr_set_remove(mid->hg_class, addr);
}

hg_return_t margo_addr_to_string(margo_instance_id mid,
                                 char*             buf,
                                 hg_size_t*        buf_size,
                                 hg_addr_t         addr)
{
    return (HG_Addr_to_string(mid->hg_class, buf, buf_size, addr));
}

hg_return_t margo_create(margo_instance_id mid,
                         hg_addr_t         addr,
                         hg_id_t           id,
                         hg_handle_t*      handle)
{
    hg_return_t hret = HG_OTHER_ERROR;

    /* look for a handle to reuse */
    hret = __margo_handle_cache_get(mid, addr, id, handle);
    if (hret != HG_SUCCESS) {
        /* else try creating a new handle */
        hret = HG_Create(mid->hg_context, addr, id, handle);
    }

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

    /* use the handle to get the associated mid */
    mid = margo_hg_handle_get_instance(handle);

    /* recycle this handle if it came from the handle cache */
    hret = __margo_handle_cache_put(mid, handle);
    if (hret != HG_SUCCESS) {
        /* else destroy the handle manually */
        hret = HG_Destroy(handle);
    }

    return hret;
}

static hg_return_t margo_cb(const struct hg_cb_info* info)
{
    hg_return_t       hret = info->ret;
    margo_request     req  = (margo_request)(info->arg);
    margo_instance_id mid;

    if (hret == HG_CANCELED && req->timer) { hret = HG_TIMEOUT; }

    /* remove timer if there is one and it is still in place (i.e., not timed
     * out) */
    if (hret != HG_TIMEOUT && req->timer && req->handle) {
        margo_instance_id mid = margo_hg_handle_get_instance(req->handle);
        __margo_timer_destroy(mid, req->timer);
    }
    if (req->timer) { free(req->timer); }

    if (req->rpc_breadcrumb != 0) {
        /* This is the callback from an HG_Forward call.  Track RPC timing
         * information.
         */
        mid = margo_hg_handle_get_instance(req->handle);

        if (mid->profile_enabled) {
            /* 0 here indicates this is a origin-side call */
            __margo_breadcrumb_measure(mid, req->rpc_breadcrumb,
                                       req->start_time, 0, req->provider_id,
                                       req->server_addr_hash, req->handle);
        }
    }

    /* propagate return code out through eventual */
    req->hret = hret;
    MARGO_EVENTUAL_SET(req->eventual);

    return (HG_SUCCESS);
}

static hg_return_t margo_wait_internal(margo_request req)
{
    MARGO_EVENTUAL_WAIT(req->eventual);
    MARGO_EVENTUAL_FREE(&(req->eventual));
    return req->hret;
}

static void margo_forward_timeout_cb(void* arg)
{
    margo_request req = (margo_request)arg;
    /* cancel the Mercury op if the forward timed out */
    HG_Cancel(req->handle);
    return;
}

static hg_return_t margo_provider_iforward_internal(
    uint16_t      provider_id,
    hg_handle_t   handle,
    double        timeout_ms,
    void*         in_struct,
    margo_request req) /* the request should have been allocated */
{
    hg_return_t           hret = HG_TIMEOUT;
    margo_eventual_t      eventual;
    int                   ret;
    const struct hg_info* hgi;
    hg_id_t               id;
    hg_proc_cb_t          in_cb, out_cb;
    hg_bool_t             flag;
    margo_instance_id     mid = margo_hg_handle_get_instance(handle);
    uint64_t*             rpc_breadcrumb;
    char                  addr_string[128];
    hg_size_t             addr_string_sz = 128;

    hgi = HG_Get_info(handle);
    id  = mux_id(hgi->id, provider_id);

    hg_bool_t is_registered;
    ret = HG_Registered(hgi->hg_class, id, &is_registered);
    if (ret != HG_SUCCESS) return (ret);
    if (!is_registered) {
        /* if Mercury does not recognize this ID (with provider id included)
         * then register it now
         */
        /* find encoders for base ID */
        ret = HG_Registered_proc_cb(hgi->hg_class, hgi->id, &flag, &in_cb,
                                    &out_cb);
        if (ret != HG_SUCCESS) return (ret);
        if (!flag) return (HG_NO_MATCH);

        /* find out if disable_response was called for this RPC */
        hg_bool_t response_disabled;
        ret = HG_Registered_disabled_response(hgi->hg_class, hgi->id,
                                              &response_disabled);
        if (ret != HG_SUCCESS) return (ret);

        /* register new ID that includes provider id */
        ret = margo_register_internal(margo_hg_info_get_instance(hgi), id,
                                      in_cb, out_cb, NULL, ABT_POOL_NULL);
        if (ret == 0) return (HG_OTHER_ERROR);
        ret = HG_Registered_disable_response(hgi->hg_class, id,
                                             response_disabled);
        if (ret != HG_SUCCESS) return (ret);
    }
    ret = HG_Reset(handle, hgi->addr, id);
    if (ret != HG_SUCCESS) return (ret);

    ret = MARGO_EVENTUAL_CREATE(&eventual);
    if (ret != 0) { return (HG_NOMEM_ERROR); }

    req->timer    = NULL;
    req->eventual = eventual;
    req->handle   = handle;

    if (timeout_ms > 0) {
        /* set a timer object to expire when this forward times out */
        req->timer = calloc(1, sizeof(*(req->timer)));
        // LCOV_EXCL_START
        if (!(req->timer)) {
            MARGO_EVENTUAL_FREE(&eventual);
            return (HG_NOMEM_ERROR);
        }
        // LCOV_EXCL_END
        __margo_timer_init(mid, req->timer, margo_forward_timeout_cb, req,
                           timeout_ms);
    }

    /* add rpc breadcrumb to outbound request; this will be used to track
     * rpc statistics.
     */

    req->rpc_breadcrumb = 0;
    if (mid->profile_enabled) {
        ret = HG_Get_input_buf(handle, (void**)&rpc_breadcrumb, NULL);
        if (ret != HG_SUCCESS) return (ret);
        req->rpc_breadcrumb = __margo_breadcrumb_set(hgi->id);
        /* LE encoding */
        *rpc_breadcrumb = htole64(req->rpc_breadcrumb);

        req->start_time = ABT_get_wtime();

        /* add information about the server and provider servicing the request
         */
        req->provider_id
            = provider_id; /*store id of provider servicing the request */
        const struct hg_info* inf = HG_Get_info(req->handle);
        margo_addr_to_string(mid, addr_string, &addr_string_sz, inf->addr);
        HASH_JEN(
            addr_string, strlen(addr_string),
            req->server_addr_hash); /*record server address in the breadcrumb */
    }

    hret = HG_Forward(handle, margo_cb, (void*)req, in_struct);

    if (hret != HG_SUCCESS) { MARGO_EVENTUAL_FREE(&eventual); }
    /* remove timer if HG_Forward failed */
    if (hret != HG_SUCCESS && req->timer) {
        __margo_timer_destroy(mid, req->timer);
        free(req->timer);
        req->timer = NULL;
    }
    return hret;
}

hg_return_t margo_provider_forward(uint16_t    provider_id,
                                   hg_handle_t handle,
                                   void*       in_struct)
{
    return margo_provider_forward_timed(provider_id, handle, in_struct, 0);
}

hg_return_t margo_provider_iforward(uint16_t       provider_id,
                                    hg_handle_t    handle,
                                    void*          in_struct,
                                    margo_request* req)
{
    return margo_provider_iforward_timed(provider_id, handle, in_struct, 0,
                                         req);
}

hg_return_t margo_provider_forward_timed(uint16_t    provider_id,
                                         hg_handle_t handle,
                                         void*       in_struct,
                                         double      timeout_ms)
{
    hg_return_t                 hret;
    struct margo_request_struct reqs;
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

hg_return_t margo_wait(margo_request req)
{
    hg_return_t hret = margo_wait_internal(req);
    free(req);
    return hret;
}

int margo_test(margo_request req, int* flag)
{
    return MARGO_EVENTUAL_TEST(req->eventual, flag);
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

static hg_return_t
margo_irespond_internal(hg_handle_t   handle,
                        void*         out_struct,
                        margo_request req) /* should have been allocated */
{
    int ret;
    ret = MARGO_EVENTUAL_CREATE(&(req->eventual));
    if (ret != 0) { return (HG_NOMEM_ERROR); }
    req->handle         = handle;
    req->timer          = NULL;
    req->start_time     = ABT_get_wtime();
    req->rpc_breadcrumb = 0;

    return HG_Respond(handle, margo_cb, (void*)req, out_struct);
}

hg_return_t margo_respond(hg_handle_t handle, void* out_struct)
{

    /* retrieve the ULT-local key for this breadcrumb and add measurement to
     * profile */
    struct margo_request_struct* treq;
    margo_instance_id            mid = margo_hg_handle_get_instance(handle);
    if (mid->profile_enabled) {
        ABT_key_get(g_margo_target_timing_key, (void**)(&treq));
        assert(treq != NULL);

        /* the "1" indicates that this a target-side breadcrumb */
        __margo_breadcrumb_measure(mid, treq->rpc_breadcrumb, treq->start_time,
                                   1, treq->provider_id, treq->server_addr_hash,
                                   handle);
    }

    hg_return_t                 hret;
    struct margo_request_struct reqs;
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
        free(req);
        return hret;
    }
    *req = tmp_req;
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
    double      tm1, tm2;
    int         diag_enabled = mid->diag_enabled;

    if (diag_enabled) tm1 = ABT_get_wtime();
    hret = HG_Bulk_create(mid->hg_class, count, buf_ptrs, buf_sizes, flags,
                          handle);
    if (diag_enabled) {
        tm2 = ABT_get_wtime();
        __DIAG_UPDATE(mid->diag_bulk_create_elapsed, (tm2 - tm1));
    }

    return (hret);
}

hg_return_t margo_bulk_free(hg_bulk_t handle) { return (HG_Bulk_free(handle)); }

hg_return_t margo_bulk_deserialize(margo_instance_id mid,
                                   hg_bulk_t*        handle,
                                   const void*       buf,
                                   hg_size_t         buf_size)
{
    return (HG_Bulk_deserialize(mid->hg_class, handle, buf, buf_size));
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
    margo_request     req) /* should have been allocated */
{
    hg_return_t hret = HG_TIMEOUT;
    int         ret;

    ret = MARGO_EVENTUAL_CREATE(&(req->eventual));
    if (ret != 0) { return (HG_NOMEM_ERROR); }
    req->timer          = NULL;
    req->handle         = HG_HANDLE_NULL;
    req->start_time     = ABT_get_wtime();
    req->rpc_breadcrumb = 0;

    hret = HG_Bulk_transfer(mid->hg_context, margo_cb, (void*)req, op,
                            origin_addr, origin_handle, origin_offset,
                            local_handle, local_offset, size, HG_OP_ID_IGNORE);

    return (hret);
}

hg_return_t margo_bulk_transfer(margo_instance_id mid,
                                hg_bulk_op_t      op,
                                hg_addr_t         origin_addr,
                                hg_bulk_t         origin_handle,
                                size_t            origin_offset,
                                hg_bulk_t         local_handle,
                                size_t            local_offset,
                                size_t            size)
{
    struct margo_request_struct reqs;
    hg_return_t                 hret = margo_bulk_itransfer_internal(
        mid, op, origin_addr, origin_handle, origin_offset, local_handle,
        local_offset, size, &reqs);
    if (hret != HG_SUCCESS) return hret;
    return margo_wait_internal(&reqs);
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
            local_offset, chunk_size, reqs + i);
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

hg_return_t margo_bulk_itransfer(margo_instance_id mid,
                                 hg_bulk_op_t      op,
                                 hg_addr_t         origin_addr,
                                 hg_bulk_t         origin_handle,
                                 size_t            origin_offset,
                                 hg_bulk_t         local_handle,
                                 size_t            local_offset,
                                 size_t            size,
                                 margo_request*    req)
{
    margo_request tmp_req = calloc(1, sizeof(*tmp_req));
    if (!tmp_req) { return (HG_NOMEM_ERROR); }
    hg_return_t hret = margo_bulk_itransfer_internal(
        mid, op, origin_addr, origin_handle, origin_offset, local_handle,
        local_offset, size, tmp_req);
    if (hret != HG_SUCCESS) {
        free(tmp_req);
        return hret;
    }

    *req = tmp_req;

    return (hret);
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

    /* set data needed for sleep callback */
    ABT_mutex_create(&(sleep_cb_dat.mutex));
    ABT_cond_create(&(sleep_cb_dat.cond));
    sleep_cb_dat.is_asleep = 1;

    /* initialize the sleep timer */
    __margo_timer_init(mid, &sleep_timer, margo_thread_sleep_cb, &sleep_cb_dat,
                       timeout_ms);

    /* yield thread for specified timeout */
    ABT_mutex_lock(sleep_cb_dat.mutex);
    while (sleep_cb_dat.is_asleep)
        ABT_cond_wait(sleep_cb_dat.cond, sleep_cb_dat.mutex);
    ABT_mutex_unlock(sleep_cb_dat.mutex);

    /* clean up */
    ABT_mutex_free(&sleep_cb_dat.mutex);
    ABT_cond_free(&sleep_cb_dat.cond);

    return;
}

int margo_get_handler_pool(margo_instance_id mid, ABT_pool* pool)
{
    if (mid) {
        *pool = mid->rpc_pool;
        return 0;
    } else {
        return -1;
    }
}

hg_context_t* margo_get_context(margo_instance_id mid)
{
    return (mid->hg_context);
}

hg_class_t* margo_get_class(margo_instance_id mid) { return (mid->hg_class); }

ABT_pool margo_hg_handle_get_handler_pool(hg_handle_t h)
{
    struct margo_rpc_data* data;
    const struct hg_info*  info;
    ABT_pool               pool;

    info = HG_Get_info(h);
    if (!info) return ABT_POOL_NULL;

    data = (struct margo_rpc_data*)HG_Registered_data(info->hg_class, info->id);
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
    struct margo_rpc_data* data;
    const struct hg_info*  info;

    info = HG_Get_info(h);
    if (!info) return MARGO_INSTANCE_NULL;

    data = (struct margo_rpc_data*)HG_Registered_data(info->hg_class, info->id);
    if (!data) return MARGO_INSTANCE_NULL;

    return data->mid;
}

static void margo_rpc_data_free(void* ptr)
{
    struct margo_rpc_data* data = (struct margo_rpc_data*)ptr;
    if (data->user_data && data->user_free_callback) {
        data->user_free_callback(data->user_data);
    }
    free(ptr);
}

/* dedicated thread function to drive Mercury progress */
void __margo_hg_progress_fn(void* foo)
{
    int                    ret;
    unsigned int           actual_count;
    struct margo_instance* mid = (struct margo_instance*)foo;
    size_t                 size;
    unsigned int           hg_progress_timeout = mid->hg_progress_timeout_ub;
    double                 next_timer_exp;
    double                 tm1, tm2;
    int                    diag_enabled = 0;
    unsigned int           pending;

    while (!mid->hg_progress_shutdown_flag) {
        do {
            /* save value of instance diag variable, in case it is modified
             * while we are in loop
             */
            diag_enabled = mid->diag_enabled;

            if (diag_enabled) tm1 = ABT_get_wtime();
            ret = HG_Trigger(mid->hg_context, 0, 1, &actual_count);
            if (diag_enabled) {
                tm2 = ABT_get_wtime();
                __DIAG_UPDATE(mid->diag_trigger_elapsed, (tm2 - tm1));
            }
        } while ((ret == HG_SUCCESS) && actual_count
                 && !mid->hg_progress_shutdown_flag);

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
        if (size) ABT_thread_yield();

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
        if (pending || (mid->profile_enabled && size > 2)
            || (!mid->profile_enabled && size > 1)) {
            /* TODO: a custom ABT scheduler could optimize this further by
             * delaying Mercury progress until all other runnable ULTs have
             * been given a chance to execute.  This will often happen
             * anyway, but not guaranteed.
             */
            if (diag_enabled) tm1 = ABT_get_wtime();
            ret = HG_Progress(mid->hg_context, 0);
            if (diag_enabled) {
                tm2 = ABT_get_wtime();
                __DIAG_UPDATE(mid->diag_progress_elapsed_zero_timeout,
                              (tm2 - tm1));
                __DIAG_UPDATE(mid->diag_progress_timeout_value, 0);
            }
            if (ret == HG_SUCCESS) {
                /* Mercury completed something; loop around to trigger
                 * callbacks
                 */
            } else if (ret == HG_TIMEOUT) {
                /* No completion; yield here to allow other ULTs to run */
                ABT_thread_yield();
            } else {
                /* TODO: error handling */
                MARGO_CRITICAL(
                    mid, "unexpected return code (%d: %s) from HG_Progress()",
                    ret, HG_Error_to_string(ret));
                assert(0);
            }
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
            if (diag_enabled) tm1 = ABT_get_wtime();
            ret = HG_Progress(mid->hg_context, hg_progress_timeout);
            if (diag_enabled) {
                tm2 = ABT_get_wtime();
                if (hg_progress_timeout == 0)
                    __DIAG_UPDATE(mid->diag_progress_elapsed_zero_timeout,
                                  (tm2 - tm1));
                else
                    __DIAG_UPDATE(mid->diag_progress_elapsed_nonzero_timeout,
                                  (tm2 - tm1));

                __DIAG_UPDATE(mid->diag_progress_timeout_value,
                              hg_progress_timeout);
            }
            if (ret != HG_SUCCESS && ret != HG_TIMEOUT) {
                /* TODO: error handling */
                MARGO_CRITICAL(
                    mid, "unexpected return code (%d: %s) from HG_Progress()",
                    ret, HG_Error_to_string(ret));
                assert(0);
            }
        }

        /* check for any expired timers */
        __margo_check_timers(mid);
    }

    return;
}

int margo_set_param(margo_instance_id mid, const char* key, const char* value)
{
    int old_enable_profiling = 0;

    if (strcmp(key, "progress_timeout_ub_msecs") == 0) {
        MARGO_TRACE(0, "Setting progress_timeout_ub_msecs to %s", value);
        int progress_timeout_ub_msecs = atoi(value);
        mid->hg_progress_timeout_ub   = progress_timeout_ub_msecs;
        json_object_object_add(
            mid->json_cfg, "progress_timeout_ub_msecs",
            json_object_new_int64(progress_timeout_ub_msecs));
        return (0);
    }

    if (strcmp(key, "enable_diagnostics") == 0) {
        MARGO_TRACE(0, "Setting enable_diagnistics to %s", value);
        bool enable_diagnostics = atoi(value);
        mid->diag_enabled       = enable_diagnostics;
        json_object_object_add(mid->json_cfg, "enable_diagnostics",
                               json_object_new_boolean(enable_diagnostics));
        /* if argobots profiling is available then try to toggle it also */
        if (g_margo_abt_prof_init) {
            if (enable_diagnostics && !g_margo_abt_prof_started) {
                /* TODO: consider supporting PROF_MODE_DETAILED also? */
                ABTX_prof_start(g_margo_abt_prof_context, ABTX_PROF_MODE_BASIC);
                g_margo_abt_prof_started = 1;
            } else if (!enable_diagnostics && g_margo_abt_prof_started) {
                ABTX_prof_stop(g_margo_abt_prof_context);
                g_margo_abt_prof_started = 0;
            }
        }
        return (0);
    }

    /* rpc profiling */
    if (strcmp(key, "enable_profiling") == 0) {
        MARGO_TRACE(0, "Setting enable_profiling to %s", value);
        bool enable_profiling = atoi(value);
        old_enable_profiling  = mid->profile_enabled;
        mid->profile_enabled  = enable_profiling;
        if (!old_enable_profiling && enable_profiling) {
            /* toggle from off to on */
            __margo_sparkline_thread_start(mid);
        } else if (old_enable_profiling && !enable_profiling) {
            /* toggle from on to off */
            __margo_sparkline_thread_stop(mid);
        }
        json_object_object_add(mid->json_cfg, "enable_profiling",
                               json_object_new_boolean(enable_profiling));
        return (0);
    }

    /* unknown key, or at least one that cannot be modified at runtime */
    return (-1);
}

static hg_id_t margo_register_internal(margo_instance_id mid,
                                       hg_id_t           id,
                                       hg_proc_cb_t      in_proc_cb,
                                       hg_proc_cb_t      out_proc_cb,
                                       hg_rpc_cb_t       rpc_cb,
                                       ABT_pool          pool)
{
    struct margo_rpc_data* margo_data;
    hg_return_t            hret;

    hret = HG_Register(mid->hg_class, id, in_proc_cb, out_proc_cb, rpc_cb);
    if (hret != HG_SUCCESS) return (hret);

    /* register the margo data with the RPC */
    margo_data = (struct margo_rpc_data*)HG_Registered_data(mid->hg_class, id);
    if (!margo_data) {
        margo_data
            = (struct margo_rpc_data*)malloc(sizeof(struct margo_rpc_data));
        if (!margo_data) return (0);
        margo_data->mid                = mid;
        margo_data->pool               = pool;
        margo_data->user_data          = NULL;
        margo_data->user_free_callback = NULL;
        hret = HG_Register_data(mid->hg_class, id, margo_data,
                                margo_rpc_data_free);
        if (hret != HG_SUCCESS) {
            // LCOV_EXCL_START
            free(margo_data);
            return (0);
            // LCOV_EXCL_END
        }
    }

    return (id);
}

int __margo_internal_finalize_requested(margo_instance_id mid)
{
    if (!mid) return 0;
    return mid->finalize_requested;
}

void __margo_internal_incr_pending(margo_instance_id mid)
{
    if (!mid) return;
    ABT_mutex_lock(mid->pending_operations_mtx);
    mid->pending_operations += 1;
    ABT_mutex_unlock(mid->pending_operations_mtx);
}

void __margo_internal_decr_pending(margo_instance_id mid)
{
    if (!mid) return;
    ABT_mutex_lock(mid->pending_operations_mtx);
    mid->pending_operations -= 1;
    ABT_mutex_unlock(mid->pending_operations_mtx);
}

static void margo_internal_breadcrumb_handler_set(uint64_t rpc_breadcrumb)
{
    uint64_t* val;

    ABT_key_get(g_margo_rpc_breadcrumb_key, (void**)(&val));

    if (val == NULL) {
        /* key not set yet on this ULT; we need to allocate a new one */
        /* best effort; just return and don't set it if we can't allocate memory
         */
        val = malloc(sizeof(*val));
        if (!val) return;
    }
    *val = rpc_breadcrumb;

    ABT_key_set(g_margo_rpc_breadcrumb_key, val);

    return;
}

void __margo_internal_pre_wrapper_hooks(margo_instance_id mid,
                                        hg_handle_t       handle)
{
    hg_return_t                  ret;
    uint64_t*                    rpc_breadcrumb;
    const struct hg_info*        info;
    struct margo_request_struct* req;

    ret = HG_Get_input_buf(handle, (void**)&rpc_breadcrumb, NULL);
    if (ret != HG_SUCCESS) {
        // LCOV_EXCL_START
        MARGO_CRITICAL(mid,
                       "HG_Get_input_buf() failed in "
                       "__margo_internal_pre_wrapper_hooks (ret = %d)",
                       ret);
        exit(-1);
        // LCOV_EXCL_END
    }
    *rpc_breadcrumb = le64toh(*rpc_breadcrumb);

    /* add the incoming breadcrumb info to a ULT-local key if profiling is
     * enabled */
    if (mid->profile_enabled) {

        ABT_key_get(g_margo_target_timing_key, (void**)(&req));

        if (req == NULL) { req = calloc(1, sizeof(*req)); }

        req->rpc_breadcrumb = *rpc_breadcrumb;

        req->timer       = NULL;
        req->handle      = handle;
        req->start_time  = ABT_get_wtime(); /* measure start time */
        info             = HG_Get_info(handle);
        req->provider_id = 0;
        req->provider_id
            += ((info->id) & (((1 << (__MARGO_PROVIDER_ID_SIZE * 8)) - 1)));
        req->server_addr_hash = mid->self_addr_hash;

        /* Note: we use this opportunity to retrieve the incoming RPC
         * breadcrumb and put it in a thread-local argobots key.  It is
         * shifted down 16 bits so that if this handler in turn issues more
         * RPCs, there will be a stack showing the ancestry of RPC calls that
         * led to that point.
         */
        ABT_key_set(g_margo_target_timing_key, req);
        margo_internal_breadcrumb_handler_set((*rpc_breadcrumb) << 16);
    }
}

void __margo_internal_post_wrapper_hooks(margo_instance_id mid)
{
    __margo_internal_decr_pending(mid);
    if (__margo_internal_finalize_requested(mid)) { margo_finalize(mid); }
}

char* margo_get_config(margo_instance_id mid)
{
    const char* content = json_object_to_json_string_ext(
        mid->json_cfg,
        JSON_C_TO_STRING_PRETTY | JSON_C_TO_STRING_NOSLASHESCAPE);
    return strdup(content);
}
