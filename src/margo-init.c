/*
 * (C) 2020 The University of Chicago
 *
 * See COPYRIGHT in top-level directory.
 */
#include <stdbool.h>
#include <ctype.h>
#include <margo.h>
#include <margo-logging.h>
#include "margo-instance.h"
#include "margo-progress.h"
#include "margo-timer.h"
#include "margo-handle-cache.h"
#include "margo-globals.h"
#include "margo-macros.h"
#include "margo-util.h"
#include "margo-prio-pool.h"
#include "abtx_prof.h"

/* default values for key ABT parameters if not specified */
#define MARGO_DEFAULT_ABT_MEM_MAX_NUM_STACKS 8
#define MARGO_DEFAULT_ABT_THREAD_STACKSIZE   2097152

// Validates the format of the configuration and
// fill default values if they are note provided
static bool
validate_and_complete_config(struct json_object*         _config,
                             ABT_pool                    _progress_pool,
                             ABT_pool                    _rpc_pool,
                             hg_class_t*                 _hg_class,
                             hg_context_t*               _hg_context,
                             const struct hg_init_info*  _hg_init_info,
                             const struct margo_monitor* _monitor);

// Reads a pool configuration and instantiate the
// corresponding ABT_pool, returning ABT_SUCCESS
// or other ABT error codes
static int create_pool_from_config(struct json_object*    pool_config,
                                   uint32_t               index,
                                   struct margo_abt_pool* mpool);

// Reads an xstream configuration and instantiate
// the corresponding ABT_xstream, returning ABT_SUCCESS
// or other ABT error codes
static int create_xstream_from_config(struct json_object*          es_config,
                                      uint32_t                     index,
                                      struct margo_abt_xstream*    es,
                                      const struct margo_abt_pool* mpools,
                                      size_t                       num_pools);

// Sets environment variables for Argobots
static void set_argobots_environment_variables(struct json_object* config);
/* confirm if Argobots is running with desired configuration or not */
static void confirm_argobots_configuration(struct json_object* config);

// Shutdown logic for a margo instance
static void remote_shutdown_ult(hg_handle_t handle);
static DECLARE_MARGO_RPC_HANDLER(remote_shutdown_ult)

int margo_set_environment(const char* optional_json_config)
{
    struct json_object*     config  = NULL;
    struct json_tokener*    tokener = json_tokener_new();
    enum json_tokener_error jerr;

    if (optional_json_config && strlen(optional_json_config) > 0) {
        config = json_tokener_parse_ex(tokener, optional_json_config,
                                       strlen(optional_json_config));
        if (!config) {
            jerr = json_tokener_get_error(tokener);
            MARGO_ERROR(0, "JSON parse error: %s",
                        json_tokener_error_desc(jerr));
            json_tokener_free(tokener);
            return -1;
        }
    }
    json_tokener_free(tokener);

    set_argobots_environment_variables(config);

    return (0);
}

