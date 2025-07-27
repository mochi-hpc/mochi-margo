/**

 * (C) 2022 The University of Chicago
 *
 * See COPYRIGHT in top-level directory.
 */
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <abt.h>
#include <json-c/json.h>
#include "margo-macros.h"
#include "margo-instance.h"
#include "margo-monitoring.h"
#include "margo-id.h"
#ifdef __clang_analyzer__
    // Prevent clang-analyzer from getting confused by the actual HASH_JEN
    // macro. This trivial HASH_FUNCTION is not actually used when the code is
    // compiled.
    #define HASH_FUNCTION(keyptr, keylen, hashv) hashv = 0
#endif
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
 *   and get_output. This is a UThash indexed by "callpath", which
 *   represents a tuple (rpc_id, parent_rpc_id, address_id),
 *   with the address id representing the destination address.
 *
 * - target_rpc_statistics: statistics on calls to an RPC at its
 *   target, including handler, ULT, respond, respond callback,
 *   wait, get_input, and set_output. This is a UThash indexed
 *   by callpath, with the address id representing the source
 *   of the RPC.
 *
 * - bulk_create_statistics: statistics on bulk creation operations
 *   (size and durations). The monitoring system will make a best
 *   attempt at locating the context (callpath) in which the bulk
 *   creation is done, using ABT_key. bulk_create_statistics is
 *   a UThash indexed by such a callpath.
 *
 * - bulk_transfer_statistics: statistics on bulk transfer operations
 *   (transfer, transfer callback, wait, and transfer size).
 *   This is a UThash indexed by a key formed using the callpath,
 *   the operation (push or pull) and the remote address of the
 *   transfer.
 *
 * The reasons bulk creation and transfer operations are tracked
 * separately are (1) services may create bulk handle ahead of time
 * and cache them, so their creation may not be linked to the execution
 * of a particular RPC, and (2) while bulk creation statistics are
 * maintained per callpath, transfer statistics are further maintained
 * per remote address and per operation (pull/push).
 *
 * Note that RPC ids above contain the encoded provider ids, so
 * demux_id can be used to further obtain the provider id and base id
 * from these RPC ids.
 */

/*
 * Time series in Margo's default monitoring system
 * ================================================
 *
 * The monitoring system will track time series by periodically
 * checking some properties of the margo instance, including
 * properties about the RPCs and about the pools. The time interval
 * is configurable via the "time_interval_sec" property.
 *
 * - RPC time series include the following:
 *   - Count of RPCs received since the last check;
 *   - Bulk size transferred in RPCs since last check;
 * - Pool time series include the following:
 *   - Size of the pool at the current check (number of
 *     runnable ULTs in the pool);
 *   - Total size of the pool at the current check
 *     (number of runnable and suspended ULTs in the pool);
 *
 * Time series also include an array of timestamps at which
 * the measurements are made. This is because while the system
 * will make a best effort in satisfying the specified time interval,
 * if it's busy servicing RPCs, the measurements may be delayed.
 *
 * RPC time series are managed by a UThash indexed by RPC id
 * (not callpath, contrary to statistics, but the RPC id does
 * include the provider id).
 * Pool time series are managed by an array initialized with
 * the number of pools specified in margo's configuration.
 */

/* ========================================================================
 * Helper JSON-C function
 * ======================================================================== */

/* This function checks if a key is present in a json object and, if not,
 * will create it and associate it with an empty object.
 * This function does not check that the provided root object is indeed
 * a JSON object.
 */
inline static struct json_object*
json_object_object_get_or_create_object(struct json_object* root,
                                        const char*         key)
{
    struct json_object* result = json_object_object_get(root, key);
    if (!result) {
        result = json_object_new_object();
        json_object_object_add_ex(root, key, result,
                                  JSON_C_OBJECT_ADD_KEY_IS_NEW);
    }
    return result;
}

/* ========================================================================
 * Statistics structure definitions
 * ======================================================================== */

struct default_monitor_state;

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
    hg_id_t  rpc_id;    /* current RPC id */
    hg_id_t  parent_id; /* id of the RPC it came from, if any */
    uint64_t addr_id; /* address the current RPC was sent to or received from */
} callpath_t;

/* Bulk transfer operations are further indexed by
 * the remote address and the operation, in addition
 * to their callpath.
 */
typedef struct bulk_key {
    callpath_t callpath;
    uint64_t
        remote_addr_id; /* address of the peer with which transfer is done */
    hg_bulk_op_t operation; /* PULL or PUSH */
} bulk_key_t;

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

static struct json_object* statistics_to_json(const statistics_t* stats,
                                              bool                reset);

/* Statistics related to the Mercury progress loop */
typedef struct hg_statistics {
    statistics_t progress_with_timeout;
    statistics_t progress_without_timeout;
    statistics_t progress_timeout_value;
    statistics_t trigger;
} hg_statistics_t;

static struct json_object* hg_statistics_to_json(const hg_statistics_t* stats,
                                                 bool                   reset);

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

/* Statistics related to bulk creation. */
typedef struct bulk_create_statistics {
    statistics_t   duration;
    statistics_t   size;
    callpath_t     callpath; /* hash key */
    UT_hash_handle hh;       /* hash handle */
} bulk_create_statistics_t;

static struct json_object*
bulk_create_statistics_to_json(const bulk_create_statistics_t* stats,
                               bool                            reset);

/* Statistics related to bulk transfers */
typedef struct bulk_transfer_statistics {
    statistics_t   transfer; /* reference timestamp */
    statistics_t   transfer_size;
    statistics_t   transfer_cb[2];
    statistics_t   wait[2];
    bulk_key_t     bulk_key; /* hash key */
    UT_hash_handle hh;       /* hash handle */
} bulk_transfer_statistics_t;

static struct json_object*
bulk_transfer_statistics_to_json(const bulk_transfer_statistics_t* stats,
                                 bool                              reset);

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
origin_rpc_statistics_to_json(const origin_rpc_statistics_t* stats, bool reset);

/* Statistics related to RPCs at their target */
typedef struct target_rpc_statistics {
    statistics_t   handler; /* handler timestamp isn't used */
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
target_rpc_statistics_to_json(const target_rpc_statistics_t* stats, bool reset);

/* ========================================================================
 * Time series structure definitions
 * ======================================================================== */

/* uint64_t value associated with a timestamp */
typedef struct timedval {
    uint64_t value;
    double   timestamp;
} timedval_t;

/* Linked list of arrays of time_uint64_t */
typedef struct timedval_frame {
    struct timedval_frame* next;    /* next frame */
    size_t                 size;    /* current size of the data array */
    timedval_t             data[1]; /* data */
} timedval_frame_t;

/* Time series */
typedef struct time_series {
    size_t            frame_capacity; /* allocated size of each frame */
    timedval_frame_t* first_frame;
    timedval_frame_t* last_frame;
} time_series_t;

static void time_series_append(time_series_t* series, double ts, uint64_t val);
static void time_series_clear(time_series_t* series);

/* RPC-related time series */
typedef struct rpc_time_series {
    uint64_t      rpc_count;        /* count of RPCs since last added value */
    time_series_t rpc_count_series; /* time series of rpc_count */
    uint64_t bulk_size; /* total size bulk-transferred since last added value */
    time_series_t  bulk_size_series; /* time series of bulk-transferred size */
    hg_id_t        rpc_id;           /* hash key */
    UT_hash_handle hh;               /* hash handle */
    /* mutex protecting accesses */
    ABT_mutex_memory mutex;
} rpc_time_series_t;

static rpc_time_series_t*
find_or_add_time_series_for_rpc(struct default_monitor_state* monitor,
                                hg_id_t                       rpc_id);
static void free_all_time_series(struct default_monitor_state* monitor);
static struct json_object* rpc_time_series_to_json(rpc_time_series_t* rpc_ts,
                                                   bool               reset);
static void update_rpc_time_series(struct default_monitor_state* monitor,
                                   double                        timestamp);

static void update_pool_time_series(struct default_monitor_state* monitor,
                                    double                        timestamp);
static struct json_object*
pool_time_series_to_json(const struct default_monitor_state* monitor,
                         bool                                reset);

/* ========================================================================
 * RPC info
 * ======================================================================== */

/* Structure used to track registered RPCs */
typedef struct rpc_info {
    hg_id_t        id;
    UT_hash_handle hh;
    char           name[1];
} rpc_info_t;

static rpc_info_t* rpc_info_find(rpc_info_t* hash, hg_id_t id);
static void        rpc_info_clear(rpc_info_t* hash);

/* Structure used to track addresses, hashed by name */
typedef struct addr_info {
    UT_hash_handle hh_by_id;
    UT_hash_handle hh_by_name;
    uint64_t       id;
    char           name[1];
} addr_info_t;

static addr_info_t* addr_info_find_or_add(struct default_monitor_state* monitor,
                                          margo_instance_id             mid,
                                          hg_addr_t                     addr);
static addr_info_t*
addr_info_find_by_id(const struct default_monitor_state* monitor, uint64_t id);
static void addr_info_clear(addr_info_t* hash_by_id, addr_info_t* hash_by_name);

struct session;
struct bulk_session;

/* Root of the monitor's state */
typedef struct default_monitor_state {
    margo_instance_id mid;
    bool              enable_statistics;
    bool              enable_time_series;
    char*             filename_prefix;
    int               precision; /* precision used when printing doubles */
    int stats_pretty_json;       /* use tabs and stuff in JSON printing */
    int time_series_pretty_json; /* use tabs and stuff in JSON printing */
    int sample_progress_every;
    /* self address */
    char* self_addr_str;
    /* sampling counter */
    uint64_t progress_sampling;
    /* RPC information */
    rpc_info_t*      rpc_info;
    ABT_mutex_memory rpc_info_mtx;
    /* Address info */
    addr_info_t* addr_info_by_name; /* hash of addr_info_t by name field */
    addr_info_t* addr_info_by_id;   /* hash of addr_info_t by id field */
    uint64_t     addr_info_last_id; /* last id used for addresses */
    ABT_mutex_memory
        addr_info_mtx; /* mutex protecting access to the addr_info fields */
    /* Argobots keys */
    ABT_key callpath_key; /* may be associated with a callpath */
    /* Statistics and their mutex */
    hg_statistics_t             hg_stats;
    ABT_mutex_memory            hg_stats_mtx;
    bulk_create_statistics_t*   bulk_create_stats;
    ABT_mutex_memory            bulk_create_stats_mtx;
    bulk_transfer_statistics_t* bulk_transfer_stats;
    ABT_mutex_memory            bulk_transfer_stats_mtx;
    origin_rpc_statistics_t*    origin_rpc_stats;
    ABT_mutex_memory            origin_rpc_stats_mtx;
    target_rpc_statistics_t*    target_rpc_stats;
    ABT_mutex_memory            target_rpc_stats_mtx;
    /* Time series and their mutex */
    rpc_time_series_t* rpc_time_series; /* hash */
    ABT_mutex_memory   rpc_time_series_mtx;
    time_series_t* pool_size_time_series; /* array of size mid->abt.pools_len */
    time_series_t*
        pool_total_size_time_series; /* array of size mid->abt.pools_len */
    ABT_mutex_memory pool_time_series_mtx;
    double           rpc_time_series_last_ts;
    double           time_series_interval;
    /* session and bulk_session pools */
    struct session*      session_pool;
    ABT_mutex_memory     session_pool_mtx;
    struct bulk_session* bulk_session_pool;
    ABT_mutex_memory     bulk_session_pool_mtx;
    /* Time series */
} default_monitor_state_t;

static struct json_object*
monitor_statistics_to_json(const default_monitor_state_t* monitor, bool reset);
static struct json_object*
monitor_time_series_to_json(const default_monitor_state_t* monitor, bool reset);

static void
write_monitor_state_to_json_file(const default_monitor_state_t* monitor,
                                 bool                           reset);

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
        double                   create_ts;
        double                   forward_start_ts;
        double                   forward_end_ts;
        double                   wait_end_ts;
        origin_rpc_statistics_t* stats;
    } origin;
    struct {
        double                   handler_start_ts;
        double                   ult_start_ts;
        double                   respond_start_ts;
        double                   respond_end_ts;
        target_rpc_statistics_t* stats;
    } target;
    struct session* next; /* used to managed the session pool */
} session_t;

