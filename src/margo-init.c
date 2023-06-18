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
static bool __margo_validate_json(struct json_object*           _config,
                                  const char*                   address,
                                  int                           mode,
                                  const struct margo_init_info* uargs);

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
    margo_trace(0, "Validating JSON configuration");
    if (!__margo_validate_json(config, address, mode, &args)) goto error;

    margo_trace(0, "Initializing Mercury");
    struct json_object* hg_config = json_object_object_get(config, "mercury");
    struct margo_hg_user_args hg_user_args = {.hg_class     = args.hg_class,
                                              .hg_context   = args.hg_context,
                                              .hg_init_info = args.hg_init_info,
                                              .listening    = mode,
                                              .protocol     = address};
    if (!__margo_hg_init_from_json(hg_config, &hg_user_args, &hg)) goto error;

    margo_trace(0, "Initializing Argobots");
    struct json_object* abt_config = json_object_object_get(config, "argobots");
    if (!__margo_abt_init_from_json(abt_config, &abt)) goto error;

    // configm the environment variables are appropriate for Argobots
    confirm_argobots_configuration(config);

    int primary_pool_idx = __margo_abt_find_pool_by_name(&abt, "__primary__");
    if (primary_pool_idx < 0) {
        margo_error(0,
                    "Could not find __primary__ pool after "
                    "initialization from configuration");
        goto error;
    }
    ABT_pool primary_pool = abt.pools[primary_pool_idx].pool;

    // initialize progress pool
    int progress_pool_idx = -1;
    {
        struct json_object* jprogress_pool
            = json_object_object_get(config, "progress_pool");
        struct json_object* juse_progress_thread
            = json_object_object_get(config, "use_progress_thread");

        if (primary_pool != ABT_POOL_NULL
            && args.progress_pool == primary_pool) {
            /* external progress_pool specified and corresponds to the primary
             * pool
             */
            progress_pool_idx = primary_pool_idx;
        } else if (args.progress_pool != ABT_POOL_NULL
                   && args.progress_pool != NULL) {
            /* external progress pool specified and not primary, add it */
            progress_pool_idx = (int)abt.pools_len;
            if (!__margo_abt_add_external_pool(&abt, NULL, args.progress_pool))
                goto error;
        } else if (jprogress_pool) {
            /* progress_pool specified in JSON, find it by index or by name */
            if (json_object_is_type(jprogress_pool, json_type_int)) {
                progress_pool_idx = json_object_get_int(jprogress_pool);
            } else { /* it's a string */
                progress_pool_idx = __margo_abt_find_pool_by_name(
                    &abt, json_object_get_string(jprogress_pool));
            }

        } else if (juse_progress_thread
                   && json_object_get_boolean(juse_progress_thread)) {
            /* use_progress_thread specified and true, add a progress ES
             * with its own progress pool */
            jprogress_pool = json_object_new_object();
            json_object_object_add(jprogress_pool, "access",
                                   json_object_new_string("mpmc"));
            progress_pool_idx = abt.pools_len;
            ret = __margo_abt_add_pool_from_json(&abt, jprogress_pool);
            json_object_put(jprogress_pool);
            if (!ret) goto error;
            /* add a proress ES */
            json_object_t* jprogress_xstream = json_object_new_object();
            json_object_t* jprogress_sched   = json_object_new_object();
            json_object_object_add(jprogress_xstream, "scheduler",
                                   jprogress_sched);
            json_object_t* jprogress_xstream_pools
                = json_object_new_array_ext(1);
            json_object_object_add(jprogress_sched, "pools",
                                   jprogress_xstream_pools);
            json_object_array_add(jprogress_xstream_pools,
                                  json_object_new_int(progress_pool_idx));
            ret = __margo_abt_add_xstream_from_json(&abt, jprogress_xstream);
            json_object_put(jprogress_xstream);
            if (!ret) goto error;

        } else {
            /* user_progress_thread is false or not defined, fall back to using
             * primary pool */
            progress_pool_idx = primary_pool_idx;
        }
    }

    // initialize RPC pool
    int rpc_pool_idx = -1;
    {
        struct json_object* jrpc_pool
            = json_object_object_get(config, "rpc_pool");
        struct json_object* jrpc_thread_count
            = json_object_object_get(config, "rpc_thread_count");

        if (primary_pool != ABT_POOL_NULL && args.rpc_pool == primary_pool) {
            /* external rpc_pool specified and corresponds to the primary pool
             */
            rpc_pool_idx = primary_pool_idx;

        } else if (args.progress_pool != ABT_POOL_NULL
                   && args.progress_pool != NULL
                   && args.rpc_pool == args.progress_pool) {
            /* external rpc_pool specified and corresponds to the primary pool
             */
            rpc_pool_idx = progress_pool_idx;

        } else if (args.rpc_pool != ABT_POOL_NULL && args.rpc_pool != NULL) {
            /* external RPC pool specified, add it as external */
            rpc_pool_idx = (int)abt.pools_len;
            ret = __margo_abt_add_external_pool(&abt, NULL, args.rpc_pool);
            if (!ret) goto error;

        } else if (jrpc_pool) {
            /* RPC pool specified in JSON, find it by index or by name */
            if (json_object_is_type(jrpc_pool, json_type_int)) {
                rpc_pool_idx = json_object_get_int(jrpc_pool);
            } else { /* it's a string */
                rpc_pool_idx = __margo_abt_find_pool_by_name(
                    &abt, json_object_get_string(jrpc_pool));
            }

        } else if (jrpc_thread_count
                   && json_object_get_int(jrpc_thread_count) < 0) {
            /* rpc_thread_count specified and < 0, RPC pool is progress pool */
            rpc_pool_idx = progress_pool_idx;

        } else if (jrpc_thread_count
                   && json_object_get_int(jrpc_thread_count) > 0) {
            /* rpc_thread_count specified and > 0, an RPC pool and some RPC ES
             * should be created. */
            jrpc_pool = json_object_new_object();
            json_object_object_add(jrpc_pool, "access",
                                   json_object_new_string("mpmc"));
            rpc_pool_idx = abt.pools_len;
            ret          = __margo_abt_add_pool_from_json(&abt, jrpc_pool);
            json_object_put(jrpc_pool);
            if (!ret) goto error;
            /* add a __rpc_X__ ESs */
            int num_rpc_es = json_object_get_int(jrpc_thread_count);
            for (int i = 0; i < num_rpc_es; i++) {
                json_object_t* jrpc_xstream = json_object_new_object();
                json_object_t* jrpc_sched   = json_object_new_object();
                json_object_object_add(jrpc_xstream, "scheduler", jrpc_sched);
                json_object_t* jrpc_xstream_pools
                    = json_object_new_array_ext(1);
                json_object_object_add(jrpc_sched, "pools", jrpc_xstream_pools);
                json_object_array_add(jrpc_xstream_pools,
                                      json_object_new_int(rpc_pool_idx));
                ret = __margo_abt_add_xstream_from_json(&abt, jrpc_xstream);
                json_object_put(jrpc_xstream);
                if (!ret) goto error;
            }

        } else {
            /* rpc_thread_count not specified or == 0, RPC pool is primary pool
             */
            rpc_pool_idx = primary_pool_idx;
        }
    }

    // allocate margo instance
    MARGO_TRACE(0, "Allocating margo instance");
    mid = calloc(1, sizeof(*mid));
    if (!mid) {
        MARGO_ERROR(0, "Could not allocate margo instance");
        goto error;
    }

    int progress_timeout_ub = json_object_object_get_int_or(
        config, "progress_timeout_ub_msec", 100);
    int handle_cache_size
        = json_object_object_get_int_or(config, "handle_cache_size", 32);
    int abt_profiling_enabled
        = json_object_object_get_bool_or(config, "enable_abt_profiling", false);

    mid->refcount = 0;

    mid->abt_profiling_enabled = abt_profiling_enabled;

    mid->hg      = hg;
    mid->abt     = abt;
    mid->abt.mid = mid;

    mid->progress_pool_idx = progress_pool_idx;
    mid->rpc_pool_idx      = rpc_pool_idx;

    mid->hg_progress_tid           = ABT_THREAD_NULL;
    mid->hg_progress_shutdown_flag = 0;
    mid->hg_progress_timeout_ub    = progress_timeout_ub;

    mid->num_registered_rpcs = 0;
    mid->registered_rpcs     = NULL;

    mid->finalize_flag     = false;
    mid->finalize_refcount = 0;
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

    mid->handle_cache_size = handle_cache_size;
    mid->free_handle_list  = NULL;
    mid->used_handle_hash  = NULL;
    hret                   = __margo_handle_cache_init(mid, handle_cache_size);
    if (hret != HG_SUCCESS) goto error;

    // create current_rpc_id_key ABT_key
    ret = ABT_key_create(NULL, &(mid->current_rpc_id_key));
    if (ret != ABT_SUCCESS) goto error;

    // set logger
    margo_set_logger(mid, args.logger);

    // set monitor
    if (args.monitor) {
        struct json_object* monitoring
            = json_object_object_get(config, "monitoring");
        struct json_object* monitoring_config = NULL;
        if (monitoring) {
            monitoring_config = json_object_object_get(monitoring, "config");
            json_object_get(monitoring_config);
        } else {
            monitoring_config = json_object_new_object();
        }

        mid->monitor = (struct margo_monitor*)malloc(sizeof(*(mid->monitor)));
        memcpy(mid->monitor, args.monitor, sizeof(*(mid->monitor)));
        if (mid->monitor->initialize)
            mid->monitor->uargs = mid->monitor->initialize(
                mid, mid->monitor->uargs, monitoring_config);

        json_object_put(monitoring_config);
    }

    /* start abt profiling if enabled */
    if (mid->abt_profiling_enabled)
        margo_start_abt_profiling(mid, ABTX_PROF_MODE_BASIC);

    mid->shutdown_rpc_id = MARGO_REGISTER(
        mid, "__shutdown__", void, margo_shutdown_out_t, remote_shutdown_ult);

    MARGO_TRACE(0, "Starting progress loop");
    ret = ABT_thread_create(MARGO_PROGRESS_POOL(mid), __margo_hg_progress_fn,
                            mid, ABT_THREAD_ATTR_NULL, &mid->hg_progress_tid);
    if (ret != ABT_SUCCESS) goto error;

