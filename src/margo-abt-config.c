/*
 * (C) 2022 The University of Chicago
 *
 * See COPYRIGHT in top-level directory.
 */
#include "margo-abt-config.h"

static inline char* generate_unused_pool_name(const margo_abt_t* abt);

static inline char* generate_unused_xstream_name(const margo_abt_t* abt);

/* ------ margo_abt_pool_* --------- */

bool __margo_abt_pool_validate_json(const json_object_t* jpool,
                                    const margo_abt_t*   abt)
{
    margo_instance_id mid = abt->mid;
    if (!jpool) return true;
    if (!json_object_is_type(jpool, json_type_object)) {
        margo_error(mid, "pool configuration must be of type object (found %s)",
                    json_type_to_name(json_object_get_type(jpool)));
        return false;
    }

#define HANDLE_CONFIG_ERROR return false

    /* default: "mpmc" for predef pools */
    ASSERT_CONFIG_HAS_OPTIONAL(jpool, "access", string, "pool");
    json_object_t* jaccess = json_object_object_get(jpool, "access");
    if (jaccess) {
        CONFIG_IS_IN_ENUM_STRING(jaccess, "pool access", "private", "spsc",
                                 "mpsc", "spmc", "mpmc");
    }

    /* default: "fifo_wait" */
    ASSERT_CONFIG_HAS_OPTIONAL(jpool, "kind", string, "pool");
    json_object_t* jkind = json_object_object_get(jpool, "kind");
    if (jkind) {
        CONFIG_IS_IN_ENUM_STRING(jkind, "pool kind", "fifo", "fifo_wait",
                                 "prio_wait", "external");
        if (strcmp(json_object_get_string(jkind), "external") == 0) {
            margo_error(mid,
                        "Pool is marked as external and "
                        "cannot be instantiated");
            return false;
        }
    }
    // TODO: support dlopen-ed pool definitions

    /* default: generated */
    ASSERT_CONFIG_HAS_OPTIONAL(jpool, "name", string, "pool");
    json_object_t* jname = json_object_object_get(jpool, "name");
    if (jname) {
        CONFIG_NAME_IS_VALID(jpool);
        if (strcmp(json_object_get_string(jname), "__primary__") == 0) {
            if (ABT_initialized() == ABT_SUCCESS) {
                margo_error(
                    mid,
                    "Defining a pool named \"__primary__\" is not"
                    " allowed when Argobots is initialized before Margo");
                return false;
            }
        }
    }

#undef HANDLE_CONFIG_ERROR

    return true;
}

bool __margo_abt_pool_init_from_json(const json_object_t* jpool,
                                     const margo_abt_t*   abt,
                                     margo_abt_pool_t*    pool)
{
    margo_instance_id mid    = abt->mid;
    ABT_pool_access   access = ABT_POOL_ACCESS_MPMC;
    ABT_pool_kind     kind   = (ABT_pool_kind)(-1);

    json_object_t* jname = json_object_object_get(jpool, "name");
    pool->name           = jname ? strdup(json_object_get_string(jname))
                                 : generate_unused_pool_name(abt);

    pool->kind
        = strdup(json_object_object_get_string_or(jpool, "kind", "fifo_wait"));
    if (strcmp(pool->kind, "fifo_wait") == 0)
        kind = ABT_POOL_FIFO_WAIT;
    else if (strcmp(pool->kind, "fifo") == 0)
        kind = ABT_POOL_FIFO;
#if ABT_NUMVERSION >= 20000000
    else if (strcmp(pool->kind, "randws") == 0)
        kind = ABT_POOL_RANDWS;
#endif

    json_object_t* jaccess = json_object_object_get(jpool, "access");
    if (jaccess) {
        pool->access = strdup(json_object_get_string(jaccess));
        if (strcmp(pool->access, "private") == 0)
            access = ABT_POOL_ACCESS_PRIV;
        else if (strcmp(pool->access, "mpmc") == 0)
            access = ABT_POOL_ACCESS_MPMC;
        else if (strcmp(pool->access, "spsc") == 0)
            access = ABT_POOL_ACCESS_SPSC;
        else if (strcmp(pool->access, "mpsc") == 0)
            access = ABT_POOL_ACCESS_MPSC;
        else if (strcmp(pool->access, "spmc") == 0)
            access = ABT_POOL_ACCESS_SPMC;
    }

    int ret;
    if (kind != (ABT_pool_kind)(-1)) {
        if (!pool->access) pool->access = strdup("mpmc");
        ret = ABT_pool_create_basic(kind, access, ABT_FALSE, &pool->pool);
        if (ret != ABT_SUCCESS) {
            margo_error(0, "ABT_pool_create_basic failed with error code %d",
                        ret);
        }
    } else if (strcmp(pool->kind, "prio_wait") == 0) {
        if (!pool->access) pool->access = strdup("mpmc");
        ABT_pool_def prio_pool_def;
        margo_create_prio_pool_def(&prio_pool_def);
        ret = ABT_pool_create(&prio_pool_def, ABT_POOL_CONFIG_NULL,
                              &pool->pool);
        if (ret != ABT_SUCCESS) {
            margo_error(mid, "ABT_pool_create failed with error code %d", ret);
        }
    } else {
        // custom pool definition, not supported for now
        margo_error(mid,
                    "Invalid pool kind \"%s\" "
                    "(custom pool definitions not yet supported)",
                    pool->kind);
        ret = ABT_ERR_INV_POOL_KIND;
    }

    if (ret != ABT_SUCCESS) {
        __margo_abt_pool_destroy(pool, abt);
        return false;
    }

    pool->margo_free_flag = true;

    return true;
}

bool __margo_abt_pool_init_external(const char*        name,
                                    ABT_pool           handle,
                                    const margo_abt_t* abt,
                                    margo_abt_pool_t*  p)
{
    memset(p, 0, sizeof(*p));
    p->name = name ? strdup(name) : generate_unused_pool_name(abt);
    p->pool = handle;
    p->kind = strdup("external");
    return true;
}

json_object_t* __margo_abt_pool_to_json(const margo_abt_pool_t* p)
{
    json_object_t* jpool = json_object_new_object();
    int flags = JSON_C_OBJECT_ADD_KEY_IS_NEW | JSON_C_OBJECT_ADD_CONSTANT_KEY;
    json_object_object_add_ex(jpool, "kind", json_object_new_string(p->kind),
                              flags);
    json_object_object_add_ex(jpool, "name", json_object_new_string(p->name),
                              flags);
    if (p->access)
        json_object_object_add_ex(jpool, "access",
                                  json_object_new_string(p->access), flags);
    return jpool;
}

void __margo_abt_pool_destroy(margo_abt_pool_t* p, const margo_abt_t* abt)
{
    free(p->kind);
    free(p->access);
    free((char*)p->name);
    if (p->margo_free_flag && !p->used_by_primary && p->pool != ABT_POOL_NULL
        && p->pool != NULL) {
        ABT_pool_free(&(p->pool));
    }
    memset(p, 0, sizeof(*p));
}

