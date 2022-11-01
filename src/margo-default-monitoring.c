/**
 * (C) 2022 The University of Chicago
 *
 * See COPYRIGHT in top-level directory.
 */
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <abt.h>
#include "margo-instance.h"
#include "margo-monitoring.h"
#include "margo-id.h"
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
 * (i.e, an ABT_key), to track the context in which a bulk operation happens.
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

static struct json_object* statistics_to_json(const statistics_t* stats);

/* Statistics related to the Mercury progress loop */
typedef struct hg_statistics {
    statistics_t progress_with_timeout;
    statistics_t progress_without_timeout;
    statistics_t trigger;
} hg_statistics_t;

static struct json_object* hg_statistics_to_json(const hg_statistics_t* stats);

/* Some statistics fields in the following structures will be
 * a pair of "duration" statistics (duration of the operation)
 * and "timestamp" statistics (timestamp of the operation
 * relative to another, earlier operation). In the latter case,
 * the reference operation used for the timestamp is marked with
 * a comment.
 */
enum
{
    DURATION  = 0,
    TIMESTAMP = 1
};

/* Statistics related to bulk transfers */
/* Note: the reference timestamp is the transfer
 * operation rather than the create operation
 * because it is not unusual for a service to
 * have pre-created bulk handles.
 */
typedef struct bulk_statistics {
    statistics_t create;
    statistics_t transfer; /* reference timestamp */
    statistics_t
        transfer_size; /* size of transfer, not timestamp or duration */
    statistics_t   transfer_cb[2];
    statistics_t   wait[2];
    callpath_t     callpath; /* hash key */
    UT_hash_handle hh;       /* hash handle */
} bulk_statistics_t;

static struct json_object*
bulk_statistics_to_json(const bulk_statistics_t* stats);

/* Statistics related to RPCs at their origin */
typedef struct origin_rpc_statistics {
    /* reference timestamp is the create operation,
     * for which no statistics are collected */
    statistics_t   forward[2];
    statistics_t   forward_cb[2];
    statistics_t   wait[2];
    statistics_t   set_input[2];
    statistics_t   get_output[2];
    callpath_t     callpath; /* hash key */
    UT_hash_handle hh;       /* hash handle */
} origin_rpc_statistics_t;

static struct json_object*
origin_rpc_statistics_to_json(const origin_rpc_statistics_t* stats);

/* Statistics related to RPCs at their target */
typedef struct target_rpc_statistics {
    statistics_t   handler; /* reference timestamp */
    statistics_t   ult[2];
    statistics_t   respond[2];
    statistics_t   respond_cb[2];
    statistics_t   wait[2];
    statistics_t   set_output[2];
    statistics_t   get_input[2];
    callpath_t     callpath; /* hash key */
    UT_hash_handle hh;       /* hash handle */
} target_rpc_statistics_t;

static struct json_object*
target_rpc_statistics_to_json(const target_rpc_statistics_t* stats);

/* Structure used to track registered RPCs */
typedef struct rpc_info {
    hg_id_t        id;
    UT_hash_handle hh;
    char           name[1];
} rpc_info_t;

static void        rpc_info_add(rpc_info_t* hash, hg_id_t id, const char* name);
static rpc_info_t* rpc_info_find(rpc_info_t* hash, hg_id_t id);
static void        rpc_info_clear(rpc_info_t* hash);

/* Structure used to track addresses, hashed by name */
typedef struct addr_info {
    UT_hash_handle hh;
    char           name[1];
} addr_info_t;

static addr_info_t*
addr_info_find_or_add(addr_info_t* hash, margo_instance_id mid, hg_addr_t addr);
static void addr_info_clear(addr_info_t* hash);

/* Root of the monitor's state */
typedef struct default_monitor_state {
    margo_instance_id mid;
    char*             filename_prefix;
    int               precision;   /* precision used when printing doubles */
    int               pretty_json; /* use tabs and stuff in JSON printing */
    /* RPC information */
    rpc_info_t* rpc_info;
    /* Address info */
    addr_info_t*     addr_info;
    ABT_mutex_memory addr_info_mtx;
    /* Argobots keys */
    ABT_key bulk_stats_key; /* may be associated with a bulk_statistics_t* */
    ABT_key callpath_key;   /* may be associated with a callpath */
    /* Mutex to access data (locked only when
     * adding new entries into hash tables or
     * when writing the state into a file) */
    ABT_mutex_memory mutex;
    /* Statistics */
    hg_statistics_t          hg_stats;
    bulk_statistics_t*       bulk_stats;
    origin_rpc_statistics_t* origin_rpc_stats;
    target_rpc_statistics_t* target_rpc_stats;
} default_monitor_state_t;

static struct json_object*
monitor_state_to_json(const default_monitor_state_t* stats);

/* A session is an object that will be associated with an hg_handle_t
 * when on_forward or on_rpc_handler is invoked, and will be destroyed
 * when on_destroy is called on the handle.
 *
 * Note that when a process sends an RPC to itself, the hg_handle_t
 * is the same in the sender logic (forward, forward_cb, etc.) and
 * in the receiver logic (rpc_handler, respond, etc.), so we cannot
 * factor the origin and target fields into a union.
 */
typedef struct session {
    struct {
        double                   start_ts;
        origin_rpc_statistics_t* stats;
    } origin;
    struct {
        double                   start_ts;
        target_rpc_statistics_t* stats;
    } target;
} session_t;

typedef struct bulk_session {
    double             start_ts;
    bulk_statistics_t* stats;
} bulk_session_t;

static void
write_monitor_state_to_json_file(const default_monitor_state_t* monitor);

static void* margo_default_monitor_initialize(margo_instance_id   mid,
                                              void*               uargs,
                                              struct json_object* config)
{
    default_monitor_state_t* monitor = calloc(1, sizeof(*monitor));
    ABT_key_create(NULL, &(monitor->bulk_stats_key));
    ABT_key_create(NULL, &(monitor->callpath_key));
    monitor->mid = mid;

