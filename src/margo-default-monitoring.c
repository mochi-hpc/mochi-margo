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

/* Statistics related to the Mercury progress loop */
typedef struct hg_statistics {
    statistics_t progress_with_timeout;
    statistics_t progress_without_timeout;
    statistics_t trigger;
} hg_statistics_t;

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

/* Structure used to track registered RPCs */
typedef struct rpc_info {
    hg_id_t        id;
    UT_hash_handle hh;
    char           name[1];
} rpc_info_t;

/* Root of the monitor's state */
typedef struct default_monitor_state {
    margo_instance_id mid;
    char*             filename_prefix;
    int               precision; /* precision used when printing doubles */
    /* RPC information */
    rpc_info_t* rpc_info;
    /* Argobots keys */
    ABT_key bulk_stats_key; /* may be associated with a bulk_statistics_t* */
    ABT_key callpath_key;   /* mat be associated with a callpath */
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

static void write_monitor_state_to_json_file(default_monitor_state_t* monitor);

static void* margo_default_monitor_initialize(margo_instance_id   mid,
                                              void*               uargs,
                                              struct json_object* config)
{
    default_monitor_state_t* monitor = calloc(1, sizeof(*monitor));
    ABT_key_create(NULL, &(monitor->bulk_stats_key));
    ABT_key_create(free, &(monitor->callpath_key));
    monitor->mid = mid;

    /* default configuration */
    char filename_prefix_template[] = "margo-XXXXXX";
    int  ret                        = mkstemp(filename_prefix_template);
    monitor->filename_prefix        = strdup(filename_prefix_template);
    monitor->precision              = 9;

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
    {
        rpc_info_t *p, *tmp;
        HASH_ITER(hh, monitor->rpc_info, p, tmp)
        {
            HASH_DEL(monitor->rpc_info, p);
            free(p);
        }
    }
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
    // retrieve the session that was create on on_create
    RETRIEVE_SESSION(event_args->handle);
    origin_rpc_statistics_t* rpc_stats = NULL;

    if (event_type == MARGO_MONITOR_FN_START) {
        event_args->uctx.f = timestamp;
        // attach statistics to session
        hg_id_t id     = margo_get_info(event_args->handle)->id;
        id             = mux_id(id, event_args->provider_id);
        callpath_t key = {.rpc_id = id, .parent_id = 0};

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
    RETRIEVE_SESSION(event_args->handle);
    target_rpc_statistics_t* rpc_stats = NULL;

    if (event_type == MARGO_MONITOR_FN_START) {
        // TODO use a session pool
        if (!session) { session = calloc(1, sizeof(*session)); }
        session->target.start_ts          = timestamp;
        margo_monitor_data_t monitor_data = {.p = (void*)session};
        margo_set_monitoring_data(event_args->handle, monitor_data);

        // attach statistics to session
        hg_id_t    id  = margo_get_info(event_args->handle)->id;
        callpath_t key = {.rpc_id = id, .parent_id = 0};

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
        callpath_t* current_callpath = calloc(1, sizeof(*current_callpath));
        // TODO: get parent_id from breadcrumb
        current_callpath->rpc_id = margo_get_info(event_args->handle)->id;
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
        static const callpath_t default_key = {0};
        callpath_t*             pkey        = NULL;
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
            static const callpath_t default_key = {0};
            callpath_t*             pkey        = NULL;
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

static void write_statistics(FILE* file, statistics_t* stats, int precision)
{
    fprintf(file,
            "{\"num\":%lu,\"min\":%.*lf,\"max\":%.*lf,\"sum\":%.*lf,\"avg\":%.*"
            "lf,\"var\":%.*lf}",
            stats->num, precision, stats->min, precision, stats->max, precision,
            stats->sum, precision, stats->avg, precision, stats->var);
}

static void
write_hg_statistics(FILE* file, hg_statistics_t* stats, int precision)
{
    fprintf(file, "{\"progress_with_timeout\":");
    write_statistics(file, &stats->progress_with_timeout, precision);
    fprintf(file, ",\"progress_without_timeout\":");
    write_statistics(file, &stats->progress_without_timeout, precision);
    fprintf(file, ",\"trigger\":");
    write_statistics(file, &stats->trigger, precision);
    fprintf(file, "}");
}

static void write_monitor_state_to_json_file(default_monitor_state_t* monitor)
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
    /* write statistics */
    ABT_mutex_spinlock(ABT_MUTEX_MEMORY_GET_HANDLE(&monitor->mutex));
    fprintf(file, "{\"progress_loop\":");
    write_hg_statistics(file, &monitor->hg_stats, monitor->precision);
    fprintf(file, "}");
    /* finish */
finish:
    ABT_mutex_unlock(ABT_MUTEX_MEMORY_GET_HANDLE(&monitor->mutex));
    free(fullname);
    if (file) fclose(file);
}