char* generate_unused_pool_name(const margo_abt_t* abt)
{
    unsigned i = abt->pools_len;
    unsigned j = 0;
    char     name[128];
    while (true) {
        snprintf(name, 128, "__pool_%d__", i);
        for (j = 0; j < abt->pools_len; j++) {
            if (strcmp(name, abt->pools[j].name) == 0) break;
        }
        if (j == abt->pools_len)
            return strdup(name);
        else
            i += 1;
    }
    return strdup("");
}

/* ------ margo_abt_sched_* --------- */

bool __margo_abt_sched_validate_json(const json_object_t* jsched,
                                     const margo_abt_t*   abt,
                                     const json_object_t* jextra_pool_array)
{
    margo_instance_id mid = abt->mid;
    if (!jsched) return true;
    if (!json_object_is_type(jsched, json_type_object)) {
        margo_error(mid,
                    "\"scheduler\" field in configuration must be an object");
        return false;
    }

#define HANDLE_CONFIG_ERROR return false

    ASSERT_CONFIG_HAS_OPTIONAL(jsched, "type", string, "scheduler");
    json_object_t* jtype = json_object_object_get(jsched, "type");
    if (jtype) {
        CONFIG_IS_IN_ENUM_STRING(jtype, "scheduler.type", "default", "basic",
                                 "prio", "randws", "basic_wait");
    }

    ASSERT_CONFIG_HAS_OPTIONAL(jsched, "pools", array, "scheduler");

    json_object_t* jsched_pools = json_object_object_get(jsched, "pools");
    size_t         sched_pool_array_len
        = jsched_pools ? json_object_array_length(jsched_pools) : 0;

    int num_pools_in_abt = abt ? abt->pools_len : 0;
    int num_pools_in_json
        = jextra_pool_array ? json_object_array_length(jextra_pool_array) : 0;

    int num_available_pools = num_pools_in_abt + num_pools_in_json;

#if ABT_NUMVERSION < 20000000
    if (sched_pool_array_len == 0) {
        margo_error(
            mid,
            "Argobots < 2.0 requires schedulers to have at least one pool");
        return false;
    }
#endif

    for (unsigned i = 0; i < sched_pool_array_len; i++) {
        json_object_t* jpool_ref = json_object_array_get_idx(jsched_pools, i);
        /* jpool_ref is an integer */
        if (json_object_is_type(jpool_ref, json_type_int64)) {
            int index = json_object_get_int(jpool_ref);
            if (index < 0 || index >= num_available_pools) {
                margo_error(
                    mid, "Invalid pool index (%d) in scheduler configuration",
                    index);
                return false;
            }
            /* jpool_ref is a string */
        } else if (json_object_is_type(jpool_ref, json_type_string)) {
            const char* pool_name = json_object_get_string(jpool_ref);
            int         j         = 0;
            bool        found     = false;
            if (abt)
                found = (__margo_abt_find_pool_by_name(abt, pool_name) >= 0);
            for (; (j < num_pools_in_json) && !found; j++) {
                json_object_t* p
                    = json_object_array_get_idx(jextra_pool_array, j);
                json_object_t* p_name = json_object_object_get(p, "name");
                found                 = (p_name
                         && strcmp(pool_name, json_object_get_string(p_name))
                                == 0);
            }
            if (!found) {
                margo_error(mid,
                            "Invalid reference to pool \"%s\" in scheduler "
                            "configuration",
                            pool_name);
                return false;
            }
            /* pool_ref is something else */
        } else {
            margo_error(mid,
                        "Invalid pool type in scheduler configuration "
                        "(expected integer or string)");
            return false;
        }
    }
    return true;
#undef HANDLE_CONFIG_ERROR
}

bool __margo_abt_sched_init_from_json(const json_object_t* jsched,
                                      const margo_abt_t*   abt,
                                      margo_abt_sched_t*   s,
                                      ABT_sched*           abt_sched)
{
    s->type = strdup(
        json_object_object_get_string_or(jsched, "type", "basic_wait"));

    ABT_sched_predef sched_predef = ABT_SCHED_DEFAULT;
    if (s->type) {
        if (strcmp(s->type, "default") == 0)
            sched_predef = ABT_SCHED_DEFAULT;
        else if (strcmp(s->type, "basic") == 0)
            sched_predef = ABT_SCHED_BASIC;
        else if (strcmp(s->type, "prio") == 0)
            sched_predef = ABT_SCHED_PRIO;
        else if (strcmp(s->type, "randws") == 0)
            sched_predef = ABT_SCHED_RANDWS;
        else if (strcmp(s->type, "basic_wait") == 0)
            sched_predef = ABT_SCHED_BASIC_WAIT;
    }
    // TODO add support for dynamically loaded sched definitions

    json_object_t* jpools     = json_object_object_get(jsched, "pools");
    size_t         jpools_len = jpools ? json_object_array_length(jpools) : 0;
    ABT_pool*      abt_pools  = malloc(jpools_len * sizeof(*abt_pools));
    for (unsigned i = 0; i < jpools_len; i++) {
        json_object_t* jpool    = json_object_array_get_idx(jpools, i);
        uint32_t       pool_idx = 0;
        if (json_object_is_type(jpool, json_type_int64)) {
            pool_idx = (uint32_t)json_object_get_int(jpool);
        } else {
            const char* pool_name = json_object_get_string(jpool);
            for (; pool_idx < abt->pools_len; ++pool_idx) {
                if (strcmp(abt->pools[pool_idx].name, pool_name) == 0) break;
            }
        }
        abt_pools[i] = abt->pools[pool_idx].pool;
    }

    int ret = ABT_sched_create_basic(sched_predef, jpools_len, abt_pools,
                                     ABT_SCHED_CONFIG_NULL, abt_sched);
    free(abt_pools);

    if (ret != ABT_SUCCESS) {
        margo_error(abt->mid,
                    "ABT_sched_create_basic failed with error code %d", ret);
        __margo_abt_sched_destroy(s);
        return false;
    }

    return true;
}

bool __margo_abt_sched_init_external(ABT_sched          sched,
                                     const margo_abt_t* abt,
                                     margo_abt_sched_t* s)
{
    s->type = strdup("external");

    int num_pools;
    int ret = ABT_sched_get_num_pools(sched, &num_pools);
    if (ret != ABT_SUCCESS) {
        margo_error(abt->mid,
                    "ABT_sched_get_num_pools failed with error code %d", ret);
        goto error;
    }

    ABT_pool* pools = alloca(num_pools * sizeof(ABT_pool));
    ret             = ABT_sched_get_pools(sched, num_pools, 0, pools);
    if (ret != ABT_SUCCESS) {
        margo_error(abt->mid, "ABT_sched_get_pools failed with error code %d",
                    ret);
        goto error;
    }

    for (int i = 0; i < num_pools; i++) {
        if (__margo_abt_find_pool_by_handle(abt, pools[i]) < 0) {
            margo_error(
                abt->mid,
                "A pool associated with this external ES is not registered");
            goto error;
        }
    }

    return true;

error:
    __margo_abt_sched_destroy(s);
    return false;
}

