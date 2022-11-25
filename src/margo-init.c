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

// Validates the format of the configuration and
// fill default values if they are note provided
static bool validate_and_complete_config(struct json_object* _config,
                                         ABT_pool            _progress_pool,
                                         ABT_pool            _rpc_pool,
                                         const struct margo_monitor* _monitor);

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

    struct margo_hg hg
        = {HG_INIT_INFO_INITIALIZER, NULL, NULL, HG_ADDR_NULL, NULL, 0};
    struct margo_abt abt = {0};

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
    bool valide = validate_and_complete_config(config, args.progress_pool,
                                               args.rpc_pool, args.monitor);
    if (!valide) {
        MARGO_ERROR(0, "Could not validate configuration");
        goto error;
    }

    struct json_object* hg_config = json_object_object_get(config, "mercury");
    struct margo_hg_user_args hg_user_args = {.hg_class     = args.hg_class,
                                              .hg_context   = args.hg_context,
                                              .hg_init_info = args.hg_init_info,
                                              .listening    = mode,
                                              .protocol     = address};
    if (!margo_hg_init_from_json(hg_config, &hg_user_args, &hg)) { goto error; }

    struct json_object*   _jabt = json_object_object_get(config, "argobots");
    margo_abt_user_args_t abt_uargs = {
        .jprogress_pool    = json_object_object_get(_jabt, "progress_pool"),
        .jrpc_pool         = json_object_object_get(_jabt, "rpc_pool"),
        .jrpc_thread_count = json_object_object_get(_jabt, "rpc_thread_count"),
        .juse_progress_thread
        = json_object_object_get(_jabt, "use_progress_thread"),
        .progress_pool = args.progress_pool,
        .rpc_pool      = args.rpc_pool};
    if (!margo_abt_init_from_json(_jabt, &abt_uargs, &abt)) { goto error; }
    confirm_argobots_configuration(config);

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

    mid->abt_profiling_enabled = abt_profiling_enabled;

    mid->hg  = hg;
    mid->abt = abt;

    mid->progress_pool = mid->abt.pools[mid->abt.progress_pool_idx].pool;
    mid->rpc_pool      = mid->abt.pools[mid->abt.rpc_pool_idx].pool;

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

    return mid;

error:
    if (mid) {
        __margo_handle_cache_destroy(mid);
        __margo_timer_list_free(mid, mid->timer_list);
        ABT_mutex_free(&mid->finalize_mutex);
        ABT_cond_free(&mid->finalize_cond);
        ABT_mutex_free(&mid->pending_operations_mtx);
        if (mid->current_rpc_id_key) ABT_key_free(&(mid->current_rpc_id_key));
        free(mid);
    }
    if (config) json_object_put(config);
    margo_hg_destroy(&hg);
    margo_abt_destroy(&abt);
    return MARGO_INSTANCE_NULL;
}

/**
 * This function takes a margo configuration (parsed JSON tree), validates its
 * content, and complete/replace the content so that it can be used by
 * initialization functions with the knowledge that it contains correct and
 * complete information.
 */
static bool validate_and_complete_config(struct json_object* _margo,
                                         ABT_pool _custom_progress_pool,
                                         ABT_pool _custom_rpc_pool,
                                         const struct margo_monitor* _monitor)
{
    struct json_object* ignore; // to pass as output to macros when we don't
                                // care ouput the output
    struct json_object* val;

#define HANDLE_CONFIG_ERROR return false

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

    // validate Argobots configuration
    margo_abt_user_args_t abt_uargs = {
        .jprogress_pool    = json_object_object_get(_margo, "progress_pool"),
        .jrpc_pool         = json_object_object_get(_margo, "rpc_pool"),
        .jrpc_thread_count = json_object_object_get(_margo, "rpc_thread_count"),
        .juse_progress_thread
        = json_object_object_get(_margo, "use_progress_thread"),
        .progress_pool = _custom_progress_pool,
        .rpc_pool      = _custom_rpc_pool};
    struct json_object* _argobots = NULL;
    CONFIG_HAS_OR_CREATE_OBJECT(_margo, "argobots", "argobots", _argobots);
    if (!margo_abt_validate_json(_argobots, &abt_uargs)) { return false; }

    CONFIG_HAS_OR_CREATE(_argobots, int, "abt_thread_stacksize",
                         MARGO_DEFAULT_ABT_THREAD_STACKSIZE,
                         "argobots.abt_thread_stacksize", ignore);

    CONFIG_HAS_OR_CREATE(_argobots, int, "abt_mem_max_num_stacks",
                         MARGO_DEFAULT_ABT_MEM_MAX_NUM_STACKS,
                         "argobots.abt_mem_max_num_stacks", ignore);

    return true;
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
