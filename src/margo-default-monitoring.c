/**
 * (C) 2022 The University of Chicago
 *
 * See COPYRIGHT in top-level directory.
 */
#include <string.h>
#include <abt.h>
#include "margo-instance.h"
#include "margo-monitoring.h"
#include "uthash.h"

/*
 * Statistics in Margo's default monitoring system
 * ===============================================
 *
 * Statistics on each type of operation are tracked using the
 * statistics_t structure. The monitor keeps the following such
 * statistics.
 *
 * - hg_statistics: provides statistics on progress (with and without
 *   timeout) and trigger calls;
 *
 * - origin_rpc_statistics: statistics on calls to an RPC at its
 *   origin, including forward, forward callback, wait, set_input,
 *   and get_output;
 *
 * - target_rpc_statistics: statistics on calls to an RPC at its
 *   target, including handler, ULT, respond, respond callback,
 *   wait, get_input, and set_output.
 *
 * - bulk_statistics: statistics on bulk operations, including
 *   bulk_create, bulk_transfer, and wait.
 *
 * RPCs statistics can be aggregated differently depending
 * on the value of the "discriminate_by" field in the configuration
 * (this field can contain multiple of the following options).
 * - id: RPCs with distinct ids will use distinct statistics;
 * - provider: RPCs with distinct provider ids will use distinct statistics;
 * - callpath: RPCs with distinct parents will use distinct statistics.
 * If an empty list is provided to "discriminate_by", all the statistics
 * will be aggregated in a single entry regardless of ID, provider, or
 * callpath.
 *
 * An attempt will be made, using the same mechanism as callpath tracking
 * (i.e, an ABT_key), to track the context in which a bulk operation
 * happens. If such a context can be found, the statistics that will be
 * updated is the one in the target_rpc_statistics structure of that
 * context. Otherwise, the bulk operation statistics will be tracked
 * using a single bulk_statistics at the root of the monitor's state.
 */

typedef struct statistics {
    uint64_t num; /* number of samples  */
    double   min; /* minimum value      */
    double   max; /* maximum value      */
    double   sum; /* sum of all samples */
    double   avg; /* running average    */
    double   var; /* running variance   */
    /* mutex protecting accesses */
    ABT_mutex_memory mutex;
} statistics_t;

typedef struct callpath {
    hg_id_t rpc_id;
    hg_id_t parent_id;
} callpath_t;

#define UPDATE_STATISTICS_WITH(stats, value)                               \
    do {                                                                   \
        ABT_mutex_spinlock(ABT_MUTEX_MEMORY_GET_HANDLE(&((stats).mutex))); \
        (stats).num += 1;                                                  \
        (stats).min = ((stats).min > (value) || (stats).num == 1)          \
                        ? (value)                                          \
                        : (stats).min;                                     \
        (stats).max = ((stats).max < (value)) ? (value) : (stats).max;     \
        (stats).sum += (value);                                            \
        double old_avg = (stats).avg;                                      \
        (stats).avg    = (stats).sum / (stats).num;                        \
        (stats).var                                                        \
            = (stats).var + ((value)-old_avg) * ((value) - (stats).avg);   \
        ABT_mutex_unlock(ABT_MUTEX_MEMORY_GET_HANDLE(&((stats).mutex)));   \
    } while (0)

/* Statistics related to the Mercury progress loop */
typedef struct hg_statistics {
    statistics_t progress_with_timeout;
    statistics_t progress_without_timeout;
    statistics_t trigger;
} hg_statistics_t;

/* Statistics related to bulk transfers */
typedef struct bulk_statistics {
    statistics_t create;
    statistics_t transfer;
    statistics_t transfer_cb;
    statistics_t wait;
} bulk_statistics_t;

/* Statistics related to RPCs at their origin */
typedef struct origin_rpc_statistics {
    statistics_t   forward;
    statistics_t   forward_cb;
    statistics_t   wait;
    statistics_t   set_input;
    statistics_t   get_output;
    callpath_t     callpath; /* hash key */
    UT_hash_handle hh;       /* hash handle */
} origin_rpc_statistics_t;

/* Statistics related to RPCs at their target */
typedef struct target_rpc_statistics {
    statistics_t       handler;
    statistics_t       ult;
    statistics_t       respond;
    statistics_t       respond_cb;
    statistics_t       wait;
    statistics_t       set_output;
    statistics_t       get_input;
    bulk_statistics_t* bulk;
    callpath_t         callpath; /* hash key */
    UT_hash_handle     hh;       /* hash handle */
} target_rpc_statistics_t;

/* Structure used to track registered RPCs */
typedef struct rpc_info {
    hg_id_t        id;
    char           name[1];
    UT_hash_handle hh;
} rpc_info_t;

/* Root of the monitor's state */
typedef struct default_monitor_state {
    /* RPC information */
    rpc_info_t* rpc_info;
    /* Argobots keys */
    ABT_key target_rpc_stats_key;
    /* Statistics */
    hg_statistics_t          hg_stats;
    bulk_statistics_t*       bulk_stats;
    origin_rpc_statistics_t* origin_rpc_stats;
    target_rpc_statistics_t* target_rpc_stats;
} default_monitor_state_t;