typedef struct bulk_session {
    double                      transfer_start_ts;
    double                      transfer_end_ts;
    bulk_transfer_statistics_t* stats;
    struct bulk_session* next; /* used to managed the bulk_session pool */
} bulk_session_t;

static inline session_t* new_session(default_monitor_state_t* monitor)
{
    session_t* result = NULL;
    ABT_mutex_spinlock(ABT_MUTEX_MEMORY_GET_HANDLE(&monitor->session_pool_mtx));
    if (monitor->session_pool) {
        result                = monitor->session_pool;
        monitor->session_pool = result->next;
        memset(result, 0, sizeof(*result));
    } else {
        result = (session_t*)calloc(1, sizeof(*result));
    }
    ABT_mutex_unlock(ABT_MUTEX_MEMORY_GET_HANDLE(&monitor->session_pool_mtx));
    return result;
}

static inline void release_session(default_monitor_state_t* monitor,
                                   session_t*               session)
{
    ABT_mutex_spinlock(ABT_MUTEX_MEMORY_GET_HANDLE(&monitor->session_pool_mtx));
    session->next         = monitor->session_pool;
    monitor->session_pool = session;
    ABT_mutex_unlock(ABT_MUTEX_MEMORY_GET_HANDLE(&monitor->session_pool_mtx));
}

static inline void clear_session_pool(default_monitor_state_t* monitor)
{
    ABT_mutex_spinlock(ABT_MUTEX_MEMORY_GET_HANDLE(&monitor->session_pool_mtx));
    session_t* session = monitor->session_pool;
    while (session) {
        session_t* next = session->next;
        free(session);
        session = next;
    }
    ABT_mutex_unlock(ABT_MUTEX_MEMORY_GET_HANDLE(&monitor->session_pool_mtx));
}

static inline bulk_session_t* new_bulk_session(default_monitor_state_t* monitor)
{
    bulk_session_t* result = NULL;
    ABT_mutex_spinlock(
        ABT_MUTEX_MEMORY_GET_HANDLE(&monitor->bulk_session_pool_mtx));
    if (monitor->bulk_session_pool) {
        result                     = monitor->bulk_session_pool;
        monitor->bulk_session_pool = result->next;
        memset(result, 0, sizeof(*result));
    } else {
        result = (bulk_session_t*)calloc(1, sizeof(*result));
    }
    ABT_mutex_unlock(
        ABT_MUTEX_MEMORY_GET_HANDLE(&monitor->bulk_session_pool_mtx));
    return result;
}

static inline void release_bulk_session(default_monitor_state_t* monitor,
                                        bulk_session_t*          session)
{
    ABT_mutex_spinlock(
        ABT_MUTEX_MEMORY_GET_HANDLE(&monitor->bulk_session_pool_mtx));
    session->next              = monitor->bulk_session_pool;
    monitor->bulk_session_pool = session;
    ABT_mutex_unlock(
        ABT_MUTEX_MEMORY_GET_HANDLE(&monitor->bulk_session_pool_mtx));
}

static inline void clear_bulk_session_pool(default_monitor_state_t* monitor)
{
    ABT_mutex_spinlock(
        ABT_MUTEX_MEMORY_GET_HANDLE(&monitor->bulk_session_pool_mtx));
    bulk_session_t* session = monitor->bulk_session_pool;
    while (session) {
        bulk_session_t* next = session->next;
        free(session);
        session = next;
    }
    ABT_mutex_unlock(
        ABT_MUTEX_MEMORY_GET_HANDLE(&monitor->bulk_session_pool_mtx));
}

/* ========================================================================
 * Functions related to implementing the monitor's callbacks
 * ======================================================================== */

static void* __margo_default_monitor_initialize(margo_instance_id   mid,
                                                void*               uargs,
                                                struct json_object* config)
{
    (void)uargs;
    default_monitor_state_t* monitor = calloc(1, sizeof(*monitor));
    ABT_key_create(NULL, &(monitor->callpath_key));
    monitor->mid = mid;

    /* default configuration */
    const char* prefix         = getenv("MARGO_MONITORING_FILENAME_PREFIX");
    const char* disable_stats  = getenv("MARGO_MONITORING_DISABLE_STATISTICS");
    const char* disable_series = getenv("MARGO_MONITORING_DISABLE_TIME_SERIES");
    monitor->filename_prefix   = strdup(prefix ? prefix : "margo");
    monitor->precision         = 9;
    monitor->stats_pretty_json = 0;
    monitor->time_series_pretty_json = 0;
    monitor->sample_progress_every   = 1;
    monitor->time_series_interval    = 1.0;
    monitor->enable_statistics       = !disable_stats;
    monitor->enable_time_series      = !disable_series;

    /* read configuration */
    struct json_object* filename_prefix
        = json_object_object_get(config, "filename_prefix");
    if (filename_prefix
        && json_object_is_type(filename_prefix, json_type_string)) {
        free(monitor->filename_prefix);
        monitor->filename_prefix
            = strdup(json_object_get_string(filename_prefix));
    }
    /* statistics configuration */
    struct json_object* statistics
        = json_object_object_get(config, "statistics");
    if (statistics && json_object_is_type(statistics, json_type_object)) {
        struct json_object* disable
            = json_object_object_get(statistics, "disable");
        if (disable && json_object_is_type(disable, json_type_boolean)) {
            monitor->enable_statistics = !json_object_get_boolean(disable);
        }
        struct json_object* precision
            = json_object_object_get(statistics, "precision");
        if (precision && json_object_is_type(precision, json_type_int)) {
            monitor->precision = json_object_get_int(precision);
        }
        if (monitor->precision > 9) monitor->precision = 9;
        struct json_object* sampling
            = json_object_object_get(statistics, "sample_progress_every");
        if (sampling && json_object_is_type(sampling, json_type_int)) {
            monitor->sample_progress_every = json_object_get_int(sampling);
            if (monitor->sample_progress_every <= 0)
                monitor->sample_progress_every = 0;
        }
        struct json_object* pretty_json
            = json_object_object_get(statistics, "pretty_json");
        if (pretty_json && json_object_is_type(pretty_json, json_type_boolean)
            && json_object_get_boolean(pretty_json)) {
            monitor->stats_pretty_json = JSON_C_TO_STRING_PRETTY;
        }
    }

    /* time_series configuration */
    struct json_object* time_series
        = json_object_object_get(config, "time_series");
    if (time_series && json_object_is_type(time_series, json_type_object)) {
        struct json_object* disable
            = json_object_object_get(time_series, "disable");
        if (disable && json_object_is_type(disable, json_type_boolean)) {
            monitor->enable_time_series = !json_object_get_boolean(disable);
        }
        struct json_object* precision
            = json_object_object_get(time_series, "precision");
        if (precision && json_object_is_type(precision, json_type_int)) {
            monitor->precision = json_object_get_int(precision);
        }
        struct json_object* time_interval
            = json_object_object_get(time_series, "time_interval_sec");
        if (time_interval
            && json_object_is_type(time_interval, json_type_double)) {
            if (json_object_is_type(time_interval, json_type_double))
                monitor->time_series_interval
                    = json_object_get_double(time_interval);
            else if (json_object_is_type(time_interval, json_type_int))
                monitor->time_series_interval
                    = (double)json_object_get_int64(time_interval);
        }
        struct json_object* pretty_json
            = json_object_object_get(time_series, "pretty_json");
        if (pretty_json && json_object_is_type(pretty_json, json_type_boolean)
            && json_object_get_boolean(pretty_json)) {
            monitor->time_series_pretty_json = JSON_C_TO_STRING_PRETTY;
        }
    }

    /* allocate time array for pool size time series */
    if (monitor->enable_time_series) {
        size_t num_pools = margo_get_num_pools(monitor->mid);
        monitor->pool_size_time_series
            = (time_series_t*)calloc(num_pools, sizeof(time_series_t));
        monitor->pool_total_size_time_series
            = (time_series_t*)calloc(num_pools, sizeof(time_series_t));
        for (size_t i = 0; i < num_pools; i++) {
            // TODO: make this configurable?
            monitor->pool_size_time_series[i].frame_capacity       = 256;
            monitor->pool_total_size_time_series[i].frame_capacity = 256;
        }
    }

    /* preinitialize sessions */
    for (int i = 0; i < 32; i++) {
        session_t* session = (session_t*)malloc(sizeof(*session));
        release_session(monitor, session);
        bulk_session_t* bulk_session
            = (bulk_session_t*)malloc(sizeof(*bulk_session));
        release_bulk_session(monitor, bulk_session);
    }

    /* get self address */
    char      self_addr_str[256] = {0};
    hg_size_t self_addr_size = 256;
    hg_addr_t self_addr = HG_ADDR_NULL;
    margo_addr_self(mid, &self_addr);
    margo_addr_to_string(mid, self_addr_str, &self_addr_size, self_addr);
    margo_addr_free(mid, self_addr);
    monitor->self_addr_str = strdup(self_addr_str[0] ? self_addr_str : "<unknown>");

    return (void*)monitor;
}

static void __margo_default_monitor_finalize(void* uargs)
{
    default_monitor_state_t* monitor = (default_monitor_state_t*)uargs;
    if (!monitor) return;

    /* do a final update of time series */
    double ts = ABT_get_wtime();
    update_rpc_time_series(monitor, ts);
    update_pool_time_series(monitor, ts);

    /* write JSON file */
    write_monitor_state_to_json_file(monitor, false);

    /* free RPC info */
    rpc_info_clear(monitor->rpc_info);

    /* free address info */
    addr_info_clear(monitor->addr_info_by_id, monitor->addr_info_by_name);
    monitor->addr_info_by_name = NULL;
    monitor->addr_info_by_id   = NULL;

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
    /* free bulk_create statistics */
    {
        bulk_create_statistics_t *p, *tmp;
        HASH_ITER(hh, monitor->bulk_create_stats, p, tmp)
        {
            HASH_DEL(monitor->bulk_create_stats, p);
            free(p);
        }
    }
    /* free bulk_transfer statistics */
    {
        bulk_transfer_statistics_t *p, *tmp;
        HASH_ITER(hh, monitor->bulk_transfer_stats, p, tmp)
        {
            HASH_DEL(monitor->bulk_transfer_stats, p);
            free(p);
        }
    }
    /* free RPC and bulk time series */
    free_all_time_series(monitor);
    /* free session pools */
    clear_session_pool(monitor);
    clear_bulk_session_pool(monitor);
    /* free ABT key */
    ABT_key_free(&(monitor->callpath_key));
    /* free filename */
    free(monitor->filename_prefix);
    /* free self_addr */
    free(monitor->self_addr_str);
    free(monitor);
}

static const char* __margo_default_monitor_name() { return "default"; }

static struct json_object* __margo_default_monitor_config(void* uargs)
{
    default_monitor_state_t* monitor = (default_monitor_state_t*)uargs;
    if (!monitor) return NULL;

    struct json_object* config = json_object_new_object();
    json_object_object_add_ex(config, "filename_prefix",
                              json_object_new_string(monitor->filename_prefix),
                              JSON_C_OBJECT_ADD_KEY_IS_NEW);
    json_object_object_add_ex(config, "precision",
                              json_object_new_int(monitor->precision),
                              JSON_C_OBJECT_ADD_KEY_IS_NEW);
    struct json_object* statistics = json_object_new_object();
    json_object_object_add_ex(config, "statistics", statistics,
                              JSON_C_OBJECT_ADD_KEY_IS_NEW);
    json_object_object_add_ex(
        statistics, "pretty_json",
        json_object_new_boolean(monitor->stats_pretty_json),
        JSON_C_OBJECT_ADD_KEY_IS_NEW);
    json_object_object_add_ex(
        statistics, "disable",
        json_object_new_boolean(!monitor->enable_statistics),
        JSON_C_OBJECT_ADD_KEY_IS_NEW);
    struct json_object* time_series = json_object_new_object();
    json_object_object_add_ex(config, "time_series", time_series,
                              JSON_C_OBJECT_ADD_KEY_IS_NEW);
    json_object_object_add_ex(
        time_series, "pretty_json",
        json_object_new_boolean(monitor->stats_pretty_json),
        JSON_C_OBJECT_ADD_KEY_IS_NEW);
    json_object_object_add_ex(
        time_series, "disable",
        json_object_new_boolean(!monitor->enable_time_series),
        JSON_C_OBJECT_ADD_KEY_IS_NEW);
    json_object_object_add_ex(
        time_series, "time_interval_sec",
        json_object_new_double(monitor->time_series_interval),
        JSON_C_OBJECT_ADD_KEY_IS_NEW);
    return config;
}