json_object_t* __margo_abt_sched_to_json(const margo_abt_sched_t* s,
                                         ABT_sched                abt_sched,
                                         const margo_abt_t*       abt,
                                         int                      options)
{
    json_object_t* json = json_object_new_object();
    int flags = JSON_C_OBJECT_ADD_KEY_IS_NEW | JSON_C_OBJECT_ADD_CONSTANT_KEY;
    json_object_object_add_ex(json, "type", json_object_new_string(s->type),
                              flags);
    int num_pools = 0;
    int ret       = ABT_sched_get_num_pools(abt_sched, &num_pools);

    json_object_t* jpools = json_object_new_array_ext(num_pools);
    json_object_object_add_ex(json, "pools", jpools, flags);

    if (ret != ABT_SUCCESS) {
        margo_error(abt->mid,
                    "ABT_sched_get_num_pools failed with error code %d"
                    " in __margo_abt_sched_to_json",
                    ret);
        goto finish;
    }

    for (uint32_t i = 0; i < (uint32_t)num_pools; i++) {
        ABT_pool pool = ABT_POOL_NULL;
        ret           = ABT_sched_get_pools(abt_sched, 1, i, &pool);
        if (ret != ABT_SUCCESS) {
            margo_error(abt->mid,
                        "ABT_sched_get_pools failed with error code %d"
                        " in __margo_abt_sched_to_json",
                        ret);
            continue;
        }
        int pool_index = __margo_abt_find_pool_by_handle(abt, pool);
        if (pool_index < 0) {
            margo_error(abt->mid,
                        "Could not find pool associated with scheduler"
                        " in __margo_abt_sched_to_json");
            continue;
        }
        if ((options & MARGO_CONFIG_HIDE_EXTERNAL)
            && (strcmp(abt->pools[pool_index].kind, "external") == 0))
            continue; // skip external pools if requested
        if (options & MARGO_CONFIG_USE_NAMES) {
            json_object_array_add(
                jpools, json_object_new_string(abt->pools[pool_index].name));
        } else {
            json_object_array_add(jpools, json_object_new_uint64(pool_index));
        }
    }
finish:
    return json;
}

void __margo_abt_sched_destroy(margo_abt_sched_t* s)
{
    free(s->type);
    memset(s, 0, sizeof(*s));
}

/* ------ margo_abt_xstream_* --------- */

bool __margo_abt_xstream_validate_json(const json_object_t* jxstream,
                                       const margo_abt_t*   abt,
                                       const json_object_t* jextra_pools_array)
{
    margo_instance_id mid = abt->mid;
#if ABT_NUMVERSION >= 20000000
    if (!jxstream) return true;
#else
    if (!json_object_is_type(jxstream, json_type_object)) {
        margo_error(
            mid, "xstream definition in configuration must be of type object");
        return false;
    }
#endif

#define HANDLE_CONFIG_ERROR return false

    ASSERT_CONFIG_HAS_OPTIONAL(jxstream, "name", string, "xstream");
    ASSERT_CONFIG_HAS_OPTIONAL(jxstream, "cpubind", int, "xstream");
    ASSERT_CONFIG_HAS_OPTIONAL(jxstream, "affinity", array, "xstream");

    json_object_t* jsched = json_object_object_get(jxstream, "scheduler");
    if (!__margo_abt_sched_validate_json(jsched, abt, jextra_pools_array)) {
        return false;
    }

#if ABT_NUMVERSION < 20000000
    if (!jsched) {
        margo_error(
            mid,
            "Argobots < 2.0 requires schedulers to have at least one pool, "
            "hence it requires a scheduler to be defined for all xstreams");
        return false;
    }
#endif

    json_object_t* jaffinity = json_object_object_get(jxstream, "affinity");
    if (jaffinity) {
        for (unsigned i = 0; i < json_object_array_length(jaffinity); i++) {
            json_object_t* value = json_object_array_get_idx(jaffinity, i);
            if (!json_object_is_type(value, json_type_int)) {
                margo_error(mid,
                            "Invalid type found in affinity array (expected "
                            "integer, found %s)",
                            json_type_to_name(json_object_get_type(value)));
                return false;
            }
        }
    }

    json_object_t* jname = json_object_object_get(jxstream, "name");
    if (jname) {
        CONFIG_NAME_IS_VALID(jxstream);
        const char* name = json_object_get_string(jname);
        if (strcmp(name, "__primary__") == 0) {
            if (ABT_initialized() == ABT_SUCCESS) {
                margo_error(
                    mid,
                    "Defining an xstream named \"__primary__\" is not"
                    " allowed when Argobots is initialized before Margo");
                return false;
            }
            if (!jsched) {
                margo_error(
                    mid, "__primary__ xstream requires a scheduler definition");
                return false;
            }
            json_object_t* jpools = json_object_object_get(jsched, "pools");
            if (!jpools || json_object_array_length(jpools) == 0) {
                margo_error(mid,
                            "__primary__ xstream requires scheduler to have at "
                            "least one pool");
                return false;
            }
        }
    }
    return true;
#undef HANDLE_CONFIG_ERROR
}

