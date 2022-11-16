/*
 * (C) 2020 The University of Chicago
 *
 * See COPYRIGHT in top-level directory.
 */
#ifndef __MARGO_INTERNAL_H
#define __MARGO_INTERNAL_H
#include <assert.h>
#include <stdbool.h>
#include <unistd.h>
#include <errno.h>
#include <abt.h>
#include <stdlib.h>
#include <json-c/json.h>

#include <margo-config-private.h>
#include <time.h>
#include <math.h>

#include "margo.h"
#include "margo-abt-macros.h"
#include "margo-logging.h"
#include "margo-monitoring.h"
#include "margo-bulk-util.h"
#include "margo-timer-private.h"
#include "utlist.h"
#include "uthash.h"

#define MARGO_OWNS_HG_CLASS   0x1
#define MARGO_OWNS_HG_CONTEXT 0x2

struct margo_handle_cache_el; /* defined in margo-handle-cache.c */

struct margo_finalize_cb {
    const void* owner;
    void (*callback)(void*);
    void*                     uargs;
    struct margo_finalize_cb* next;
};

struct margo_timer_list; /* defined in margo-timer.c */

/* Stores the name and rpc id of a registered RPC.  We track this purely for
 * debugging and instrumentation purposes
 */
struct margo_registered_rpc {
    hg_id_t                      id;            /* rpc id */
    char                         func_name[64]; /* string name of rpc */
    struct margo_registered_rpc* next;          /* pointer to next in list */
};

/* Struct to track pools created by margo along with a flag indicating if
 * margo is responsible for explicitly free'ing the pool or not.
 */
struct margo_abt_pool {
    ABT_pool pool;            /* Argobots pool */
    bool     margo_free_flag; /* flag if Margo is responsible for freeing */
};

struct margo_instance {
    /* json config */
    struct json_object* json_cfg;

    /* mercury/argobots state */
    hg_class_t*   hg_class;
    hg_context_t* hg_context;
    uint8_t       hg_ownership;
    ABT_pool      progress_pool;
    ABT_pool      rpc_pool;

    /* xstreams and pools built from argobots config */
    struct margo_abt_pool* abt_pools;
    ABT_xstream*           abt_xstreams;
    unsigned               num_abt_pools;
    unsigned               num_abt_xstreams;
    bool*                  owns_abt_xstream;

    /* internal to margo for this particular instance */
    ABT_thread hg_progress_tid;
    int        hg_progress_shutdown_flag;
    int        hg_progress_timeout_ub;

    uint16_t num_registered_rpcs; /* number of registered rpc's by all providers
                                     on this instance */
    /* list of rpcs registered on this instance for debugging and profiling
     * purposes */
    struct margo_registered_rpc* registered_rpcs;

    /* control logic for callers waiting on margo to be finalized */
    int                       finalize_flag;
    int                       refcount;
    ABT_mutex                 finalize_mutex;
    ABT_cond                  finalize_cond;
    struct margo_finalize_cb* finalize_cb;
    struct margo_finalize_cb* prefinalize_cb;

    /* control logic to prevent margo_finalize from destroying
       the instance when some operations are pending */
    unsigned  pending_operations;
    ABT_mutex pending_operations_mtx;
    int       finalize_requested;

    /* control logic for shutting down */
    hg_id_t shutdown_rpc_id;
    bool    enable_remote_shutdown;

    /* timer data */
    struct margo_timer_list* timer_list;

    /* linked list of free hg handles and a hash of in-use handles */
    struct margo_handle_cache_el* free_handle_list;
    struct margo_handle_cache_el* used_handle_hash;
    ABT_mutex handle_cache_mtx; /* mutex protecting access to above caches */

    /* logging */
    struct margo_logger logger;
    margo_log_level     log_level;

    /* monitoring */
    struct margo_monitor* monitor;

    /* callpath tracking */
    ABT_key current_rpc_id_key;

    /* optional diagnostics data tracking */
    int      abt_profiling_enabled;
    char*    self_addr_str;
    uint64_t self_addr_hash;
};

struct margo_request_struct {
    margo_eventual_t     eventual;
    hg_return_t          hret;
    margo_timer*         timer;
    margo_instance_id    mid;
    hg_handle_t          handle;
    margo_monitor_data_t monitor_data;
    margo_request_type   type;
};

// Data registered to an RPC id with HG_Register_data
struct margo_rpc_data {
    margo_instance_id mid;
    ABT_pool          pool;
    char*             rpc_name;
    hg_proc_cb_t      in_proc_cb;  /* user-provided input proc */
    hg_proc_cb_t      out_proc_cb; /* user-provided output proc */
    void*             user_data;
    void (*user_free_callback)(void*);
};

// Data associated with a handle with HG_Set_data
struct margo_handle_data {
    margo_instance_id mid;
    ABT_pool          pool;
    const char*       rpc_name; /* note: same pointer as in margo_rpc_data,
                                   not the responsibility of the handle to free it */
    hg_proc_cb_t in_proc_cb;    /* user-provided input proc */
    hg_proc_cb_t out_proc_cb;   /* user-provided output proc */
    void*        user_data;
    void (*user_free_callback)(void*);
    margo_monitor_data_t monitor_data;
};

struct lookup_cb_evt {
    hg_return_t hret;
    hg_addr_t   addr;
};

MERCURY_GEN_PROC(margo_shutdown_out_t, ((int32_t)(ret)))

typedef struct {
    hg_handle_t handle;
} margo_forward_timeout_cb_dat;

typedef struct {
    ABT_mutex mutex;
    ABT_cond  cond;
    char      is_asleep;
} margo_thread_sleep_cb_dat;

#define MARGO_TRACE    margo_trace
#define MARGO_DEBUG    margo_debug
#define MARGO_INFO     margo_info
#define MARGO_WARNING  margo_warning
#define MARGO_ERROR    margo_error
#define MARGO_CRITICAL margo_critical

#endif
