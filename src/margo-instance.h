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

#include <margo-config.h>
#include <time.h>
#include <math.h>

#include "margo.h"
#include "margo-abt-macros.h"
#include "margo-logging.h"
#include "margo-bulk-util.h"
#include "margo-timer.h"
#include "utlist.h"
#include "uthash.h"

#define MARGO_OWNS_HG_CLASS   0x1
#define MARGO_OWNS_HG_CONTEXT 0x2

/* Structure to store timing information */
struct diag_data {
    /* breadcrumb stats */
    margo_breadcrumb_stats stats;

    /* origin or target */
    margo_breadcrumb_type type;

    uint64_t rpc_breadcrumb; /* identifier for rpc and it's ancestors */
    struct margo_global_breadcrumb_key key;

    /* used to combine rpc_breadcrumb, addr_hash and provider_id to create a
     * unique key for HASH_ADD inside margo_breadcrumb_measure */
    __uint128_t x;

    /*sparkline data for breadcrumb */
    double sparkline_time[100];
    double sparkline_count[100];

    UT_hash_handle hh; /* hash table link */
};

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
    hg_id_t  id;                       /* rpc id */
    uint64_t rpc_breadcrumb_fragment;  /* fragment id used in rpc tracing */
    char     func_name[64];            /* string name of rpc */
    struct margo_registered_rpc* next; /* pointer to next in list */
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

    /* optional diagnostics data tracking */
    /* NOTE: technically the following fields are subject to races if they
     * are updated from more than one thread at a time.  We will be careful
     * to only update the counters from the progress_fn,
     * which will serialize access.
     */
    ABT_thread        sparkline_data_collection_tid;
    int               diag_enabled;
    int               profile_enabled;
    uint64_t          self_addr_hash;
    double            previous_sparkline_data_collection_time;
    uint16_t          sparkline_index;
    struct diag_data  diag_trigger_elapsed;
    struct diag_data  diag_progress_elapsed_zero_timeout;
    struct diag_data  diag_progress_elapsed_nonzero_timeout;
    struct diag_data  diag_progress_timeout_value;
    struct diag_data  diag_bulk_create_elapsed;
    struct diag_data* diag_rpc;
    ABT_mutex         diag_rpc_mutex;
};

struct margo_request_struct {
    margo_eventual_t eventual;
    hg_return_t      hret;
    margo_timer_t*   timer;
    hg_handle_t      handle;
    double           start_time; /* timestamp of when the operation started */
    uint64_t rpc_breadcrumb; /* statistics tracking identifier, if applicable */
    uint64_t server_addr_hash; /* hash of globally unique string addr of margo
                                  server instance */
    uint16_t provider_id; /* id of the provider servicing the request, local to
                             the margo server instance */
};

struct margo_rpc_data {
    margo_instance_id mid;
    ABT_pool          pool;
    void*             user_data;
    void (*user_free_callback)(void*);
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