bool __margo_abt_xstream_init_from_json(const json_object_t* jxstream,
                                        const margo_abt_t*   abt,
                                        margo_abt_xstream_t* x)
{
    margo_instance_id mid    = abt->mid;
    bool              result = true;
    json_object_t*    jname  = json_object_object_get(jxstream, "name");
    if (jname)
        x->name = strdup(json_object_get_string(jname));
    else {
        x->name = generate_unused_xstream_name(abt);
    }

    int cpubind = json_object_object_get_int_or(jxstream, "cpubind", -1);

    int*           affinity     = NULL;
    int            affinity_len = 0;
    json_object_t* jaffinity    = json_object_object_get(jxstream, "affinity");
    if (jaffinity) {
        affinity_len = json_object_array_length(jaffinity);
        affinity     = malloc(sizeof(*affinity) * affinity_len);
        for (int i = 0; i < affinity_len; i++) {
            affinity[i]
                = json_object_get_int(json_object_array_get_idx(jaffinity, i));
        }
    }

    json_object_t* jsched    = json_object_object_get(jxstream, "scheduler");
    ABT_sched      abt_sched = ABT_SCHED_NULL;
    if (!__margo_abt_sched_init_from_json(jsched, abt, &(x->sched), &abt_sched))
        goto error;

    bool xstream_is_primary = strcmp(x->name, "__primary__") == 0;

    int ret;
    if (!xstream_is_primary) {
        /* not the primary ES, create a new ES */
        ret                = ABT_xstream_create(abt_sched, &(x->xstream));
        x->margo_free_flag = true;
    } else {
        /* primary ES, change its scheduler */
        ret = ABT_xstream_self(&x->xstream);
        if (ret != ABT_SUCCESS) {
            margo_error(mid,
                        "Failed to retrieve self xstream (ABT_xstream_self "
                        "returned %d) in __margo_abt_xstream_init_from_json",
                        ret);
            goto error;
        }
        ret = ABT_xstream_set_main_sched(x->xstream, abt_sched);
        if (ret != ABT_SUCCESS) {
            margo_error(mid,
                        "Failed to set the main scheduler for the primary ES"
                        " (ABT_xstream_set_main_sched returned %d)"
                        " in __margo_abt_xstream_init_from_json",
                        ret);
            goto error;
        }
    }

    int num_pools = 0;
    ret           = ABT_sched_get_num_pools(abt_sched, &num_pools);
    if (ret != ABT_SUCCESS) {
        margo_error(mid,
                    "Failed to get num pools from scheduler"
                    " (ABT_sched_get_num_pools returned %d)"
                    " in __margo_abt_xstream_init_from_json",
                    ret);
        goto error;
    }
    for (unsigned i = 0; i < (unsigned)num_pools; i++) {
        ABT_pool pool = ABT_POOL_NULL;
        ret           = ABT_sched_get_pools(abt_sched, 1, i, &pool);
        if (ret != ABT_SUCCESS) {
            margo_error(mid,
                        "Failed to get pool %d from scheduler"
                        " (ABT_sched_get_pools returned %d)"
                        " in __margo_abt_xstream_init_from_json",
                        i, ret);
            goto error;
        }
        int pool_idx = __margo_abt_find_pool_by_handle(abt, pool);
        if (pool_idx < 0) {
            margo_error(mid,
                        "Failed to find pool from ABT_pool handle"
                        " in __margo_abt_xstream_init_from_json");
            goto error;
        }
        margo_abt_pool_t* pool_entry
            = &((margo_abt_pool_t*)abt->pools)[pool_idx];
        pool_entry->num_xstreams += 1;
        if (xstream_is_primary) pool_entry->used_by_primary = true;
    }

    if (ret == ABT_SUCCESS) {
        if (affinity)
            ABT_xstream_set_affinity(x->xstream, affinity_len, affinity);
        if (cpubind >= 0) ABT_xstream_set_cpubind(x->xstream, cpubind);
    }

finish:
    free(affinity);
    return result;

error:
    result = false;
    __margo_abt_xstream_destroy(x, abt);
    goto finish;
}

bool __margo_abt_xstream_init_external(const char*          name,
                                       ABT_xstream          handle,
                                       const margo_abt_t*   abt,
                                       margo_abt_xstream_t* x)
{
    margo_instance_id mid = abt->mid;

    x->name    = name ? strdup(name) : generate_unused_xstream_name(abt);
    x->xstream = handle;
    x->margo_free_flag = false;

    ABT_sched abt_sched;
    int       ret = ABT_xstream_get_main_sched(handle, &abt_sched);
    if (ret != ABT_SUCCESS) {
        margo_error(mid,
                    "Failed to retrieve main scheduler from ES "
                    "(ABT_xstream_get_main_sched returned %d)"
                    " in __margo_abt_xstream_init_external",
                    ret);
        goto error;
    }

    if (!__margo_abt_sched_init_external(abt_sched, abt, &x->sched)) goto error;

    bool xstream_is_primary = strcmp(name, "__primary__") == 0;

    int num_pools = 0;
    ret           = ABT_sched_get_num_pools(abt_sched, &num_pools);
    if (ret != ABT_SUCCESS) {
        margo_error(mid,
                    "Failed to get the scheduler's number of pools "
                    "(ABT_sched_get_num_pools returned %d)"
                    " in __margo_abt_xstream_init_external",
                    ret);
        goto error;
    }
    for (unsigned i = 0; i < (unsigned)num_pools; i++) {
        ABT_pool pool = ABT_POOL_NULL;
        ret           = ABT_sched_get_pools(abt_sched, 1, i, &pool);
        if (ret != ABT_SUCCESS) {
            margo_error(mid,
                        "Failed to get pool %d from scheduler"
                        " (ABT_sched_get_pools returned %d)"
                        " in __margo_abt_xstream_init_external",
                        i, ret);
            goto error;
        }
        int pool_idx = __margo_abt_find_pool_by_handle(abt, pool);
        if (pool_idx < 0) {
            margo_error(mid,
                        "Failed to find pool from ABT_pool handle"
                        " in __margo_abt_xstream_init_external");
            goto error;
        }
        margo_abt_pool_t* pool_entry
            = &((margo_abt_pool_t*)abt->pools)[pool_idx];
        pool_entry->num_xstreams += 1;
        if (xstream_is_primary) pool_entry->used_by_primary = true;
    }

    return true;

error:
    __margo_abt_xstream_destroy(x, abt);
    return false;
}

json_object_t* __margo_abt_xstream_to_json(const margo_abt_xstream_t* x,
                                           const margo_abt_t*         abt,
                                           int                        options)
{
    json_object_t* jxstream  = json_object_new_object();
    ABT_sched      abt_sched = ABT_SCHED_NULL;

    int ret = ABT_xstream_get_main_sched(x->xstream, &abt_sched);
    if (ret != ABT_SUCCESS) {
        margo_error(abt->mid,
                    "Failed to get main scheduler from xstream"
                    " (ABT_xstream_get_main_sched returned %d)"
                    " in __margo_abt_xstream_to_json",
                    ret);
        return jxstream;
    }

    json_object_t* jsched
        = __margo_abt_sched_to_json(&(x->sched), abt_sched, abt, options);
    int flags = JSON_C_OBJECT_ADD_KEY_IS_NEW | JSON_C_OBJECT_ADD_CONSTANT_KEY;
    json_object_object_add_ex(jxstream, "scheduler", jsched, flags);

    json_object_object_add_ex(jxstream, "name", json_object_new_string(x->name),
                              flags);

    int cpuid;
    ret = ABT_xstream_get_cpubind(x->xstream, &cpuid);
    if (ret == ABT_SUCCESS) {
        json_object_object_add_ex(jxstream, "cpubind",
                                  json_object_new_int(cpuid), flags);
    }

    int num_cpus;
    ret = ABT_xstream_get_affinity(x->xstream, 0, NULL, &num_cpus);
    if (ret == ABT_SUCCESS) {
        int* cpuids = malloc(num_cpus * sizeof(*cpuids));
        ret = ABT_xstream_get_affinity(x->xstream, num_cpus, cpuids, &num_cpus);
        if (ret == ABT_SUCCESS) {
            json_object_t* jcpuids = json_object_new_array_ext(num_cpus);
            for (int i = 0; i < num_cpus; i++) {
                json_object_array_add(jcpuids,
                                      json_object_new_int64(cpuids[i]));
            }
            json_object_object_add_ex(jxstream, "affinity", jcpuids, flags);
        }
        free(cpuids);
    }
    return jxstream;
}