margo_instance_id margo_init_ext(const char*                   address,
                                 int                           mode,
                                 const struct margo_init_info* uargs)
{
    struct margo_init_info args = {0};
    if (uargs) args = *uargs;

    struct json_object* config = NULL;
    int                 ret;
    hg_return_t         hret;
    margo_instance_id   mid = MARGO_INSTANCE_NULL;

    struct margo_hg  hg  = {HG_INIT_INFO_INITIALIZER, NULL, NULL, 0};
    struct margo_abt abt = {0};

    ABT_pool progress_pool = ABT_POOL_NULL;
    ABT_pool rpc_pool      = ABT_POOL_NULL;
    ABT_bool tool_enabled;

    if (getenv("MARGO_ENABLE_MONITORING") && !args.monitor) {
        args.monitor = margo_default_monitor;
    }

    if (args.json_config && strlen(args.json_config) > 0) {
        // read JSON config from provided string argument
        struct json_tokener*    tokener = json_tokener_new();
        enum json_tokener_error jerr;
        config = json_tokener_parse_ex(tokener, args.json_config,
                                       strlen(args.json_config));
        if (!config) {
            jerr = json_tokener_get_error(tokener);
            MARGO_ERROR(0, "JSON parse error: %s",
                        json_tokener_error_desc(jerr));
            json_tokener_free(tokener);
            return MARGO_INSTANCE_NULL;
        }
        json_tokener_free(tokener);
    } else {
        // create default JSON config
        config = json_object_new_object();
    }

    // validate and complete configuration
    MARGO_TRACE(0, "Validating and completing configuration");
    bool valide = validate_and_complete_config(
        config, args.progress_pool, args.rpc_pool, args.hg_class,
        args.hg_context, args.hg_init_info, args.monitor);
    if (!valide) {
        MARGO_ERROR(0, "Could not validate and complete configuration");
        goto error;
    }

    /* START NEW */
    struct json_object* hg_config = json_object_object_get(config, "mercury");
    struct margo_hg_user_args hg_user_args = {.hg_class     = args.hg_class,
                                              .hg_context   = args.hg_context,
                                              .hg_init_info = args.hg_init_info,
                                              .listening    = mode,
                                              .protocol     = address};
    margo_hg_init_from_json(hg_config, &hg_user_args, &hg);
    /* END NEW */

    // initialize Argobots if needed
    if (ABT_initialized() == ABT_ERR_UNINITIALIZED) {
        set_argobots_environment_variables(config);
        MARGO_TRACE(0, "Initializing Argobots");
        ret = ABT_init(0, NULL);
        if (ret != 0) goto error;
        g_margo_abt_init = 1;
    }

    /* Check if Argobots is now initialized with the desired parameters
     * (regardless of whether Margo initialized it or not)
     */
    confirm_argobots_configuration(config);

    /* Turn on profiling capability if a) it has not been done already (this
     * is global to Argobots) and b) the argobots tool interface is enabled.
     */
    if (!g_margo_abt_prof_init) {
        ABT_info_query_config(ABT_INFO_QUERY_KIND_ENABLED_TOOL, &tool_enabled);
        if (tool_enabled == ABT_TRUE) {
            ABTX_prof_init(&g_margo_abt_prof_context);
            g_margo_abt_prof_init = 1;
        }
    }

    // instantiate pools
    struct json_object* argobots_config
        = json_object_object_get(config, "argobots");
    MARGO_TRACE(0, "Instantiating pools from configuration");
    struct json_object* pools_config
        = json_object_object_get(argobots_config, "pools");
    abt.num_pools = json_object_array_length(pools_config);
    abt.pools     = calloc(sizeof(*abt.pools), abt.num_pools);
    if (!abt.pools) {
        MARGO_ERROR(0, "Could not allocate pools array");
        goto error;
    }
    for (unsigned i = 0; i < abt.num_pools; i++) {
        struct json_object* p = json_object_array_get_idx(pools_config, i);
        if (create_pool_from_config(p, i, &abt.pools[i]) != ABT_SUCCESS) {
            goto error;
        }
    }

    // instantiate xstreams
    struct json_object* es_config
        = json_object_object_get(argobots_config, "xstreams");
    abt.num_xstreams = json_object_array_length(es_config);
    abt.xstreams     = calloc(sizeof(*abt.xstreams), abt.num_xstreams);
    if (!abt.xstreams) {
        MARGO_ERROR(0, "Could not allocate xstreams array");
        goto error;
    }
    for (unsigned i = 0; i < abt.num_xstreams; i++) {
        struct json_object* es = json_object_array_get_idx(es_config, i);
        if (create_xstream_from_config(es, i, &abt.xstreams[i], abt.pools,
                                       abt.num_pools)
            != ABT_SUCCESS) {
            goto error;
        }
    }

    // find progress pool
    MARGO_TRACE(0, "Finding progress pool");
    int progress_pool_index = json_object_get_int64(
        json_object_object_get(config, "progress_pool"));
    if (progress_pool_index == -1)
        progress_pool = args.progress_pool;
    else
        progress_pool = abt.pools[progress_pool_index].info.pool;

    // find rpc pool
    MARGO_TRACE(0, "Finding RPC pool");
    int rpc_pool_index
        = json_object_get_int64(json_object_object_get(config, "rpc_pool"));
    if (rpc_pool_index == -1)
        rpc_pool = args.rpc_pool;
    else
        rpc_pool = abt.pools[rpc_pool_index].info.pool;

    // allocate margo instance
    MARGO_TRACE(0, "Allocating margo instance");
    mid = calloc(1, sizeof(*mid));
    if (!mid) {
        MARGO_ERROR(0, "Could not allocate margo instance");
        goto error;
    }

    int progress_timeout_ub = json_object_get_int64(
        json_object_object_get(config, "progress_timeout_ub_msec"));
    int handle_cache_size = json_object_get_int64(
        json_object_object_get(config, "handle_cache_size"));
    int abt_profiling_enabled = json_object_get_boolean(
        json_object_object_get(config, "enable_abt_profiling"));

    mid->json_cfg = config;

    mid->abt_profiling_enabled = abt_profiling_enabled;

    mid->hg  = hg;
    mid->abt = abt;

    mid->progress_pool = progress_pool;
    mid->rpc_pool      = rpc_pool;

    mid->hg_progress_tid           = ABT_THREAD_NULL;
    mid->hg_progress_shutdown_flag = 0;
    mid->hg_progress_timeout_ub    = progress_timeout_ub;

    mid->num_registered_rpcs = 0;
    mid->registered_rpcs     = NULL;

    mid->finalize_flag = 0;
    mid->refcount      = 1;
    ABT_mutex_create(&mid->finalize_mutex);
    ABT_cond_create(&mid->finalize_cond);
    mid->finalize_cb    = NULL;
    mid->prefinalize_cb = NULL;

    mid->pending_operations = 0;
    ABT_mutex_create(&mid->pending_operations_mtx);
    mid->finalize_requested = 0;

    mid->shutdown_rpc_id        = 0;
    mid->enable_remote_shutdown = 0;

    mid->timer_list = __margo_timer_list_create();

    mid->free_handle_list = NULL;
    mid->used_handle_hash = NULL;
    hret                  = __margo_handle_cache_init(mid, handle_cache_size);
    if (hret != HG_SUCCESS) goto error;

    // create current_rpc_id_key ABT_key
    ret = ABT_key_create(NULL, &(mid->current_rpc_id_key));
    if (ret != ABT_SUCCESS) goto error;

    margo_set_logger(mid, args.logger);

    if (args.monitor) {
        struct json_object* monitoring
            = json_object_object_get(config, "monitoring");
        struct json_object* monitoring_config
            = json_object_object_get(monitoring, "config");

        mid->monitor = (struct margo_monitor*)malloc(sizeof(*(mid->monitor)));
        memcpy(mid->monitor, args.monitor, sizeof(*(mid->monitor)));
        if (mid->monitor->initialize)
            mid->monitor->uargs = mid->monitor->initialize(
                mid, mid->monitor->uargs, monitoring_config);

        // replace the "config" section with one provided by the monitoring
        // backend
        if (mid->monitor->config) {
            monitoring_config = mid->monitor->config(mid->monitor->uargs);
            if (monitoring_config) {
                json_object_object_add(monitoring, "config", monitoring_config);
            }
        }
    }

    mid->shutdown_rpc_id = MARGO_REGISTER(
        mid, "__shutdown__", void, margo_shutdown_out_t, remote_shutdown_ult);

    MARGO_TRACE(0, "Starting progress loop");
    ret = ABT_thread_create(mid->progress_pool, __margo_hg_progress_fn, mid,
                            ABT_THREAD_ATTR_NULL, &mid->hg_progress_tid);
    if (ret != ABT_SUCCESS) goto error;

    // increment the number of margo instances
    g_margo_num_instances++;

finish:
    return mid;

error:
    if (mid) {
        if (mid->hg.self_addr_str) free(mid->hg.self_addr_str);
        __margo_handle_cache_destroy(mid);
        __margo_timer_list_free(mid, mid->timer_list);
        ABT_mutex_free(&mid->finalize_mutex);
        ABT_cond_free(&mid->finalize_cond);
        ABT_mutex_free(&mid->pending_operations_mtx);
        if (mid->current_rpc_id_key) ABT_key_free(&(mid->current_rpc_id_key));
        free(mid);
    }
    for (unsigned i = 0; i < abt.num_xstreams; i++) {
        if (abt.xstreams && abt.xstreams[i].info.xstream
            && abt.xstreams[i].margo_free_flag) {
            ABT_xstream_join(abt.xstreams[i].info.xstream);
            ABT_xstream_free(&(abt.xstreams[i].info.xstream));
        }
    }
    free(abt.xstreams);
    // The pools are supposed to be freed automatically
    /*
    for (unsigned i = 0; i < num_pools; i++) {
        if (pools[i] != ABT_POOL_NULL) ABT_pool_free(&pools[i]);
    }
    */
    free(abt.pools);
    if (config) json_object_put(config);
    /* START NEW */
    margo_hg_destroy(&hg);
    /* END NEW */
    return MARGO_INSTANCE_NULL;
}