static void
__margo_default_monitor_on_register(void*                         uargs,
                                    double                        timestamp,
                                    margo_monitor_event_t         event_type,
                                    margo_monitor_register_args_t event_args)
{
    (void)timestamp;
    if (event_type == MARGO_MONITOR_FN_START) return;
    default_monitor_state_t* monitor = (default_monitor_state_t*)uargs;
    rpc_info_t*              rpc_info
        = (rpc_info_t*)calloc(1, sizeof(*rpc_info) + strlen(event_args->name));
    rpc_info->id = event_args->id;
    strcpy(rpc_info->name, event_args->name);
    ABT_mutex_spinlock(ABT_MUTEX_MEMORY_GET_HANDLE(&monitor->rpc_info_mtx));
    HASH_ADD(hh, monitor->rpc_info, id, sizeof(hg_id_t), rpc_info);
    ABT_mutex_unlock(ABT_MUTEX_MEMORY_GET_HANDLE(&monitor->rpc_info_mtx));
}

static void
__margo_default_monitor_on_progress(void*                         uargs,
                                    double                        timestamp,
                                    margo_monitor_event_t         event_type,
                                    margo_monitor_progress_args_t event_args)
{
    default_monitor_state_t* monitor = (default_monitor_state_t*)uargs;

    /* update time series */
    if ((event_type == MARGO_MONITOR_FN_END) && monitor->enable_time_series
        && (timestamp > (monitor->rpc_time_series_last_ts
                         + monitor->time_series_interval))) {
        update_rpc_time_series(monitor, timestamp);
        update_pool_time_series(monitor, timestamp);
    }

    if (event_type == MARGO_MONITOR_FN_START) monitor->progress_sampling += 1;

    /* statistics */
    if (!monitor->enable_statistics) return;

    if (event_type == MARGO_MONITOR_FN_START) {
        if (!monitor->sample_progress_every
            || (monitor->progress_sampling % monitor->sample_progress_every))
            return;

        event_args->uctx.f = timestamp;
        return;
    }

    if (!monitor->sample_progress_every
        || (monitor->progress_sampling % monitor->sample_progress_every))
        return;
    monitor->progress_sampling %= monitor->sample_progress_every;

    // MARGO_MONITOR_FN_END
    double t = timestamp - event_args->uctx.f;
    if (event_args->timeout_ms) {
        UPDATE_STATISTICS_WITH(monitor->hg_stats.progress_with_timeout, t);
        UPDATE_STATISTICS_WITH(monitor->hg_stats.progress_timeout_value,
                               event_args->timeout_ms);
    } else {
        UPDATE_STATISTICS_WITH(monitor->hg_stats.progress_without_timeout, t);
    }
}

static void
__margo_default_monitor_on_trigger(void*                        uargs,
                                   double                       timestamp,
                                   margo_monitor_event_t        event_type,
                                   margo_monitor_trigger_args_t event_args)
{
    default_monitor_state_t* monitor = (default_monitor_state_t*)uargs;
    if (!monitor->enable_statistics) return;

    if (event_type == MARGO_MONITOR_FN_START) {
        event_args->uctx.f = timestamp;
        return;
    }
    // MARGO_MONITOR_FN_END
    if (event_args->actual_count == 0) return;
    double t = timestamp - event_args->uctx.f;
    UPDATE_STATISTICS_WITH(monitor->hg_stats.trigger, t);
}

static void
__margo_default_monitor_on_create(void*                       uargs,
                                  double                      timestamp,
                                  margo_monitor_event_t       event_type,
                                  margo_monitor_create_args_t event_args)
{
    default_monitor_state_t* monitor = (default_monitor_state_t*)uargs;
    if (!monitor->enable_statistics) return;

    if (event_type == MARGO_MONITOR_FN_START) {
        event_args->uctx.f = timestamp;
        return;
    }
    // MARGO_MONITOR_FN_END

    session_t* session = new_session(monitor);

    session->origin.create_ts         = timestamp;
    margo_monitor_data_t monitor_data = {.p = (void*)session};
    margo_set_monitoring_data(event_args->handle, monitor_data);

#ifdef __clang_analyzer__
    // disable static analyzer warning about leaking memory
    // this is code is not actually executed.
    free(session);
#endif
}

#define RETRIEVE_SESSION(handle)                                            \
    session_t* session = NULL;                                              \
    do {                                                                    \
        margo_monitor_data_t monitor_data;                                  \
        hg_return_t ret = margo_get_monitoring_data(handle, &monitor_data); \
        (void)ret;                                                          \
        session = (session_t*)monitor_data.p;                               \
    } while (0)

#define RETRIEVE_BULK_SESSION(request)                                   \
    bulk_session_t* session = NULL;                                      \
    do {                                                                 \
        margo_monitor_data_t monitor_data;                               \
        hg_return_t          ret                                         \
            = margo_request_get_monitoring_data(request, &monitor_data); \
        (void)ret;                                                       \
        session = (bulk_session_t*)monitor_data.p;                       \
    } while (0)

static void
__margo_default_monitor_on_forward(void*                        uargs,
                                   double                       timestamp,
                                   margo_monitor_event_t        event_type,
                                   margo_monitor_forward_args_t event_args)
{
    default_monitor_state_t* monitor = (default_monitor_state_t*)uargs;
    if (!monitor->enable_statistics) return;

    margo_instance_id mid = margo_hg_handle_get_instance(event_args->handle);
    const struct hg_info* handle_info = margo_get_info(event_args->handle);
    if (!handle_info) return;
    // retrieve the session that was create on on_create
    RETRIEVE_SESSION(event_args->handle);
    origin_rpc_statistics_t* rpc_stats = NULL;

    if (event_type == MARGO_MONITOR_FN_START) {

        event_args->uctx.f = timestamp;

        // get address info
        ABT_mutex_spinlock(
            ABT_MUTEX_MEMORY_GET_HANDLE(&monitor->addr_info_mtx));
        addr_info_t* addr_info
            = addr_info_find_or_add(monitor, mid, handle_info->addr);
        ABT_mutex_unlock(ABT_MUTEX_MEMORY_GET_HANDLE(&monitor->addr_info_mtx));

        // attach statistics to session
        hg_id_t id = handle_info->id;
        id         = mux_id(id, event_args->provider_id);
        callpath_t key
            = {.rpc_id = id, .parent_id = 0, .addr_id = addr_info->id};
        // try to get parent RPC id from context
        margo_get_current_rpc_id(mid, &key.parent_id);

        ABT_mutex_spinlock(
            ABT_MUTEX_MEMORY_GET_HANDLE(&monitor->origin_rpc_stats_mtx));
        HASH_FIND(hh, monitor->origin_rpc_stats, &key, sizeof(key), rpc_stats);
        if (!rpc_stats) {
            rpc_stats = (origin_rpc_statistics_t*)calloc(1, sizeof(*rpc_stats));
            rpc_stats->callpath = key;
            HASH_ADD(hh, monitor->origin_rpc_stats, callpath, sizeof(key),
                     rpc_stats);
        }
        ABT_mutex_unlock(
            ABT_MUTEX_MEMORY_GET_HANDLE(&monitor->origin_rpc_stats_mtx));
        session->origin.stats = rpc_stats;

        double t = timestamp - session->origin.create_ts;
        UPDATE_STATISTICS_WITH(rpc_stats->forward[TIMESTAMP], t);
        session->origin.forward_start_ts = timestamp;

    } else if (event_type == MARGO_MONITOR_FN_END) {

        // update statistics
        rpc_stats = session->origin.stats;
        double t  = timestamp - event_args->uctx.f;
        UPDATE_STATISTICS_WITH(rpc_stats->forward[DURATION], t);
        session->origin.forward_end_ts = timestamp;
    }
}

static void
__margo_default_monitor_on_set_input(void*                          uargs,
                                     double                         timestamp,
                                     margo_monitor_event_t          event_type,
                                     margo_monitor_set_input_args_t event_args)
{
    default_monitor_state_t* monitor = (default_monitor_state_t*)uargs;
    if (!monitor->enable_statistics) return;
    // retrieve the session that was create on on_create
    hg_handle_t handle = margo_request_get_handle(event_args->request);
    RETRIEVE_SESSION(handle);
    origin_rpc_statistics_t* rpc_stats = session->origin.stats;

    if (event_type == MARGO_MONITOR_FN_START) {
        event_args->uctx.f = timestamp;
        double t           = timestamp - session->origin.forward_start_ts;
        UPDATE_STATISTICS_WITH(rpc_stats->set_input[TIMESTAMP], t);
    } else {
        double t = timestamp - event_args->uctx.f;
        UPDATE_STATISTICS_WITH(rpc_stats->set_input[DURATION], t);
    }
}

static void __margo_default_monitor_on_set_output(
    void*                           uargs,
    double                          timestamp,
    margo_monitor_event_t           event_type,
    margo_monitor_set_output_args_t event_args)
{
    default_monitor_state_t* monitor = (default_monitor_state_t*)uargs;
    if (!monitor->enable_statistics) return;
    // retrieve the session that was create on on_create
    hg_handle_t handle = margo_request_get_handle(event_args->request);
    RETRIEVE_SESSION(handle);
    target_rpc_statistics_t* rpc_stats = session->target.stats;

    if (event_type == MARGO_MONITOR_FN_START) {
        event_args->uctx.f = timestamp;
        double t           = timestamp - session->target.respond_start_ts;
        UPDATE_STATISTICS_WITH(rpc_stats->set_output[TIMESTAMP], t);
    } else {
        double t = timestamp - event_args->uctx.f;
        UPDATE_STATISTICS_WITH(rpc_stats->set_output[DURATION], t);
    }
}

static void __margo_default_monitor_on_get_output(
    void*                           uargs,
    double                          timestamp,
    margo_monitor_event_t           event_type,
    margo_monitor_get_output_args_t event_args)
{
    default_monitor_state_t* monitor = (default_monitor_state_t*)uargs;
    if (!monitor->enable_statistics) return;
    // retrieve the session that was create on on_create
    RETRIEVE_SESSION(event_args->handle);
    origin_rpc_statistics_t* rpc_stats = session->origin.stats;

    if (event_type == MARGO_MONITOR_FN_START) {
        event_args->uctx.f = timestamp;
        double t           = timestamp - session->origin.wait_end_ts;
        UPDATE_STATISTICS_WITH(rpc_stats->get_output[TIMESTAMP], t);
    } else {
        double t = timestamp - event_args->uctx.f;
        UPDATE_STATISTICS_WITH(rpc_stats->get_output[DURATION], t);
    }
}

static void
__margo_default_monitor_on_get_input(void*                          uargs,
                                     double                         timestamp,
                                     margo_monitor_event_t          event_type,
                                     margo_monitor_get_input_args_t event_args)
{
    default_monitor_state_t* monitor = (default_monitor_state_t*)uargs;
    if (!monitor->enable_statistics) return;
    // retrieve the session that was create on on_create
    RETRIEVE_SESSION(event_args->handle);
    target_rpc_statistics_t* rpc_stats = session->target.stats;

    if (event_type == MARGO_MONITOR_FN_START) {
        event_args->uctx.f = timestamp;
        double t           = timestamp - session->target.ult_start_ts;
        UPDATE_STATISTICS_WITH(rpc_stats->get_input[TIMESTAMP], t);
    } else {
        double t = timestamp - event_args->uctx.f;
        UPDATE_STATISTICS_WITH(rpc_stats->get_input[DURATION], t);
    }
}