void __margo_abt_xstream_destroy(margo_abt_xstream_t* x, const margo_abt_t* abt)
{
    if (x->xstream != ABT_XSTREAM_NULL && x->xstream != NULL) {
        int       num_pools = 0;
        ABT_sched abt_sched = ABT_SCHED_NULL;
        int       ret = ABT_xstream_get_main_sched(x->xstream, &abt_sched);
        if (ret != ABT_SUCCESS) {
            margo_error(abt->mid,
                        "Failed to get scheduler from xstream"
                        " (ABT_xstream_get_main_sched returned %d)"
                        " in __margo_abt_xstream_destroy",
                        ret);
            goto step2;
        }
        ret = ABT_sched_get_num_pools(abt_sched, &num_pools);
        if (ret != ABT_SUCCESS) {
            margo_error(abt->mid,
                        "Failed to get the scheduler's number of pools "
                        "(ABT_sched_get_num_pools returned %d)"
                        " in __margo_abt_xstream_destroy",
                        ret);
            goto step2;
        }
        for (unsigned i = 0; i < (unsigned)num_pools; i++) {
            ABT_pool pool = ABT_POOL_NULL;
            ret           = ABT_sched_get_pools(abt_sched, 1, i, &pool);
            if (ret != ABT_SUCCESS) {
                margo_error(abt->mid,
                            "Failed to get pool %d from scheduler"
                            " (ABT_sched_get_pools returned %d)"
                            " in __margo_abt_xstream_destroy",
                            i, ret);
                goto step2;
            }
            int pool_idx = __margo_abt_find_pool_by_handle(abt, pool);
            if (pool_idx < 0) {
                margo_error(abt->mid,
                            "Failed to find pool from ABT_pool handle"
                            " in __margo_abt_xstream_destroy");
                goto step2;
            }
            margo_abt_pool_t* pool_entry
                = &((margo_abt_pool_t*)abt->pools)[pool_idx];
            pool_entry->num_xstreams -= 1;
        }
    }
step2:
    if (x->margo_free_flag && x->xstream != ABT_XSTREAM_NULL
        && x->xstream != NULL) {
        ABT_xstream_join(x->xstream);
        ABT_xstream_free(&x->xstream);
    }
    __margo_abt_sched_destroy(&(x->sched));
    free((char*)x->name);
    memset(x, 0, sizeof(*x));
}

static inline char* generate_unused_xstream_name(const margo_abt_t* abt)
{
    unsigned i = abt->xstreams_len;
    unsigned j = 0;
    char     name[128];
    while (true) {
        snprintf(name, 128, "__xstream_%d__", i);
        for (j = 0; j < abt->xstreams_len; j++) {
            if (strcmp(name, abt->xstreams[j].name) == 0) break;
        }
        if (j == abt->xstreams_len)
            return strdup(name);
        else
            i += 1;
    }
    return strdup("");
}

/* ------ margo_abt_* --------- */

static inline void check_abt_env_variables(const json_object* a)
{

    json_object_t* jabt_mem_max_num_stacks
        = json_object_object_get(a, "abt_mem_max_num_stacks");
    if (jabt_mem_max_num_stacks) {
        if (getenv("ABT_MEM_MAX_NUM_STACKS")) {
            long val = atol(getenv("ABT_MEM_MAX_NUM_STACKS"));
            if (val != json_object_get_int64(jabt_mem_max_num_stacks)) {
                margo_warning(
                    0,
                    "\"abt_mem_max_num_stacks\" will be ignored"
                    " because the ABT_MEM_MAX_NUM_STACKS environment variable"
                    " is defined");
            }
        } else if (ABT_initialized() == ABT_SUCCESS) {
            margo_warning(0,
                          "\"abt_mem_max_num_stacks\" will be ignored"
                          " because Argobots is already initialized");
        }
    }

    json_object_t* jabt_thread_stacksize
        = json_object_object_get(a, "abt_thread_stacksize");
    long abt_thread_stacksize = jabt_thread_stacksize
                                  ? json_object_get_int64(jabt_thread_stacksize)
                                  : MARGO_DEFAULT_ABT_THREAD_STACKSIZE;
    if (getenv("ABT_THREAD_STACKSIZE")) {
        long val = atol(getenv("ABT_THREAD_STACKSIZE"));
        if (val != abt_thread_stacksize) {
            margo_warning(
                0,
                "\"abt_thread_stacksize\" will be ignored"
                " because the ABT_THREAD_STACKSIZE environment variable"
                " is defined");
        }
    } else if (ABT_initialized() == ABT_SUCCESS) {
        margo_warning(0,
                      "\"abt_thread_stacksize\" will be ignored"
                      " because Argobots is already initialized");
    }
}

bool __margo_abt_validate_json(const json_object_t* a)
{
#define HANDLE_CONFIG_ERROR \
    result = false;         \
    goto finish

    if (!a) {
        check_abt_env_variables(a);
        return true;
    }
    if (!json_object_is_type(a, json_type_object)) {
        margo_error(0, "\"argobots\" field in configuration must be an object");
        return false;
    }

    margo_abt_t dummy_abt = {0};

    check_abt_env_variables(a);

    json_object_t* jpools = NULL;
    bool           result = true;
    ASSERT_CONFIG_HAS_OPTIONAL(a, "pools", array, "argobots");
    ASSERT_CONFIG_HAS_OPTIONAL(a, "xstreams", array, "argobots");
    ASSERT_CONFIG_HAS_OPTIONAL(a, "abt_mem_max_num_stacks", int, "argobots");
    ASSERT_CONFIG_HAS_OPTIONAL(a, "abt_thread_stacksize", int, "argobots");
    ASSERT_CONFIG_HAS_OPTIONAL(a, "profiling_dir", string, "argobots");

    /* validate the "pools" list */

    jpools        = json_object_object_get(a, "pools");
    int num_pools = 0;
    if (jpools) {
        json_object_get(jpools);
        num_pools = json_object_array_length(jpools);
        for (int i = 0; i < num_pools; ++i) {
            json_object_t* jpool = json_object_array_get_idx(jpools, i);
            if (!__margo_abt_pool_validate_json(jpool, &dummy_abt)) {
                margo_error(0, "^ in \"argobots.pools[%d]\"", i);
                HANDLE_CONFIG_ERROR;
            }
        }
        CONFIG_NAMES_MUST_BE_UNIQUE(jpools, "argobots.pools");
    } else {
        jpools = json_object_new_array_ext(0);
    }

    /* validate the list of xstreams */

    json_object_t* jxstreams = json_object_object_get(a, "xstreams");
    if (jxstreams) {
        size_t num_es = json_object_array_length(jxstreams);
        for (unsigned i = 0; i < num_es; ++i) {
            json_object_t* jxstream = json_object_array_get_idx(jxstreams, i);
            if (!__margo_abt_xstream_validate_json(jxstream, &dummy_abt,
                                                   jpools)) {
                margo_error(0, "^ in \"argobots.xstreams[%d]\"", i);
                HANDLE_CONFIG_ERROR;
            }
        }
        CONFIG_NAMES_MUST_BE_UNIQUE(jxstreams, "argobots.xstreams");
    }

finish:
    if (jpools) json_object_put(jpools);
    return result;

#undef HANDLE_CONFIG_ERROR
}