/**
 * This function takes a margo configuration (parsed JSON tree), validates its
 * content, and complete/replace the content so that it can be used by
 * initialization functions with the knowledge that it contains correct and
 * complete information.
 */
static bool
validate_and_complete_config(struct json_object*         _margo,
                             ABT_pool                    _custom_progress_pool,
                             ABT_pool                    _custom_rpc_pool,
                             hg_class_t*                 _hg_class,
                             hg_context_t*               _hg_context,
                             const struct hg_init_info*  _hg_init_info,
                             const struct margo_monitor* _monitor)
{
    struct json_object* ignore; // to pass as output to macros when we don't
                                // care ouput the output
    struct json_object* val;

    /* ------- Margo configuration ------ */
    /* Fields:
       - [required] mercury: object
       - [optional] argobots: object
       - [optional] progress_timeout_ub_msec: integer >= 0 (default 100)
       - [optional] handle_cache_size: integer >= 0 (default 32)
       - [optional] use_progress_thread: bool (default false)
       - [optional] rpc_thread_count: integer (default 0)
       - [optional] monitoring: object
    */

    /* report version number for this component */
    CONFIG_OVERRIDE_STRING(_margo, "version", PACKAGE_VERSION, "version", 1);

    { // add or override progress_timeout_ub_msec
        CONFIG_HAS_OR_CREATE(_margo, int64, "progress_timeout_ub_msec", 100,
                             "progress_timeout_ub_msec", val);
        CONFIG_INTEGER_MUST_BE_POSITIVE(_margo, "progress_timeout_ub_msec",
                                        "progress_timeout_ub_msec");
        MARGO_TRACE(0, "progress_timeout_ub_msec = %ld",
                    json_object_get_int64(val));
    }

    { // add or override handle_cache_size
        CONFIG_HAS_OR_CREATE(_margo, int64, "handle_cache_size", 32,
                             "handle_cache_size", val);
        CONFIG_INTEGER_MUST_BE_POSITIVE(_margo, "handle_cache_size",
                                        "handle_cache_size");
        MARGO_TRACE(0, "handle_cache_size = %ld", json_object_get_int64(val));
    }

    /* find the "monitoring" object in the configuration */
    struct json_object* _monitoring = NULL;
    CONFIG_HAS_OR_CREATE_OBJECT(_margo, "monitoring", "monitoring",
                                _monitoring);

    { // override monitor name
        const char* monitor_name
            = (_monitor && _monitor->name) ? _monitor->name() : "<unknown>";
        CONFIG_OVERRIDE_STRING(_monitoring, "name", monitor_name,
                               "monitoring.name", 1);
        MARGO_TRACE(0, "monitoring.name = %s", monitor_name);
    }
    { // add or create "config" in monitoring
        CONFIG_HAS_OR_CREATE_OBJECT(_monitoring, "config", "config", ignore);
    }

    /* ------- Argobots configuration ------ */
    /* Fields:
       - abt_mem_max_num_stacks: integer >= 0 (default
       MARGO_DEFAULT_ABT_MEM_MAX_NUM_STACKS)
       - abt_thread_stacksize: integer >= 0 (default
       MARGO_DEFAULT_ABT_THREAD_STACKSIZE)
       - pools: array
       - xstreams: array
    */
    struct json_object* _argobots = NULL;
    CONFIG_HAS_OR_CREATE_OBJECT(_margo, "argobots", "argobots", _argobots);

    { // handle abt_mem_max_num_stacks
        const char* abt_mem_max_num_stacks_str
            = getenv("ABT_MEM_MAX_NUM_STACKS");
        int abt_mem_max_num_stacks = abt_mem_max_num_stacks_str
                                       ? atoi(abt_mem_max_num_stacks_str)
                                       : MARGO_DEFAULT_ABT_MEM_MAX_NUM_STACKS;
        if (abt_mem_max_num_stacks_str) {
            CONFIG_OVERRIDE_INTEGER(_argobots, "abt_mem_max_num_stacks",
                                    abt_mem_max_num_stacks,
                                    "argobots.abt_mem_max_num_stacks", 1);
            MARGO_TRACE(0, "argobots.abt_mem_max_num_stacks = %d",
                        abt_mem_max_num_stacks);
        } else {
            CONFIG_HAS_OR_CREATE(_argobots, int64, "abt_mem_max_num_stacks",
                                 abt_mem_max_num_stacks,
                                 "argobots.abt_mem_max_num_stacks", val);
            MARGO_TRACE(0, "argobots.abt_mem_max_num_stacks = %d",
                        json_object_get_int64(val));
        }
    }

    { // handle abt_thread_stacksize
        const char* abt_thread_stacksize_str = getenv("ABT_THREAD_STACKSIZE");
        int         abt_thread_stacksize     = abt_thread_stacksize_str
                                                 ? atoi(abt_thread_stacksize_str)
                                                 : MARGO_DEFAULT_ABT_THREAD_STACKSIZE;
        if (abt_thread_stacksize_str) {
            CONFIG_OVERRIDE_INTEGER(_argobots, "abt_thread_stacksize",
                                    abt_thread_stacksize,
                                    "argobots.abt_thread_stacksize", 1);
            MARGO_TRACE(0, "argobots.abt_thread_stacksize = %d",
                        abt_thread_stacksize);
        } else {
            CONFIG_HAS_OR_CREATE(_argobots, int64, "abt_thread_stacksize",
                                 abt_thread_stacksize,
                                 "argobots.abt_thread_stacksize", val);
            MARGO_TRACE(0, "argobots.abt_thread_stacksize = %d",
                        json_object_get_int64(val));
        }
    }

    { // handle version
        CONFIG_OVERRIDE_STRING(_argobots, "version", ABT_VERSION,
                               "argobots.version", 1);
        MARGO_TRACE(0, "argobots.version = " ABT_VERSION);
    }

    /* ------- Argobots pools configuration ------- */
    /* Fields:
       - [optional] name: string (default generated)
       - [optional] kind: string (default "fifo_wait")
       - [optional] access: string (default "mpmc")
    */
    struct json_object* _pools = NULL;
    CONFIG_HAS_OR_CREATE_ARRAY(_argobots, "pools", "argobots.pools", _pools);
    unsigned num_custom_pools = json_object_array_length(_pools);
    { // check each pool in the array of pools
        struct json_object* _pool = NULL;
        unsigned            i     = 0;
        json_array_foreach(_pools, i, _pool)
        {
            // handle "name" field
            char default_name[64];
            snprintf(default_name, 64, "__pool_%d__", i);
            CONFIG_HAS_OR_CREATE(_pool, string, "name", default_name,
                                 "argobots.pools[?].name", val);
            // check that the name is authorized
            CONFIG_NAME_IS_VALID(_pool);
            MARGO_TRACE(0, "argobots.pools[%d].name = \"%s\"", i,
                        json_object_get_string(val));
            // handle "kind" field
            CONFIG_HAS_OR_CREATE(_pool, string, "kind", "fifo_wait",
                                 "argobots.pools[?].kind", val);
            CONFIG_IS_IN_ENUM_STRING(val, "argobots.pools[?].kind", "fifo",
                                     "fifo_wait", "prio_wait");
            MARGO_TRACE(0, "argobots.pools[%d].kind = %s", i,
                        json_object_get_string(val));
            // handle "access" field
            CONFIG_HAS_OR_CREATE(_pool, string, "access", "mpmc",
                                 "argobots.pools[?].access", val);
            CONFIG_IS_IN_ENUM_STRING(val, "argobots.pools[?].access", "private",
                                     "spsc", "mpsc", "spmc", "mpmc");
            MARGO_TRACE(0, "argobots.pools[%d].access = %s", i,
                        json_object_get_string(val));
        }
        // check that the names aren't repeated
        CONFIG_NAMES_MUST_BE_UNIQUE(_pools, "argobots.pools");
    }

    /* ------- Argobots xstreams configuration ------- */
    /* Fields:
       - [optional] name: string (default generated)
       - [optional] cpubind: integer
       - [optional] affinity: array of integers
       - [required] scheduler: object with fields
                    [required] type: string
                    [required] pools: array of integers os trings
    */
    struct json_object* _xstreams = NULL;
    CONFIG_HAS_OR_CREATE_ARRAY(_argobots, "xstreams", "argobots.xstreams",
                               _xstreams);
    {
        struct json_object* _xstream = NULL;
        unsigned            i        = 0;
        json_array_foreach(_xstreams, i, _xstream)
        {
            // handle "name" field
            char default_name[64];
            snprintf(default_name, 64, "__xstream_%d__", i);
            CONFIG_HAS_OR_CREATE(_xstream, string, "name", default_name,
                                 "argobots.xstreams[?].name", val);
            // check that the name is authorized
            CONFIG_NAME_IS_VALID(_xstream);
            MARGO_TRACE(0, "argobots.xstreams[%d].name = \"%s\"", i,
                        json_object_get_string(val));
            // handle cpubind entry
            CONFIG_HAS_OR_CREATE(_xstream, int64, "cpubind", -1,
                                 "argobots.xstreams[?].cpubind", val);
            MARGO_TRACE(0, "argobots.xstreams[%d].cpubind = %d", i,
                        json_object_get_int64(val));
            // handle affinity
            struct json_object* _affinity
                = json_object_object_get(_xstream, "affinity");
            if (!_affinity) {
                json_object_object_add(_xstream, "affinity",
                                       json_object_new_array());
            } else if (json_object_is_type(_affinity, json_type_array)) {
                for (unsigned j = 0; j < json_object_array_length(_affinity);
                     j++) {
                    val = json_object_array_get_idx(_affinity, j);
                    if (!json_object_is_type(val, json_type_int)) {
                        MARGO_ERROR(0,
                                    "Invalid element type found in affinity "
                                    "array (should be integers)");
                        return false;
                    }
                    MARGO_TRACE(0, "argobots.xstreams[%d].affinity[%d] = %d", i,
                                j, json_object_get_int64(val));
                }
            } else {
                MARGO_ERROR(0,
                            "Invalid type for affinity field (should be array "
                            "of integers)");
                return -1;
            }
            // find "scheduler" entry
            struct json_object* _sched      = NULL;
            struct json_object* _sched_type = NULL;
            struct json_object* _pool_refs  = NULL;
            CONFIG_MUST_HAVE(_xstream, object, "scheduler",
                             "argobots.xstreams[?].scheduler", _sched);
            CONFIG_MUST_HAVE(_sched, string, "type",
                             "argobots.xstreams[?].scheduler.type",
                             _sched_type);
            CONFIG_IS_IN_ENUM_STRING(
                _sched_type, "argobots.xstreams[?].scheduler.type", "default",
                "basic", "prio", "randws", "basic_wait");
            MARGO_TRACE(0, "argobots.xstreams[%d].scheduler.type = %s", i,
                        json_object_get_string(_sched_type));
            CONFIG_MUST_HAVE(_sched, array, "pools",
                             "argobots.xstreams[?].scheduler.pools",
                             _pool_refs);
            // pools array must not be empty
            size_t num_pool_refs = json_object_array_length(_pool_refs);
            if (num_pool_refs == 0) {
                MARGO_ERROR(0,
                            "In scheduler definition, pools should not be an "
                            "empty array");
                return false;
            }
            // check that all the pool references refer to a known pool
            unsigned            j;
            struct json_object* _pool_ref = NULL;
            int                 _pool_ref_index;
            json_array_foreach(_pool_refs, j, _pool_ref)
            {
                if (json_object_is_type(_pool_ref, json_type_int)) {
                    _pool_ref_index = json_object_get_int64(_pool_ref);
                    if (_pool_ref_index < 0
                        || _pool_ref_index >= (int)num_custom_pools) {
                        MARGO_ERROR(
                            0, "Invalid pool index %d in scheduler definition",
                            _pool_ref_index);
                        return false;
                    }
                } else if (json_object_is_type(_pool_ref, json_type_string)) {
                    const char* _pool_ref_name
                        = json_object_get_string(_pool_ref);
                    CONFIG_FIND_BY_NAME(_pools, _pool_ref_name, _pool_ref_index,
                                        ignore);
                    if (_pool_ref_index == -1) {
                        MARGO_ERROR(
                            0,
                            "Invalid pool name \"%s\" in scheduler definition",
                            _pool_ref_name);
                        return false;
                    }
                    // replace the name with the index
                    json_object_array_put_idx(
                        _pool_refs, j, json_object_new_int64(_pool_ref_index));
                } else {
                    MARGO_ERROR(
                        0,
                        "Reference to pool should be an integer or a string");
                    return false;
                }
                MARGO_TRACE(0, "argobots.xstreams[%d].scheduler.pools[%d] = %d",
                            i, j, _pool_ref_index);
            }
        }
        // check that the names of xstreams are unique
        CONFIG_NAMES_MUST_BE_UNIQUE(_xstreams, "argobots.xstreams");
        // if there is no __primary__ xstream, create one, along with its
        // scheduler and pool
        {
            int xstream_index, pool_index;
            CONFIG_FIND_BY_NAME(_xstreams, "__primary__", xstream_index,
                                ignore);
            if (xstream_index
                == -1) { // need to create __primary__ xstream entry
                CONFIG_FIND_BY_NAME(_pools, "__primary__", pool_index, ignore);
                if (pool_index == -1) { // need to create __primary__ pool entry
                    MARGO_TRACE(0,
                                "__primary__ pool not found, will be created");
                    CONFIG_ADD_NEW_POOL(_pools, "__primary__", "fifo_wait",
                                        "mpmc");
                }
                pool_index = json_object_array_length(_pools) - 1;
                MARGO_TRACE(0,
                            "__primary__ xstream not found, will be created");
                CONFIG_ADD_NEW_XSTREAM(_xstreams, "__primary__", "basic_wait",
                                       pool_index);
            }
        }
    }

    /* ------- Margo configuration (cont'd) ------ */

    { // handle progress_pool and use_progress_thread fields and _progress_pool
      // argument

        if (_custom_progress_pool != ABT_POOL_NULL
            && _custom_progress_pool
                   != NULL) { // custom pool provided as argument

            // -1 is used to indicate that progress_pool is provided by the user
            CONFIG_OVERRIDE_INTEGER(_margo, "progress_pool", -1,
                                    "progress_pool", 1);
            if (CONFIG_HAS(_margo, "use_progress_thread", ignore)) {
                MARGO_WARNING(0,
                              "\"use_progress_thread\" ignored because custom "
                              "progress pool was provided");
            }
            if (CONFIG_HAS(_pools, "__progress__", ignore)) {
                MARGO_WARNING(
                    0,
                    "__progress__ pool defined but will NOT be used "
                    "for progress since custom progress pool was provided");
            }

        } else { // no custom pool provided as argument

            struct json_object* _progress_pool = NULL;
            if (CONFIG_HAS(_margo, "progress_pool", _progress_pool)) {

                if (CONFIG_HAS(_margo, "use_progress_thread",
                               ignore)) { // progress_pool and
                                          // use_progress_thread both specified
                    MARGO_WARNING(0,
                                  "\"use_progress_thread\" ignored because "
                                  "\"progress_pool\" was provided");
                }
                int progress_pool_index = -1;
                if (json_object_is_type(_progress_pool, json_type_string)) {
                    // progres_pool specified by name
                    const char* progress_pool_name
                        = json_object_get_string(_progress_pool);
                    CONFIG_ARRAY_MUST_HAVE_ITEM_NAMED(
                        _pools, progress_pool_name, "argobots.pools", ignore);
                    CONFIG_FIND_BY_NAME(_pools, progress_pool_name,
                                        progress_pool_index, ignore);
                } else if (json_object_is_type(_progress_pool, json_type_int)) {
                    // progress_pool specified by index
                    progress_pool_index = json_object_get_int64(_progress_pool);
                    if ((progress_pool_index < -1)
                        || (progress_pool_index
                            >= (int)json_object_array_length(_pools))) {
                        MARGO_ERROR(0,
                                    "\"progress_pool\" value (%d) out of range",
                                    progress_pool_index);
                        return false;
                    }
                } else {
                    // invalid type for progress_pool field
                    MARGO_ERROR(0,
                                "\"progress_pool\" should be of type integer "
                                "or string");
                    return false;
                }
                // update the progress_pool to an integer index
                json_object_object_add(
                    _margo, "progress_pool",
                    json_object_new_int64(progress_pool_index));
                MARGO_TRACE(0, "progress_pool = %d", progress_pool_index);
            } else {
                // progress_pool not specified, we will try to find
                // use_progress_thread
                bool use_progress_thread = 0;
                if (CONFIG_HAS(_margo, "use_progress_thread",
                               ignore)) { // use_progress_thread specified,
                    if (!json_object_is_type(json_object_object_get(
                                                 _margo, "use_progress_thread"),
                                             json_type_boolean)) {
                        MARGO_ERROR(
                            0, "\"use_progress_thread\" should be a boolean");
                        return false;
                    }
                    use_progress_thread = json_object_get_boolean(
                        json_object_object_get(_margo, "use_progress_thread"));
                }
                if (use_progress_thread) {
                    // create a specific pool, scheduler, and xstream for the
                    // progress loop
                    int pool_index = -1;
                    // check if __progress__ pool already defined
                    CONFIG_FIND_BY_NAME(_pools, "__progress__", pool_index,
                                        ignore);
                    if (pool_index == -1) {
                        // create __progress__ pool only if it does not exist
                        MARGO_TRACE(0, "Creating __progress__ pool");
                        CONFIG_ADD_NEW_POOL(_pools, "__progress__", "fifo_wait",
                                            "mpmc");
                        pool_index = json_object_array_length(_pools) - 1;
                    }
                    // create new xstream called __progress__ associated with
                    // the pool
                    MARGO_TRACE(0, "Creating __progress__ xstream");
                    CONFIG_ADD_NEW_XSTREAM(_xstreams, "__progress__",
                                           "basic_wait", pool_index);
                    json_object_object_add(_margo, "progress_pool",
                                           json_object_new_int64(pool_index));
                    MARGO_TRACE(0, "progress_pool = %d", pool_index);
                } else {
                    // use primary xstream's scheduler's first pool for the
                    // progress loop
                    struct json_object* _primary_xstream       = NULL;
                    struct json_object* _primary_sched         = NULL;
                    int                 _primary_xstream_index = -1;
                    if (CONFIG_HAS(_pools, "__progress__", ignore)) {
                        MARGO_WARNING(
                            0,
                            "__progress__ pool defined but will NOT be"
                            " used for progress unless it is the first pool of "
                            "the __primary__ ES");
                    }
                    // find __primary__ xstream
                    CONFIG_FIND_BY_NAME(_xstreams, "__primary__",
                                        _primary_xstream_index,
                                        _primary_xstream);
                    (void)_primary_xstream_index; // silence warnings because we
                                                  // are not using it
                    // find its scheduler
                    _primary_sched
                        = json_object_object_get(_primary_xstream, "scheduler");
                    // find the scheduler's first pool
                    struct json_object* _primary_pool_refs
                        = json_object_object_get(_primary_sched, "pools");
                    struct json_object* _first_pool_ref
                        = json_object_array_get_idx(_primary_pool_refs, 0);
                    // set "progress_pool" to the pool's reference
                    json_object_object_add(_margo, "progress_pool",
                                           json_object_copy(_first_pool_ref));
                    MARGO_TRACE(0, "progress_pool = %d",
                                json_object_get_int64(_first_pool_ref));
                }
            }
        }
    }
    // delete the "use_progress_thread" field
    json_object_object_del(_margo, "use_progress_thread");

    { // handle rpc_thread_count and rpc_pool

        if (_custom_rpc_pool != ABT_POOL_NULL
            && _custom_rpc_pool != NULL) { // custom pool provided as argument
            // -1 means user-provided pool
            CONFIG_OVERRIDE_INTEGER(_margo, "rpc_pool", -1, "rpc_pool", 1);
            if (CONFIG_HAS(_margo, "rpc_thread_count", ignore)) {
                MARGO_WARNING(0,
                              "\"rpc_thread_count\" ignored because custom "
                              "RPC pool was provided");
            }
            if (CONFIG_HAS(_pools, "__rpc__", ignore)) {
                MARGO_WARNING(0,
                              "__rpc__ pool defined by will NOT be used "
                              "for RPCs since custom RPC pool was provided");
            }

        } else { // no custom pool provided as argument

            struct json_object* _rpc_pool = NULL;
            if (CONFIG_HAS(_margo, "rpc_pool", _rpc_pool)) {
                // rpc_pool field specified
                if (CONFIG_HAS(_margo, "rpc_thread_count",
                               ignore)) { // rpc_pool and rpc_thread_count both
                                          // specified
                    MARGO_WARNING(0,
                                  "\"rpc_thread_count\" ignored"
                                  "because \"rpc_pool\" is provided");
                }
                int rpc_pool_index = -1;
                if (json_object_is_type(_rpc_pool, json_type_string)) {
                    // rpc_pool specified as a string
                    const char* rpc_pool_name
                        = json_object_get_string(_rpc_pool);
                    CONFIG_ARRAY_MUST_HAVE_ITEM_NAMED(_pools, rpc_pool_name,
                                                      "argobots.pools", ignore);
                    CONFIG_FIND_BY_NAME(_pools, rpc_pool_name, rpc_pool_index,
                                        ignore);
                } else if (json_object_is_type(_rpc_pool, json_type_int)) {
                    // rpc_pool specified as an integer
                    rpc_pool_index = json_object_get_int64(_rpc_pool);
                    if (rpc_pool_index < -1
                        || rpc_pool_index
                               >= (int)json_object_array_length(_pools)) {
                        MARGO_ERROR(0, "\"rpc_pool\" value (%d) out of range",
                                    rpc_pool_index);
                        return false;
                    }
                } else {
                    // rpc_pool has invalid type
                    MARGO_ERROR(
                        0, "\"rpc_pool\" should be of type integer or string");
                    return false;
                }
                // update the rpc_pool to an integer index
                json_object_object_add(_margo, "rpc_pool",
                                       json_object_new_int64(rpc_pool_index));
                MARGO_TRACE(0, "rpc_pool = %d", rpc_pool_index);
            } else { // rpc_pool not specified, use rpc_thread_count instead
                int rpc_thread_count = 0;
                if (CONFIG_HAS(_margo, "rpc_thread_count",
                               ignore)) { // rpc_thread_count specified
                    if (!json_object_is_type(
                            json_object_object_get(_margo, "rpc_thread_count"),
                            json_type_int)) {
                        MARGO_ERROR(
                            0, "\"rpc_thread_count\" should be an integer");
                        return false;
                    }
                    rpc_thread_count = json_object_get_int64(
                        json_object_object_get(_margo, "rpc_thread_count"));
                    MARGO_TRACE(0, "\"rpc_thread_count\" found to be %d",
                                rpc_thread_count);
                }
                if (rpc_thread_count < 0) { // use progress loop's pool
                    if (CONFIG_HAS(_pools, "__rpc__", ignore)) {
                        MARGO_WARNING(0,
                                      "__rpc__ pool defined but will NOT be the"
                                      " pool used for RPC unless it is also "
                                      "used as progress pool");
                    }
                    struct json_object* _progress_pool
                        = json_object_object_get(_margo, "progress_pool");
                    struct json_object* _rpc_pool
                        = json_object_copy(_progress_pool);
                    json_object_object_add(_margo, "rpc_pool", _rpc_pool);
                    MARGO_TRACE(0, "rpc_pool = %d",
                                json_object_get_int64(_rpc_pool));
                } else if (rpc_thread_count == 0) { // use primary pool
                    if (CONFIG_HAS(_pools, "__rpc__", ignore)) {
                        MARGO_WARNING(
                            0,
                            "__rpc__ pool defined but will NOT be"
                            " used for RPCs unless it is the first pool of the"
                            " __primary__ ES");
                    }
                    // use primary xstream's scheduler's first pool for the RPC
                    // loop
                    struct json_object* _primary_xstream       = NULL;
                    struct json_object* _primary_sched         = NULL;
                    int                 _primary_xstream_index = -1;
                    // find __primary__ xstream
                    CONFIG_FIND_BY_NAME(_xstreams, "__primary__",
                                        _primary_xstream_index,
                                        _primary_xstream);
                    (void)_primary_xstream_index; // silence warning because we
                                                  // are not using it
                    // find its scheduler
                    _primary_sched
                        = json_object_object_get(_primary_xstream, "scheduler");
                    // find the scheduler's first pool
                    struct json_object* _primary_pool_refs
                        = json_object_object_get(_primary_sched, "pools");
                    struct json_object* _first_pool_ref
                        = json_object_array_get_idx(_primary_pool_refs, 0);
                    // set "rpc_pool"
                    json_object_object_add(_margo, "rpc_pool",
                                           json_object_copy(_first_pool_ref));
                    MARGO_TRACE(0, "rpc_pool = %d",
                                json_object_get_int64(_first_pool_ref));
                } else { // define a new pool (if not present) and some new
                         // xstreams
                    int pool_index = -1;
                    CONFIG_FIND_BY_NAME(_pools, "__rpc__", pool_index, ignore);
                    if (pool_index == -1) {
                        MARGO_TRACE(0, "Creating new __rpc__ pool");
                        CONFIG_ADD_NEW_POOL(_pools, "__rpc__", "fifo_wait",
                                            "mpmc");
                        pool_index = json_object_array_length(_pools) - 1;
                    }
                    for (int i = 0; i < rpc_thread_count; i++) {
                        char name[64];
                        snprintf(name, 64, "__rpc_%d__", i);
                        MARGO_TRACE(0, "Creating new __rpc_%d__ xstream", i);
                        CONFIG_ADD_NEW_XSTREAM(_xstreams, name, "basic_wait",
                                               pool_index);
                    }
                    json_object_object_add(_margo, "rpc_pool",
                                           json_object_new_int64(pool_index));
                    MARGO_TRACE(0, "rpc_pool = %d", pool_index);
                }
            }
        }
    }
    json_object_object_del(_margo, "rpc_thread_count");

    return true;
}