    /* default configuration */
    monitor->filename_prefix = strdup("margo-statistics");
    monitor->precision       = 9;
    monitor->pretty_json     = 0;

    /* read configuration */
    struct json_object* statistics
        = json_object_object_get(config, "statistics");
    if (statistics && json_object_is_type(statistics, json_type_object)) {
        struct json_object* filename_prefix
            = json_object_object_get(statistics, "filename_prefix");
        if (filename_prefix
            && json_object_is_type(filename_prefix, json_type_string)) {
            free(monitor->filename_prefix);
            monitor->filename_prefix
                = strdup(json_object_get_string(filename_prefix));
        }
        struct json_object* precision
            = json_object_object_get(statistics, "precision");
        if (precision && json_object_is_type(precision, json_type_int)) {
            monitor->precision = json_object_get_int(precision);
        }
        struct json_object* pretty
            = json_object_object_get(statistics, "pretty");
        if (pretty && json_object_is_type(pretty, json_type_boolean)
            && json_object_get_boolean(pretty)) {
            monitor->pretty_json = JSON_C_TO_STRING_PRETTY;
        }
    }

    return (void*)monitor;
}

static void margo_default_monitor_finalize(void* uargs)
{
    default_monitor_state_t* monitor = (default_monitor_state_t*)uargs;
    if (!monitor) return;

    /* write JSON file */
    write_monitor_state_to_json_file(monitor);

    /* free RPC info */
    rpc_info_clear(monitor->rpc_info);

    /* free address info */
    addr_info_clear(monitor->addr_info);

    /* free origin RPC statistics */
    {
        origin_rpc_statistics_t *p, *tmp;
        HASH_ITER(hh, monitor->origin_rpc_stats, p, tmp)
        {
            HASH_DEL(monitor->origin_rpc_stats, p);
            free(p);
        }
    }
    /* free target RPC statistics */
    {
        target_rpc_statistics_t *p, *tmp;
        HASH_ITER(hh, monitor->target_rpc_stats, p, tmp)
        {
            HASH_DEL(monitor->target_rpc_stats, p);
            free(p);
        }
    }
    /* free bulk statistics */
    {
        bulk_statistics_t *p, *tmp;
        HASH_ITER(hh, monitor->bulk_stats, p, tmp)
        {
            HASH_DEL(monitor->bulk_stats, p);
            free(p);
        }
    }
    /* free ABT key */
    ABT_key_free(&(monitor->bulk_stats_key));
    ABT_key_free(&(monitor->callpath_key));
    /* free filename */
    free(monitor->filename_prefix);
    free(monitor);
}

static const char* margo_default_monitor_name() { return "default"; }

static struct json_object* margo_default_monitor_config(void* uargs)
{
    default_monitor_state_t* monitor = (default_monitor_state_t*)uargs;
    if (!monitor) return NULL;

    struct json_object* config     = json_object_new_object();
    struct json_object* statistics = json_object_new_object();
    json_object_object_add_ex(config, "statistics", statistics,
                              JSON_C_OBJECT_ADD_KEY_IS_NEW);
    json_object_object_add_ex(statistics, "filename_prefix",
                              json_object_new_string(monitor->filename_prefix),
                              JSON_C_OBJECT_ADD_KEY_IS_NEW);
    json_object_object_add_ex(statistics, "precision",
                              json_object_new_int(monitor->precision),
                              JSON_C_OBJECT_ADD_KEY_IS_NEW);