bool __margo_abt_init_from_json(const json_object_t* jabt, margo_abt_t* a)
{
    int            ret;
    bool           result         = true;
    bool           first_abt_init = false;
    json_object_t* jpools         = json_object_object_get(jabt, "pools");
    json_object_t* jxstreams      = json_object_object_get(jabt, "xstreams");

    /* handle ABT initialization */
    if (ABT_initialized() == ABT_ERR_UNINITIALIZED) {
        if (!getenv("ABT_THREAD_STACKSIZE")) {
            int abt_thread_stacksize = json_object_object_get_uint64_or(
                jabt, "abt_thread_stacksize",
                MARGO_DEFAULT_ABT_THREAD_STACKSIZE);
            char abt_thread_stacksize_str[128];
            sprintf(abt_thread_stacksize_str, "%d", abt_thread_stacksize);
            setenv("ABT_THREAD_STACKSIZE", abt_thread_stacksize_str, 1);
        }
        if (!getenv("ABT_MEM_MAX_NUM_STACKS")) {
            int abt_mem_max_num_stacks = json_object_object_get_uint64_or(
                jabt, "abt_mem_max_num_stacks",
                MARGO_DEFAULT_ABT_MEM_MAX_NUM_STACKS);
            char abt_mem_max_num_stacks_str[128];
            sprintf(abt_mem_max_num_stacks_str, "%d", abt_mem_max_num_stacks);
            setenv("ABT_MEM_MAX_NUM_STACKS", abt_mem_max_num_stacks_str, 1);
        }
        ret = ABT_init(0, NULL);
        if (ret != ABT_SUCCESS) {
            margo_error(a->mid,
                        "Failed to initialize Argobots (ABT_init returned %d)"
                        " in __margo_abt_init_from_json",
                        ret);
            result = false;
            goto error;
        }
        g_margo_abt_init = 1;
        first_abt_init   = true;
    }
    g_margo_num_instances++;

    /* Turn on profiling capability if a) it has not been done already (this
     * is global to Argobots) and b) the argobots tool interface is enabled.
     */
    if (!g_margo_abt_prof_init) {
        ABT_bool tool_enabled;
        ABT_info_query_config(ABT_INFO_QUERY_KIND_ENABLED_TOOL, &tool_enabled);
        if (tool_enabled == ABT_TRUE) {
            ABTX_prof_init(&g_margo_abt_prof_context);
            g_margo_abt_prof_init = 1;
        }
    }

    json_object_t* jprofiling_dir
        = json_object_object_get(jabt, "profiling_dir");
    a->profiling_dir
        = strdup(jprofiling_dir ? json_object_get_string(jprofiling_dir) : ".");

    /* build pools that are specified in the JSON */

    /* note: we allocate 3 more spaces than needed because we
     * may have to add a __primary__ pool, and/or a __progress__
     * pool, and/or an __rpc__ pool down the line.
     */
    unsigned num_pools = jpools ? json_object_array_length(jpools) : 0;
    a->pools_len       = 0;
    a->pools_cap       = num_pools + 3;
    a->pools           = calloc(a->pools_cap, sizeof(*(a->pools)));
    for (unsigned i = 0; i < num_pools; ++i) {
        json_object_t* jpool = json_object_array_get_idx(jpools, i);
        if (!__margo_abt_pool_init_from_json(jpool, a, &(a->pools[i]))) {
            result = false;
            goto error;
        }
        a->pools_len += 1;
    }

    /* build xstreams that are specified in the JSON.
     * note: we allocare 1 more space than needed because we may
     * have to add a __primary__ ES.
     */
    unsigned num_xstreams = jxstreams ? json_object_array_length(jxstreams) : 0;
    a->xstreams_len       = 0;
    a->xstreams_cap       = num_xstreams + 1;
    a->xstreams           = calloc(a->xstreams_cap, sizeof(*(a->xstreams)));
    for (unsigned i = 0; i < num_xstreams; ++i) {
        json_object_t* jxstream = json_object_array_get_idx(jxstreams, i);
        if (!__margo_abt_xstream_init_from_json(jxstream, a,
                                                &(a->xstreams[i]))) {
            result = false;
            goto error;
        }
        a->xstreams_len += 1;
    }

    ABT_pool primary_pool        = ABT_POOL_NULL;
    int      primary_pool_idx    = -1;
    int      primary_es_pool_idx = -1;
    int      primary_es_idx      = -1;
    primary_es_idx   = __margo_abt_find_xstream_by_name(a, "__primary__");
    primary_pool_idx = __margo_abt_find_pool_by_name(a, "__primary__");

    if (primary_es_idx >= 0) {
        ABT_pool pool = ABT_POOL_NULL;
        ret = ABT_xstream_get_main_pools(a->xstreams[primary_es_idx].xstream, 1,
                                         &pool);
        if (ret != ABT_SUCCESS) {
            margo_error(a->mid,
                        "Could not get first pool of primary ES"
                        " (ABT_xstream_get_main_pools returned %d)"
                        " in __margo_abt_init_from_json",
                        ret);
            result = false;
            goto error;
        }
        primary_es_pool_idx = __margo_abt_find_pool_by_handle(a, pool);
        if (primary_pool_idx >= 0 && primary_pool_idx != primary_es_pool_idx) {
            margo_error(
                a->mid,
                "Pool with name \"__primary__\" should be the first pool"
                " of the primary xstream");
            result = false;
            goto error;
        }
    }

    /* initialize __primary__ ES if it's not defined in the JSON */
    if (primary_es_idx == -1) { /* no __primary__ ES defined */
        if (first_abt_init) {
            if (primary_pool_idx < 0) {
                /* no __primary__ pool defined, add one */
                json_object_t* jprimary_pool = json_object_new_object();
                json_object_object_add(jprimary_pool, "name",
                                       json_object_new_string("__primary__"));
                json_object_object_add(jprimary_pool, "access",
                                       json_object_new_string("mpmc"));
                primary_pool_idx = a->pools_len;
                result           = __margo_abt_pool_init_from_json(
                    jprimary_pool, a, &a->pools[a->pools_len]);
                json_object_put(jprimary_pool);
                if (!result) goto error;
                a->pools_len += 1;
            }
            primary_pool = a->pools[primary_pool_idx].pool;
            a->pools[primary_pool_idx].margo_free_flag = false;
            /* add a __primary__ ES */
            json_object_t* jprimary_xstream = json_object_new_object();
            json_object_object_add(jprimary_xstream, "name",
                                   json_object_new_string("__primary__"));
            json_object_t* jprimary_sched = json_object_new_object();
            json_object_object_add(jprimary_xstream, "scheduler",
                                   jprimary_sched);
            json_object_t* jprimary_xstream_pools
                = json_object_new_array_ext(1);
            json_object_object_add(jprimary_sched, "pools",
                                   jprimary_xstream_pools);
            json_object_array_add(jprimary_xstream_pools,
                                  json_object_new_int(primary_pool_idx));
            result = __margo_abt_xstream_init_from_json(
                jprimary_xstream, a, &a->xstreams[a->xstreams_len]);
            json_object_put(jprimary_xstream);
            if (!result) goto error;
            a->xstreams_len += 1;
        } else { /* ABT was initialized before margo */
            ABT_xstream self_es;
            ABT_xstream_self(&self_es);
            ABT_xstream_get_main_pools(self_es, 1, &primary_pool);
            primary_pool_idx = a->pools_len;
            /* add the ES' pool as external */
            result = __margo_abt_pool_init_external("__primary__", primary_pool,
                                                    a, &a->pools[a->pools_len]);
            if (!result) goto error;
            a->pools_len += 1;
            /* add an external __primary__ ES */
            result = __margo_abt_xstream_init_external(
                "__primary__", self_es, a, &a->xstreams[a->xstreams_len]);
            if (!result) goto error;
            a->xstreams_len += 1;
        }
    }

finish:
    return result;

error:
    __margo_abt_destroy(a);
    goto finish;
}