static int create_pool_from_config(struct json_object*    pool_config,
                                   uint32_t               index,
                                   struct margo_abt_pool* mpool)
{
    int          ret;
    ABT_pool_def prio_pool_def;

    const char* jname
        = json_object_get_string(json_object_object_get(pool_config, "name"));
    const char* jkind
        = json_object_get_string(json_object_object_get(pool_config, "kind"));
    const char* jaccess
        = json_object_get_string(json_object_object_get(pool_config, "access"));

    ABT_pool_kind   kind;
    ABT_pool_access access;

    if (strcmp(jkind, "fifo") == 0)
        kind = ABT_POOL_FIFO;
    else if (strcmp(jkind, "fifo_wait") == 0)
        kind = ABT_POOL_FIFO_WAIT;
    else if (strcmp(jkind, "prio_wait") == 0)
        kind = ABT_POOL_FIFO_WAIT; /* just to silence CodeQL */
    else {
        MARGO_ERROR(0, "Invalid pool kind \"%s\"", jkind);
        return -1;
    }

    if (strcmp(jaccess, "private") == 0)
        access = ABT_POOL_ACCESS_PRIV;
    else if (strcmp(jaccess, "spsc") == 0)
        access = ABT_POOL_ACCESS_SPSC;
    else if (strcmp(jaccess, "mpsc") == 0)
        access = ABT_POOL_ACCESS_MPSC;
    else if (strcmp(jaccess, "spmc") == 0)
        access = ABT_POOL_ACCESS_SPMC;
    else if (strcmp(jaccess, "mpmc") == 0)
        access = ABT_POOL_ACCESS_MPMC;
    else {
        MARGO_ERROR(0, "Invalid pool access \"%s\"", jaccess);
        return -1;
    }

