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
#include "margo-diag-internal.h"
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
static int
validate_and_complete_config(struct json_object*        _config,
                             ABT_pool                   _progress_pool,
                             ABT_pool                   _rpc_pool,
                             hg_class_t*                _hg_class,
                             hg_context_t*              _hg_context,
                             const struct hg_init_info* _hg_init_info);

// Reads values from configuration to set the
// various fields in the hg_init_info structure
static void fill_hg_init_info_from_config(struct json_object*  _config,
                                          struct hg_init_info* _hg_init_info);

// Reads a pool configuration and instantiate the
// corresponding ABT_pool, returning ABT_SUCCESS
// or other ABT error codes
static int create_pool_from_config(struct json_object*    pool_config,
                                   struct margo_abt_pool* mpool);

// Reads an xstream configuration and instantiate
// the corresponding ABT_xstream, returning ABT_SUCCESS
// or other ABT error codes
static int create_xstream_from_config(struct json_object*          es_config,
                                      ABT_xstream*                 es,
                                      bool*                        own_xstream,
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

    hg_class_t*            hg_class      = NULL;
    hg_context_t*          hg_context    = NULL;
    uint8_t                hg_ownership  = 0;
    struct hg_init_info    hg_init_info  = HG_INIT_INFO_INITIALIZER;
    hg_addr_t              self_addr     = HG_ADDR_NULL;
    struct margo_abt_pool* pools         = NULL;
    size_t                 num_pools     = 0;
    ABT_xstream*           xstreams      = NULL;
    bool*                  owns_xstream  = NULL;
    size_t                 num_xstreams  = 0;
    ABT_pool               progress_pool = ABT_POOL_NULL;
    ABT_pool               rpc_pool      = ABT_POOL_NULL;
    ABT_bool               tool_enabled;
    char*                  name;

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
    ret = validate_and_complete_config(config, args.progress_pool,
                                       args.rpc_pool, args.hg_class,
                                       args.hg_context, args.hg_init_info);
    if (ret != 0) {
        MARGO_ERROR(0, "Could not validate and complete configuration");
        goto error;
    }

    // handle hg_init_info
    MARGO_TRACE(0, "Instantating hg_init_info structure");
    fill_hg_init_info_from_config(config, &hg_init_info);

    // handle hg_class
    if (args.hg_class) {
        MARGO_TRACE(0, "Using user-provided hg_class");
        hg_class = args.hg_class;
    } else if (args.hg_context) {
        MARGO_TRACE(0, "Using hg_class from provided hg_context");
        hg_class = HG_Context_get_class(hg_context);
    } else {
        MARGO_TRACE(0,
                    "Initializing hg_class from address \"%s\" and mode \"%s\"",
                    address, mode == 0 ? "client" : "server");
        hg_class = HG_Init_opt(address, mode, &hg_init_info);
        if (!hg_class) {
            MARGO_ERROR(0, "Could not initialize hg_class");
            goto error;
        }
        hg_ownership |= MARGO_OWNS_HG_CLASS;
    }

    // handle hg_context
    if (args.hg_context) {
        MARGO_TRACE(0, "Using user-provided hg_context");
        hg_context = args.hg_context;
    } else {
        MARGO_TRACE(0, "Initializing hg_context");
        hg_context = HG_Context_create(hg_class);
        if (!hg_context) {
            MARGO_ERROR(0, "Could not initialize hg_context");
            goto error;
        }
        hg_ownership |= MARGO_OWNS_HG_CONTEXT;
    }

    // updating config with address and listening fields
    MARGO_TRACE(0, "Updating configuration with mercury address");
    hret = HG_Addr_self(hg_class, &self_addr);
    if (hret != HG_SUCCESS) {
        MARGO_ERROR(0, "Could not get self address from hg_class (hret = %d)",
                    hret);
        goto error;
    }
    char      self_addr_str[1024];
    hg_size_t self_addr_str_size = 1024;
    hret = HG_Addr_to_string(hg_class, self_addr_str, &self_addr_str_size,
                             self_addr);
    if (hret != HG_SUCCESS) {
        MARGO_ERROR(0, "Could not convert self address to string (hret = %d)",
                    hret);
        goto error;
    }
    struct json_object* hg_config = json_object_object_get(config, "mercury");
    json_object_object_add(hg_config, "address",
                           json_object_new_string(self_addr_str));
    json_object_object_add(hg_config, "listening",
                           json_object_new_boolean(mode));

    // initialize Argobots if needed
    if (ABT_initialized() == ABT_ERR_UNINITIALIZED) {
        set_argobots_environment_variables(config);
        MARGO_TRACE(0, "Initializing Argobots");
        ret = ABT_init(0, NULL);
        if (ret != 0) goto error;
        g_margo_abt_init = 1;
        ret              = ABT_mutex_create(&g_margo_num_instances_mtx);
        if (ret != 0) goto error;
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
    num_pools = json_object_array_length(pools_config);
    pools     = calloc(sizeof(*pools), num_pools);
    if (!pools) {
        MARGO_ERROR(0, "Could not allocate pools array");
        goto error;
    }
    for (unsigned i = 0; i < num_pools; i++) {
        struct json_object* p = json_object_array_get_idx(pools_config, i);
        if (create_pool_from_config(p, &pools[i]) != ABT_SUCCESS) {
            goto error;
        }
    }

    // instantiate xstreams
    struct json_object* es_config
        = json_object_object_get(argobots_config, "xstreams");
    num_xstreams = json_object_array_length(es_config);
    xstreams     = calloc(sizeof(ABT_xstream), num_xstreams);
    owns_xstream = calloc(sizeof(*owns_xstream), num_xstreams);
    if (!xstreams || !owns_xstream) {
        MARGO_ERROR(0, "Could not allocate xstreams array or ownership array");
        goto error;
    }
    for (unsigned i = 0; i < num_xstreams; i++) {
        struct json_object* es = json_object_array_get_idx(es_config, i);
        if (create_xstream_from_config(es, &xstreams[i], &owns_xstream[i],
                                       pools, num_pools)
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
        progress_pool = pools[progress_pool_index].pool;

    // find rpc pool
    MARGO_TRACE(0, "Finding RPC pool");
    int rpc_pool_index
        = json_object_get_int64(json_object_object_get(config, "rpc_pool"));
    if (rpc_pool_index == -1)
        rpc_pool = args.rpc_pool;
    else
        rpc_pool = pools[rpc_pool_index].pool;

    // set input offset to include breadcrumb information in Mercury requests
    MARGO_TRACE(0, "Setting input offset in hg_class as %d", sizeof(uint64_t));
    hret = HG_Class_set_input_offset(hg_class, sizeof(uint64_t));
    if (hret != HG_SUCCESS) {
        MARGO_ERROR(0, "Could not set input offset in hg_class");
        goto error;
    }

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
    int diag_enabled = json_object_get_boolean(
        json_object_object_get(config, "enable_diagnostics"));
    int profile_enabled = json_object_get_boolean(
        json_object_object_get(config, "enable_profiling"));

    mid->json_cfg = config;

    mid->hg_class      = hg_class;
    mid->hg_context    = hg_context;
    mid->hg_ownership  = hg_ownership;
    mid->progress_pool = progress_pool;
    mid->rpc_pool      = rpc_pool;

    mid->abt_pools        = pools;
    mid->abt_xstreams     = xstreams;
    mid->num_abt_pools    = num_pools;
    mid->num_abt_xstreams = num_xstreams;
    mid->owns_abt_xstream = owns_xstream;

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

    margo_set_logger(mid, NULL);

    mid->shutdown_rpc_id = MARGO_REGISTER(
        mid, "__shutdown__", void, margo_shutdown_out_t, remote_shutdown_ult);

    MARGO_TRACE(0, "Starting progress loop");
    ret = ABT_thread_create(mid->progress_pool, __margo_hg_progress_fn, mid,
                            ABT_THREAD_ATTR_NULL, &mid->hg_progress_tid);
    if (ret != ABT_SUCCESS) goto error;

    /* TODO the initialization code bellow (until END) should probably be put in
     * a separate module that deals with diagnostics and profiling */

    /* register thread local key to track RPC breadcrumbs across threads */
    /* NOTE: we are registering a global key, even though init could be called
     * multiple times for different margo instances.  As of May 2019 this
     * doesn't seem to be a problem to call ABT_key_create() multiple times.
     */
    MARGO_TRACE(0, "Creating ABT keys for profiling");
    ret = ABT_key_create(free, &g_margo_rpc_breadcrumb_key);
    if (ret != ABT_SUCCESS) goto error;

    ret = ABT_key_create(free, &g_margo_target_timing_key);
    if (ret != ABT_SUCCESS) goto error;

    mid->sparkline_data_collection_tid = ABT_THREAD_NULL;
    mid->diag_enabled                  = diag_enabled;
    mid->profile_enabled               = profile_enabled;
    ABT_mutex_create(&mid->diag_rpc_mutex);

    // record own address hash to be used for profiling and diagnostics
    // NOTE: we do this even if profiling is presently disabled so that the
    // information will be available if profiling is dynamically enabled
    GET_SELF_ADDR_STR(mid, name);
    HASH_JEN(name, strlen(name), mid->self_addr_hash);
    free(name);

    if (profile_enabled) __margo_sparkline_thread_start(mid);

    /* END diagnostics/profiling initialization */

    // increment the number of margo instances
    if (g_margo_num_instances_mtx == ABT_MUTEX_NULL)
        ABT_mutex_create(&g_margo_num_instances_mtx);
    ABT_mutex_lock(g_margo_num_instances_mtx);
    g_margo_num_instances += 1;
    ABT_mutex_unlock(g_margo_num_instances_mtx);

finish:
    if (self_addr != HG_ADDR_NULL) HG_Addr_free(hg_class, self_addr);
    return mid;

error:
    if (mid) {
        __margo_handle_cache_destroy(mid);
        __margo_timer_list_free(mid, mid->timer_list);
        ABT_mutex_free(&mid->finalize_mutex);
        ABT_cond_free(&mid->finalize_cond);
        ABT_mutex_free(&mid->pending_operations_mtx);
        ABT_mutex_free(&mid->diag_rpc_mutex);
        free(mid);
    }
    for (unsigned i = 0; i < num_xstreams; i++) {
        if (xstreams[i] && owns_xstream[i]) {
            ABT_xstream_join(xstreams[i]);
            ABT_xstream_free(&xstreams[i]);
        }
    }
    free(xstreams);
    free(owns_xstream);
    // The pools are supposed to be freed automatically
    /*
    for (unsigned i = 0; i < num_pools; i++) {
        if (pools[i] != ABT_POOL_NULL) ABT_pool_free(&pools[i]);
    }
    */
    free(pools);
    if (config) json_object_put(config);
    if (self_addr != HG_ADDR_NULL) HG_Addr_free(hg_class, self_addr);
    self_addr = HG_ADDR_NULL;
    if (hg_context) HG_Context_destroy(hg_context);
    if (hg_class) HG_Finalize(hg_class);
    goto finish;
}

/**
 * This function takes a margo configuration (parsed JSON tree), validates its
 * content, and complete/replace the content so that it can be used by
 * initialization functions with the knowledge that it contains correct and
 * complete information.
 */
static int
validate_and_complete_config(struct json_object*        _margo,
                             ABT_pool                   _custom_progress_pool,
                             ABT_pool                   _custom_rpc_pool,
                             hg_class_t*                _hg_class,
                             hg_context_t*              _hg_context,
                             const struct hg_init_info* _hg_init_info)
{
    struct json_object* ignore; // to pass as output to macros when we don't
                                // care ouput the output
    struct json_object* val;

    // for convenience later
    if (_hg_context && !_hg_class)
        _hg_class = HG_Context_get_class(_hg_context);

    /* ------- Margo configuration ------ */
    /* Fields:
       - [required] mercury: object
       - [optional] argobots: object
       - [optional] progress_timeout_ub_msec: integer >= 0 (default 100)
       - [optional] enable_profiling: bool (default false)
       - [optional] enable_diagnostics: bool (default false)
       - [optional] handle_cache_size: integer >= 0 (default 32)
       - [optional] profile_sparkline_timeslice_msec: integer >= 0 (default
       1000)
       - [optional] use_progress_thread: bool (default false)
       - [optional] rpc_thread_count: integer (default 0)
    */

    /* report version number for this component */
    CONFIG_OVERRIDE_STRING(_margo, "version", PACKAGE_VERSION, "version", 1);

    { // add or override progress_timeout_ub_msec
        CONFIG_HAS_OR_CREATE(_margo, int64, "progress_timeout_ub_msec", 100,
                             "progress_timeout_ub_msec", val);
        CONFIG_INTEGER_MUST_BE_POSITIVE(_margo, "progress_timeout_ub_msec",
                                        "progress_timeout_ub_msec");
        MARGO_TRACE(0, "progress_timeout_ub_msec = %d",
                    json_object_get_int64(val));
    }

    { // add or override enable_profiling
        const char* margo_enable_profiling_str
            = getenv("MARGO_ENABLE_PROFILING");
        int margo_enable_profiling
            = margo_enable_profiling_str ? atoi(margo_enable_profiling_str) : 0;
        CONFIG_HAS_OR_CREATE(_margo, boolean, "enable_profiling",
                             margo_enable_profiling, "enable_profiling", val);
        MARGO_TRACE(0, "enable_profiling = %s",
                    json_object_get_boolean(val) ? "true" : "false");
    }

    { // add or override enable_diagnostics
        const char* margo_enable_diagnostics_str
            = getenv("MARGO_ENABLE_DIAGNOSTICS");
        int margo_enable_diagnostics = margo_enable_diagnostics_str
                                         ? atoi(margo_enable_diagnostics_str)
                                         : 0;
        CONFIG_HAS_OR_CREATE(_margo, boolean, "enable_diagnostics",
                             margo_enable_diagnostics, "enable_diagnostics",
                             val);
        MARGO_TRACE(0, "enable_diagnostics = %s",
                    json_object_get_boolean(val) ? "true" : "false");
    }

    { // add or override output_dir
        char* margo_output_dir_str = getenv("MARGO_OUTPUT_DIR");
        if (margo_output_dir_str) {
            CONFIG_HAS_OR_CREATE(_margo, string, "output_dir",
                                 margo_output_dir_str, "output_dir", val);
        } else {
            margo_output_dir_str = getcwd(NULL, 0);
            CONFIG_HAS_OR_CREATE(_margo, string, "output_dir",
                                 margo_output_dir_str, "output_dir", val);
            /* getwd() mallocs the string if buf is NULL */
            free(margo_output_dir_str);
        }

        MARGO_TRACE(0, "output_dir = %s", json_object_get_string(val));
    }

    { // add or override handle_cache_size
        CONFIG_HAS_OR_CREATE(_margo, int64, "handle_cache_size", 32,
                             "handle_cache_size", val);
        CONFIG_INTEGER_MUST_BE_POSITIVE(_margo, "handle_cache_size",
                                        "handle_cache_size");
        MARGO_TRACE(0, "handle_cache_size = %d", json_object_get_int64(val));
    }

    { // add or override profile_sparkline_timeslice_msec
        CONFIG_HAS_OR_CREATE(_margo, int64, "profile_sparkline_timeslice_msec",
                             1000, "profile_sparkline_timeslice_msec", val);
        CONFIG_INTEGER_MUST_BE_POSITIVE(_margo,
                                        "profile_sparkline_timeslice_msec",
                                        "profile_sparkline_timeslice_msec");
        MARGO_TRACE(0, "profile_sparkline_timeslice_msec = %d",
                    json_object_get_int64(val));
    }

    /* ------- Mercury configuration ------ */
    /* Fields:
       - [added] address: string
       - [added] listening: bool (optional if hg_context provided)
       - [optional] request_post_init: int (default 256)
       - [optional] request_post_incr: int (default 256)
       - [optional] auto_sm: bool (default false)
       - [optional] no_bulk_eager: bool (default false)
       - [optional] no_loopback: bool (default false)
       - [optional] stats: bool (default false)
       - [optional] na_no_block: bool (default false)
       - [optional] na_no_retry: bool (default false)
       - [optional] max_contexts: integer (default 1)
       - [optional] ip_subnet: string (added only if found in provided
       hg_init_info)
       - [optional] auth_key: auth_key (added only if found in provided
       hg_init_info)
       - [added]    version: string
    */

    /* find the "mercury" object in the configuration */
    struct json_object* _mercury = NULL;
    CONFIG_HAS_OR_CREATE_OBJECT(_margo, "mercury", "mercury", _mercury);

    { // add or override Mercury version
        char         hg_version_string[64];
        unsigned int hg_major = 0, hg_minor = 0, hg_patch = 0;
        HG_Version_get(&hg_major, &hg_minor, &hg_patch);
        snprintf(hg_version_string, 64, "%u.%u.%u", hg_major, hg_minor,
                 hg_patch);
        CONFIG_OVERRIDE_STRING(_mercury, "version", hg_version_string,
                               "mercury.version", 1);
        MARGO_TRACE(0, "mercury.version = %s", hg_version_string);
    }

    { // add mercury.request_post_incr or set it as default
        if (_hg_init_info) {
            hg_uint32_t request_post_incr = _hg_init_info->request_post_incr;
            CONFIG_OVERRIDE_INTEGER(_mercury, "request_post_incr",
                                    request_post_incr,
                                    "mercury.request_post_incr", 256);
        }
        CONFIG_HAS_OR_CREATE(_mercury, int, "request_post_incr", 256,
                             "mercury.request_post_incr", val);
        MARGO_TRACE(0, "mercury.request_post_incr = %d",
                    json_object_get_int(val));
    }

    { // add mercury.request_post_init or set it as default
        if (_hg_init_info) {
            hg_uint32_t request_post_init = _hg_init_info->request_post_init;
            CONFIG_OVERRIDE_INTEGER(_mercury, "request_post_init",
                                    request_post_init,
                                    "mercury.request_post_init", 256);
        }
        CONFIG_HAS_OR_CREATE(_mercury, int, "request_post_init", 256,
                             "mercury.request_post_init", val);
        MARGO_TRACE(0, "mercury.request_post_init = %d",
                    json_object_get_int(val));
    }

    { // add mercury.auto_sm or set it as default
        if (_hg_init_info)
            CONFIG_OVERRIDE_BOOL(_mercury, "auto_sm", _hg_init_info->auto_sm,
                                 "mercury.auto_sm", 1);
        CONFIG_HAS_OR_CREATE(_mercury, boolean, "auto_sm", 0, "mercury.auto_sm",
                             val);
        MARGO_TRACE(0, "mercury.auto_sm = %s",
                    json_object_get_boolean(val) ? "true" : "false");
    }

    { // add mercury.no_bulk_eager or set it as default
        if (_hg_init_info)
            CONFIG_OVERRIDE_BOOL(_mercury, "no_bulk_eager",
                                 _hg_init_info->no_bulk_eager,
                                 "mercury.no_bulk_eager", 1);
        CONFIG_HAS_OR_CREATE(_mercury, boolean, "no_bulk_eager", 0,
                             "mercury.no_bulk_eager", val);
        MARGO_TRACE(0, "mercury.no_bulk_eager = %s",
                    json_object_get_boolean(val) ? "true" : "false");
    }

    { // add mercury.no_loopback or set it as default
        if (_hg_init_info)
            CONFIG_OVERRIDE_BOOL(_mercury, "no_loopback",
                                 _hg_init_info->no_loopback,
                                 "mercury.no_loopback", 1);
        CONFIG_HAS_OR_CREATE(_mercury, boolean, "no_loopback", 0,
                             "mercury.no_loopback", val);
        MARGO_TRACE(0, "mercury.no_loopback = %s",
                    json_object_get_boolean(val) ? "true" : "false");
    }

    { // add mercury.stats or set it as default
        if (_hg_init_info)
            CONFIG_OVERRIDE_BOOL(_mercury, "stats", _hg_init_info->stats,
                                 "mercury.stats", 1);
        CONFIG_HAS_OR_CREATE(_mercury, boolean, "stats", 0, "mercury.stats",
                             val);
        MARGO_TRACE(0, "mercury.stats = %s",
                    json_object_get_boolean(val) ? "true" : "false");
    }

    { // add mercury.na_no_block or set it as default
        if (_hg_init_info) {
            bool na_no_block
                = _hg_init_info->na_init_info.progress_mode & NA_NO_BLOCK;
            CONFIG_OVERRIDE_BOOL(_mercury, "na_no_block", na_no_block,
                                 "mercury.na_no_block", 1);
        }
        CONFIG_HAS_OR_CREATE(_mercury, boolean, "na_no_block", 0,
                             "mercury.na_no_block", val);
        MARGO_TRACE(0, "mercury.na_no_block = %s",
                    json_object_get_boolean(val) ? "true" : "false");
    }

    { // add mercury.na_no_retry or set it as default
        if (_hg_init_info) {
            bool na_no_retry
                = _hg_init_info->na_init_info.progress_mode & NA_NO_RETRY;
            CONFIG_OVERRIDE_BOOL(_mercury, "na_no_retry", na_no_retry,
                                 "mercury.na_no_retry", 1);
        }
        CONFIG_HAS_OR_CREATE(_mercury, boolean, "na_no_retry", 0,
                             "mercury.na_no_retry", val);
        MARGO_TRACE(0, "mercury.na_no_retry = %s",
                    json_object_get_boolean(val) ? "true" : "false");
    }

    { // add mercury.max_contexts or set it as default
        if (_hg_init_info) {
            na_uint8_t max_contexts = _hg_init_info->na_init_info.max_contexts;
            CONFIG_OVERRIDE_INTEGER(_mercury, "max_contexts", max_contexts,
                                    "mercury.max_contexts", 1);
        }
        CONFIG_HAS_OR_CREATE(_mercury, int64, "max_contexts", 1,
                             "mercury.max_contexts", val);
        MARGO_TRACE(0, "mercury.max_contexts = %d", json_object_get_int64(val));
    }

    { // add mercury.ip_subnet to configuration if present _hg_init_info
        if (_hg_init_info && _hg_init_info->na_init_info.ip_subnet) {
            CONFIG_OVERRIDE_STRING(_mercury, "ip_subnet",
                                   _hg_init_info->na_init_info.ip_subnet,
                                   "mercury.ip_subnet", 1);
            MARGO_TRACE(0, "mercury.ip_subnet = %s",
                        _hg_init_info->na_init_info.ip_subnet);
        }
    }

    { // add mercury.auth_key to configuration if present in _hg_init_info
        if (_hg_init_info && _hg_init_info->na_init_info.auth_key) {
            CONFIG_OVERRIDE_STRING(_mercury, "auth_key",
                                   _hg_init_info->na_init_info.auth_key,
                                   "mercury.auth_key", 1);
            MARGO_TRACE(0, "mercury.auth_key = %s",
                        _hg_init_info->na_init_info.auth_key);
        }
    }

    /* ------- Argobots configuration ------ */
    /* Fields:
       - abt_mem_max_num_stacks: integer >= 0 (default
       MARGO_DEFAULT_ABT_MEM_MAX_NUM_STACKS)
       - abt_thread_stacksize: integer >= 0 (default
       MARGO_DEFAULT_ABT_THREAD_STACKSIZE)
       - pools: array
       - schedulers: array
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
                        return -1;
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
                return -1;
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
                        return -1;
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
                        return -1;
                    }
                    // replace the name with the index
                    json_object_array_put_idx(
                        _pool_refs, j, json_object_new_int64(_pool_ref_index));
                } else {
                    MARGO_ERROR(
                        0,
                        "Reference to pool should be an integer or a string");
                    return -1;
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
                        return -1;
                    }
                } else {
                    // invalid type for progress_pool field
                    MARGO_ERROR(0,
                                "\"progress_pool\" should be of type integer "
                                "or string");
                    return -1;
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
                        return -1;
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
                        return -1;
                    }
                } else {
                    // rpc_pool has invalid type
                    MARGO_ERROR(
                        0, "\"rpc_pool\" should be of type integer or string");
                    return -1;
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
                        return -1;
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

    return 0;
}

static void fill_hg_init_info_from_config(struct json_object*  config,
                                          struct hg_init_info* info)
{
    struct json_object* hg = json_object_object_get(config, "mercury");
    info->na_class         = NULL;
    info->request_post_init
        = json_object_get_int(json_object_object_get(hg, "request_post_init"));
    info->request_post_incr
        = json_object_get_int(json_object_object_get(hg, "request_post_incr"));
    info->auto_sm
        = json_object_get_boolean(json_object_object_get(hg, "auto_sm"));
    info->no_bulk_eager
        = json_object_get_boolean(json_object_object_get(hg, "no_bulk_eager"));
    info->no_loopback
        = json_object_get_boolean(json_object_object_get(hg, "no_loopback"));
    info->stats = json_object_get_boolean(json_object_object_get(hg, "stats"));
    struct json_object* ip_subnet = json_object_object_get(hg, "ip_subnet");
    info->na_init_info.ip_subnet
        = ip_subnet ? json_object_get_string(ip_subnet) : NULL;
    struct json_object* auth_key = json_object_object_get(hg, "auth_key");
    info->na_init_info.auth_key
        = auth_key ? json_object_get_string(auth_key) : NULL;
    info->na_init_info.progress_mode = 0;
    if (json_object_get_boolean(json_object_object_get(hg, "na_no_block")))
        info->na_init_info.progress_mode |= NA_NO_BLOCK;
    if (json_object_get_boolean(json_object_object_get(hg, "na_no_retry")))
        info->na_init_info.progress_mode |= NA_NO_RETRY;
    info->na_init_info.max_contexts
        = json_object_get_int64(json_object_object_get(hg, "max_contexts"));
}

static int create_pool_from_config(struct json_object*    pool_config,
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

    MARGO_TRACE(0, "Instantiating pool \"%s\"", jname);

    if (strcmp(jkind, "prio_wait") == 0) {
        margo_create_prio_pool_def(&prio_pool_def);
        ret = ABT_pool_create(&prio_pool_def, ABT_POOL_CONFIG_NULL,
                              &mpool->pool);
        if (ret != ABT_SUCCESS) {
            MARGO_ERROR(
                0, "ABT_pool_create failed to create prio_wait pool (ret = %d)",
                ret);
        }
        /* Note that for a custom pool like this, Margo is responsible for
         * free'ing it, but only if it is _not_ the primary pool.  See
         * https://lists.argobots.org/pipermail/discuss/2021-March/000109.html
         */
        if (strcmp(jname, "__primary__")) mpool->margo_free_flag = 1;
    } else {
        /* one of the standard Argobots pool types */
        ret = ABT_pool_create_basic(kind, access, ABT_TRUE, &mpool->pool);
        if (ret != ABT_SUCCESS) {
            MARGO_ERROR(
                0, "ABT_pool_create_basic failed to create pool (ret = %d)",
                ret);
        }
    }
    return ret;
}

static int create_xstream_from_config(struct json_object*          es_config,
                                      ABT_xstream*                 es,
                                      bool*                        owns_xstream,
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

    ABT_pool es_pools[es_num_pools];
    for (unsigned i = 0; i < es_num_pools; i++) {
        int pool_ref = json_object_get_int64(
            json_object_array_get_idx(es_pools_array, i));
        es_pools[i] = mpools[pool_ref].pool;
    }

    if (strcmp(es_name, "__primary__") == 0) {

        ret = ABT_xstream_self(es);
        if (ret != ABT_SUCCESS) {
            MARGO_ERROR(0, "ABT_xstream_self failed (ret = %d)", ret);
            return ret;
        }
        ABT_bool is_primary;
        ABT_xstream_is_primary(*es, &is_primary);
        if (!is_primary) {
            MARGO_WARNING(0, "margo_init_ext called from non-primary ES");
        }
        *owns_xstream = 0;
        ret = ABT_xstream_set_main_sched_basic(*es, predef, es_num_pools,
                                               es_pools);
        if (ret != ABT_SUCCESS) {
            MARGO_ERROR(0,
                        "ABT_xstream_set_main_sched_basic failed to set "
                        "scheduler (ret = %d)",
                        ret);
            return ret;
        }

    } else {

        ret = ABT_xstream_create_basic(predef, es_num_pools, es_pools,
                                       ABT_SCHED_CONFIG_NULL, es);
        if (ret != ABT_SUCCESS) {
            MARGO_ERROR(
                0, "ABT_xstream_create_basic failed to create pool (ref = %d)",
                ret);
            return ret;
        }
        *owns_xstream = 1;
    }

    // get/set cpubind
    if (es_cpubind == -1) {
        ret = ABT_xstream_get_cpubind(*es, &es_cpubind);
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
        ret = ABT_xstream_set_cpubind(*es, es_cpubind);
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
        ret = ABT_xstream_get_affinity(*es, 0, NULL, &num_cpus);
        if (ret == ABT_ERR_FEATURE_NA) {
            /* feature not supported in this Argobots build; skip */
        } else if (ret != ABT_SUCCESS) {
            MARGO_WARNING(
                0, "ABT_xstream_get_affinity failed to get affinity (ret = %d)",
                ret);
        } else if (num_cpus) {
            int cpuids[num_cpus];
            ABT_xstream_get_affinity(*es, num_cpus, cpuids, &num_cpus);
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
        ret = ABT_xstream_set_affinity(*es, num_cpus, cpuids);
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
    if (runtime_abt_thread_stacksize != abt_thread_stacksize) {
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

int margo_get_pool_by_name(margo_instance_id mid,
                           const char*       name,
                           ABT_pool*         pool)
{
    int index = margo_get_pool_index(mid, name);
    if (index >= 0) {
        return margo_get_pool_by_index(mid, index, pool);
    } else {
        return -1;
    }
}

int margo_get_pool_by_index(margo_instance_id mid,
                            unsigned          index,
                            ABT_pool*         pool)
{
    if (index >= mid->num_abt_pools) {
        *pool = ABT_POOL_NULL;
        return -1;
    } else {
        *pool = mid->abt_pools[index].pool;
    }
    return 0;
}

const char* margo_get_pool_name(margo_instance_id mid, unsigned index)
{
    if (index >= mid->num_abt_pools) {
        return NULL;
    } else {
        struct json_object* argobots
            = json_object_object_get(mid->json_cfg, "argobots");
        struct json_object* pool_array
            = json_object_object_get(argobots, "pools");
        struct json_object* pool = json_object_array_get_idx(pool_array, index);
        if (pool) {
            return json_object_get_string(json_object_object_get(pool, "name"));
        } else {
            return NULL;
        }
    }
}

int margo_get_pool_index(margo_instance_id mid, const char* name)
{
    int                 index;
    struct json_object* p = NULL;
    struct json_object* argobots
        = json_object_object_get(mid->json_cfg, "argobots");
    struct json_object* pool_array = json_object_object_get(argobots, "pools");
    CONFIG_FIND_BY_NAME(pool_array, name, index, p);
    (void)p; // silence warning about variable not used
    if (index >= 0) {
        return index;
    } else {
        return -1;
    }
}

size_t margo_get_num_pools(margo_instance_id mid) { return mid->num_abt_pools; }

int margo_get_xstream_by_name(margo_instance_id mid,
                              const char*       name,
                              ABT_xstream*      es)
{
    int index = margo_get_xstream_index(mid, name);
    if (index >= 0) {
        return margo_get_xstream_by_index(mid, index, es);
    } else {
        return -1;
    }
}

int margo_get_xstream_by_index(margo_instance_id mid,
                               unsigned          index,
                               ABT_xstream*      es)
{
    if (index >= mid->num_abt_xstreams) {
        *es = ABT_XSTREAM_NULL;
        return -1;
    } else {
        *es = mid->abt_xstreams[index];
    }
    return 0;
}

const char* margo_get_xstream_name(margo_instance_id mid, unsigned index)
{
    if (index >= mid->num_abt_xstreams) {
        return NULL;
    } else {
        struct json_object* argobots
            = json_object_object_get(mid->json_cfg, "argobots");
        struct json_object* es_array
            = json_object_object_get(argobots, "xstreams");
        struct json_object* es = json_object_array_get_idx(es_array, index);
        if (es) {
            return json_object_get_string(json_object_object_get(es, "name"));
        } else {
            return NULL;
        }
    }
}

int margo_get_xstream_index(margo_instance_id mid, const char* name)
{
    int                 index;
    struct json_object* e = NULL;
    struct json_object* argobots
        = json_object_object_get(mid->json_cfg, "argobots");
    struct json_object* es_array = json_object_object_get(argobots, "xstreams");
    CONFIG_FIND_BY_NAME(es_array, name, index, e);
    (void)e; // silence warning about variable not used
    return index;
}

size_t margo_get_num_xstreams(margo_instance_id mid)
{
    return mid->num_abt_xstreams;
}