static void __margo_default_monitor_on_forward_cb(
    void*                           uargs,
    double                          timestamp,
    margo_monitor_event_t           event_type,
    margo_monitor_forward_cb_args_t event_args)
{
    default_monitor_state_t* monitor = (default_monitor_state_t*)uargs;
    if (!monitor->enable_statistics) return;
    // retrieve the session that was create on on_create
    hg_handle_t handle = margo_request_get_handle(event_args->request);
    RETRIEVE_SESSION(handle);
    origin_rpc_statistics_t* rpc_stats = session->origin.stats;

    if (event_type == MARGO_MONITOR_FN_START) {
        event_args->uctx.f = timestamp;
        double t           = timestamp - session->origin.forward_start_ts;
        UPDATE_STATISTICS_WITH(rpc_stats->forward_cb[TIMESTAMP], t);
    } else {
        double t = timestamp - event_args->uctx.f;
        UPDATE_STATISTICS_WITH(rpc_stats->forward_cb[DURATION], t);
    }
}

static void
__margo_default_monitor_on_respond(void*                        uargs,
                                   double                       timestamp,
                                   margo_monitor_event_t        event_type,
                                   margo_monitor_respond_args_t event_args)
{
    default_monitor_state_t* monitor = (default_monitor_state_t*)uargs;
    if (!monitor->enable_statistics) return;
    // retrieve the session that was create on on_create
    hg_handle_t handle = margo_request_get_handle(event_args->request);
    RETRIEVE_SESSION(handle);
    target_rpc_statistics_t* rpc_stats = session->target.stats;

    if (event_type == MARGO_MONITOR_FN_START) {
        event_args->uctx.f = timestamp;
        double t           = timestamp - session->target.ult_start_ts;
        UPDATE_STATISTICS_WITH(rpc_stats->respond[TIMESTAMP], t);
        session->target.respond_start_ts = timestamp;
    } else {
        double t = timestamp - event_args->uctx.f;
        UPDATE_STATISTICS_WITH(rpc_stats->respond[DURATION], t);
        session->target.respond_end_ts = timestamp;
    }
}

static void __margo_default_monitor_on_respond_cb(
    void*                           uargs,
    double                          timestamp,
    margo_monitor_event_t           event_type,
    margo_monitor_respond_cb_args_t event_args)
{
    default_monitor_state_t* monitor = (default_monitor_state_t*)uargs;
    if (!monitor->enable_statistics) return;
    // retrieve the session that was create on on_create
    hg_handle_t handle = margo_request_get_handle(event_args->request);
    RETRIEVE_SESSION(handle);
    target_rpc_statistics_t* rpc_stats = session->target.stats;

    if (event_type == MARGO_MONITOR_FN_START) {
        event_args->uctx.f = timestamp;
        double t           = timestamp - session->target.respond_start_ts;
        UPDATE_STATISTICS_WITH(rpc_stats->respond_cb[TIMESTAMP], t);
    } else {
        double t = timestamp - event_args->uctx.f;
        UPDATE_STATISTICS_WITH(rpc_stats->respond_cb[DURATION], t);
    }
}

static void
__margo_default_monitor_on_wait(void*                     uargs,
                                double                    timestamp,
                                margo_monitor_event_t     event_type,
                                margo_monitor_wait_args_t event_args)
{
    default_monitor_state_t* monitor = (default_monitor_state_t*)uargs;
    if (!monitor->enable_statistics) return;
    // retrieve the session that was create on on_create
    statistics_t*   duration_stats  = NULL;
    statistics_t*   timestamp_stats = NULL;
    bulk_session_t* bulk_session    = NULL;
    double          ref_ts          = 0.0;

    margo_request_type request_type
        = margo_request_get_type(event_args->request);
    if (request_type == MARGO_FORWARD_REQUEST) {
        hg_handle_t handle = margo_request_get_handle(event_args->request);
        RETRIEVE_SESSION(handle);
        ref_ts          = session->origin.forward_end_ts;
        duration_stats  = &(session->origin.stats->wait[DURATION]);
        timestamp_stats = &(session->origin.stats->wait[TIMESTAMP]);
        if (event_type == MARGO_MONITOR_FN_END)
            session->origin.wait_end_ts = timestamp;
    } else if (request_type == MARGO_RESPONSE_REQUEST) {
        hg_handle_t handle = margo_request_get_handle(event_args->request);
        RETRIEVE_SESSION(handle);
        ref_ts          = session->target.respond_end_ts;
        duration_stats  = &(session->target.stats->wait[DURATION]);
        timestamp_stats = &(session->target.stats->wait[TIMESTAMP]);
    } else if (request_type == MARGO_BULK_REQUEST) {
        RETRIEVE_BULK_SESSION(event_args->request);
        ref_ts          = session->transfer_end_ts;
        duration_stats  = &(session->stats->wait[DURATION]);
        timestamp_stats = &(session->stats->wait[TIMESTAMP]);
        bulk_session    = session;
    }

    if (event_type == MARGO_MONITOR_FN_START) {
        event_args->uctx.f = timestamp;
        double t           = timestamp - ref_ts;
        if (timestamp_stats) UPDATE_STATISTICS_WITH(*timestamp_stats, t);
    } else {
        double t = timestamp - event_args->uctx.f;
        if (duration_stats) UPDATE_STATISTICS_WITH(*duration_stats, t);
    }

    if ((event_type == MARGO_MONITOR_FN_END) && bulk_session) {
        release_bulk_session(monitor, bulk_session);
    }
}

static void __margo_default_monitor_on_rpc_handler(
    void*                            uargs,
    double                           timestamp,
    margo_monitor_event_t            event_type,
    margo_monitor_rpc_handler_args_t event_args)
{
    default_monitor_state_t* monitor = (default_monitor_state_t*)uargs;
    if (!(monitor->enable_statistics || monitor->enable_time_series)) return;

    margo_instance_id mid = margo_hg_handle_get_instance(event_args->handle);
    const struct hg_info* handle_info = margo_get_info(event_args->handle);
    if (!handle_info) return;
    RETRIEVE_SESSION(event_args->handle);
    target_rpc_statistics_t* rpc_stats = NULL;

    if (event_type == MARGO_MONITOR_FN_START) {
        // get address info
        ABT_mutex_spinlock(
            ABT_MUTEX_MEMORY_GET_HANDLE(&monitor->addr_info_mtx));
        addr_info_t* addr_info
            = addr_info_find_or_add(monitor, mid, handle_info->addr);
        ABT_mutex_unlock(ABT_MUTEX_MEMORY_GET_HANDLE(&monitor->addr_info_mtx));

        // form callpath key
        hg_id_t    id  = margo_get_info(event_args->handle)->id;
        callpath_t key = {.rpc_id    = id,
                          .parent_id = event_args->parent_rpc_id,
                          .addr_id   = addr_info->id};

        /* statistics */
        if (monitor->enable_statistics) {
            if (!session) { session = new_session(monitor); }
            session->target.handler_start_ts  = timestamp;
            margo_monitor_data_t monitor_data = {.p = (void*)session};
            margo_set_monitoring_data(event_args->handle, monitor_data);

            ABT_mutex_spinlock(
                ABT_MUTEX_MEMORY_GET_HANDLE(&monitor->target_rpc_stats_mtx));
            HASH_FIND(hh, monitor->target_rpc_stats, &key, sizeof(key),
                      rpc_stats);
            if (!rpc_stats) {
                rpc_stats
                    = (target_rpc_statistics_t*)calloc(1, sizeof(*rpc_stats));
                rpc_stats->callpath = key;
                HASH_ADD(hh, monitor->target_rpc_stats, callpath, sizeof(key),
                         rpc_stats);
            }
            ABT_mutex_unlock(
                ABT_MUTEX_MEMORY_GET_HANDLE(&monitor->target_rpc_stats_mtx));
            session->target.stats = rpc_stats;
            event_args->uctx.f    = timestamp;

#ifdef __clang_analyzer__
            // Disable static analyzer warning about leaking memory.
            // This is code is not actually executed.
            free(session);
#endif
        }

        /* time series */
        if (monitor->enable_time_series) {
            // find the time series associated with
            // this callpath and increment its count
            ABT_mutex_spinlock(
                ABT_MUTEX_MEMORY_GET_HANDLE(&monitor->rpc_time_series_mtx));
            rpc_time_series_t* rpc_ts
                = find_or_add_time_series_for_rpc(monitor, id);
            rpc_ts->rpc_count += 1;
            ABT_mutex_unlock(
                ABT_MUTEX_MEMORY_GET_HANDLE(&monitor->rpc_time_series_mtx));
        }

    } else {

        if (monitor->enable_statistics) {
            // update statistics
            rpc_stats = session->target.stats;
            double t  = timestamp - event_args->uctx.f;
            UPDATE_STATISTICS_WITH(rpc_stats->handler, t);
        }
    }
}

static void
__margo_default_monitor_on_rpc_ult(void*                        uargs,
                                   double                       timestamp,
                                   margo_monitor_event_t        event_type,
                                   margo_monitor_rpc_ult_args_t event_args)
{
    default_monitor_state_t* monitor = (default_monitor_state_t*)uargs;
    if (!monitor->enable_statistics) return;
    // retrieve the session that was create on on_create
    RETRIEVE_SESSION(event_args->handle);
    target_rpc_statistics_t* rpc_stats = session->target.stats;

    if (event_type == MARGO_MONITOR_FN_START) {

        event_args->uctx.f = timestamp;
        double t           = timestamp - session->target.handler_start_ts;
        UPDATE_STATISTICS_WITH(rpc_stats->ult[TIMESTAMP], t);
        // set callpath key
        callpath_t* current_callpath = &(session->target.stats->callpath);
        ABT_key_set(monitor->callpath_key, current_callpath);
        // set the reference time start_ts to the current timestamp
        session->target.ult_start_ts = timestamp;

    } else {
        double t = timestamp - event_args->uctx.f;
        UPDATE_STATISTICS_WITH(rpc_stats->ult[DURATION], t);
    }
}

static void
__margo_default_monitor_on_destroy(void*                        uargs,
                                   double                       timestamp,
                                   margo_monitor_event_t        event_type,
                                   margo_monitor_destroy_args_t event_args)
{
    (void)timestamp;
    default_monitor_state_t* monitor = (default_monitor_state_t*)uargs;
    if (!monitor->enable_statistics) return;
    if (event_type == MARGO_MONITOR_FN_END) {
        // WARNING: handle is no longer valid after destroy
        return;
    }
    // MARGO_MONITOR_FN_START
    margo_monitor_data_t monitor_data;
    margo_get_monitoring_data(event_args->handle, &monitor_data);
    release_session(monitor, (session_t*)monitor_data.p);
}

#define __MONITOR_FN(__event__)                                          \
    static void __margo_default_monitor_on_##__event__(                  \
        void* uargs, double timestamp, margo_monitor_event_t event_type, \
        margo_monitor_##__event__##_args_t event_args)

static void __margo_default_monitor_on_bulk_create(
    void*                            uargs,
    double                           timestamp,
    margo_monitor_event_t            event_type,
    margo_monitor_bulk_create_args_t event_args)
{
    default_monitor_state_t* monitor = (default_monitor_state_t*)uargs;
    if (!monitor->enable_statistics) return;

    bulk_create_statistics_t* bulk_stats = NULL;

    // no bulk_statistics_t attached to current ULT
    callpath_t default_key = {0};
    default_key.parent_id  = mux_id(0, MARGO_DEFAULT_PROVIDER_ID);
    default_key.rpc_id     = mux_id(0, MARGO_DEFAULT_PROVIDER_ID);
    callpath_t* pkey       = NULL;
    // try to get current callpath from the installed ABT_key
    ABT_key_get(monitor->callpath_key, (void**)&pkey);
    if (!pkey) pkey = (callpath_t*)&default_key;

    ABT_mutex_spinlock(
        ABT_MUTEX_MEMORY_GET_HANDLE(&monitor->bulk_create_stats_mtx));
    HASH_FIND(hh, monitor->bulk_create_stats, pkey, sizeof(*pkey), bulk_stats);
    if (!bulk_stats) {
        bulk_stats = (bulk_create_statistics_t*)calloc(1, sizeof(*bulk_stats));
        bulk_stats->callpath = *pkey;
        HASH_ADD(hh, monitor->bulk_create_stats, callpath, sizeof(*pkey),
                 bulk_stats);
    }
    ABT_mutex_unlock(
        ABT_MUTEX_MEMORY_GET_HANDLE(&monitor->bulk_create_stats_mtx));

    if (event_type == MARGO_MONITOR_FN_START) {
        event_args->uctx.f = timestamp;
    } else {
        double t = timestamp - event_args->uctx.f;
        // compute total size
        size_t size = 0;
        for (unsigned i = 0; i < event_args->count; i++) {
            size += event_args->sizes[i];
        }
        UPDATE_STATISTICS_WITH(bulk_stats->duration, t);
        UPDATE_STATISTICS_WITH(bulk_stats->size, (double)size);
    }
}