    MARGO_TRACE(0, "Instantiating pool \"%s\"", jname);

    if (strcmp(jkind, "prio_wait") == 0) {
        margo_create_prio_pool_def(&prio_pool_def);
        ret = ABT_pool_create(&prio_pool_def, ABT_POOL_CONFIG_NULL,
                              &(mpool->info.pool));
        if (ret != ABT_SUCCESS) {
            MARGO_ERROR(
                0, "ABT_pool_create failed to create prio_wait pool (ret = %d)",
                ret);
        }
        /* Note that for a custom pool like this, Margo is responsible for
         * free'ing it, but only if it is _not_ the primary pool.  See
         * https://lists.argobots.org/pipermail/discuss/2021-March/000109.html
         */
        if (strcmp(jname, "__primary__")) mpool->margo_free_flag = true;
    } else {
        /* one of the standard Argobots pool types */
        ret = ABT_pool_create_basic(kind, access, ABT_TRUE,
                                    &(mpool->info.pool));
        if (ret != ABT_SUCCESS) {
            MARGO_ERROR(
                0, "ABT_pool_create_basic failed to create pool (ret = %d)",
                ret);
        }
    }
    mpool->info.name  = jname;
    mpool->info.index = index;
    return ret;
}

static int create_xstream_from_config(struct json_object*          es_config,
                                      uint32_t                     index,
                                      struct margo_abt_xstream*    es,
                                      const struct margo_abt_pool* mpools,
                                      size_t total_num_pools)
{
    (void)total_num_pools; // silence warning about unused variable

    int         ret = ABT_SUCCESS;
    const char* es_name
        = json_object_get_string(json_object_object_get(es_config, "name"));
    struct json_object* sched = json_object_object_get(es_config, "scheduler");
    const char*         es_sched_type
        = json_object_get_string(json_object_object_get(sched, "type"));
    struct json_object* es_pools_array = json_object_object_get(sched, "pools");
    size_t              es_num_pools = json_object_array_length(es_pools_array);
    int                 es_cpubind
        = json_object_get_int64(json_object_object_get(es_config, "cpubind"));
    struct json_object* es_affinity
        = json_object_object_get(es_config, "affinity");

    ABT_sched_predef predef;
    if (strcmp(es_sched_type, "default") == 0)
        predef = ABT_SCHED_DEFAULT;
    else if (strcmp(es_sched_type, "basic") == 0)
        predef = ABT_SCHED_BASIC;
    else if (strcmp(es_sched_type, "prio") == 0)
        predef = ABT_SCHED_PRIO;
    else if (strcmp(es_sched_type, "randws") == 0)
        predef = ABT_SCHED_RANDWS;
    else if (strcmp(es_sched_type, "basic_wait") == 0)
        predef = ABT_SCHED_BASIC_WAIT;
    else {
        MARGO_ERROR(0, "Invalid scheduler type \"%s\"", es_sched_type);
        return -1;
    }

    ABT_pool es_pools[es_num_pools];
    for (unsigned i = 0; i < es_num_pools; i++) {
        int pool_ref = json_object_get_int64(
            json_object_array_get_idx(es_pools_array, i));
        es_pools[i] = mpools[pool_ref].info.pool;
    }

    if (strcmp(es_name, "__primary__") == 0) {

        ret = ABT_xstream_self(&(es->info.xstream));
        if (ret != ABT_SUCCESS) {
            MARGO_ERROR(0, "ABT_xstream_self failed (ret = %d)", ret);
            return ret;
        }
        ABT_bool is_primary;
        ABT_xstream_is_primary(es->info.xstream, &is_primary);
        if (!is_primary) {
            MARGO_WARNING(0, "margo_init_ext called from non-primary ES");
        }
        es->margo_free_flag = false;
        ret = ABT_xstream_set_main_sched_basic(es->info.xstream, predef,
                                               es_num_pools, es_pools);
        if (ret != ABT_SUCCESS) {
            MARGO_ERROR(0,
                        "ABT_xstream_set_main_sched_basic failed to set "
                        "scheduler (ret = %d)",
                        ret);
            return ret;
        }

    } else {

        ret = ABT_xstream_create_basic(predef, es_num_pools, es_pools,
                                       ABT_SCHED_CONFIG_NULL,
                                       &(es->info.xstream));
        if (ret != ABT_SUCCESS) {
            MARGO_ERROR(
                0, "ABT_xstream_create_basic failed to create pool (ref = %d)",
                ret);
            return ret;
        }
        es->margo_free_flag = true;
    }
    es->info.name  = es_name;
    es->info.index = index;

    // get/set cpubind
    if (es_cpubind == -1) {
        ret = ABT_xstream_get_cpubind(es->info.xstream, &es_cpubind);
        if (ret == ABT_ERR_FEATURE_NA) {
            /* feature not supported in this Argobots build; skip */
        } else if (ret != ABT_SUCCESS) {
            MARGO_WARNING(
                0, "ABT_xstream_get_cpubind failed to get cpubind (ret = %d)",
                ret);
        } else {
            json_object_object_add(es_config, "cpubind",
                                   json_object_new_int64(es_cpubind));
        }
    } else {
        ret = ABT_xstream_set_cpubind(es->info.xstream, es_cpubind);
        if (ret != ABT_SUCCESS) {
            MARGO_WARNING(
                0, "ABT_xstream_set_cpubind failed to set cpubind (ret = %d)",
                ret);
        }
    }

    // get/set affinity
    if (json_object_array_length(es_affinity) == 0) {
        // get affinity
        int num_cpus;
        ret = ABT_xstream_get_affinity(es->info.xstream, 0, NULL, &num_cpus);
        if (ret == ABT_ERR_FEATURE_NA) {
            /* feature not supported in this Argobots build; skip */
        } else if (ret != ABT_SUCCESS) {
            MARGO_WARNING(
                0, "ABT_xstream_get_affinity failed to get affinity (ret = %d)",
                ret);
        } else if (num_cpus) {
            int cpuids[num_cpus];
            ABT_xstream_get_affinity(es->info.xstream, num_cpus, cpuids,
                                     &num_cpus);
            for (int i = 0; i < num_cpus; i++) {
                json_object_array_put_idx(es_affinity, i,
                                          json_object_new_int64(cpuids[i]));
            }
        }
    } else {
        // set affinity
        int num_cpus = json_object_array_length(es_affinity);
        int cpuids[num_cpus];
        for (int i = 0; i < num_cpus; i++) {
            cpuids[i] = json_object_get_int64(
                json_object_array_get_idx(es_affinity, i));
        }
        ret = ABT_xstream_set_affinity(es->info.xstream, num_cpus, cpuids);
        if (ret != ABT_SUCCESS) {
            MARGO_WARNING(
                0, "ABT_xtsream_set_affinity failed to set affinity (ret = %d)",
                ret);
        }
    }

    return ABT_SUCCESS;
}