    return config;
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
    ABT_mutex_spinlock(ABT_MUTEX_MEMORY_GET_HANDLE(&monitor->mutex));
    HASH_ADD(hh, monitor->rpc_info, id, sizeof(hg_id_t), rpc_info);
    ABT_mutex_unlock(ABT_MUTEX_MEMORY_GET_HANDLE(&monitor->mutex));
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

static void
margo_default_monitor_on_create(void*                       uargs,
                                double                      timestamp,
                                margo_monitor_event_t       event_type,
                                margo_monitor_create_args_t event_args)
{
    if (event_type == MARGO_MONITOR_FN_START) {
        event_args->uctx.f = timestamp;
        return;
    }
    // MARGO_MONITOR_FN_END
    default_monitor_state_t* monitor = (default_monitor_state_t*)uargs;
    // TODO use a session pool
    session_t* session = calloc(1, sizeof(*session));

    session->origin.start_ts          = event_args->uctx.f;
    margo_monitor_data_t monitor_data = {.p = (void*)session};
    margo_set_monitoring_data(event_args->handle, monitor_data);
}

#define RETRIEVE_SESSION(handle)                                            \
    session_t* session = NULL;                                              \
    do {                                                                    \
        margo_monitor_data_t monitor_data;                                  \
        hg_return_t ret = margo_get_monitoring_data(handle, &monitor_data); \
        session         = (session_t*)monitor_data.p;                       \
    } while (0)

#define RETRIEVE_BULK_SESSION(request)                                   \
    bulk_session_t* session = NULL;                                      \
    do {                                                                 \
        margo_monitor_data_t monitor_data;                               \
        hg_return_t          ret                                         \
            = margo_request_get_monitoring_data(request, &monitor_data); \
        session = (bulk_session_t*)monitor_data.p;                       \
    } while (0)

static void
margo_default_monitor_on_forward(void*                        uargs,
                                 double                       timestamp,
                                 margo_monitor_event_t        event_type,
                                 margo_monitor_forward_args_t event_args)
{
    default_monitor_state_t* monitor = (default_monitor_state_t*)uargs;
    margo_instance_id mid = margo_hg_handle_get_instance(event_args->handle);
    const struct hg_info* handle_info = margo_get_info(event_args->handle);
    // retrieve the session that was create on on_create
    RETRIEVE_SESSION(event_args->handle);
    origin_rpc_statistics_t* rpc_stats = NULL;

    if (event_type == MARGO_MONITOR_FN_START) {

        event_args->uctx.f = timestamp;

        // get address info
        ABT_mutex_spinlock(
            ABT_MUTEX_MEMORY_GET_HANDLE(&monitor->addr_info_mtx));
        addr_info_t* addr_info
            = addr_info_find_or_add(monitor->addr_info, mid, handle_info->addr);
        ABT_mutex_unlock(ABT_MUTEX_MEMORY_GET_HANDLE(&monitor->addr_info_mtx));

        // attach statistics to session
        hg_id_t id     = handle_info->id;
        id             = mux_id(id, event_args->provider_id);
        callpath_t key = {.rpc_id = id, .parent_id = 0};
        // try to get parent RPC id from context
        margo_get_current_rpc_id(mid, &key.parent_id);

        ABT_mutex_spinlock(ABT_MUTEX_MEMORY_GET_HANDLE(&monitor->mutex));
        HASH_FIND(hh, monitor->origin_rpc_stats, &key, sizeof(key), rpc_stats);
        if (!rpc_stats) {
            rpc_stats = (origin_rpc_statistics_t*)calloc(1, sizeof(*rpc_stats));
            rpc_stats->callpath = key;
            HASH_ADD(hh, monitor->origin_rpc_stats, callpath, sizeof(key),
                     rpc_stats);
        }
        ABT_mutex_unlock(ABT_MUTEX_MEMORY_GET_HANDLE(&monitor->mutex));
        session->origin.stats = rpc_stats;

        double t = timestamp - session->origin.start_ts;
        UPDATE_STATISTICS_WITH(rpc_stats->forward[TIMESTAMP], t);

    } else if (event_type == MARGO_MONITOR_FN_END) {

        // update statistics
        rpc_stats = session->origin.stats;
        double t  = timestamp - event_args->uctx.f;
        UPDATE_STATISTICS_WITH(rpc_stats->forward[DURATION], t);
    }
}

static void
margo_default_monitor_on_set_input(void*                          uargs,
                                   double                         timestamp,
                                   margo_monitor_event_t          event_type,
                                   margo_monitor_set_input_args_t event_args)
{
    default_monitor_state_t* monitor = (default_monitor_state_t*)uargs;
    // retrieve the session that was create on on_create
    hg_handle_t handle = margo_request_get_handle(event_args->request);
    RETRIEVE_SESSION(handle);
    origin_rpc_statistics_t* rpc_stats = session->origin.stats;

    if (event_type == MARGO_MONITOR_FN_START) {
        event_args->uctx.f = timestamp;
        double t           = timestamp - session->origin.start_ts;
        UPDATE_STATISTICS_WITH(rpc_stats->set_input[TIMESTAMP], t);
    } else {
        double t = timestamp - event_args->uctx.f;
        UPDATE_STATISTICS_WITH(rpc_stats->set_input[DURATION], t);
    }
}

static void
margo_default_monitor_on_set_output(void*                           uargs,
                                    double                          timestamp,
                                    margo_monitor_event_t           event_type,
                                    margo_monitor_set_output_args_t event_args)
{
    default_monitor_state_t* monitor = (default_monitor_state_t*)uargs;
    // retrieve the session that was create on on_create
    hg_handle_t handle = margo_request_get_handle(event_args->request);
    RETRIEVE_SESSION(handle);
    target_rpc_statistics_t* rpc_stats = session->target.stats;

    if (event_type == MARGO_MONITOR_FN_START) {
        event_args->uctx.f = timestamp;
        double t           = timestamp - session->target.start_ts;
        UPDATE_STATISTICS_WITH(rpc_stats->set_output[TIMESTAMP], t);
    } else {
        double t = timestamp - event_args->uctx.f;
        UPDATE_STATISTICS_WITH(rpc_stats->set_output[DURATION], t);
    }
}

static void
margo_default_monitor_on_get_output(void*                           uargs,
                                    double                          timestamp,
                                    margo_monitor_event_t           event_type,
                                    margo_monitor_get_output_args_t event_args)
{
    default_monitor_state_t* monitor = (default_monitor_state_t*)uargs;
    // retrieve the session that was create on on_create
    RETRIEVE_SESSION(event_args->handle);
    origin_rpc_statistics_t* rpc_stats = session->origin.stats;

    if (event_type == MARGO_MONITOR_FN_START) {
        event_args->uctx.f = timestamp;
        double t           = timestamp - session->origin.start_ts;
        UPDATE_STATISTICS_WITH(rpc_stats->get_output[TIMESTAMP], t);
    } else {
        double t = timestamp - event_args->uctx.f;
        UPDATE_STATISTICS_WITH(rpc_stats->get_output[DURATION], t);
    }
}

static void
margo_default_monitor_on_get_input(void*                          uargs,
                                   double                         timestamp,
                                   margo_monitor_event_t          event_type,
                                   margo_monitor_get_input_args_t event_args)
{
    default_monitor_state_t* monitor = (default_monitor_state_t*)uargs;
    // retrieve the session that was create on on_create
    RETRIEVE_SESSION(event_args->handle);
    target_rpc_statistics_t* rpc_stats = session->target.stats;

    if (event_type == MARGO_MONITOR_FN_START) {
        event_args->uctx.f = timestamp;
        double t           = timestamp - session->target.start_ts;
        UPDATE_STATISTICS_WITH(rpc_stats->get_input[TIMESTAMP], t);
    } else {
        double t = timestamp - event_args->uctx.f;
        UPDATE_STATISTICS_WITH(rpc_stats->get_input[DURATION], t);
    }
}

static void
margo_default_monitor_on_forward_cb(void*                           uargs,
                                    double                          timestamp,
                                    margo_monitor_event_t           event_type,
                                    margo_monitor_forward_cb_args_t event_args)
{
    default_monitor_state_t* monitor = (default_monitor_state_t*)uargs;
    // retrieve the session that was create on on_create
    hg_handle_t handle = margo_request_get_handle(event_args->request);
    RETRIEVE_SESSION(handle);
    origin_rpc_statistics_t* rpc_stats = session->origin.stats;

    if (event_type == MARGO_MONITOR_FN_START) {
        event_args->uctx.f = timestamp;
        double t           = timestamp - session->origin.start_ts;
        UPDATE_STATISTICS_WITH(rpc_stats->forward_cb[TIMESTAMP], t);
    } else {
        double t = timestamp - event_args->uctx.f;
        UPDATE_STATISTICS_WITH(rpc_stats->forward_cb[DURATION], t);
    }
}

static void
margo_default_monitor_on_respond(void*                        uargs,
                                 double                       timestamp,
                                 margo_monitor_event_t        event_type,
                                 margo_monitor_respond_args_t event_args)
{
    default_monitor_state_t* monitor = (default_monitor_state_t*)uargs;
    // retrieve the session that was create on on_create
    hg_handle_t handle = margo_request_get_handle(event_args->request);
    RETRIEVE_SESSION(handle);
    target_rpc_statistics_t* rpc_stats = session->target.stats;

    if (event_type == MARGO_MONITOR_FN_START) {
        event_args->uctx.f = timestamp;
        double t           = timestamp - session->target.start_ts;
        UPDATE_STATISTICS_WITH(rpc_stats->respond[TIMESTAMP], t);
    } else {
        double t = timestamp - event_args->uctx.f;
        UPDATE_STATISTICS_WITH(rpc_stats->respond[DURATION], t);
    }
}

static void
margo_default_monitor_on_respond_cb(void*                           uargs,
                                    double                          timestamp,
                                    margo_monitor_event_t           event_type,
                                    margo_monitor_respond_cb_args_t event_args)
{
    default_monitor_state_t* monitor = (default_monitor_state_t*)uargs;
    // retrieve the session that was create on on_create
    hg_handle_t handle = margo_request_get_handle(event_args->request);
    RETRIEVE_SESSION(handle);
    target_rpc_statistics_t* rpc_stats = session->target.stats;

    if (event_type == MARGO_MONITOR_FN_START) {
        event_args->uctx.f = timestamp;
        double t           = timestamp - session->target.start_ts;
        UPDATE_STATISTICS_WITH(rpc_stats->respond_cb[TIMESTAMP], t);
    } else {
        double t = timestamp - event_args->uctx.f;
        UPDATE_STATISTICS_WITH(rpc_stats->respond_cb[DURATION], t);
    }
}

static void margo_default_monitor_on_wait(void*                     uargs,
                                          double                    timestamp,
                                          margo_monitor_event_t     event_type,
                                          margo_monitor_wait_args_t event_args)
{
    default_monitor_state_t* monitor = (default_monitor_state_t*)uargs;
    // retrieve the session that was create on on_create
    statistics_t*   duration_stats  = NULL;
    statistics_t*   timestamp_stats = NULL;
    bulk_session_t* bulk_session    = NULL;
    double          start_ts;

    margo_request_type request_type
        = margo_request_get_type(event_args->request);
    if (request_type == MARGO_FORWARD_REQUEST) {
        hg_handle_t handle = margo_request_get_handle(event_args->request);
        RETRIEVE_SESSION(handle);
        start_ts        = session->origin.start_ts;
        duration_stats  = &(session->origin.stats->wait[DURATION]);
        timestamp_stats = &(session->origin.stats->wait[TIMESTAMP]);
    } else if (request_type == MARGO_RESPONSE_REQUEST) {
        hg_handle_t handle = margo_request_get_handle(event_args->request);
        RETRIEVE_SESSION(handle);
        start_ts        = session->target.start_ts;
        duration_stats  = &(session->target.stats->wait[DURATION]);
        timestamp_stats = &(session->target.stats->wait[TIMESTAMP]);
    } else if (request_type == MARGO_BULK_REQUEST) {
        RETRIEVE_BULK_SESSION(event_args->request);
        start_ts        = session->start_ts;
        duration_stats  = &(session->stats->wait[DURATION]);
        timestamp_stats = &(session->stats->wait[TIMESTAMP]);
        bulk_session    = session;
    }

    if (event_type == MARGO_MONITOR_FN_START) {
        event_args->uctx.f = timestamp;
        double t           = timestamp - start_ts;
        UPDATE_STATISTICS_WITH(*timestamp_stats, t);
    } else {
        double t = timestamp - event_args->uctx.f;
        UPDATE_STATISTICS_WITH(*duration_stats, t);
    }

    if (event_type == MARGO_MONITOR_FN_END) { free(bulk_session); }
}

static void margo_default_monitor_on_rpc_handler(
    void*                            uargs,
    double                           timestamp,
    margo_monitor_event_t            event_type,
    margo_monitor_rpc_handler_args_t event_args)
{
    default_monitor_state_t* monitor = (default_monitor_state_t*)uargs;
    margo_instance_id mid = margo_hg_handle_get_instance(event_args->handle);
    const struct hg_info* handle_info = margo_get_info(event_args->handle);
    RETRIEVE_SESSION(event_args->handle);
    target_rpc_statistics_t* rpc_stats = NULL;

    if (event_type == MARGO_MONITOR_FN_START) {
        // TODO use a session pool
        if (!session) { session = calloc(1, sizeof(*session)); }
        session->target.start_ts          = timestamp;
        margo_monitor_data_t monitor_data = {.p = (void*)session};
        margo_set_monitoring_data(event_args->handle, monitor_data);

        // get address info
        ABT_mutex_spinlock(
            ABT_MUTEX_MEMORY_GET_HANDLE(&monitor->addr_info_mtx));
        addr_info_t* addr_info
            = addr_info_find_or_add(monitor->addr_info, mid, handle_info->addr);
        ABT_mutex_unlock(ABT_MUTEX_MEMORY_GET_HANDLE(&monitor->addr_info_mtx));

        // attach statistics to session
        hg_id_t    id  = margo_get_info(event_args->handle)->id;
        callpath_t key = {.rpc_id = id, .parent_id = event_args->parent_rpc_id};

        ABT_mutex_spinlock(ABT_MUTEX_MEMORY_GET_HANDLE(&monitor->mutex));
        HASH_FIND(hh, monitor->target_rpc_stats, &key, sizeof(key), rpc_stats);
        if (!rpc_stats) {
            rpc_stats = (target_rpc_statistics_t*)calloc(1, sizeof(*rpc_stats));
            rpc_stats->callpath = key;
            HASH_ADD(hh, monitor->target_rpc_stats, callpath, sizeof(key),
                     rpc_stats);
        }
        ABT_mutex_unlock(ABT_MUTEX_MEMORY_GET_HANDLE(&monitor->mutex));
        session->target.stats = rpc_stats;

        event_args->uctx.f = timestamp;

    } else {

        // update statistics
        rpc_stats = session->target.stats;
        double t  = timestamp - event_args->uctx.f;
        UPDATE_STATISTICS_WITH(rpc_stats->handler, t);
    }
}

static void
margo_default_monitor_on_rpc_ult(void*                        uargs,
                                 double                       timestamp,
                                 margo_monitor_event_t        event_type,
                                 margo_monitor_rpc_ult_args_t event_args)
{
    default_monitor_state_t* monitor = (default_monitor_state_t*)uargs;
    // retrieve the session that was create on on_create
    RETRIEVE_SESSION(event_args->handle);
    target_rpc_statistics_t* rpc_stats = session->target.stats;

    if (event_type == MARGO_MONITOR_FN_START) {

        event_args->uctx.f = timestamp;
        double t           = timestamp - session->target.start_ts;
        UPDATE_STATISTICS_WITH(rpc_stats->ult[TIMESTAMP], t);
        // set callpath key
        callpath_t* current_callpath = &(session->target.stats->callpath);
        ABT_key_set(monitor->callpath_key, current_callpath);

    } else {
        double t = timestamp - event_args->uctx.f;
        UPDATE_STATISTICS_WITH(rpc_stats->ult[DURATION], t);
    }
}

static void
margo_default_monitor_on_destroy(void*                        uargs,
                                 double                       timestamp,
                                 margo_monitor_event_t        event_type,
                                 margo_monitor_destroy_args_t event_args)
{
    if (event_type == MARGO_MONITOR_FN_END) {
        // WARNING: handle is no longer valid after destroy
        return;
    }
    // MARGO_MONITOR_FN_START
    margo_monitor_data_t monitor_data;
    margo_get_monitoring_data(event_args->handle, &monitor_data);
    // TODO use a session pool
    free(monitor_data.p);
}

#define __MONITOR_FN(__event__)                                          \
    static void margo_default_monitor_on_##__event__(                    \
        void* uargs, double timestamp, margo_monitor_event_t event_type, \
        margo_monitor_##__event__##_args_t event_args)

static void margo_default_monitor_on_bulk_create(
    void*                            uargs,
    double                           timestamp,
    margo_monitor_event_t            event_type,
    margo_monitor_bulk_create_args_t event_args)
{
    default_monitor_state_t* monitor = (default_monitor_state_t*)uargs;

    bulk_statistics_t* bulk_stats = NULL;

    // try to get current bulk_statistics_t from ABT_key
    ABT_key_get(monitor->bulk_stats_key, (void**)&bulk_stats);

    if (!bulk_stats) {
        // no bulk_statistics_t attached to current ULT
        callpath_t default_key = {0};
        default_key.parent_id  = mux_id(0, MARGO_DEFAULT_PROVIDER_ID);
        default_key.rpc_id     = mux_id(0, MARGO_DEFAULT_PROVIDER_ID);
        callpath_t* pkey       = NULL;
        // try to get current callpath from the installed ABT_key
        ABT_key_get(monitor->callpath_key, (void**)&pkey);
        if (!pkey) pkey = (callpath_t*)&default_key;

        ABT_mutex_spinlock(ABT_MUTEX_MEMORY_GET_HANDLE(&monitor->mutex));
        HASH_FIND(hh, monitor->bulk_stats, pkey, sizeof(*pkey), bulk_stats);
        if (!bulk_stats) {
            bulk_stats = (bulk_statistics_t*)calloc(1, sizeof(*bulk_stats));
            bulk_stats->callpath = *pkey;
            HASH_ADD(hh, monitor->bulk_stats, callpath, sizeof(*pkey),
                     bulk_stats);
        }
        ABT_mutex_unlock(ABT_MUTEX_MEMORY_GET_HANDLE(&monitor->mutex));
        ABT_key_set(monitor->bulk_stats_key, bulk_stats);
    }

    if (event_type == MARGO_MONITOR_FN_START) {
        event_args->uctx.f = timestamp;
    } else {
        double t = timestamp - event_args->uctx.f;
        UPDATE_STATISTICS_WITH(bulk_stats->create, t);
    }
}

static void margo_default_monitor_on_bulk_transfer(
    void*                              uargs,
    double                             timestamp,
    margo_monitor_event_t              event_type,
    margo_monitor_bulk_transfer_args_t event_args)
{
    default_monitor_state_t* monitor    = (default_monitor_state_t*)uargs;
    bulk_statistics_t*       bulk_stats = NULL;

    if (event_type == MARGO_MONITOR_FN_START) {

        // try to get current bulk_statistics_t from ABT_key
        ABT_key_get(monitor->bulk_stats_key, (void**)&bulk_stats);

        if (!bulk_stats) {
            // no bulk_statistics_t attached to current ULT
            callpath_t default_key = {0};
            default_key.parent_id  = mux_id(0, MARGO_DEFAULT_PROVIDER_ID);
            default_key.rpc_id     = mux_id(0, MARGO_DEFAULT_PROVIDER_ID);
            callpath_t* pkey       = NULL;
            // try to get current callpath from the installed ABT_key
            ABT_key_get(monitor->callpath_key, (void**)&pkey);
            if (!pkey) pkey = (callpath_t*)&default_key;

            ABT_mutex_spinlock(ABT_MUTEX_MEMORY_GET_HANDLE(&monitor->mutex));
            HASH_FIND(hh, monitor->bulk_stats, pkey, sizeof(*pkey), bulk_stats);
            if (!bulk_stats) {
                bulk_stats = (bulk_statistics_t*)calloc(1, sizeof(*bulk_stats));
                bulk_stats->callpath = *pkey;
                HASH_ADD(hh, monitor->bulk_stats, callpath, sizeof(*pkey),
                         bulk_stats);
            }
            ABT_mutex_unlock(ABT_MUTEX_MEMORY_GET_HANDLE(&monitor->mutex));
            ABT_key_set(monitor->bulk_stats_key, bulk_stats);
        }

        event_args->uctx.f = timestamp;

        // TODO use session pool
        bulk_session_t* session           = calloc(1, sizeof(*session));
        session->start_ts                 = event_args->uctx.f;
        session->stats                    = bulk_stats;
        margo_monitor_data_t monitor_data = {.p = (void*)session};
        margo_request_set_monitoring_data(event_args->request, monitor_data);

    } else {

        margo_monitor_data_t monitor_data;
        margo_request_get_monitoring_data(event_args->request, &monitor_data);
        bulk_stats = ((bulk_session_t*)monitor_data.p)->stats;
        double t   = timestamp - event_args->uctx.f;
        UPDATE_STATISTICS_WITH(bulk_stats->transfer, t);
        UPDATE_STATISTICS_WITH(bulk_stats->transfer_size, event_args->size);
    }
}

static void margo_default_monitor_on_bulk_transfer_cb(
    void*                                 uargs,
    double                                timestamp,
    margo_monitor_event_t                 event_type,
    margo_monitor_bulk_transfer_cb_args_t event_args)
{
    default_monitor_state_t* monitor = (default_monitor_state_t*)uargs;
    // retrieve the session that was create on on_create
    RETRIEVE_BULK_SESSION(event_args->request);
    bulk_statistics_t* bulk_stats = session->stats;

    if (event_type == MARGO_MONITOR_FN_START) {

        event_args->uctx.f = timestamp;
        double t           = timestamp - session->start_ts;
        UPDATE_STATISTICS_WITH(bulk_stats->transfer_cb[TIMESTAMP], t);

    } else {
        double t = timestamp - event_args->uctx.f;
        UPDATE_STATISTICS_WITH(bulk_stats->transfer_cb[DURATION], t);
    }
}

__MONITOR_FN(bulk_free) {}
__MONITOR_FN(deregister) {}
__MONITOR_FN(lookup) {}
__MONITOR_FN(sleep) {}
__MONITOR_FN(free_input) {}
__MONITOR_FN(free_output) {}
__MONITOR_FN(prefinalize) {}
__MONITOR_FN(finalize) {}
__MONITOR_FN(user) {}

struct margo_monitor __margo_default_monitor
    = {.uargs      = NULL,
       .initialize = margo_default_monitor_initialize,
       .finalize   = margo_default_monitor_finalize,
       .name       = margo_default_monitor_name,
       .config     = margo_default_monitor_config,
#define X(__x__, __y__) .on_##__y__ = margo_default_monitor_on_##__y__,
       MARGO_EXPAND_MONITOR_MACROS
#undef X
};

struct margo_monitor* margo_default_monitor = &__margo_default_monitor;

static void
write_monitor_state_to_json_file(const default_monitor_state_t* monitor)
{
    if ((!monitor->filename_prefix) || (strlen(monitor->filename_prefix) == 0))
        return;
    /* get hostname */
    char hostname[1024];
    hostname[1023] = '\0';
    gethostname(hostname, 1023);
    /* get pid */
    pid_t pid = getpid();
    /* compute size needed for the full file name */
    size_t fullname_size = snprintf(NULL, 0, "%s.%s.%d.json",
                                    monitor->filename_prefix, hostname, pid);
    /* create full file name */
    char* fullname = calloc(1, fullname_size + 1);
    sprintf(fullname, "%s.%s.%d.json", monitor->filename_prefix, hostname, pid);
    /* open the file */
    int   errnum;
    FILE* file = fopen(fullname, "w");
    if (!file) {
        errnum = errno;
        margo_error(monitor->mid, "Error open file %s: %s", fullname,
                    strerror(errnum));
        goto finish;
    }
    /* create JSON statistics */
    ABT_mutex_spinlock(ABT_MUTEX_MEMORY_GET_HANDLE(&monitor->mutex));

    char double_format[] = "%.Xf";
    double_format[2]     = (char)(48 + monitor->precision);
    json_c_set_serialization_double_format(double_format, JSON_C_OPTION_GLOBAL);
    struct json_object* json = monitor_state_to_json(monitor);

    /* write statistics */
    size_t      json_len = 0;
    const char* json_str = json_object_to_json_string_length(
        json, monitor->pretty_json, &json_len);
    fwrite(json_str, json_len, 1, file);
    ABT_mutex_unlock(ABT_MUTEX_MEMORY_GET_HANDLE(&monitor->mutex));
    /* finish */
finish:
    free(fullname);
    if (file) fclose(file);
}

static struct json_object* statistics_to_json(const statistics_t* stats)
{
    struct json_object* json = json_object_new_object();
    json_object_object_add_ex(json, "num", json_object_new_int(stats->num),
                              JSON_C_OBJECT_ADD_KEY_IS_NEW);
    json_object_object_add_ex(json, "min", json_object_new_double(stats->min),
                              JSON_C_OBJECT_ADD_KEY_IS_NEW);
    json_object_object_add_ex(json, "max", json_object_new_double(stats->max),
                              JSON_C_OBJECT_ADD_KEY_IS_NEW);
    json_object_object_add_ex(json, "avg", json_object_new_double(stats->avg),
                              JSON_C_OBJECT_ADD_KEY_IS_NEW);
    json_object_object_add_ex(json, "var", json_object_new_double(stats->var),
                              JSON_C_OBJECT_ADD_KEY_IS_NEW);
    json_object_object_add_ex(json, "sum", json_object_new_double(stats->sum),
                              JSON_C_OBJECT_ADD_KEY_IS_NEW);
    return json;
}

static struct json_object*
duration_and_timestamp_statistics_to_json(const statistics_t* stats)
{
    struct json_object* json = json_object_new_object();
    json_object_object_add_ex(json, "duration", statistics_to_json(stats),
                              JSON_C_OBJECT_ADD_KEY_IS_NEW);
    json_object_object_add_ex(json, "timestamp", statistics_to_json(stats + 1),
                              JSON_C_OBJECT_ADD_KEY_IS_NEW);
    return json;
}

static struct json_object* hg_statistics_to_json(const hg_statistics_t* stats)
{
    struct json_object* json = json_object_new_object();
    json_object_object_add_ex(json, "progress_with_timeout",
                              statistics_to_json(&stats->progress_with_timeout),
                              JSON_C_OBJECT_ADD_KEY_IS_NEW);
    json_object_object_add_ex(
        json, "progress_without_timeout",
        statistics_to_json(&stats->progress_without_timeout),
        JSON_C_OBJECT_ADD_KEY_IS_NEW);
    json_object_object_add_ex(json, "trigger",
                              statistics_to_json(&stats->trigger),
                              JSON_C_OBJECT_ADD_KEY_IS_NEW);
    return json;
}

static struct json_object*
bulk_statistics_to_json(const bulk_statistics_t* stats)
{
    struct json_object* json = json_object_new_object();
    json_object_object_add_ex(json, "create",
                              statistics_to_json(&stats->create),
                              JSON_C_OBJECT_ADD_KEY_IS_NEW);
    json_object_object_add_ex(json, "transfer",
                              statistics_to_json(&stats->transfer),
                              JSON_C_OBJECT_ADD_KEY_IS_NEW);
    json_object_object_add_ex(json, "size",
                              statistics_to_json(&stats->transfer_size),
                              JSON_C_OBJECT_ADD_KEY_IS_NEW);
    json_object_object_add_ex(
        json, "transfer_cb",
        duration_and_timestamp_statistics_to_json(stats->transfer_cb),
        JSON_C_OBJECT_ADD_KEY_IS_NEW);
    json_object_object_add_ex(
        json, "wait", duration_and_timestamp_statistics_to_json(stats->wait),
        JSON_C_OBJECT_ADD_KEY_IS_NEW);
    return json;
}

static struct json_object*
origin_rpc_statistics_to_json(const origin_rpc_statistics_t* stats)
{
    struct json_object* json = json_object_new_object();
    json_object_object_add_ex(
        json, "forward",
        duration_and_timestamp_statistics_to_json(stats->forward),
        JSON_C_OBJECT_ADD_KEY_IS_NEW);
    json_object_object_add_ex(
        json, "forward_cb",
        duration_and_timestamp_statistics_to_json(stats->forward_cb),
        JSON_C_OBJECT_ADD_KEY_IS_NEW);
    json_object_object_add_ex(
        json, "wait", duration_and_timestamp_statistics_to_json(stats->wait),
        JSON_C_OBJECT_ADD_KEY_IS_NEW);
    json_object_object_add_ex(
        json, "set_input",
        duration_and_timestamp_statistics_to_json(stats->set_input),
        JSON_C_OBJECT_ADD_KEY_IS_NEW);
    json_object_object_add_ex(
        json, "get_output",
        duration_and_timestamp_statistics_to_json(stats->get_output),
        JSON_C_OBJECT_ADD_KEY_IS_NEW);
    return json;
}

static struct json_object*
target_rpc_statistics_to_json(const target_rpc_statistics_t* stats)
{
    struct json_object* json = json_object_new_object();
    json_object_object_add_ex(json, "handler",
                              statistics_to_json(&stats->handler),
                              JSON_C_OBJECT_ADD_KEY_IS_NEW);
    json_object_object_add_ex(
        json, "ult", duration_and_timestamp_statistics_to_json(stats->ult),
        JSON_C_OBJECT_ADD_KEY_IS_NEW);
    json_object_object_add_ex(
        json, "respond",
        duration_and_timestamp_statistics_to_json(stats->respond),
        JSON_C_OBJECT_ADD_KEY_IS_NEW);
    json_object_object_add_ex(
        json, "respond_cb",
        duration_and_timestamp_statistics_to_json(stats->respond_cb),
        JSON_C_OBJECT_ADD_KEY_IS_NEW);
    json_object_object_add_ex(
        json, "wait", duration_and_timestamp_statistics_to_json(stats->wait),
        JSON_C_OBJECT_ADD_KEY_IS_NEW);
    json_object_object_add_ex(
        json, "set_output",
        duration_and_timestamp_statistics_to_json(stats->set_output),
        JSON_C_OBJECT_ADD_KEY_IS_NEW);
    json_object_object_add_ex(
        json, "get_input",
        duration_and_timestamp_statistics_to_json(stats->get_input),
        JSON_C_OBJECT_ADD_KEY_IS_NEW);
    return json;
}

static char* build_rpc_key(const callpath_t* callpath)
{
    uint16_t provider_id;
    hg_id_t  base_id;
    uint16_t parent_provider_id;
    hg_id_t  parent_base_id;
    demux_id(callpath->rpc_id, &base_id, &provider_id);
    demux_id(callpath->parent_id, &parent_base_id, &parent_provider_id);
    size_t len     = snprintf(NULL, 0, "%lu:%d:%lu:%d", parent_base_id,
                          parent_provider_id, base_id, provider_id);
    char*  rpc_key = (char*)calloc(len + 1, 1);
    sprintf(rpc_key, "%lu:%d:%lu:%d", parent_base_id, parent_provider_id,
            base_id, provider_id);
    return rpc_key;
}

static void fill_with_rpc_info(struct json_object* rpc_json,
                               const callpath_t*   callpath,
                               rpc_info_t*         rpc_info_hash)
{
    uint16_t provider_id;
    hg_id_t  base_id;
    uint16_t parent_provider_id;
    hg_id_t  parent_base_id;
    demux_id(callpath->rpc_id, &base_id, &provider_id);
    demux_id(callpath->parent_id, &parent_base_id, &parent_provider_id);
    // add "id" entry
    json_object_object_add_ex(rpc_json, "id", json_object_new_uint64(base_id),
                              JSON_C_OBJECT_ADD_KEY_IS_NEW);
    // add "provider_id" entry
    json_object_object_add_ex(rpc_json, "provider_id",
                              json_object_new_uint64(provider_id),
                              JSON_C_OBJECT_ADD_KEY_IS_NEW);
    // add "parent_id" entry
    json_object_object_add_ex(rpc_json, "parent_id",
                              json_object_new_uint64(parent_base_id),
                              JSON_C_OBJECT_ADD_KEY_IS_NEW);
    // add "parent_provider_id" entry
    json_object_object_add_ex(rpc_json, "parent_provider_id",
                              json_object_new_uint64(parent_provider_id),
                              JSON_C_OBJECT_ADD_KEY_IS_NEW);
    // add "name" entry
    rpc_info_t* rpc_info = rpc_info_find(rpc_info_hash, callpath->rpc_id);
    const char* rpc_name = rpc_info ? rpc_info->name : "";
    rpc_name             = rpc_name ? rpc_name : "";
    json_object_object_add_ex(rpc_json, "name",
                              json_object_new_string(rpc_name),
                              JSON_C_OBJECT_ADD_KEY_IS_NEW);
}

static struct json_object*
monitor_state_to_json(const default_monitor_state_t* state)
{
    struct json_object* json = json_object_new_object();
    // mercury progress loop statistics
    json_object_object_add_ex(json, "progress_loop",
                              hg_statistics_to_json(&state->hg_stats),
                              JSON_C_OBJECT_ADD_KEY_IS_NEW);
    // RPC statistics
    struct json_object* rpcs = json_object_new_object();
    json_object_object_add_ex(json, "rpcs", rpcs, JSON_C_OBJECT_ADD_KEY_IS_NEW);
    // origin statistics
    {
        origin_rpc_statistics_t *p, *tmp;
        HASH_ITER(hh, state->origin_rpc_stats, p, tmp)
        {
            // build RPC key
            char* rpc_key = build_rpc_key(&(p->callpath));
            // create the entry in the "rpcs" section
            struct json_object* rpc_json = json_object_new_object();
            json_object_object_add(rpcs, rpc_key, rpc_json);
            // fill RPC information
            fill_with_rpc_info(rpc_json, &(p->callpath), state->rpc_info);
            // add "origin" statistics
            json_object_object_add_ex(rpc_json, "origin",
                                      origin_rpc_statistics_to_json(p),
                                      JSON_C_OBJECT_ADD_KEY_IS_NEW);
            free(rpc_key);
        }
    }
    // target statistics
    {
        target_rpc_statistics_t *p, *tmp;
        HASH_ITER(hh, state->target_rpc_stats, p, tmp)
        {
            // build RPC key
            char* rpc_key = build_rpc_key(&(p->callpath));
            // find json object corresponding to this RPC
            struct json_object* rpc_json
                = json_object_object_get(rpcs, rpc_key);
            if (!rpc_json) {
                rpc_json = json_object_new_object();
                json_object_object_add(rpcs, rpc_key, rpc_json);
                // fill RPC information
                fill_with_rpc_info(rpc_json, &(p->callpath), state->rpc_info);
            }
            // add "target" statistics
            json_object_object_add_ex(rpc_json, "target",
                                      target_rpc_statistics_to_json(p),
                                      JSON_C_OBJECT_ADD_KEY_IS_NEW);
            free(rpc_key);
        }
    }
    // bulk statistics
    {
        bulk_statistics_t *p, *tmp;
        HASH_ITER(hh, state->bulk_stats, p, tmp)
        {
            // build RPC key
            char* rpc_key = build_rpc_key(&(p->callpath));
            // find json object corresponding to this RPC
            struct json_object* rpc_json
                = json_object_object_get(rpcs, rpc_key);
            if (!rpc_json) {
                rpc_json = json_object_new_object();
                json_object_object_add(rpcs, rpc_key, rpc_json);
                // fill RPC information
                fill_with_rpc_info(rpc_json, &(p->callpath), state->rpc_info);
            }
            // add "bulk" statistics
            json_object_object_add_ex(rpc_json, "bulk",
                                      bulk_statistics_to_json(p),
                                      JSON_C_OBJECT_ADD_KEY_IS_NEW);
            free(rpc_key);
        }
    }
    return json;
}

static rpc_info_t* rpc_info_find(rpc_info_t* hash, hg_id_t id)
{
    rpc_info_t* info = NULL;
    HASH_FIND(hh, hash, &id, sizeof(id), info);
    return info;
}

static void rpc_info_clear(rpc_info_t* hash)
{
    rpc_info_t *p, *tmp;
    HASH_ITER(hh, hash, p, tmp)
    {
        HASH_DEL(hash, p);
        free(p);
    }
}

static addr_info_t* addr_info_find_or_add(addr_info_t*      addr_info_hash,
                                          margo_instance_id mid,
                                          hg_addr_t         addr)
{
    addr_info_t* info          = NULL;
    char         addr_str[128] = {0};
    hg_size_t    addr_str_size = 128;
    hg_return_t  hret
        = margo_addr_to_string(mid, addr_str, &addr_str_size, addr);
    if (hret != HG_SUCCESS) {
        strcpy(addr_str, "<unknown>");
        addr_str_size = 9;
    }
    HASH_FIND(hh, addr_info_hash, addr_str, addr_str_size, info);
    if (info) return info;
    info = (addr_info_t*)calloc(1, sizeof(*info) + addr_str_size);
    memcpy(info->name, addr_str, addr_str_size);
    HASH_ADD(hh, addr_info_hash, name[0], addr_str_size, info);
    return info;
}

static void addr_info_clear(addr_info_t* hash)
{
    addr_info_t *p, *tmp;
    HASH_ITER(hh, hash, p, tmp)
    {
        HASH_DEL(hash, p);
        free(p);
    }
}