static void __margo_default_monitor_on_bulk_transfer(
    void*                              uargs,
    double                             timestamp,
    margo_monitor_event_t              event_type,
    margo_monitor_bulk_transfer_args_t event_args)
{
    default_monitor_state_t* monitor = (default_monitor_state_t*)uargs;
    if (!(monitor->enable_statistics || monitor->enable_time_series)) return;

    bulk_transfer_statistics_t* bulk_stats = NULL;
    margo_instance_id mid = margo_request_get_instance(event_args->request);

    if (event_type == MARGO_MONITOR_FN_START) {

        // get address info
        ABT_mutex_spinlock(
            ABT_MUTEX_MEMORY_GET_HANDLE(&monitor->addr_info_mtx));
        addr_info_t* addr_info
            = addr_info_find_or_add(monitor, mid, event_args->origin_addr);
        ABT_mutex_unlock(ABT_MUTEX_MEMORY_GET_HANDLE(&monitor->addr_info_mtx));

        // create the bulk_key for the bulk_transfer_statistics in the hash
        // table
        bulk_key_t  bulk_key     = {.operation      = event_args->op,
                               .remote_addr_id = addr_info ? addr_info->id : 0};
        callpath_t* callpath_ptr = NULL;
        // try to get current callpath from the installed ABT_key
        ABT_key_get(monitor->callpath_key, (void**)&callpath_ptr);
        if (callpath_ptr) {
            memcpy(&bulk_key.callpath, callpath_ptr, sizeof(*callpath_ptr));
        } else {
            bulk_key.callpath.parent_id = mux_id(0, MARGO_DEFAULT_PROVIDER_ID);
            bulk_key.callpath.rpc_id    = mux_id(0, MARGO_DEFAULT_PROVIDER_ID);
        }

        /* statistics */
        if (monitor->enable_statistics) {
            ABT_mutex_spinlock(
                ABT_MUTEX_MEMORY_GET_HANDLE(&monitor->bulk_transfer_stats_mtx));
            HASH_FIND(hh, monitor->bulk_transfer_stats, &bulk_key,
                      sizeof(bulk_key), bulk_stats);
            if (!bulk_stats) {
                bulk_stats = (bulk_transfer_statistics_t*)calloc(
                    1, sizeof(*bulk_stats));
                bulk_stats->bulk_key = bulk_key;
                HASH_ADD(hh, monitor->bulk_transfer_stats, bulk_key,
                         sizeof(bulk_key), bulk_stats);
            }
            ABT_mutex_unlock(
                ABT_MUTEX_MEMORY_GET_HANDLE(&monitor->bulk_transfer_stats_mtx));

            event_args->uctx.f = timestamp;

            bulk_session_t* session           = new_bulk_session(monitor);
            session->transfer_start_ts        = event_args->uctx.f;
            session->stats                    = bulk_stats;
            margo_monitor_data_t monitor_data = {.p = (void*)session};
            margo_request_set_monitoring_data(event_args->request,
                                              monitor_data);
#ifdef __clang_analyzer__
            // Disable static analyzer warning about leaking memory.
            // This is code is not actually executed.
            free(session);
#endif
        }

        /* time series */
        if (monitor->enable_time_series) {
            // find the time series associated with
            // this callpath and increment its count
            ABT_mutex_spinlock(
                ABT_MUTEX_MEMORY_GET_HANDLE(&monitor->rpc_time_series_mtx));
            rpc_time_series_t* rpc_ts = find_or_add_time_series_for_rpc(
                monitor, bulk_key.callpath.rpc_id);
            rpc_ts->bulk_size += event_args->size;
            ABT_mutex_unlock(
                ABT_MUTEX_MEMORY_GET_HANDLE(&monitor->rpc_time_series_mtx));
        }

    } else {

        if (monitor->enable_statistics) {
            margo_monitor_data_t monitor_data;
            margo_request_get_monitoring_data(event_args->request,
                                              &monitor_data);
            bulk_session_t* session = (bulk_session_t*)monitor_data.p;
            bulk_stats              = session->stats;
            double t                = timestamp - event_args->uctx.f;
            UPDATE_STATISTICS_WITH(bulk_stats->transfer, t);
            UPDATE_STATISTICS_WITH(bulk_stats->transfer_size, event_args->size);
            session->transfer_end_ts = timestamp;
        }
    }
}

static void __margo_default_monitor_on_bulk_transfer_cb(
    void*                                 uargs,
    double                                timestamp,
    margo_monitor_event_t                 event_type,
    margo_monitor_bulk_transfer_cb_args_t event_args)
{
    default_monitor_state_t* monitor = (default_monitor_state_t*)uargs;
    if (!monitor->enable_statistics) return;
    // retrieve the session that was create on on_create
    RETRIEVE_BULK_SESSION(event_args->request);
    bulk_transfer_statistics_t* bulk_stats = session->stats;

    if (event_type == MARGO_MONITOR_FN_START) {

        event_args->uctx.f = timestamp;
        double t           = timestamp - session->transfer_start_ts;
        UPDATE_STATISTICS_WITH(bulk_stats->transfer_cb[TIMESTAMP], t);

    } else {
        double t = timestamp - event_args->uctx.f;
        UPDATE_STATISTICS_WITH(bulk_stats->transfer_cb[DURATION], t);
    }
}

static void
__margo_default_monitor_on_add_pool(void*                         uargs,
                                    double                        timestamp,
                                    margo_monitor_event_t         event_type,
                                    margo_monitor_add_pool_args_t event_args)
{
    default_monitor_state_t* monitor = (default_monitor_state_t*)uargs;
    if (!monitor->enable_time_series) return;
    if (event_type != MARGO_MONITOR_FN_START) return;

    ABT_mutex_spinlock(
        ABT_MUTEX_MEMORY_GET_HANDLE(&monitor->pool_time_series_mtx));
    // current_num_pools = number of pools before adding
    // since event_type == MARGO_MONITOR_FN_START
    size_t current_num_pools       = monitor->mid->abt.pools_len;
    monitor->pool_size_time_series = realloc(
        monitor->pool_size_time_series,
        (current_num_pools + 1) * sizeof(*monitor->pool_size_time_series));
    monitor->pool_total_size_time_series
        = realloc(monitor->pool_total_size_time_series,
                  (current_num_pools + 1)
                      * sizeof(*monitor->pool_total_size_time_series));
    memset(&monitor->pool_size_time_series[current_num_pools], 0,
           sizeof(monitor->pool_size_time_series[current_num_pools]));
    monitor->pool_size_time_series[current_num_pools].frame_capacity
        = monitor->pool_size_time_series[current_num_pools - 1].frame_capacity;
    memset(&monitor->pool_total_size_time_series[current_num_pools], 0,
           sizeof(monitor->pool_total_size_time_series[current_num_pools]));
    monitor->pool_total_size_time_series[current_num_pools].frame_capacity
        = monitor->pool_total_size_time_series[current_num_pools - 1]
              .frame_capacity;
    ABT_mutex_unlock(
        ABT_MUTEX_MEMORY_GET_HANDLE(&monitor->pool_time_series_mtx));
}

static void __margo_default_monitor_on_remove_pool(
    void*                            uargs,
    double                           timestamp,
    margo_monitor_event_t            event_type,
    margo_monitor_remove_pool_args_t event_args)
{
    // TODO we might want to keep somewhere the time series of the pool we have
    // removed, otherwise its data will be unavailable when dumping the
    // statistics
    if (event_type != MARGO_MONITOR_FN_END || event_args->ret != HG_SUCCESS)
        return;

    default_monitor_state_t* monitor = (default_monitor_state_t*)uargs;
    if (!monitor->enable_time_series) return;

    ABT_mutex_spinlock(
        ABT_MUTEX_MEMORY_GET_HANDLE(&monitor->pool_time_series_mtx));
    // current_num_pools = number of pools after removing
    // since event_type == MARGO_MONITOR_FN_END
    size_t   current_num_pools = monitor->mid->abt.pools_len;
    uint32_t index             = event_args->info->index;
    time_series_clear(&monitor->pool_size_time_series[index]);
    time_series_clear(&monitor->pool_total_size_time_series[index]);
    memmove(&monitor->pool_size_time_series[index],
            &monitor->pool_size_time_series[index + 1],
            (current_num_pools - index)
                * sizeof(monitor->pool_size_time_series[index]));
    memmove(&monitor->pool_total_size_time_series[index],
            &monitor->pool_total_size_time_series[index + 1],
            (current_num_pools - index)
                * sizeof(monitor->pool_total_size_time_series[index]));
    ABT_mutex_unlock(
        ABT_MUTEX_MEMORY_GET_HANDLE(&monitor->pool_time_series_mtx));
}

static void __margo_default_monitor_on_add_xstream(
    void*                            uargs,
    double                           timestamp,
    margo_monitor_event_t            event_type,
    margo_monitor_add_xstream_args_t event_args)
{
    (void)uargs;
    (void)timestamp;
    (void)event_type;
    (void)event_args;
}

static void __margo_default_monitor_on_remove_xstream(
    void*                               uargs,
    double                              timestamp,
    margo_monitor_event_t               event_type,
    margo_monitor_remove_xstream_args_t event_args)
{
    (void)uargs;
    (void)timestamp;
    (void)event_type;
    (void)event_args;
}

#define __MONITOR_FN_EMPTY(__name__) \
    __MONITOR_FN(__name__)           \
    {                                \
        (void)uargs;                 \
        (void)timestamp;             \
        (void)event_args;            \
        (void)event_type;            \
    }

__MONITOR_FN_EMPTY(bulk_free)
__MONITOR_FN_EMPTY(deregister)
__MONITOR_FN_EMPTY(lookup)
__MONITOR_FN_EMPTY(sleep)
__MONITOR_FN_EMPTY(free_input)
__MONITOR_FN_EMPTY(free_output)
__MONITOR_FN_EMPTY(prefinalize)
__MONITOR_FN_EMPTY(finalize)
__MONITOR_FN_EMPTY(user)

static hg_return_t __margo_default_monitor_dump(void*                 uargs,
                                                margo_monitor_dump_fn dump_fn,
                                                void*                 dump_args,
                                                bool                  reset)
{
    const default_monitor_state_t* monitor
        = (const default_monitor_state_t*)uargs;

    /* get printing precision */
    char double_format[] = "%.Xf";
    double_format[2]     = (char)(48 + monitor->precision);
    json_c_set_serialization_double_format(double_format, JSON_C_OPTION_GLOBAL);

    struct json_object* dump = json_object_new_object();

    /* write statistics to json */
    if (monitor->enable_statistics) {
        /* create JSON statistics */
        struct json_object* stats = monitor_statistics_to_json(monitor, reset);
        json_object_object_add(dump, "stats", stats);
    }

    /* write time series to json */
    if (monitor->enable_time_series) {
        /* create JSON statistics */
        struct json_object* series
            = monitor_time_series_to_json(monitor, reset);
        json_object_object_add(dump, "series", series);
    }

    if (dump_fn) {
        /* convert to string and call the dump function */
        size_t      json_len = 0;
        const char* json_str = json_object_to_json_string_length(
            dump, monitor->stats_pretty_json | JSON_C_TO_STRING_NOSLASHESCAPE,
            &json_len);
        dump_fn(dump_args, json_str, json_len);
    }
    json_object_put(dump);
    return HG_SUCCESS;
}