json_object_t* __margo_abt_to_json(const margo_abt_t* a, int options)
{
    json_object_t* json      = json_object_new_object();
    json_object_t* jpools    = json_object_new_array_ext(a->pools_len);
    json_object_t* jxstreams = json_object_new_array_ext(a->xstreams_len);
    int flags = JSON_C_OBJECT_ADD_KEY_IS_NEW | JSON_C_OBJECT_ADD_CONSTANT_KEY;
    json_object_object_add_ex(json, "pools", jpools, flags);
    json_object_object_add_ex(json, "xstreams", jxstreams, flags);
    for (unsigned i = 0; i < a->xstreams_len; ++i) {
        if ((options & MARGO_CONFIG_HIDE_EXTERNAL)
            && (strcmp(a->xstreams[i].sched.type, "external") == 0))
            continue; // skip external xstreams if requested
        json_object_t* jxstream
            = __margo_abt_xstream_to_json(&(a->xstreams[i]), a, options);
        json_object_array_add(jxstreams, jxstream);
    }
    for (unsigned i = 0; i < a->pools_len; ++i) {
        if ((options & MARGO_CONFIG_HIDE_EXTERNAL)
            && (strcmp(a->pools[i].kind, "external") == 0))
            continue; // skip external pools if requested
        json_object_t* jpool = __margo_abt_pool_to_json(a->pools + i);
        json_object_array_add(jpools, jpool);
    }
    char* abt_mem_max_num_stacks = getenv("ABT_MEM_MAX_NUM_STACKS");
    if (abt_mem_max_num_stacks) {
        json_object_object_add_ex(
            json, "abt_mem_max_num_stacks",
            json_object_new_int64(atol(abt_mem_max_num_stacks)), flags);
    }
    char* abt_thread_stacksize = getenv("ABT_THREAD_STACKSIZE");
    if (abt_thread_stacksize) {
        json_object_object_add_ex(
            json, "abt_thread_stacksize",
            json_object_new_int64(atol(abt_thread_stacksize)), flags);
    }
    json_object_object_add_ex(json, "profiling_dir",
                              json_object_new_string(a->profiling_dir), flags);
#ifdef HAVE_ABT_INFO_QUERY_KIND_ENABLED_LAZY_STACK_ALLOC
    ABT_bool lazy_stack_alloc;
    ABT_info_query_config(ABT_INFO_QUERY_KIND_ENABLED_LAZY_STACK_ALLOC,
                          &lazy_stack_alloc);
    json_object_object_add_ex(json, "lazy_stack_alloc",
                              json_object_new_boolean(lazy_stack_alloc), flags);
#endif
    return json;
}

void __margo_abt_destroy(margo_abt_t* a)
{
    for (unsigned i = 0; i < a->xstreams_len; ++i) {
        __margo_abt_xstream_destroy(a->xstreams + i, a);
    }
    free(a->xstreams);
    for (unsigned i = 0; i < a->pools_len; ++i) {
        __margo_abt_pool_destroy(a->pools + i, a);
    }
    free(a->pools);
    free(a->profiling_dir);
    memset(a, 0, sizeof(*a));
    if (--g_margo_num_instances == 0 && g_margo_abt_init) {
        /* shut down global abt profiling if needed */
        if (g_margo_abt_prof_init) {
            if (g_margo_abt_prof_started) {
                ABTX_prof_stop(g_margo_abt_prof_context);
                g_margo_abt_prof_started = 0;
            }
            ABTX_prof_finalize(g_margo_abt_prof_context);
            g_margo_abt_prof_init = 0;
        }
        ABT_finalize();
        g_margo_abt_init = false;
    }
}

void __margo_abt_lock(const margo_abt_t* abt)
{
    ABT_mutex_lock(ABT_MUTEX_MEMORY_GET_HANDLE(&abt->mtx));
}

void __margo_abt_unlock(const margo_abt_t* abt)
{
    ABT_mutex_unlock(ABT_MUTEX_MEMORY_GET_HANDLE(&abt->mtx));
}

int __margo_abt_find_pool_by_name(const margo_abt_t* abt, const char* name)
{
    if (abt == NULL || name == NULL) return -1;
    for (uint32_t i = 0; i < abt->pools_len; ++i) {
        if (abt->pools[i].name == NULL) continue;
        if (strcmp(abt->pools[i].name, name) == 0) return i;
    }
    return -1;
}

int __margo_abt_find_pool_by_handle(const margo_abt_t* abt, ABT_pool pool)
{
    if (abt == NULL || pool == ABT_POOL_NULL) return -1;
    for (uint32_t i = 0; i < abt->pools_len; ++i) {
        if (abt->pools[i].pool == pool) return i;
    }
    return -1;
}

int __margo_abt_find_xstream_by_name(const margo_abt_t* abt, const char* name)
{
    if (abt == NULL || name == NULL) return -1;
    for (uint32_t i = 0; i < abt->xstreams_len; ++i) {
        if (abt->xstreams[i].name == NULL) continue;
        if (strcmp(abt->xstreams[i].name, name) == 0) return i;
    }
    return -1;
}

int __margo_abt_find_xstream_by_handle(const margo_abt_t* abt,
                                       ABT_xstream        xstream)
{
    if (abt == NULL || xstream == ABT_XSTREAM_NULL) return -1;
    for (uint32_t i = 0; i < abt->xstreams_len; ++i) {
        if (abt->xstreams[i].xstream == xstream) return i;
    }
    return -1;
}

bool __margo_abt_add_pool_from_json(margo_abt_t*         abt,
                                    const json_object_t* config)
{
    // validate JSON
    if (!__margo_abt_pool_validate_json(config, abt)) return false;

    // check that the pool name isn't used already
    json_object_t* jname = json_object_object_get(config, "name");
    if (jname) {
        const char* name  = json_object_get_string(jname);
        int         index = __margo_abt_find_pool_by_name(abt, name);
        if (index >= 0) {
            margo_error(abt->mid,
                        "Could not create new pool: name (\"%s\") already used",
                        name);
            return false;
        }
    }

    // check that the pools array has space
    if (abt->pools_len == abt->pools_cap) {
        const size_t psize = sizeof(*abt->pools);
        abt->pools_cap *= 1.5;
        abt->pools_cap += 1;
        abt->pools
            = (margo_abt_pool_t*)realloc(abt->pools, abt->pools_cap * psize);
        memset(&(abt->pools[abt->pools_len]), 0,
               psize * (abt->pools_cap - abt->pools_len));
    }

    // create the pool
    bool ret = __margo_abt_pool_init_from_json(config, abt,
                                               &abt->pools[abt->pools_len]);
    if (ret) abt->pools_len += 1;

    return ret;
}

