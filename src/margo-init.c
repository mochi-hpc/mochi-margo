/*
 * (C) 2020 The University of Chicago
 *
 * See COPYRIGHT in top-level directory.
 */
#include <stdbool.h>
#include <ctype.h>
#include <margo.h>
#include <margo-logging.h>
#include <jansson.h>
#include "margo-internal.h"
#include "margo-macros.h"

// Validates the format of the configuration and
// fill default values if they are note provided
static int validate_and_fill_config(
        json_t* _config,
        ABT_pool _progress_pool,
        ABT_pool _rpc_pool,
        hg_context_t* _hg_context,
        const struct hg_init_info* _hg_init_info);

// Considers a string of the form "key1/key2/ ... /keyN"
// and recursively search for the JSON fields.
// The separator (here /) should be specified by the caller.
static json_t* search_json_fields(
        json_t* root,
        const char* hierarchy,
        char separator);

margo_instance_id margo_init_internal(
        json_t* _config,
        ABT_pool _progress_pool,
        ABT_pool _rpc_pool,
        hg_context_t* _hg_context,
        const struct hg_init_info* _hg_init_info)
{
    if(!validate_and_fill_config(_config, _progress_pool, _rpc_pool, _hg_context, _hg_init_info))
        return MARGO_INSTANCE_NULL;
}

static json_t* search_json_fields(
        json_t* root,
        const char* hierarchy,
        char separator)
{
    char* fields = strdup(hierarchy);
    char* remaining_fields = fields;
    json_t* current_root = root;

    while(remaining_fields && current_root) {
        char* dot = strchr(remaining_fields, separator);
        char* current_field = remaining_fields;
        if(dot != NULL) *dot = '\0';
        remaining_fields = dot + 1;
        current_root = json_object_get(current_root, current_field);
    }

    free(fields);
    return current_root;
}