struct margo_monitor __margo_default_monitor
    = {.uargs      = NULL,
       .initialize = __margo_default_monitor_initialize,
       .finalize   = __margo_default_monitor_finalize,
       .dump       = __margo_default_monitor_dump,
       .name       = __margo_default_monitor_name,
       .config     = __margo_default_monitor_config,
#define X(__x__, __y__) .on_##__y__ = __margo_default_monitor_on_##__y__,
       MARGO_EXPAND_MONITOR_MACROS
#undef X
};

struct margo_monitor* margo_default_monitor = &__margo_default_monitor;

/* ========================================================================
 * Functions related to dumping the monitor's state into a JSON file
 * ======================================================================== */

static void
write_monitor_state_to_json_file(const default_monitor_state_t* monitor,
                                 bool                           reset)
{
    if ((!monitor->filename_prefix) || (strlen(monitor->filename_prefix) == 0))
        return;
    /* get hostname */
    char hostname[1024];
    hostname[1023] = '\0';
    gethostname(hostname, 1023);
    /* get pid */
    pid_t pid = getpid();
    /* get printing precision */
    char double_format[] = "%.Xf";
    double_format[2]     = (char)(48 + monitor->precision);
    json_c_set_serialization_double_format(double_format, JSON_C_OPTION_GLOBAL);

    /* write statistics file */
    if (monitor->enable_statistics) {
        /* compute size needed for the full file name */
        size_t stats_filename_size
            = snprintf(NULL, 0, "%s.%s.%d.stats.json", monitor->filename_prefix,
                       hostname, pid);
        /* create full file name */
        char* stats_filename = calloc(1, stats_filename_size + 1);
        sprintf(stats_filename, "%s.%s.%d.stats.json", monitor->filename_prefix,
                hostname, pid);
        /* open the file */
        int   errnum;
        FILE* file = fopen(stats_filename, "w");
        if (!file) {
            errnum = errno;
            margo_error(monitor->mid, "Error open file %s: %s", stats_filename,
                        strerror(errnum));
            goto finish_stats_file;
        }
        /* create JSON statistics */
        struct json_object* json = monitor_statistics_to_json(monitor, reset);

        /* write statistics */
        size_t      json_len = 0;
        const char* json_str = json_object_to_json_string_length(
            json, monitor->stats_pretty_json | JSON_C_TO_STRING_NOSLASHESCAPE,
            &json_len);
        fwrite(json_str, json_len, 1, file);
        json_object_put(json);
        /* finish */
finish_stats_file:
        free(stats_filename);
        if (file) fclose(file);
    }

    /* write time series file */
    if (monitor->enable_time_series) {
        /* compute size needed for the full file name */
        size_t series_filename_size
            = snprintf(NULL, 0, "%s.%s.%d.series.json",
                       monitor->filename_prefix, hostname, pid);
        /* create full file name */
        char* series_filename = calloc(1, series_filename_size + 1);
        sprintf(series_filename, "%s.%s.%d.series.json",
                monitor->filename_prefix, hostname, pid);
        /* open the file */
        int   errnum;
        FILE* file = fopen(series_filename, "w");
        if (!file) {
            errnum = errno;
            margo_error(monitor->mid, "Error open file %s: %s", series_filename,
                        strerror(errnum));
            goto finish_series_file;
        }
        /* create JSON statistics */
        struct json_object* json = monitor_time_series_to_json(monitor, reset);

        /* write statistics */
        size_t      json_len = 0;
        const char* json_str = json_object_to_json_string_length(
            json,
            monitor->time_series_pretty_json | JSON_C_TO_STRING_NOSLASHESCAPE,
            &json_len);
        fwrite(json_str, json_len, 1, file);
        json_object_put(json);
        /* finish */
finish_series_file:
        free(series_filename);
        if (file) fclose(file);
    }
}

/* ========================================================================
 * Functions related to converting statistics into a json_object tree
 * ======================================================================== */

static struct json_object* statistics_to_json(const statistics_t* stats,
                                              bool                reset)
{
    struct json_object* json = json_object_new_object();
    ABT_mutex_spinlock(ABT_MUTEX_MEMORY_GET_HANDLE(&stats->mutex));
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
    if (reset) {
        ((statistics_t*)stats)->num = 0;
        ((statistics_t*)stats)->min = 0;
        ((statistics_t*)stats)->max = 0;
        ((statistics_t*)stats)->avg = 0;
        ((statistics_t*)stats)->var = 0;
        ((statistics_t*)stats)->sum = 0;
    }
    ABT_mutex_unlock(ABT_MUTEX_MEMORY_GET_HANDLE(&stats->mutex));
    return json;
}

static struct json_object* statistics_pair_to_json(const statistics_t* stats,
                                                   const char*         name1,
                                                   const char*         name2,
                                                   bool                reset)
{
    struct json_object* json = json_object_new_object();
    json_object_object_add_ex(json, name1, statistics_to_json(stats, reset),
                              JSON_C_OBJECT_ADD_KEY_IS_NEW);
    json_object_object_add_ex(json, name2, statistics_to_json(stats + 1, reset),
                              JSON_C_OBJECT_ADD_KEY_IS_NEW);
    return json;
}

static struct json_object* hg_statistics_to_json(const hg_statistics_t* stats,
                                                 bool                   reset)
{
    struct json_object* json = json_object_new_object();
    json_object_object_add_ex(
        json, "progress_with_timeout",
        statistics_to_json(&stats->progress_with_timeout, reset),
        JSON_C_OBJECT_ADD_KEY_IS_NEW);
    json_object_object_add_ex(
        json, "progress_timeout_value_msec",
        statistics_to_json(&stats->progress_timeout_value, reset),
        JSON_C_OBJECT_ADD_KEY_IS_NEW);
    json_object_object_add_ex(
        json, "progress_without_timeout",
        statistics_to_json(&stats->progress_without_timeout, reset),
        JSON_C_OBJECT_ADD_KEY_IS_NEW);
    json_object_object_add_ex(json, "trigger",
                              statistics_to_json(&stats->trigger, reset),
                              JSON_C_OBJECT_ADD_KEY_IS_NEW);
    return json;
}

static struct json_object*
bulk_create_statistics_to_json(const bulk_create_statistics_t* stats,
                               bool                            reset)
{
    struct json_object* json = json_object_new_object();
    json_object_object_add_ex(json, "duration",
                              statistics_to_json(&stats->duration, reset),
                              JSON_C_OBJECT_ADD_KEY_IS_NEW);
    json_object_object_add_ex(json, "size",
                              statistics_to_json(&stats->size, reset),
                              JSON_C_OBJECT_ADD_KEY_IS_NEW);
    return json;
}

static struct json_object*
bulk_transfer_statistics_to_json(const bulk_transfer_statistics_t* stats,
                                 bool                              reset)
{
    struct json_object* json     = json_object_new_object();
    struct json_object* transfer = json_object_new_object();
    json_object_object_add_ex(json, "itransfer", transfer,
                              JSON_C_OBJECT_ADD_KEY_IS_NEW);
    json_object_object_add_ex(transfer, "duration",
                              statistics_to_json(&stats->transfer, reset),
                              JSON_C_OBJECT_ADD_KEY_IS_NEW);
    json_object_object_add_ex(transfer, "size",
                              statistics_to_json(&stats->transfer_size, reset),
                              JSON_C_OBJECT_ADD_KEY_IS_NEW);
    json_object_object_add_ex(
        json, "transfer_cb",
        statistics_pair_to_json(stats->transfer_cb, "duration",
                                "relative_timestamp_from_itransfer_start",
                                reset),
        JSON_C_OBJECT_ADD_KEY_IS_NEW);
    json_object_object_add_ex(
        json, "itransfer_wait",
        statistics_pair_to_json(stats->wait, "duration",
                                "relative_timestamp_from_itransfer_end", reset),
        JSON_C_OBJECT_ADD_KEY_IS_NEW);
    return json;
}

static struct json_object*
origin_rpc_statistics_to_json(const origin_rpc_statistics_t* stats, bool reset)
{
    struct json_object* json = json_object_new_object();
    json_object_object_add_ex(
        json, "iforward",
        statistics_pair_to_json(stats->forward, "duration",
                                "relative_timestamp_from_create", reset),
        JSON_C_OBJECT_ADD_KEY_IS_NEW);
    json_object_object_add_ex(
        json, "forward_cb",
        statistics_pair_to_json(stats->forward_cb, "duration",
                                "relative_timestamp_from_iforward_start",
                                reset),
        JSON_C_OBJECT_ADD_KEY_IS_NEW);
    json_object_object_add_ex(
        json, "iforward_wait",
        statistics_pair_to_json(stats->wait, "duration",
                                "relative_timestamp_from_iforward_end", reset),
        JSON_C_OBJECT_ADD_KEY_IS_NEW);
    json_object_object_add_ex(
        json, "set_input",
        statistics_pair_to_json(stats->set_input, "duration",
                                "relative_timestamp_from_iforward_start",
                                reset),
        JSON_C_OBJECT_ADD_KEY_IS_NEW);
    json_object_object_add_ex(
        json, "get_output",
        statistics_pair_to_json(stats->get_output, "duration",
                                "relative_timestamp_from_wait_end", reset),
        JSON_C_OBJECT_ADD_KEY_IS_NEW);
    return json;
}

static struct json_object*
target_rpc_statistics_to_json(const target_rpc_statistics_t* stats, bool reset)
{
    struct json_object* json = json_object_new_object();

    struct json_object* handler = json_object_new_object();
    json_object_object_add_ex(json, "handler", handler,
                              JSON_C_OBJECT_ADD_KEY_IS_NEW);
    json_object_object_add_ex(handler, "duration",
                              statistics_to_json(&stats->handler, reset),
                              JSON_C_OBJECT_ADD_KEY_IS_NEW);

    json_object_object_add_ex(
        json, "ult",
        statistics_pair_to_json(stats->ult, "duration",
                                "relative_timestamp_from_handler_start", reset),
        JSON_C_OBJECT_ADD_KEY_IS_NEW);
    json_object_object_add_ex(
        json, "irespond",
        statistics_pair_to_json(stats->respond, "duration",
                                "relative_timestamp_from_ult_start", reset),
        JSON_C_OBJECT_ADD_KEY_IS_NEW);
    json_object_object_add_ex(
        json, "respond_cb",
        statistics_pair_to_json(stats->respond_cb, "duration",
                                "relative_timestamp_from_irespond_start",
                                reset),
        JSON_C_OBJECT_ADD_KEY_IS_NEW);
    json_object_object_add_ex(
        json, "irespond_wait",
        statistics_pair_to_json(stats->wait, "duration",
                                "relative_timestamp_from_irespond_end", reset),
        JSON_C_OBJECT_ADD_KEY_IS_NEW);
    json_object_object_add_ex(
        json, "set_output",
        statistics_pair_to_json(stats->set_output, "duration",
                                "relative_timestamp_from_irespond_start",
                                reset),
        JSON_C_OBJECT_ADD_KEY_IS_NEW);
    json_object_object_add_ex(
        json, "get_input",
        statistics_pair_to_json(stats->get_input, "duration",
                                "relative_timestamp_from_ult_start", reset),
        JSON_C_OBJECT_ADD_KEY_IS_NEW);
    return json;
}

/* This function creates a string in the form "A:B:C:D" where
 * - A is the parent RPC id
 * - B is the parent provider id
 * - C is the current RPC id
 * - D is the current provider id
 * The returned string must be freed by the caller.
 */
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

/* This function adds the id, provider_id, parent_id,
 * parent_provider_id, and name  attributes to a
 * JSON fragment containing RPC statistics.
 */