static void confirm_argobots_configuration(struct json_object* config)
{
    /* this function assumes that the json is already fully populated */
    size_t runtime_abt_thread_stacksize = 0;
#ifdef HAVE_ABT_INFO_QUERY_KIND_ENABLED_LAZY_STACK_ALLOC
    ABT_bool config_bool;
#endif

    /* retrieve expected values according to Margo configuration */
    struct json_object* argobots = json_object_object_get(config, "argobots");
    int                 abt_thread_stacksize = json_object_get_int64(
        json_object_object_get(argobots, "abt_thread_stacksize"));

    /* NOTE: we skip checking num_stacks; this cannot be retrieved with
     * ABT_info_query_config(). Fortunately it also is not as crucial as the
     * stack size.  Recent ABT releases have conservative caps on stack
     * cache sizes by default.
     */

    /* query Argobots to see if it is in agreement */
    ABT_info_query_config(ABT_INFO_QUERY_KIND_DEFAULT_THREAD_STACKSIZE,
                          &runtime_abt_thread_stacksize);
    if ((int)runtime_abt_thread_stacksize != abt_thread_stacksize) {
        MARGO_WARNING(
            0,
            "Margo requested an Argobots ULT stack size of %d, but "
            "Argobots is using a ULT stack size of %zd. "
            "If you initialized Argobots externally before calling "
            "margo_init(), please consider calling the margo_set_environment() "
            "function before ABT_init() in order to set preferred Argobots "
            "parameters for Margo usage. "
            "Margo is likely to encounter stack overflows and memory "
            "corruption if the Argobots stack size is not large "
            "enough to accomodate typical userspace network "
            "transport libraries.",
            abt_thread_stacksize, runtime_abt_thread_stacksize);
    }

    /* also simply report a few relevant compile-time parameters */

#ifdef HAVE_ABT_INFO_QUERY_KIND_ENABLED_LAZY_STACK_ALLOC
    ABT_info_query_config(ABT_INFO_QUERY_KIND_ENABLED_LAZY_STACK_ALLOC,
                          &config_bool);
    CONFIG_OVERRIDE_BOOL(argobots, "lazy_stack_alloc", config_bool,
                         "argobots.lazy_stack_alloc", 0);
#endif

    return;
}