static void* margo_default_monitor_initialize(margo_instance_id mid,
                                              void*             uargs,
                                              const char*       config)
{
    default_monitor_state_t* monitor = calloc(1, sizeof(*monitor));
    ABT_key_create(NULL, &(monitor->target_rpc_stats_key));
    // TODO look at config
    return (void*)monitor;
}

static void margo_default_monitor_finalize(void* uargs)
{
    default_monitor_state_t* monitor = (default_monitor_state_t*)uargs;
    if (!monitor) return;
    // TODO write data to a file

    /* free RPC info */
    {
        rpc_info_t *p, *tmp;
        HASH_ITER(hh, monitor->rpc_info, p, tmp)
        {
            HASH_DEL(monitor->rpc_info, p);
            free(p);
        }
    }
    /* free ABT key */
    ABT_key_free(&(monitor->target_rpc_stats_key));
    free(monitor);
}

static void
margo_default_monitor_on_register(void*                         uargs,
                                  double                        timestamp,
                                  margo_monitor_event_t         event_type,
                                  margo_monitor_register_args_t event_args)
{
    if (event_type == MARGO_MONITOR_FN_START) return;
    default_monitor_state_t* monitor = (default_monitor_state_t*)uargs;
    rpc_info_t*              rpc_info
        = (rpc_info_t*)calloc(1, sizeof(*rpc_info) + strlen(event_args->name));
    rpc_info->id = event_args->id;
    strcpy(rpc_info->name, event_args->name);
    HASH_ADD(hh, monitor->rpc_info, id, sizeof(hg_id_t), rpc_info);
}

static void
margo_default_monitor_on_progress(void*                         uargs,
                                  double                        timestamp,
                                  margo_monitor_event_t         event_type,
                                  margo_monitor_progress_args_t event_args)
{
    if (event_type == MARGO_MONITOR_FN_START) {
        event_args->uctx.f = timestamp;
        return;
    }
    // MARGO_MONITOR_FN_END
    default_monitor_state_t* monitor = (default_monitor_state_t*)uargs;
    double                   t       = timestamp - event_args->uctx.f;
    if (event_args->timeout_ms)
        UPDATE_STATISTICS_WITH(monitor->hg_stats.progress_with_timeout, t);
    else
        UPDATE_STATISTICS_WITH(monitor->hg_stats.progress_without_timeout, t);
}

static void
margo_default_monitor_on_trigger(void*                        uargs,
                                 double                       timestamp,
                                 margo_monitor_event_t        event_type,
                                 margo_monitor_trigger_args_t event_args)
{
    if (event_type == MARGO_MONITOR_FN_START) {
        event_args->uctx.f = timestamp;
        return;
    }
    // MARGO_MONITOR_FN_END
    default_monitor_state_t* monitor = (default_monitor_state_t*)uargs;
    double                   t       = timestamp - event_args->uctx.f;
    UPDATE_STATISTICS_WITH(monitor->hg_stats.trigger, t);
}

#define __MONITOR_FN(__event__)                                          \
    static void margo_default_monitor_on_##__event__(                    \
        void* uargs, double timestamp, margo_monitor_event_t event_type, \
        margo_monitor_##__event__##_args_t event_args)

__MONITOR_FN(deregister) {}
__MONITOR_FN(lookup) {}
__MONITOR_FN(create) {}
__MONITOR_FN(forward) {}
__MONITOR_FN(forward_cb) {}
__MONITOR_FN(respond) {}
__MONITOR_FN(respond_cb) {}
__MONITOR_FN(destroy) {}
__MONITOR_FN(bulk_create) {}
__MONITOR_FN(bulk_transfer) {}
__MONITOR_FN(bulk_transfer_cb) {}
__MONITOR_FN(bulk_free) {}
__MONITOR_FN(rpc_handler) {}
__MONITOR_FN(rpc_ult) {}
__MONITOR_FN(wait) {}
__MONITOR_FN(sleep) {}
__MONITOR_FN(set_input) {}
__MONITOR_FN(set_output) {}
__MONITOR_FN(get_input) {}
__MONITOR_FN(get_output) {}
__MONITOR_FN(free_input) {}
__MONITOR_FN(free_output) {}
__MONITOR_FN(prefinalize) {}
__MONITOR_FN(finalize) {}
__MONITOR_FN(user) {}

struct margo_monitor __margo_default_monitor
    = {.uargs      = NULL,
       .initialize = margo_default_monitor_initialize,
       .finalize   = margo_default_monitor_finalize,
#define X(__x__, __y__) .on_##__y__ = margo_default_monitor_on_##__y__,
       MARGO_EXPAND_MONITOR_MACROS
#undef X
};

const struct margo_monitor* margo_default_monitor = &__margo_default_monitor;