static int validate_and_fill_config(
        json_t* _margo,
        ABT_pool _progress_pool,
        ABT_pool _rpc_pool,
        hg_context_t* _hg_context,
        const struct hg_init_info* _hg_init_info)
{
    json_t* ignore; // to pass as output to macros when we don't care ouput the output

    /* ------- Margo configuration ------ */
    /* Fields:
       - [required] mercury: object
       - [optional] argobots: object
       - [optional] progress_timeout_ub_msec: integer >= 0 (default 100)
       - [optional] enable_profiling: bool (default false)
       - [optional] enable_diagnostics: bool (default false)
       - [optional] handle_cache_size: integer >= 0 (default 32)
       - [optional] profile_sparkline_timeslice_msec: integer >= 0 (default 1000)
       - [optional] use_progress_thread: bool (default false)
       - [optional] rpc_thread_count: integer (default 0)
    */
    {   // add or override progress_timeout_ub_msec
        CONFIG_HAS_OR_CREATE(_margo, integer, "progress_timeout_ub_msec", 100, "progress_timeout_ub_msec", ignore);
        CONFIG_INTEGER_MUST_BE_POSITIVE(_margo, "progress_timeout_ub_msec", "progress_timeout_ub_msec");
    }

    {   // add or override enable_profiling
        CONFIG_HAS_OR_CREATE(_margo, boolean, "enable_profiling", 0, "enable_profiling", ignore);
    }

    {   // add or override enable_diagnostics
        CONFIG_HAS_OR_CREATE(_margo, boolean, "enable_diagnostics", 0, "enable_diagnostics", ignore);
    }

    {   // add or override handle_cache_size
        CONFIG_HAS_OR_CREATE(_margo, integer, "handle_cache_size", 32, "handle_cache_size", ignore);
        CONFIG_INTEGER_MUST_BE_POSITIVE(_margo, "handle_cache_size", "handle_cache_size");
    }

    {   // add or override profile_sparkline_timeslice_msec
        CONFIG_HAS_OR_CREATE(_margo, integer, "profile_sparkline_timeslice_msec", 32, "profile_sparkline_timeslice_msec", ignore);
        CONFIG_INTEGER_MUST_BE_POSITIVE(_margo, "profile_sparkline_timeslice_msec", "profile_sparkline_timeslice_msec");
    }

    /* ------- Mercury configuration ------ */
    /* Fields:
       - [required] address: string
       - [required] listening: bool (optional if hg_context provided)
       - [optional] auto_sm: bool (default false)
       - [optional] stats: bool (default false)
       - [optional] na_no_block: bool (default false)
       - [optional] na_no_retry: bool (default false)
       - [optional] max_contexts: integer (default 1)
       - [optional] ip_subnet: string (added only if found in provided hg_init_info)
       - [optional] auth_key: auth_key (added only if found in provided hg_init_info)
       - [added]    version: string
    */

    /* find the "mercury" object in the configuration */
    json_t* _mercury = NULL;
    CONFIG_MUST_HAVE(_margo, object, "mercury", "mercury", _mercury);

    {   // must have an "address" field if hg_context is NULL
        if(!_hg_context)
            CONFIG_MUST_HAVE(_mercury, string, "address", "mercury.address", ignore);
    }

    {   // must have an "listening" boolean field if hg_context is NULL
        if(!_hg_context) {
            CONFIG_MUST_HAVE(_mercury, boolean, "listening", "mercury.listening", ignore);
        } else {
            bool is_listening = HG_Class_is_listening(_hg_context->hg_class);
            CONFIG_HAS_OR_CREATE(_mercury, boolean, "listening", is_listening, "mercury.listening", ignore);
        }
    }

    {   // add or override Mercury version
        char hg_version_string[64];
        unsigned int hg_major=0, hg_minor=0, hg_patch=0;
        HG_Version_get(&hg_major, &hg_minor, &hg_patch);
        snprintf(hg_version_string, 64, "%u.%u.%u", hg_major, hg_minor, hg_patch);
        CONFIG_OVERRIDE_STRING(_mercury, "version", hg_version_string, "mercury.version", 1);
    }

    {   // add mercury.auto_sm or set it as default
        if(_hg_init_info)
            CONFIG_OVERRIDE_BOOL(_mercury, "auto_sm", _hg_init_info->auto_sm, "mercury.auto_sm", 1);
        else
            CONFIG_HAS_OR_CREATE(_mercury, boolean, "auto_sm", 0, "mercury.auto_sm", ignore);
    }

    {   // add mercury.stats or set it as default
        if(_hg_init_info)
            CONFIG_OVERRIDE_BOOL(_mercury, "stats", _hg_init_info->stats, "mercury.stats", 1);
        else
            CONFIG_HAS_OR_CREATE(_mercury, boolean, "stats", 0, "mercury.stats", ignore);
    }
    
    {   // add mercury.na_no_block or set it as default
        if(_hg_init_info) {
            bool na_no_block = _hg_init_info->na_init_info.progress_mode & NA_NO_BLOCK;
            CONFIG_OVERRIDE_BOOL(_mercury, "na_no_block", na_no_block, "mercury.na_no_block", 1);
        } else
            CONFIG_HAS_OR_CREATE(_mercury, boolean, "na_no_block", 0, "mercury.na_no_block", ignore);
    }

    {   // add mercury.na_no_retry or set it as default
        if(_hg_init_info) {
            bool na_no_retry = _hg_init_info->na_init_info.progress_mode & NA_NO_RETRY;
            CONFIG_OVERRIDE_BOOL(_mercury, "na_no_retry", na_no_retry, "mercury.na_no_retry", 1);
        } else
            CONFIG_HAS_OR_CREATE(_mercury, boolean, "na_no_retry", 0, "mercury.na_no_retry", ignore);
    }

    {   // add mercury.max_contexts or set it as default
        if(_hg_init_info) {
            na_uint8_t max_contexts = _hg_init_info->na_init_info.max_contexts;
            CONFIG_OVERRIDE_INTEGER(_mercury, "max_contexts", max_contexts, "mercury.max_contexts", 1);
        } else
            CONFIG_HAS_OR_CREATE(_mercury, boolean, "max_contexts", 1, "mercury.max_contexts", ignore);
    }

    {   // add mercury.ip_subnet to configuration if present _hg_init_info
        if(_hg_init_info && _hg_init_info->na_init_info.ip_subnet)
            CONFIG_OVERRIDE_STRING(_mercury, "ip_subnet", _hg_init_info->na_init_info.ip_subnet, "mercury.ip_subnet", 1);
    }

    {   // add mercury.auth_key to configuration if present in _hg_init_info
        if(_hg_init_info && _hg_init_info->na_init_info.auth_key)
            CONFIG_OVERRIDE_STRING(_mercury, "auth_key", _hg_init_info->na_init_info.auth_key, "mercury.auth_key", 1);
    }

    /* ------- Argobots configuration ------ */
    /* Fields:
       - abt_mem_max_num_stacks: integer >= 0 (default 8)
       - abt_thread_stacksize: integer >= 0 (default 2097152)
       - pools: array
       - schedulers: array
       - xstreams: array
    */
    json_t* _argobots = NULL;
    CONFIG_HAS_OR_CREATE_OBJECT(_margo, "argobots", "argobots", _argobots);

    {   // handle abt_mem_max_num_stacks
        const char* abt_mem_max_num_stacks_str = getenv("ABT_MEM_MAX_NUM_STACKS");
        int abt_mem_max_num_stacks = abt_mem_max_num_stacks_str ? atoi(abt_mem_max_num_stacks_str) : 8;
        if(abt_mem_max_num_stacks_str) {
            CONFIG_OVERRIDE_INTEGER(_argobots, "abt_mem_max_num_stacks", abt_mem_max_num_stacks, "argobots.abt_mem_max_num_stacks", 1);
        } else {
            CONFIG_HAS_OR_CREATE(_argobots, integer, "abt_mem_max_num_stacks", abt_mem_max_num_stacks, "argobots.abt_mem_max_num_stacks", ignore);
        }
    }

    {   // handle abt_thread_stacksize
        const char* abt_thread_stacksize_str = getenv("ABT_THREAD_STACKSIZE");
        int abt_thread_stacksize = abt_thread_stacksize_str ? atoi(abt_thread_stacksize_str) : 8;
        if(abt_thread_stacksize_str) {
            CONFIG_OVERRIDE_INTEGER(_argobots, "abt_thread_stacksize", abt_thread_stacksize, "argobots.abt_thread_stacksize", 1);
        } else {
            CONFIG_HAS_OR_CREATE(_argobots, integer, "abt_thread_stacksize", abt_thread_stacksize, "argobots.abt_thread_stacksize", ignore);
        }
    }

    {   // handle version
        CONFIG_OVERRIDE_STRING(_argobots, "version", ABT_VERSION, "argobots.version", 1);
    }

    /* ------- Argobots pools configuration ------- */
    /* Fields:
       - [optional] name: string (default generated)
       - [optional] kind: string (default "fifo_wait")
       - [optional] access: string (default "mpmc")
    */
    json_t* _pools = NULL;
    CONFIG_HAS_OR_CREATE_ARRAY(_argobots, "pools", "argobots.pools", _pools);
    unsigned num_custom_pools = json_array_size(_pools);
    {   // check each pool in the array of pools
        json_t* _pool = NULL;
        unsigned i = 0;
        json_array_foreach(_pools, i, _pool) {
            // handle "name" field
            char default_name[64];
            snprintf(default_name, 64, "__pool_%d__", i);
            CONFIG_HAS_OR_CREATE(_pool, string, "name", default_name, "argobots.pools[?].name", ignore);
            // check that the name is authorized
            CONFIG_NAME_IS_VALID(_pool);
            // handle "kind" field
            CONFIG_HAS_OR_CREATE(_pool, string, "kind", "fifo_wait", "argobots.pools[?].kind", ignore);
            json_t* _kind = json_object_get(_pool, "kind");
            CONFIG_IS_IN_ENUM_STRING(_kind, "argobots.pools[?].kind", "fifo", "fifo_wait");
            // handle "access" field
            CONFIG_HAS_OR_CREATE(_pool, string, "access", "mpmc", "argobots.pools[?].access", ignore);
            json_t* _access = json_object_get(_pool, "access");
            CONFIG_IS_IN_ENUM_STRING(_access, "argobots.pools[?].access", "private", "spsc", "mpsc", "spmc", "mpmc");
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
                    [required] sched_predef: string
                    [required] pools: array of integers os trings
    */
    json_t* _xstreams = NULL;
    CONFIG_HAS_OR_CREATE_ARRAY(_argobots, "xstreams", "argobots.xstreams", _xstreams);
    {
        bool primary_found = 0;
        json_t* _xstream = NULL;
        unsigned i = 0;
        json_array_foreach(_xstreams, i, _xstream) {
            // handle "name" field
            char default_name[64];
            snprintf(default_name, 64, "__xstream_%d__", i);
            CONFIG_HAS_OR_CREATE(_xstream, string, "name", default_name, "argobots.xstreams[?].name", ignore);
            // check that the name is authorized
            CONFIG_NAME_IS_VALID(_xstream);
            // handle cpubind entry
            CONFIG_HAS_OR_CREATE(_xstream, integer, "cpubind", -1, "argobots.xstreams[?].cpubind", ignore);
            // handle affinity
            json_t* _affinity = json_object_get(_xstream, "affinity");
            if(!_affinity) {
                json_object_set_new(_xstream, "affinity", json_array());
            } else if(json_is_array(_affinity)) {
                for(unsigned j = 0; j < json_array_size(_affinity); j++) {
                    if(!json_is_integer(json_array_get(_affinity, j))) {
                        fprintf(stderr, "ERROR: invalid element type found in affinity array (should be integers) "
                                "in argobots.xstreams[?].affinity\n");
                        return -1;
                    }
                }
            } else {
                fprintf(stderr, "ERROR: invalid type for argobots.xstreams[?].affinity (should be array of integers)\n");
                return -1;
            }
            // find "scheduler" entry
            json_t* _sched = NULL;
            json_t* _sched_type = NULL;
            json_t* _pool_refs = NULL;
            CONFIG_MUST_HAVE(_xstream, object, "scheduler", "argobots.xstreams[?].scheduler", _sched);
            CONFIG_MUST_HAVE(_sched, string, "type", "argobots.xstreams[?].scheduler.type", _sched_type);
            CONFIG_IS_IN_ENUM_STRING(_sched_type, "argobots.xstreams[?].scheduler.type", "default", "basic", "prio", "randws", "basic_wait");
            CONFIG_MUST_HAVE(_sched, array, "pools", "argobots.xstreams[?].scheduler.pools", _pool_refs);
            // pools array must not be empty
            size_t num_pool_refs = json_array_size(_pool_refs);
            if(num_pool_refs == 0) {
                fprintf(stderr, "ERROR: argobots.schedulers[?].pools should not be an empty array\n");
                return -1;
            }
            // check that all the pool references refer to a known pool
            unsigned j;
            json_t* _pool_ref = NULL;
            json_array_foreach(_pool_refs, j, _pool_ref) {
                if(json_is_integer(_pool_ref)) {
                    int _pool_ref_index = json_integer_value(_pool_ref);
                    if(_pool_ref_index < 0 || _pool_ref_index >= num_custom_pools) {
                        fprintf(stderr, "ERROR: invalid pool index %d in argobots.schedulers[?].pools\n", _pool_ref_index);
                        return -1;
                    }
                } else if(json_is_string(_pool_ref)) {
                    int _pool_ref_index;
                    const char* _pool_ref_name = json_string_value(_pool_ref);
                    CONFIG_FIND_BY_NAME(_pools, _pool_ref_name, _pool_ref_index, ignore);
                    if(_pool_ref_index == -1) {
                        fprintf(stderr, "ERROR: invalid pool name \"%s\" in argobots.schedulers[?].pools\n", _pool_ref_name);
                        return -1;
                    }
                    // replace the name with the index
                    json_array_set_new(_pool_refs, j, json_integer(_pool_ref_index));
                } else {
                    fprintf(stderr, "ERROR: in argobots.schedulers[?].pools, pool reference should be an integer or a string\n");
                    return -1;
                }
            }
        }
        // check that the names of xstreams are unique
        CONFIG_NAMES_MUST_BE_UNIQUE(_xstreams, "argobots.xstreams");
        // if there is no __primary__ xstream, create one, along with its scheduler and pool
        {
            int xstream_index, pool_index;
            CONFIG_FIND_BY_NAME(_xstreams, "__primary__", xstream_index, ignore);
            if(xstream_index == -1) { // need to create __primary__ xstream entry
                CONFIG_FIND_BY_NAME(_pools, "__primary__", pool_index, ignore);
                if(pool_index == -1) { // need to create __primary__ pool entry
                    CONFIG_ADD_NEW_POOL(_pools, "__primary__", "fifo_wait", "mpmc");
                }
                pool_index = json_array_size(_pools)-1;
                CONFIG_ADD_NEW_XSTREAM(_xstreams, "__primary__", "basic_wait", pool_index);
            }
        }
    }

    /* ------- Margo configuration (cont'd) ------ */

    {   // handle progress_pool and use_progress_thread fields and _progress_pool argument
       
        if(_progress_pool != ABT_POOL_NULL) { // custom pool provided as argument

            // -1 is used to indicate that progress_pool is provided by the user
            CONFIG_OVERRIDE_INTEGER(_margo, "progress_pool", -1, "progress_pool", 1);
            if(CONFIG_HAS(_margo, "use_progress_thread", ignore)) {
                fprintf(stderr, "WARNING: ignoring \"use_progress_thread\" because custom progress pool was provided\n");
            }

        } else { // no custom pool provided as argument

            json_t* _progress_pool = NULL;
            if(CONFIG_HAS(_margo, "progress_pool", _progress_pool)) {

                if(CONFIG_HAS(_margo, "use_progress_thread", ignore)) { // progress_pool and use_progress_thread both specified
                    fprintf(stderr, "WARNING: \"use_progress_thread\" ignored because \"progress_pool\" is specified\n");
                }
                int progress_pool_index = -1;
                if(json_is_string(_progress_pool)) {
                    const char* progress_pool_name = json_string_value(_progress_pool);
                    CONFIG_ARRAY_MUST_HAVE_ITEM_NAMED(_pools, progress_pool_name, "argobots.pools", ignore);
                    CONFIG_FIND_BY_NAME(_pools, progress_pool_name, progress_pool_index, ignore);
                } else if(json_is_integer(_progress_pool)) {
                    progress_pool_index = json_integer_value(_progress_pool);
                    if(progress_pool_index < 0 || progress_pool_index >= json_array_size(_pools)) {
                        fprintf(stderr, "ERROR: \"progress_pool\" value out of range\n");
                        return -1;
                    }
                } else {
                    fprintf(stderr, "ERROR: \"progress_pool\" should be of type integer or string\n");
                    return -1;
                }
                // update the progress_pool to an integer index
                json_object_set(_margo, "progress_pool", json_integer(progress_pool_index));
            } else {
                bool use_progress_thread = 0;
                if(CONFIG_HAS(_margo, "use_progress_thread", ignore)) { // use_progress_thread specified, progress_pool not specified
                    if(!json_is_boolean(json_object_get(_margo, "use_progress_thread"))) {
                        fprintf(stderr,"ERROR: \"use_progress_thread\" should be an boolean\n");
                        return -1;
                    }
                    use_progress_thread = json_boolean_value(json_object_get(_margo, "use_progress_thread"));
                }
                if(use_progress_thread) {
                    // create a specific pool, scheduler, and xstream for the progress loop
                    CONFIG_ADD_NEW_POOL(_pools, "__progress__", "fifo_wait", "mpmc");
                    int pool_index = json_array_size(_pools)-1;
                    CONFIG_ADD_NEW_XSTREAM(_xstreams, "__progress__", "basic_wait", pool_index);
                    json_object_set_new(_margo, "progress_pool", json_string("__progress__"));
                } else {
                    // use primary xstream's scheduler's first pool for the progress loop
                    json_t* _primary_xstream = NULL;
                    json_t* _primary_sched = NULL;
                    int _primary_xstream_index = -1;
                    // find __primary__ xstream
                    CONFIG_FIND_BY_NAME(_xstreams, "__primary__", _primary_xstream_index, _primary_xstream);
                    // find its scheduler
                    _primary_sched = json_object_get(_primary_xstream, "scheduler");
                    // find the scheduler's first pool
                    json_t* _primary_pool_refs = json_object_get(_primary_sched, "pools");
                    json_t* _first_pool_ref = json_array_get(_primary_pool_refs, 0);
                    // set "progress_pool" to the pool's reference 
                    json_object_set_new(_margo, "progress_pool", json_copy(_first_pool_ref));
                }
            }
        }
    }
    json_object_del(_margo, "use_progress_thread");

    {   // handle rpc_thread_count and rpc_pool
        
        if(_rpc_pool != ABT_POOL_NULL) { // custom pool provided as argument
            // -1 means user-provided pool
            CONFIG_OVERRIDE_INTEGER(_margo, "rpc_pool", -1, "rpc_pool", 1);
            if(CONFIG_HAS(_margo, "rpc_thread_count", ignore)) {
                fprintf(stderr, "WARNING: ignoring \"rpc_thread_count\" because custom RPC pool was provided\n");
            }

        } else { // no custom pool provided as argument
            
            json_t* _rpc_pool = NULL;
            if(CONFIG_HAS(_margo, "rpc_pool", _rpc_pool)) {
                if(CONFIG_HAS(_margo, "rpc_thread_count", ignore)) { // rpc_pool and rpc_thread_count both specified
                    fprintf(stderr, "WARNING: \"rpc_thread_count\" ignored because \"rpc_pool\" is specified\n");
                }
                int rpc_pool_index = -1;
                if(json_is_string(_rpc_pool)) {
                    const char* rpc_pool_name = json_string_value(_rpc_pool);
                    CONFIG_ARRAY_MUST_HAVE_ITEM_NAMED(_pools, rpc_pool_name, "argobots.pools", ignore);
                    CONFIG_FIND_BY_NAME(_pools, rpc_pool_name, rpc_pool_index, ignore);
                } else if(json_is_integer(_rpc_pool)) {
                    rpc_pool_index = json_integer_value(_rpc_pool);
                    if(rpc_pool_index < 0 || rpc_pool_index >= json_array_size(_pools)) {
                        fprintf(stderr, "ERROR: \"rpc_pool\" value out of range\n");
                        return -1;
                    }
                } else {
                    fprintf(stderr, "ERROR: \"rpc_pool\" should be of type integer or string\n");
                    return -1;
                }
                // update the rpc_pool to an integer index
                json_object_set(_margo, "rpc_pool", json_integer(rpc_pool_index));
            } else { // rpc_pool not specified, use rpc_thread_count instead
                int rpc_thread_count = 0;
                if(CONFIG_HAS(_margo, "rpc_thread_count", ignore)) { // rpc_thread_count specified
                    if(!json_is_integer(json_object_get(_margo, "rpc_thread_count"))) {
                        fprintf(stderr,"ERROR: \"rpc_thread_count\" should be an integer\n");
                        return -1;
                    }
                    rpc_thread_count = json_integer_value(json_object_get(_margo, "rpc_thread_count"));
                }
                if(rpc_thread_count < 0) { // use progress loop's pool
                    json_t* _progress_pool = json_object_get(_margo, "progress_pool");
                    json_t* _rpc_pool = json_copy(_progress_pool);
                    json_object_set_new(_margo, "rpc_pool", _rpc_pool);
                } else if(rpc_thread_count == 0) { // use primary pool
                    // use primary xstream's scheduler's first pool for the RPC loop
                    json_t* _primary_xstream = NULL;
                    json_t* _primary_sched = NULL;
                    json_t* _primary_pool = NULL;
                    int _primary_xstream_index = -1;
                    int _primary_pool_index = -1;
                    // find __primary__ xstream
                    CONFIG_FIND_BY_NAME(_xstreams, "__primary__", _primary_xstream_index, _primary_xstream);
                    // find its scheduler
                    _primary_sched = json_object_get(_primary_xstream, "scheduler");
                    // find the scheduler's first pool
                    json_t* _primary_pool_refs = json_object_get(_primary_sched, "pools");
                    json_t* _first_pool_ref = json_array_get(_primary_pool_refs, 0);
                    // set "rpc_pool"
                    json_object_set_new(_margo, "rpc_pool", json_copy(_first_pool_ref));

                } else { // define a new pool and some new xstreams
                    CONFIG_ADD_NEW_POOL(_pools, "__rpc__", "fifo_wait", "mpmc");
                    int pool_index = json_array_size(_pools)-1;
                    for(unsigned i = 0; i < rpc_thread_count; i++) {
                        char name[64];
                        snprintf(name, 64, "__rpc_%d__", i);
                        CONFIG_ADD_NEW_XSTREAM(_xstreams, name, "basic_wait", pool_index);
                    }
                    json_object_set_new(_margo, "rpc_pool", json_integer(pool_index));
                }
            }
        }
    }
    json_object_del(_margo, "rpc_thread_count");

    return 0;
}