static void set_argobots_environment_variables(struct json_object* config)
{
    int abt_mem_max_num_stacks = MARGO_DEFAULT_ABT_MEM_MAX_NUM_STACKS;
    int abt_thread_stacksize   = MARGO_DEFAULT_ABT_THREAD_STACKSIZE;

    /* handle cases in which config is not yet fully resolved */
    if (config) {
        struct json_object* argobots
            = json_object_object_get(config, "argobots");
        struct json_object* param;

        if (argobots) {
            if ((param
                 = json_object_object_get(argobots, "abt_mem_max_num_stacks")))
                abt_mem_max_num_stacks = json_object_get_int64(param);
            if ((param
                 = json_object_object_get(argobots, "abt_thread_stacksize")))
                abt_thread_stacksize = json_object_get_int64(param);
        }
    }

    margo_set_abt_mem_max_num_stacks(abt_mem_max_num_stacks);
    margo_set_abt_thread_stacksize(abt_thread_stacksize);

    return;
}

static void remote_shutdown_ult(hg_handle_t handle)
{
    margo_instance_id    mid = margo_hg_handle_get_instance(handle);
    margo_shutdown_out_t out;
    if (!(mid->enable_remote_shutdown)) {
        out.ret = -1;
    } else {
        out.ret = 0;
    }
    margo_respond(handle, &out);
    margo_destroy(handle);
    if (mid->enable_remote_shutdown) { margo_finalize(mid); }
}
static DEFINE_MARGO_RPC_HANDLER(remote_shutdown_ult)