finish:
    json_object_put(config);
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
    __margo_hg_destroy(&hg);
    __margo_abt_destroy(&abt);
    mid = MARGO_INSTANCE_NULL;
    goto finish;
}

/**
 * This function takes a margo configuration (parsed JSON tree), validates its
 * content, and complete/replace the content so that it can be used by
 * initialization functions with the knowledge that it contains correct and
 * complete information.
 */
static bool __margo_validate_json(struct json_object*           _margo,
                                  const char*                   address,
                                  int                           mode,
                                  const struct margo_init_info* uargs)
{
    struct json_object* ignore = NULL;

#define HANDLE_CONFIG_ERROR return false

    /* ------- Margo configuration ------ */
    /* Fields:
       - [required] mercury: object
       - [optional] argobots: object
       - [optional] progress_timeout_ub_msec: integer >= 0 (default 100)
       - [optional] handle_cache_size: integer >= 0 (default 32)
       - [optional] use_progress_thread: bool (default false)
       - [optional] rpc_thread_count: integer (default 0)
       - [optional] progress_pool: integer or string
       - [optional] rpc_pool: integer or string
       - [optional] monitoring: object
    */

    // check "mercury" configuration field
    struct json_object*  _mercury = json_object_object_get(_margo, "mercury");
    margo_hg_user_args_t hg_uargs = {.protocol     = address,
                                     .listening    = mode == MARGO_SERVER_MODE,
                                     .hg_init_info = uargs->hg_init_info,
                                     .hg_class     = uargs->hg_class,
                                     .hg_context   = uargs->hg_context};
    if (!__margo_hg_validate_json(_mercury, &hg_uargs)) { return false; }

    // check "argobots" configuration field
    struct json_object* _argobots = json_object_object_get(_margo, "argobots");
    if (!__margo_abt_validate_json(_argobots)) { return false; }

    // check "progress_timeout_ub_msec" field
    ASSERT_CONFIG_HAS_OPTIONAL(_margo, "progress_timeout_ub_msec", int,
                               "margo");
    if (CONFIG_HAS(_margo, "progress_timeout_ub_msec", ignore)) {
        CONFIG_INTEGER_MUST_BE_POSITIVE(_margo, "progress_timeout_ub_msec",
                                        "progress_timeout_ub_msec");
    }

    // check "handle_cache_size" field
    ASSERT_CONFIG_HAS_OPTIONAL(_margo, "handle_cache_size", int, "margo");
    if (CONFIG_HAS(_margo, "handle_cache_size", ignore)) {
        CONFIG_INTEGER_MUST_BE_POSITIVE(_margo, "handle_cache_size",
                                        "handle_cache_size");
    }

    // check "progress_pool"
    struct json_object* _progress_pool
        = json_object_object_get(_margo, "progress_pool");
    if (_progress_pool
        && !(json_object_is_type(_progress_pool, json_type_int)
             || json_object_is_type(_progress_pool, json_type_string))) {
        margo_error(0,
                    "\"progress_pool\" field in configuration "
                    "should be an integer or a string");
        HANDLE_CONFIG_ERROR;
    }

    // check that progress_pool is present, if provided */
    if (_progress_pool) {
        struct json_object* _pools = json_object_object_get(_argobots, "pools");
        if (json_object_is_type(_progress_pool, json_type_int64)) {
            /* progress_pool is an integer */
            int progress_pool_idx = json_object_get_int(_progress_pool);
            int num_pools = _pools ? json_object_array_length(_pools) : 0;
            if (progress_pool_idx < 0 || progress_pool_idx >= num_pools) {
                margo_error(0, "Invalid \"progress_pool\" index (%d)",
                            progress_pool_idx);
                HANDLE_CONFIG_ERROR;
            }
        } else {
            /* progress_pool is a string */
            const char* progress_pool_name
                = json_object_get_string(_progress_pool);
            if (strcmp(progress_pool_name, "__primary__") != 0)
                CONFIG_ARRAY_MUST_HAVE_ITEM_NAMED(_pools, progress_pool_name,
                                                  "argobots.pools", ignore);
        }
    }

    // check "use_progress_thread" field
    ASSERT_CONFIG_HAS_OPTIONAL(_margo, "use_progress_thread", boolean, "margo");
    struct json_object* _use_progress_thread
        = json_object_object_get(_margo, "use_progress_thread");
    bool has_external_progress_pool = (uargs->progress_pool != ABT_POOL_NULL)
                                   && (uargs->progress_pool != NULL);

    // throw some warnings if more than one of use_progres_thread,
    // progress_pool, or external progress pool are used.
    if (_use_progress_thread) {
        if (has_external_progress_pool) {
            margo_warning(0,
                          "\"use_progress_thread\" will be ignored"
                          " because external progress pool was provided");
        } else if (_progress_pool) {
            margo_warning(0,
                          "\"use_progress_thread\" will be ignored"
                          " because \"progress_pool\" field was specified");
        }
    }
    if (has_external_progress_pool && _progress_pool) {
        margo_warning(0,
                      "\"progress_pool\" will be ignored because"
                      " external progress pool was provided");
    }

    // check rpc_pool
    struct json_object* _rpc_pool = json_object_object_get(_margo, "rpc_pool");
    if (_rpc_pool
        && !(json_object_is_type(_rpc_pool, json_type_int)
             || json_object_is_type(_rpc_pool, json_type_string))) {
        margo_error(0,
                    "\"rpc_pool\" field in configuration "
                    "should be an integer or a string");
        HANDLE_CONFIG_ERROR;
    }

    // check that rpc_pool is present, if provided */
    if (_rpc_pool) {
        struct json_object* _pools = json_object_object_get(_argobots, "pools");
        if (json_object_is_type(_rpc_pool, json_type_int64)) {
            /* rpc_pool is an integer */
            int rpc_pool_idx = json_object_get_int(_rpc_pool);
            int num_pools    = _pools ? json_object_array_length(_pools) : 0;
            if (rpc_pool_idx < 0 || rpc_pool_idx >= num_pools) {
                margo_error(0, "Invalid \"rpc_pool\" index (%d)", rpc_pool_idx);
                HANDLE_CONFIG_ERROR;
            }
        } else {
            /* rpc_pool is a string */
            const char* rpc_pool_name = json_object_get_string(_rpc_pool);
            if (strcmp(rpc_pool_name, "__primary__") != 0)
                CONFIG_ARRAY_MUST_HAVE_ITEM_NAMED(_pools, rpc_pool_name,
                                                  "argobots.pools", ignore);
        }
    }

    // check rpc_thread_count
    ASSERT_CONFIG_HAS_OPTIONAL(_margo, "rpc_thread_count", int, "margo");
    struct json_object* _rpc_thread_count
        = json_object_object_get(_margo, "rpc_thread_count");
    bool has_external_rpc_pool
        = (uargs->rpc_pool != ABT_POOL_NULL) && (uargs->rpc_pool != NULL);

    // throw some warnings if more than of rpc_thread_count,
    // rpc_pool, or external rpc pool are used.
    if (_rpc_thread_count) {
        if (has_external_rpc_pool) {
            margo_warning(0,
                          "\"rpc_thread_count\" will be ignored"
                          " because external rpc pool was provided");
        } else if (_rpc_pool) {
            margo_warning(0,
                          "\"rpc_thread_count\" will be ignored"
                          " because \"rpc_pool\" field was specified");
        }
    }
    if (has_external_rpc_pool && _rpc_pool) {
        margo_warning(0,
                      "\"rpc_pool\" will be ignored because"
                      " external rpc pool was provided");
    }

    return true;
#undef HANDLE_CONFIG_ERROR
}

static void confirm_argobots_configuration(struct json_object* config)
{
    /* this function assumes that the json is already fully populated */
    size_t runtime_abt_thread_stacksize = 0;

    /* retrieve expected values according to Margo configuration */
    struct json_object* argobots = json_object_object_get(config, "argobots");
    struct json_object* jabt_thread_stacksize
        = json_object_object_get(argobots, "abt_thread_stacksize");
    int abt_thread_stacksize;
    if (jabt_thread_stacksize) {
        abt_thread_stacksize = json_object_get_int64(jabt_thread_stacksize);
    } else {
        return;
    }

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