static void fill_json_with_rpc_info(struct json_object*            rpc_json,
                                    const callpath_t*              callpath,
                                    const default_monitor_state_t* monitor)
{
    uint16_t provider_id;
    hg_id_t  base_id;
    uint16_t parent_provider_id;
    hg_id_t  parent_base_id;
    demux_id(callpath->rpc_id, &base_id, &provider_id);
    demux_id(callpath->parent_id, &parent_base_id, &parent_provider_id);
    // add "rpc_id" entry
    json_object_object_add_ex(rpc_json, "rpc_id",
                              json_object_new_uint64(base_id),
                              JSON_C_OBJECT_ADD_KEY_IS_NEW);
    // add "provider_id" entry
    json_object_object_add_ex(rpc_json, "provider_id",
                              json_object_new_uint64(provider_id),
                              JSON_C_OBJECT_ADD_KEY_IS_NEW);
    // add "parent_id" entry
    json_object_object_add_ex(rpc_json, "parent_rpc_id",
                              json_object_new_uint64(parent_base_id),
                              JSON_C_OBJECT_ADD_KEY_IS_NEW);
    // add "parent_provider_id" entry
    json_object_object_add_ex(rpc_json, "parent_provider_id",
                              json_object_new_uint64(parent_provider_id),
                              JSON_C_OBJECT_ADD_KEY_IS_NEW);
    // add "name" entry
    rpc_info_t* rpc_info = rpc_info_find(monitor->rpc_info, callpath->rpc_id);
    const char* rpc_name = rpc_info ? rpc_info->name : "";
    rpc_name             = rpc_name ? rpc_name : "";
    json_object_object_add_ex(rpc_json, "name",
                              json_object_new_string(rpc_name),
                              JSON_C_OBJECT_ADD_KEY_IS_NEW);
}

static struct json_object*
monitor_statistics_to_json(const default_monitor_state_t* state, bool reset)
{
    struct json_object* json = json_object_new_object();
    // add self address
    json_object_object_add_ex(json, "address",
                              json_object_new_string(state->self_addr_str),
                              JSON_C_OBJECT_ADD_KEY_IS_NEW);
    // mercury progress loop statistic
    json_object_object_add_ex(json, "progress_loop",
                              hg_statistics_to_json(&state->hg_stats, reset),
                              JSON_C_OBJECT_ADD_KEY_IS_NEW);
    // RPC statistics
    struct json_object* rpcs = json_object_new_object();
    json_object_object_add_ex(json, "rpcs", rpcs, JSON_C_OBJECT_ADD_KEY_IS_NEW);
    // origin statistics
    {
        origin_rpc_statistics_t *p, *tmp;
        ABT_mutex_spinlock(
            ABT_MUTEX_MEMORY_GET_HANDLE(&state->origin_rpc_stats_mtx));
        HASH_ITER(hh, state->origin_rpc_stats, p, tmp)
        {
            // build RPC key
            char* rpc_key = build_rpc_key(&(p->callpath));
            // find JSON object corresponding to this RPC key
            struct json_object* rpc_json
                = json_object_object_get(rpcs, rpc_key);
            if (!rpc_json) {
                rpc_json = json_object_new_object();
                json_object_object_add(rpcs, rpc_key, rpc_json);
                // fill RPC information
                fill_json_with_rpc_info(rpc_json, &(p->callpath), state);
            }
            // find or add "origin" section
            struct json_object* origin
                = json_object_object_get_or_create_object(rpc_json, "origin");
            // get the destination address from the callpath
            addr_info_t* addr_info
                = addr_info_find_by_id(state, p->callpath.addr_id);
            char addr_key[256];
            sprintf(addr_key, "sent to %s",
                    addr_info ? addr_info->name : "<unknown>");
            // convert origin_rpc_statistics to json
            struct json_object* stats = origin_rpc_statistics_to_json(p, reset);
            // add statistics to "origin" object with the address as key
            json_object_object_add_ex(origin, addr_key, stats,
                                      JSON_C_OBJECT_ADD_KEY_IS_NEW);
            free(rpc_key);
        }
        ABT_mutex_unlock(
            ABT_MUTEX_MEMORY_GET_HANDLE(&state->origin_rpc_stats_mtx));
    }
    // target statistics
    {
        target_rpc_statistics_t *p, *tmp;
        ABT_mutex_spinlock(
            ABT_MUTEX_MEMORY_GET_HANDLE(&state->target_rpc_stats_mtx));
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
                fill_json_with_rpc_info(rpc_json, &(p->callpath), state);
            }
            // find or add "target" section
            struct json_object* target
                = json_object_object_get_or_create_object(rpc_json, "target");
            // get the source address from the callpath
            addr_info_t* addr_info
                = addr_info_find_by_id(state, p->callpath.addr_id);
            char addr_key[256];
            sprintf(addr_key, "received from %s",
                    addr_info ? addr_info->name : "<unknown>");
            // convert target_rpc_statistics to json
            struct json_object* stats = target_rpc_statistics_to_json(p, reset);
            // add statistics to "target" object with the address as key
            json_object_object_add_ex(target, addr_key, stats,
                                      JSON_C_OBJECT_ADD_KEY_IS_NEW);
            free(rpc_key);
        }
        ABT_mutex_unlock(
            ABT_MUTEX_MEMORY_GET_HANDLE(&state->target_rpc_stats_mtx));
    }
    // bulk create statistics
    {
        bulk_create_statistics_t *p, *tmp;
        ABT_mutex_spinlock(
            ABT_MUTEX_MEMORY_GET_HANDLE(&state->bulk_create_stats_mtx));
        HASH_ITER(hh, state->bulk_create_stats, p, tmp)
        {
            // build RPC key
            char* rpc_key = build_rpc_key(&(p->callpath));
            // find json object corresponding to the RPC
            struct json_object* rpc_json
                = json_object_object_get(rpcs, rpc_key);
            if (!rpc_json) {
                rpc_json = json_object_new_object();
                json_object_object_add(rpcs, rpc_key, rpc_json);
                // fill RPC information
                fill_json_with_rpc_info(rpc_json, &(p->callpath), state);
            }
            // find or add a "target" section for it
            struct json_object* target
                = json_object_object_get_or_create_object(rpc_json, "target");
            // get the source address from the callpath
            addr_info_t* addr_info
                = addr_info_find_by_id(state, p->callpath.addr_id);
            char addr_key[256];
            sprintf(addr_key, "received from %s",
                    addr_info ? addr_info->name : "<unknown>");
            // find or add the "received from <address>" section
            struct json_object* received_from
                = json_object_object_get_or_create_object(target, addr_key);
            // find or add the "bulk" section in "received_from"
            struct json_object* bulk = json_object_object_get_or_create_object(
                received_from, "bulk");
            // convert bulk_create_statistics to json
            struct json_object* create
                = bulk_create_statistics_to_json(p, reset);
            // add statistics
            json_object_object_add_ex(bulk, "create", create,
                                      JSON_C_OBJECT_ADD_KEY_IS_NEW);
            free(rpc_key);
        }
        ABT_mutex_unlock(
            ABT_MUTEX_MEMORY_GET_HANDLE(&state->bulk_create_stats_mtx));
    }
    // bulk transfer statistics
    {
        bulk_transfer_statistics_t *p, *tmp;
        ABT_mutex_spinlock(
            ABT_MUTEX_MEMORY_GET_HANDLE(&state->bulk_transfer_stats_mtx));
        HASH_ITER(hh, state->bulk_transfer_stats, p, tmp)
        {
            // build RPC key
            char* rpc_key = build_rpc_key(&(p->bulk_key.callpath));
            // find json object corresponding to the RPC
            struct json_object* rpc_json
                = json_object_object_get(rpcs, rpc_key);
            if (!rpc_json) {
                rpc_json = json_object_new_object();
                json_object_object_add(rpcs, rpc_key, rpc_json);
                // fill RPC information
                fill_json_with_rpc_info(rpc_json, &(p->bulk_key.callpath),
                                        state);
            }
            // find or add a "target" section for it
            struct json_object* target
                = json_object_object_get_or_create_object(rpc_json, "target");
            // get the source address from the callpath
            addr_info_t* addr_info
                = addr_info_find_by_id(state, p->bulk_key.callpath.addr_id);
            char addr_key[256];
            sprintf(addr_key, "received from %s",
                    addr_info ? addr_info->name : "<unknown>");
            // find or add the "received from <address>" section
            struct json_object* received_from
                = json_object_object_get_or_create_object(target, addr_key);
            // find or add the "bulk" section
            struct json_object* bulk = json_object_object_get_or_create_object(
                received_from, "bulk");
            // get the address used in the transfer and the direction of the
            // transfer
            addr_info_t* xfer_addr_info
                = addr_info_find_by_id(state, p->bulk_key.remote_addr_id);
            char xfer_addr_key[256];
            if (p->bulk_key.operation == HG_BULK_PULL) {
                sprintf(xfer_addr_key, "pull from %s",
                        xfer_addr_info ? xfer_addr_info->name : "<unknown>");
            } else {
                sprintf(xfer_addr_key, "push to %s",
                        xfer_addr_info ? xfer_addr_info->name : "<unknown>");
            }
            // convert bulk_transfer_statistics to json
            struct json_object* stats
                = bulk_transfer_statistics_to_json(p, reset);
            // add statistics with the address as key
            json_object_object_add_ex(bulk, xfer_addr_key, stats,
                                      JSON_C_OBJECT_ADD_KEY_IS_NEW);
            free(rpc_key);
        }
        ABT_mutex_unlock(
            ABT_MUTEX_MEMORY_GET_HANDLE(&state->bulk_transfer_stats_mtx));
    }
    // add hostname and pid
    char hostname[1024];
    hostname[1023] = '\0';
    gethostname(hostname, 1023);
    pid_t pid = getpid();
    json_object_object_add(json, "hostname", json_object_new_string(hostname));
    json_object_object_add(json, "pid", json_object_new_int(pid));

    // add command line
    FILE* cmdline_file = fopen("/proc/self/cmdline", "r");
    if (cmdline_file) {
        char* cmdline = (char*)malloc(4096);
        if (cmdline) {
            size_t size = fread(cmdline, 1, 4096, cmdline_file);
            struct json_object* cmdline_json = json_object_new_array();
            char* p = cmdline;
            while (p < cmdline + size) {
                size_t len = strlen(p);
                if (len > 0) {
                    json_object_array_add(cmdline_json, json_object_new_string(p));
                }
                p += len + 1;
            }
            json_object_object_add(json, "cmdline", cmdline_json);
        }
        free(cmdline);
        fclose(cmdline_file);
    }
    return json;
}

static struct json_object*
monitor_time_series_to_json(const default_monitor_state_t* monitor, bool reset)
{
    struct json_object* json = json_object_new_object();
    // add self address
    json_object_object_add_ex(json, "address",
                              json_object_new_string(monitor->self_addr_str),
                              JSON_C_OBJECT_ADD_KEY_IS_NEW);
    /* RPC time series */
    struct json_object* rpcs = json_object_new_object();
    json_object_object_add_ex(json, "rpcs", rpcs, JSON_C_OBJECT_ADD_KEY_IS_NEW);
    {
        rpc_time_series_t *rpc_ts, *tmp;
        ABT_mutex_spinlock(
            ABT_MUTEX_MEMORY_GET_HANDLE(&monitor->rpc_time_series_mtx));
        HASH_ITER(hh, monitor->rpc_time_series, rpc_ts, tmp)
        {
            // get RPC name
            rpc_info_t* rpc_info
                = rpc_info_find(monitor->rpc_info, rpc_ts->rpc_id);
            // get provider id and base id
            hg_id_t  base_id;
            uint16_t provider_id;
            demux_id(rpc_ts->rpc_id, &base_id, &provider_id);
            // create the key
            size_t key_size;
            char*  key;
            if (rpc_info) {
                key_size
                    = snprintf(NULL, 0, "%s:%d", rpc_info->name, provider_id);
                key = malloc(key_size + 1);
                snprintf(key, key_size + 1, "%s:%d", rpc_info->name,
                         provider_id);
            } else {
                key = malloc(10);
                snprintf(key, 10, "<unknown>");
            }
            // create JSON object corresponding to this RPC's time series
            struct json_object* json_ts
                = rpc_time_series_to_json(rpc_ts, reset);
            // add it to the RPC section
            json_object_object_add_ex(rpcs, key, json_ts,
                                      JSON_C_OBJECT_ADD_KEY_IS_NEW);
            // free the key
            free(key);
        }
        ABT_mutex_unlock(
            ABT_MUTEX_MEMORY_GET_HANDLE(&monitor->rpc_time_series_mtx));
    }
    struct json_object* pools = pool_time_series_to_json(monitor, reset);
    json_object_object_add_ex(json, "pools", pools,
                              JSON_C_OBJECT_ADD_KEY_IS_NEW);
    return json;
}