bool __margo_abt_add_xstream_from_json(margo_abt_t*         abt,
                                       const json_object_t* config)
{
    // validate JSON
    if (!__margo_abt_xstream_validate_json(config, abt, NULL)) return false;

    // check that the xstream name isn't used already
    json_object_t* jname = json_object_object_get(config, "name");
    if (jname) {
        const char* name  = json_object_get_string(jname);
        int         index = __margo_abt_find_xstream_by_name(abt, name);
        if (index >= 0) {
            margo_error(
                abt->mid,
                "Could not create new xstream: name (\"%s\") already used",
                name);
            return false;
        }
    }

    // check that the xstreams array has space
    if (abt->xstreams_len == abt->xstreams_cap) {
        const size_t psize = sizeof(*abt->xstreams);
        abt->xstreams_cap *= 1.5;
        abt->xstreams_cap += 1;
        abt->xstreams = (margo_abt_xstream_t*)realloc(
            abt->xstreams, abt->xstreams_cap * psize);
        memset(&(abt->xstreams[abt->xstreams_len]), 0,
               psize * (abt->xstreams_cap - abt->xstreams_len));
    }

    // create the xstream
    bool ret = __margo_abt_xstream_init_from_json(
        config, abt, &abt->xstreams[abt->xstreams_len]);
    if (ret) abt->xstreams_len += 1;

    return ret;
}

bool __margo_abt_add_external_pool(margo_abt_t* abt,
                                   const char*  name,
                                   ABT_pool     pool)
{
    // check that the pool name isn't used already
    int index = __margo_abt_find_pool_by_name(abt, name);
    if (index >= 0) {
        margo_error(abt->mid,
                    "Could not add external pool: name (\"%s\") already used",
                    name);
        return false;
    }

    // check that the pool isn't already registered
    index = __margo_abt_find_pool_by_handle(abt, pool);
    if (index >= 0) {
        margo_error(abt->mid,
                    "Could not add external pool \"%s\":"
                    " pool already registered",
                    name);
        return false;
    }

    // check that the pools array has space
    if (abt->pools_len == abt->pools_cap) {
        const size_t psize = sizeof(*abt->pools);
        abt->pools_cap *= 1.5;
        abt->pools_cap += 1;
        abt->pools
            = (margo_abt_pool_t*)realloc(abt->pools, abt->pools_cap * psize);
        memset(&(abt->pools[abt->pools_len]), 0,
               psize * (abt->pools_cap - abt->pools_len));
    }

    // add the pool
    bool ret = __margo_abt_pool_init_external(name, pool, abt,
                                              &abt->pools[abt->pools_len]);
    if (ret) abt->pools_len += 1;

    return ret;
}

bool __margo_abt_add_external_xstream(margo_abt_t* abt,
                                      const char*  name,
                                      ABT_xstream  xstream)
{
    // check that the xstream name isn't used already
    int index = __margo_abt_find_xstream_by_name(abt, name);
    if (index >= 0) {
        margo_error(
            abt->mid,
            "Could not add external xstream: name (\"%s\") already used", name);
        return false;
    }

    // check that the xstream isn't already registered
    index = __margo_abt_find_xstream_by_handle(abt, xstream);
    if (index >= 0) {
        margo_error(abt->mid,
                    "Could not add external xstream \"%s\":"
                    " xstream already registered",
                    name);
        return false;
    }

    // check that the xstreams array has space
    if (abt->xstreams_len == abt->xstreams_cap) {
        const size_t psize = sizeof(*abt->xstreams);
        abt->xstreams_cap *= 1.5;
        abt->xstreams_cap += 1;
        abt->xstreams = (margo_abt_xstream_t*)realloc(
            abt->xstreams, abt->xstreams_cap * psize);
        memset(&(abt->xstreams[abt->xstreams_len]), 0,
               psize * (abt->xstreams_cap - abt->xstreams_len));
    }

    // add the xstream
    bool ret = __margo_abt_xstream_init_external(
        name, xstream, abt, &abt->xstreams[abt->xstreams_len]);
    if (ret) abt->xstreams_len += 1;

    return ret;
}

bool __margo_abt_remove_pool(margo_abt_t* abt, uint32_t index)
{
    if (index >= abt->pools_len) {
        margo_error(abt->mid, "Invalid index %u in __margo_abt_remove_pool",
                    index);
        return false;
    }
    margo_abt_pool_t* pool = &(abt->pools[index]);
    if (pool->num_rpc_ids) {
        margo_error(abt->mid,
                    "Cannot remove pool %s at index %u "
                    "because it is used by %u RPC handlers",
                    pool->name, index, pool->num_rpc_ids);
        return false;
    }
    if (pool->num_xstreams) {
        margo_error(abt->mid,
                    "Cannot remove pool %s at index %u "
                    "because it is used by %u running xstreams",
                    pool->name, index, pool->num_xstreams);
        return false;
    }
    size_t pool_size = 0;
    int    ret       = ABT_pool_get_total_size(pool->pool, &pool_size);
    if (ret != ABT_SUCCESS) {
        margo_error(0,
                    "Failed to get total size of pool"
                    " (ABT_pool_get_total_size returned %d)"
                    " in __margo_abt_pool_destroy",
                    ret);
        return false;
    } else if (pool_size != 0) {
        margo_error(0, "Destroying a pool (%s) that is not empty", pool->name);
        return false;
    }
    __margo_abt_pool_destroy(pool, abt);
    margo_abt_pool_t* last_pool = &(abt->pools[abt->pools_len - 1]);
    if (index != abt->pools_len - 1) memcpy(pool, last_pool, sizeof(*pool));
    abt->pools_len -= 1;
    memset(last_pool, 0, sizeof(*last_pool));
    return true;
}

bool __margo_abt_remove_xstream(margo_abt_t* abt, uint32_t index)
{
    if (index >= abt->xstreams_len) {
        margo_error(abt->mid, "Invalid index %u in __margo_abt_remove_xstream",
                    index);
        return false;
    }
    margo_abt_xstream_t* xstream = &(abt->xstreams[index]);
    if (strcmp(xstream->name, "__primary__") == 0) {
        margo_error(abt->mid, "Cannot remove primary xstream");
        return false;
    }
    __margo_abt_xstream_destroy(xstream, abt);
    margo_abt_xstream_t* last_xstream = &(abt->xstreams[abt->xstreams_len - 1]);
    if (index != abt->xstreams_len - 1)
        memcpy(xstream, last_xstream, sizeof(*xstream));
    abt->xstreams_len -= 1;
    memset(last_xstream, 0, sizeof(*last_xstream));
    return true;
}