/* ========================================================================
 * Functions related to the hash of rpc_info_t maintained by the monitor
 * ======================================================================== */

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

/* ========================================================================
 * Functions related to the hash of addr_info_t maintained by the monitor
 * ======================================================================== */

static addr_info_t* addr_info_find_or_add(default_monitor_state_t* monitor,
                                          margo_instance_id        mid,
                                          hg_addr_t                addr)
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
    HASH_FIND(hh_by_name, monitor->addr_info_by_name, addr_str, addr_str_size,
              info);
    if (info) return info;
    info = (addr_info_t*)calloc(1, sizeof(*info) + addr_str_size);
    monitor->addr_info_last_id += 1;
    info->id = monitor->addr_info_last_id;
    memcpy(info->name, addr_str, addr_str_size);
    HASH_ADD(hh_by_name, monitor->addr_info_by_name, name[0], addr_str_size,
             info);
    HASH_ADD(hh_by_id, monitor->addr_info_by_id, id, sizeof(info->id), info);
    return info;
}

static addr_info_t* addr_info_find_by_id(const default_monitor_state_t* monitor,
                                         uint64_t                       id)
{
    addr_info_t* info = NULL;
    HASH_FIND(hh_by_id, monitor->addr_info_by_id, &id, sizeof(id), info);
    return info;
}

static void addr_info_clear(addr_info_t* hash_by_id, addr_info_t* hash_by_name)
{
    addr_info_t *p, *tmp;
    HASH_ITER(hh_by_id, hash_by_id, p, tmp)
    {
        HASH_DELETE(hh_by_id, hash_by_id, p);
    }
    HASH_ITER(hh_by_name, hash_by_name, p, tmp)
    {
        HASH_DELETE(hh_by_name, hash_by_name, p);
        free(p);
    }
}

/* ========================================================================
 * Time series function definitions
 * ======================================================================== */

static void time_series_append(time_series_t* series, double ts, uint64_t val)
{
    timedval_frame_t* frame = series->last_frame;
    if (!frame || frame->size == series->frame_capacity) {
        frame = (timedval_frame_t*)malloc(
            sizeof(*frame) + sizeof(timedval_t) * (series->frame_capacity - 1));
        frame->next        = NULL;
        frame->size        = 0;
        series->last_frame = frame;
    }
    if (!series->first_frame) series->first_frame = frame;
    frame->data[frame->size].timestamp = ts;
    frame->data[frame->size].value     = val;
    frame->size += 1;
}

static void time_series_clear(time_series_t* series)
{
    if (!series) return;
    while (series->first_frame) {
        timedval_frame_t* next = series->first_frame->next;
        free(series->first_frame);
        series->first_frame = next;
    }
    series->last_frame = NULL;
}

/* Free both RPC time series and Pool time series */
static void free_all_time_series(default_monitor_state_t* monitor)
{
    if (!monitor->enable_time_series) return;
    ABT_mutex_spinlock(
        ABT_MUTEX_MEMORY_GET_HANDLE(&monitor->rpc_time_series_mtx));
    rpc_time_series_t *p, *tmp;
    HASH_ITER(hh, monitor->rpc_time_series, p, tmp)
    {
        HASH_DEL(monitor->rpc_time_series, p);
        time_series_clear(&(p->rpc_count_series));
        time_series_clear(&(p->bulk_size_series));
        free(p);
    }
    ABT_mutex_unlock(
        ABT_MUTEX_MEMORY_GET_HANDLE(&monitor->rpc_time_series_mtx));

    ABT_mutex_spinlock(
        ABT_MUTEX_MEMORY_GET_HANDLE(&monitor->pool_time_series_mtx));
    size_t num_pools = margo_get_num_pools(monitor->mid);
    for (size_t i = 0; i < num_pools; i++) {
        time_series_clear(&(monitor->pool_size_time_series[i]));
        time_series_clear(&(monitor->pool_total_size_time_series[i]));
    }
    free(monitor->pool_size_time_series);
    free(monitor->pool_total_size_time_series);
    ABT_mutex_unlock(
        ABT_MUTEX_MEMORY_GET_HANDLE(&monitor->rpc_time_series_mtx));
}

/* ========================================================================
 * RPC time series function definitions
 * ======================================================================== */

static rpc_time_series_t*
find_or_add_time_series_for_rpc(default_monitor_state_t* monitor,
                                hg_id_t                  rpc_id)
{
    rpc_time_series_t* ts = NULL;
    HASH_FIND(hh, monitor->rpc_time_series, &rpc_id, sizeof(rpc_id), ts);
    if (!ts) {
        ts         = (rpc_time_series_t*)calloc(1, sizeof(*ts));
        ts->rpc_id = rpc_id;
        ts->rpc_count_series.frame_capacity
            = 256; // TODO: should this be configurable?
        ts->bulk_size_series.frame_capacity
            = 256; // TODO: should this be configurable?
        HASH_ADD(hh, monitor->rpc_time_series, rpc_id, sizeof(rpc_id), ts);
    }
    return ts;
}

static struct json_object* rpc_time_series_to_json(rpc_time_series_t* rpc_ts,
                                                   bool               reset)
{
    timedval_frame_t* frame      = rpc_ts->rpc_count_series.first_frame;
    size_t            array_size = 0;
    while (frame) {
        array_size += frame->size;
        frame = frame->next;
    }

    struct json_object* json       = json_object_new_object();
    struct json_object* timestamps = json_object_new_array_ext(array_size);
    struct json_object* count      = json_object_new_array_ext(array_size);
    struct json_object* bulk_size  = json_object_new_array_ext(array_size);
    json_object_object_add_ex(json, "timestamps", timestamps,
                              JSON_C_OBJECT_ADD_KEY_IS_NEW);
    json_object_object_add_ex(json, "count", count,
                              JSON_C_OBJECT_ADD_KEY_IS_NEW);
    json_object_object_add_ex(json, "bulk_size", bulk_size,
                              JSON_C_OBJECT_ADD_KEY_IS_NEW);
    frame = rpc_ts->rpc_count_series.first_frame;
    while (frame) {
        for (size_t i = 0; i < frame->size; i++) {
            json_object_array_add(
                timestamps, json_object_new_double(frame->data[i].timestamp));
            json_object_array_add(count,
                                  json_object_new_uint64(frame->data[i].value));
        }
        frame = frame->next;
    }

    // note: count and bulk_size time series are updated at
    // the same time in the progress callback, so we know
    // the series of timestamps will be the same. No need to
    // use the series of timestamps of bulk_size.

    frame = rpc_ts->bulk_size_series.first_frame;
    while (frame) {
        for (size_t i = 0; i < frame->size; i++) {
            json_object_array_add(bulk_size,
                                  json_object_new_uint64(frame->data[i].value));
        }
        frame = frame->next;
    }

    if (!reset) goto finish;
    time_series_clear(&(rpc_ts->rpc_count_series));
    time_series_clear(&(rpc_ts->bulk_size_series));

finish:
    return json;
}

static void update_rpc_time_series(struct default_monitor_state* monitor,
                                   double                        timestamp)
{
    if (!monitor->enable_time_series) return;
    rpc_time_series_t *rpc_ts, *tmp;
    ABT_mutex_spinlock(
        ABT_MUTEX_MEMORY_GET_HANDLE(&monitor->rpc_time_series_mtx));
    HASH_ITER(hh, monitor->rpc_time_series, rpc_ts, tmp)
    {
        time_series_append(&rpc_ts->rpc_count_series, timestamp,
                           rpc_ts->rpc_count);
        rpc_ts->rpc_count = 0;

        time_series_append(&rpc_ts->bulk_size_series, timestamp,
                           rpc_ts->bulk_size);
        rpc_ts->bulk_size = 0;
    }
    ABT_mutex_unlock(
        ABT_MUTEX_MEMORY_GET_HANDLE(&monitor->rpc_time_series_mtx));
    monitor->rpc_time_series_last_ts = timestamp;
}

/* ========================================================================
 * Pool time series function definitions
 * ======================================================================== */

static struct json_object*
pool_time_series_to_json(const default_monitor_state_t* monitor, bool reset)
{
    ABT_mutex_spinlock(
        ABT_MUTEX_MEMORY_GET_HANDLE(&monitor->pool_time_series_mtx));

    struct json_object* json = json_object_new_object();

    size_t num_pools = margo_get_num_pools(monitor->mid);
    for (size_t i = 0; i < num_pools; i++) {

        time_series_t* pool_size_ts = &monitor->pool_size_time_series[i];
        time_series_t* pool_total_size_ts
            = &monitor->pool_total_size_time_series[i];

        timedval_frame_t* frame      = pool_size_ts->first_frame;
        size_t            array_size = 0;
        while (frame) {
            array_size += frame->size;
            frame = frame->next;
        }

        struct json_object*    pool_json = json_object_new_object();
        struct margo_pool_info pool_info;
        margo_find_pool_by_index(monitor->mid, i, &pool_info);
        json_object_object_add_ex(json, pool_info.name, pool_json,
                                  JSON_C_OBJECT_ADD_KEY_IS_NEW);

        struct json_object* timestamps = json_object_new_array_ext(array_size);
        struct json_object* size       = json_object_new_array_ext(array_size);
        struct json_object* total_size = json_object_new_array_ext(array_size);
        json_object_object_add_ex(pool_json, "timestamps", timestamps,
                                  JSON_C_OBJECT_ADD_KEY_IS_NEW);
        json_object_object_add_ex(pool_json, "size", size,
                                  JSON_C_OBJECT_ADD_KEY_IS_NEW);
        json_object_object_add_ex(pool_json, "total_size", total_size,
                                  JSON_C_OBJECT_ADD_KEY_IS_NEW);

        frame = pool_size_ts->first_frame;
        while (frame) {
            for (size_t j = 0; j < frame->size; j++) {
                json_object_array_add(
                    timestamps,
                    json_object_new_double(frame->data[j].timestamp));
                json_object_array_add(
                    size, json_object_new_uint64(frame->data[j].value));
            }
            frame = frame->next;
        }
        frame = pool_total_size_ts->first_frame;
        while (frame) {
            for (size_t j = 0; j < frame->size; j++) {
                json_object_array_add(
                    total_size, json_object_new_uint64(frame->data[j].value));
            }
            frame = frame->next;
        }
    }

    if (!reset) goto finish;

    for (size_t i = 0; i < num_pools; i++) {
        time_series_clear(&(monitor->pool_size_time_series[i]));
        time_series_clear(&(monitor->pool_total_size_time_series[i]));
    }

finish:
    ABT_mutex_unlock(
        ABT_MUTEX_MEMORY_GET_HANDLE(&monitor->pool_time_series_mtx));

    return json;
}

static void update_pool_time_series(struct default_monitor_state* monitor,
                                    double                        timestamp)
{
    if (!monitor->enable_time_series) return;
    ABT_mutex_spinlock(
        ABT_MUTEX_MEMORY_GET_HANDLE(&monitor->pool_time_series_mtx));
    size_t num_pools = margo_get_num_pools(monitor->mid);
    for (size_t i = 0; i < num_pools; i++) {
        struct margo_pool_info pool_info;
        if (margo_find_pool_by_index(monitor->mid, i, &pool_info)
            != HG_SUCCESS) {
            continue;
        }
        size_t pool_size, pool_total_size;
        if (ABT_pool_get_size(pool_info.pool, &pool_size) != ABT_SUCCESS) {
            continue;
        }
        if (ABT_pool_get_total_size(pool_info.pool, &pool_total_size)
            != ABT_SUCCESS) {
            continue;
        }
        time_series_append(&(monitor->pool_size_time_series[i]), timestamp,
                           pool_size);
        time_series_append(&(monitor->pool_total_size_time_series[i]),
                           timestamp, pool_total_size);
    }
    ABT_mutex_unlock(
        ABT_MUTEX_MEMORY_GET_HANDLE(&monitor->pool_time_series_mtx));
}
