/*
 * (C) 2022 The University of Chicago
 *
 * See COPYRIGHT in top-level directory.
 */
#include <stdbool.h>
#include <ctype.h>
#include <margo.h>
#include <margo-logging.h>
#include "margo-monitoring-internal.h"
#include "margo-instance.h"

char* margo_get_config(margo_instance_id mid)
{
    return margo_get_config_opt(mid, 0);
}

char* margo_get_config_opt(margo_instance_id mid, int options)
{
    int flags = JSON_C_OBJECT_ADD_KEY_IS_NEW | JSON_C_OBJECT_ADD_CONSTANT_KEY;
    int json_to_string_flags = JSON_C_TO_STRING_NOSLASHESCAPE;
    if (options & MARGO_CONFIG_PRETTY_JSON) {
        json_to_string_flags |= JSON_C_TO_STRING_PRETTY;
    }
    struct json_object* root     = json_object_new_object();
    struct json_object* _plumber = NULL;

    // margo version
    json_object_object_add_ex(root, "version",
                              json_object_new_string(PACKAGE_VERSION), flags);
    // argobots section
    __margo_abt_lock(&mid->abt);
    struct json_object* abt_json = __margo_abt_to_json(&(mid->abt), options);
    __margo_abt_unlock(&mid->abt);
    json_object_object_add_ex(root, "argobots", abt_json, flags);
    // mercury section
    struct json_object* hg_json = __margo_hg_to_json(&(mid->hg));
    json_object_object_add_ex(root, "mercury", hg_json, flags);
    // monitoring section
    if (mid->monitor) {
        struct json_object* monitoring = json_object_new_object();
        if (mid->monitor->name) {
            json_object_object_add_ex(
                monitoring, "name",
                json_object_new_string(mid->monitor->name()), flags);
        }
        if (mid->monitor->config) {
            json_object_object_add_ex(monitoring, "config",
                                      mid->monitor->config(mid->monitor->uargs),
                                      flags);
        }
        json_object_object_add_ex(root, "monitoring", monitoring, flags);
    }

#ifdef HAVE_MOCHI_PLUMBER
    // plumber policy
    _plumber = json_object_new_object();
    json_object_object_add_ex(
        _plumber, "bucket_policy",
        json_object_new_string(mid->plumber_bucket_policy), flags);
    json_object_object_add_ex(_plumber, "nic_policy",
                              json_object_new_string(mid->plumber_nic_policy),
                              flags);
    json_object_object_add_ex(root, "plumber", _plumber, flags);
#endif

    // progress_timeout_ub_msec
    json_object_object_add_ex(
        root, "progress_timeout_ub_msec",
        json_object_new_uint64(mid->hg_progress_timeout_ub), flags);
    // progress_spindown_msec
    json_object_object_add_ex(
        root, "progress_spindown_msec",
        json_object_new_uint64(mid->hg_progress_spindown_msec), flags);
    // handle_cache_size
    json_object_object_add_ex(root, "handle_cache_size",
                              json_object_new_uint64(mid->handle_cache_size),
                              flags);
    // abt profiling
    json_object_object_add_ex(
        root, "enable_abt_profiling",
        json_object_new_boolean(mid->abt_profiling_enabled), flags);

    // progress_pool and rpc_pool
    if (options & MARGO_CONFIG_USE_NAMES) {
        json_object_object_add_ex(
            root, "progress_pool",
            json_object_new_string(mid->abt.pools[mid->progress_pool_idx].name),
            flags);
        json_object_object_add_ex(
            root, "rpc_pool",
            json_object_new_string(mid->abt.pools[mid->rpc_pool_idx].name),
            flags);
    } else {
        json_object_object_add_ex(
            root, "progress_pool",
            json_object_new_uint64(mid->progress_pool_idx), flags);
        json_object_object_add_ex(
            root, "rpc_pool", json_object_new_uint64(mid->rpc_pool_idx), flags);
    }
    // serialize
    const char* content
        = json_object_to_json_string_ext(root, json_to_string_flags);
    content = strdup(content);
    json_object_put(root);
    return (char*)content;
}

size_t margo_get_num_pools(margo_instance_id mid)
{
    __margo_abt_lock(&mid->abt);
    size_t ret = mid->abt.pools_len;
    __margo_abt_unlock(&mid->abt);
    return ret;
}

size_t margo_get_num_xstreams(margo_instance_id mid)
{
    __margo_abt_lock(&mid->abt);
    size_t ret = mid->abt.xstreams_len;
    __margo_abt_unlock(&mid->abt);
    return ret;
}

hg_return_t margo_find_pool_by_handle(margo_instance_id       mid,
                                      ABT_pool                handle,
                                      struct margo_pool_info* info)
{
    if (mid == MARGO_INSTANCE_NULL || handle == ABT_POOL_NULL)
        return HG_INVALID_ARG;
    hg_return_t ret = HG_NOENTRY;
    __margo_abt_lock(&mid->abt);
    for (uint32_t i = 0; i < mid->abt.pools_len; ++i) {
        if (mid->abt.pools[i].pool == handle) {
            if (info) {
                info->index = i;
                info->name  = mid->abt.pools[i].name;
                info->pool  = mid->abt.pools[i].pool;
            }
            ret = HG_SUCCESS;
            break;
        }
    }
    __margo_abt_unlock(&mid->abt);
    return ret;
}

hg_return_t margo_find_pool_by_name(margo_instance_id       mid,
                                    const char*             name,
                                    struct margo_pool_info* info)
{
    if (mid == MARGO_INSTANCE_NULL || name == NULL) return HG_INVALID_ARG;
    hg_return_t ret = HG_NOENTRY;
    __margo_abt_lock(&mid->abt);
    for (uint32_t i = 0; i < mid->abt.pools_len; ++i) {
        if (mid->abt.pools[i].name == NULL) continue;
        if (strcmp(mid->abt.pools[i].name, name) == 0) {
            if (info) {
                info->index = i;
                info->name  = mid->abt.pools[i].name;
                info->pool  = mid->abt.pools[i].pool;
            }
            ret = HG_SUCCESS;
            break;
        }
    }
    __margo_abt_unlock(&mid->abt);
    return ret;
}

hg_return_t margo_find_pool_by_index(margo_instance_id       mid,
                                     uint32_t                index,
                                     struct margo_pool_info* info)
{
    if (!mid) return HG_INVALID_ARG;
    __margo_abt_lock(&mid->abt);
    if (index >= mid->abt.pools_len) {
        __margo_abt_unlock(&mid->abt);
        return HG_INVALID_ARG;
    }
    if (info) {
        info->index = index;
        info->name  = mid->abt.pools[index].name;
        info->pool  = mid->abt.pools[index].pool;
    }
    __margo_abt_unlock(&mid->abt);
    return HG_SUCCESS;
}

hg_return_t margo_pool_ref_incr_by_handle(margo_instance_id mid,
                                          ABT_pool          handle)
{
    if (mid == MARGO_INSTANCE_NULL || handle == ABT_POOL_NULL)
        return HG_INVALID_ARG;
    hg_return_t ret = HG_NOENTRY;
    __margo_abt_lock(&mid->abt);
    for (uint32_t i = 0; i < mid->abt.pools_len; ++i) {
        if (mid->abt.pools[i].pool == handle) {
            mid->abt.pools[i].refcount++;
            ret = HG_SUCCESS;
            break;
        }
    }
    __margo_abt_unlock(&mid->abt);
    return ret;
}

hg_return_t margo_pool_ref_incr_by_name(margo_instance_id mid, const char* name)
{
    if (mid == MARGO_INSTANCE_NULL || name == NULL) return HG_INVALID_ARG;
    hg_return_t ret = HG_NOENTRY;
    __margo_abt_lock(&mid->abt);
    for (uint32_t i = 0; i < mid->abt.pools_len; ++i) {
        if (mid->abt.pools[i].name == NULL) continue;
        if (strcmp(mid->abt.pools[i].name, name) == 0) {
            mid->abt.pools[i].refcount++;
            ret = HG_SUCCESS;
            break;
        }
    }
    __margo_abt_unlock(&mid->abt);
    return ret;
}

hg_return_t margo_pool_ref_incr_by_index(margo_instance_id mid, uint32_t index)
{
    if (!mid) return HG_INVALID_ARG;
    __margo_abt_lock(&mid->abt);
    if (index >= mid->abt.pools_len) {
        __margo_abt_unlock(&mid->abt);
        return HG_INVALID_ARG;
    }
    mid->abt.pools[index].refcount++;
    __margo_abt_unlock(&mid->abt);
    return HG_SUCCESS;
}

hg_return_t margo_pool_ref_count_by_handle(margo_instance_id mid,
                                           ABT_pool          handle,
                                           unsigned*         refcount)
{
    if (mid == MARGO_INSTANCE_NULL || handle == ABT_POOL_NULL)
        return HG_INVALID_ARG;
    hg_return_t ret = HG_NOENTRY;
    __margo_abt_lock(&mid->abt);
    for (uint32_t i = 0; i < mid->abt.pools_len; ++i) {
        if (mid->abt.pools[i].pool == handle) {
            *refcount = mid->abt.pools[i].refcount;
            ret       = HG_SUCCESS;
            break;
        }
    }
    __margo_abt_unlock(&mid->abt);
    return ret;
}

hg_return_t margo_pool_ref_count_by_name(margo_instance_id mid,
                                         const char*       name,
                                         unsigned*         refcount)
{
    if (mid == MARGO_INSTANCE_NULL || name == NULL) return HG_INVALID_ARG;
    hg_return_t ret = HG_NOENTRY;
    __margo_abt_lock(&mid->abt);
    for (uint32_t i = 0; i < mid->abt.pools_len; ++i) {
        if (mid->abt.pools[i].name == NULL) continue;
        if (strcmp(mid->abt.pools[i].name, name) == 0) {
            *refcount = mid->abt.pools[i].refcount;
            ret       = HG_SUCCESS;
            break;
        }
    }
    __margo_abt_unlock(&mid->abt);
    return ret;
}

hg_return_t margo_pool_ref_count_by_index(margo_instance_id mid,
                                          uint32_t          index,
                                          unsigned*         refcount)
{
    if (!mid) return HG_INVALID_ARG;
    __margo_abt_lock(&mid->abt);
    if (index >= mid->abt.pools_len) {
        __margo_abt_unlock(&mid->abt);
        return HG_INVALID_ARG;
    }
    *refcount = mid->abt.pools[index].refcount;
    __margo_abt_unlock(&mid->abt);
    return HG_SUCCESS;
}

hg_return_t margo_pool_release_by_handle(margo_instance_id mid, ABT_pool handle)
{
    if (mid == MARGO_INSTANCE_NULL || handle == ABT_POOL_NULL)
        return HG_INVALID_ARG;
    hg_return_t ret = HG_NOENTRY;
    __margo_abt_lock(&mid->abt);
    for (uint32_t i = 0; i < mid->abt.pools_len; ++i) {
        if (mid->abt.pools[i].pool == handle) {
            if (mid->abt.pools[i].refcount == 0) {
                __margo_abt_unlock(&mid->abt);
                ret = HG_PERMISSION;
                break;
            }
            mid->abt.pools[i].refcount--;
            ret = HG_SUCCESS;
            break;
        }
    }
    __margo_abt_unlock(&mid->abt);
    return ret;
}

hg_return_t margo_pool_release_by_name(margo_instance_id mid, const char* name)
{
    if (mid == MARGO_INSTANCE_NULL || name == NULL) return HG_INVALID_ARG;
    hg_return_t ret = HG_NOENTRY;
    __margo_abt_lock(&mid->abt);
    for (uint32_t i = 0; i < mid->abt.pools_len; ++i) {
        if (mid->abt.pools[i].name == NULL) continue;
        if (strcmp(mid->abt.pools[i].name, name) == 0) {
            if (mid->abt.pools[i].refcount == 0) {
                __margo_abt_unlock(&mid->abt);
                ret = HG_PERMISSION;
                break;
            }
            mid->abt.pools[i].refcount--;
            ret = HG_SUCCESS;
            break;
        }
    }
    __margo_abt_unlock(&mid->abt);
    return ret;
}

hg_return_t margo_pool_release_by_index(margo_instance_id mid, uint32_t index)
{
    if (!mid) return HG_INVALID_ARG;
    __margo_abt_lock(&mid->abt);
    if (index >= mid->abt.pools_len) {
        __margo_abt_unlock(&mid->abt);
        return HG_INVALID_ARG;
    }
    if (mid->abt.pools[index].refcount == 0) {
        __margo_abt_unlock(&mid->abt);
        return HG_PERMISSION;
    }
    mid->abt.pools[index].refcount--;
    __margo_abt_unlock(&mid->abt);
    return HG_SUCCESS;
}

hg_return_t margo_add_pool_from_json(margo_instance_id       mid,
                                     const char*             json_str,
                                     struct margo_pool_info* info)
{
    struct json_object*     json    = NULL;
    struct json_tokener*    tokener = json_tokener_new();
    hg_return_t             ret     = HG_SUCCESS;
    enum json_tokener_error jerr;
    if (json_str && json_str[0]) {
        json = json_tokener_parse_ex(tokener, json_str, strlen(json_str));
        if (!json) {
            jerr = json_tokener_get_error(tokener);
            margo_error(mid, "JSON parse error: %s",
                        json_tokener_error_desc(jerr));
            json_tokener_free(tokener);
            return HG_INVALID_ARG;
        }
    }
    json_tokener_free(tokener);
    __margo_abt_lock(&mid->abt);

    /* monitoring */
    struct margo_pool_info             m_info = {0};
    struct margo_monitor_add_pool_args monitoring_args
        = {.info = &m_info, .ret = HG_SUCCESS};
    __MARGO_MONITOR(mid, FN_START, add_pool, monitoring_args);

    bool b = __margo_abt_add_pool_from_json(&mid->abt, json);
    json_object_put(json);
    if (b) {
        m_info.index = mid->abt.pools_len - 1;
        m_info.name  = mid->abt.pools[m_info.index].name;
        m_info.pool  = mid->abt.pools[m_info.index].pool;
        if (info) memcpy(info, &m_info, sizeof(m_info));
    } else {
        ret = HG_INVALID_ARG;
    }

    /* monitoring */
    __MARGO_MONITOR(mid, FN_END, add_pool, monitoring_args);
    monitoring_args.ret = ret;

    __margo_abt_unlock(&mid->abt);
    return ret;
}

hg_return_t margo_add_pool_external(margo_instance_id       mid,
                                    const char*             name,
                                    ABT_pool                pool,
                                    ABT_bool                take_ownership,
                                    struct margo_pool_info* info)
{
    if (!mid) return HG_INVALID_ARG;
    hg_return_t ret = HG_SUCCESS;
    __margo_abt_lock(&mid->abt);

    /* monitoring */
    struct margo_pool_info             m_info = {0};
    struct margo_monitor_add_pool_args monitoring_args
        = {.info = &m_info, .ret = HG_SUCCESS};
    __MARGO_MONITOR(mid, FN_START, add_pool, monitoring_args);

    bool b = __margo_abt_add_external_pool(&mid->abt, name, pool);
    if (b) {
        mid->abt.pools[mid->abt.pools_len - 1].margo_free_flag = take_ownership;
        m_info.index = mid->abt.pools_len - 1;
        m_info.name  = mid->abt.pools[m_info.index].name;
        m_info.pool  = mid->abt.pools[m_info.index].pool;
        if (info) memcpy(info, &m_info, sizeof(m_info));
    } else {
        ret = HG_INVALID_ARG;
    }

    /* monitoring */
    __MARGO_MONITOR(mid, FN_END, add_pool, monitoring_args);
    monitoring_args.ret = ret;

    __margo_abt_unlock(&mid->abt);
    return ret;
}

hg_return_t margo_remove_pool_by_index(margo_instance_id mid, uint32_t index)
{
    if (!mid) return HG_INVALID_ARG;
    hg_return_t ret = HG_SUCCESS;
    __margo_abt_lock(&mid->abt);
    if (index >= mid->abt.pools_len) {
        ret = HG_INVALID_ARG;
        goto finish;
    }
    if (mid->abt.pools[index].pool == MARGO_PROGRESS_POOL(mid)) {
        margo_error(mid, "Removing the progress pool is not allowed");
        ret = HG_OTHER_ERROR;
        goto finish;
    }
    if (mid->abt.pools[index].pool == MARGO_RPC_POOL(mid)) {
        margo_error(mid, "Removing the default handler pool is not allowed");
        ret = HG_OTHER_ERROR;
        goto finish;
    }

    /* monitoring */
    struct margo_pool_info m_info = {
        .index = index,
        .name  = mid->abt.pools[index].name,
        .pool  = mid->abt.pools[index].pool,
    };
    struct margo_monitor_remove_pool_args monitoring_args
        = {.info = &m_info, .ret = HG_SUCCESS};
    __MARGO_MONITOR(mid, FN_START, remove_pool, monitoring_args);

    ret = __margo_abt_remove_pool(&mid->abt, index);

    /* monitoring */
    m_info.name         = NULL;
    m_info.pool         = ABT_POOL_NULL;
    monitoring_args.ret = ret;
    __MARGO_MONITOR(mid, FN_END, remove_pool, monitoring_args);

finish:
    __margo_abt_unlock(&mid->abt);
    return ret;
}

hg_return_t margo_remove_pool_by_name(margo_instance_id mid, const char* name)
{
    if (!mid) return HG_INVALID_ARG;
    __margo_abt_lock(&mid->abt);
    hg_return_t ret = HG_SUCCESS;
    ;
    int32_t index = __margo_abt_find_pool_by_name(&mid->abt, name);
    if (index < 0) {
        ret = HG_INVALID_ARG;
        goto finish;
    }
    if (index >= (int32_t)mid->abt.pools_len) {
        ret = HG_INVALID_ARG;
        goto finish;
    }
    if (mid->abt.pools[index].pool == MARGO_PROGRESS_POOL(mid)) {
        margo_error(mid, "Removing the progress pool is not allowed");
        ret = HG_OTHER_ERROR;
        goto finish;
    }
    if (mid->abt.pools[index].pool == MARGO_RPC_POOL(mid)) {
        margo_error(mid, "Removing the default handler pool is not allowed");
        ret = HG_OTHER_ERROR;
        goto finish;
    }

    /* monitoring */
    struct margo_pool_info m_info = {
        .index = index,
        .name  = mid->abt.pools[index].name,
        .pool  = mid->abt.pools[index].pool,
    };
    struct margo_monitor_remove_pool_args monitoring_args
        = {.info = &m_info, .ret = HG_SUCCESS};
    __MARGO_MONITOR(mid, FN_START, remove_pool, monitoring_args);

    ret = __margo_abt_remove_pool(&mid->abt, index);

    /* monitoring */
    m_info.name         = NULL;
    m_info.pool         = ABT_POOL_NULL;
    monitoring_args.ret = ret;
    __MARGO_MONITOR(mid, FN_END, remove_pool, monitoring_args);

finish:
    __margo_abt_unlock(&mid->abt);
    return ret;
}

hg_return_t margo_remove_pool_by_handle(margo_instance_id mid, ABT_pool handle)
{
    if (!mid) return HG_INVALID_ARG;
    __margo_abt_lock(&mid->abt);
    hg_return_t ret   = HG_SUCCESS;
    int32_t     index = __margo_abt_find_pool_by_handle(&mid->abt, handle);
    if (index < 0) {
        ret = HG_INVALID_ARG;
        goto finish;
    }
    if (index >= (int32_t)mid->abt.pools_len) {
        ret = HG_INVALID_ARG;
        goto finish;
    }
    if (mid->abt.pools[index].pool == MARGO_PROGRESS_POOL(mid)) {
        margo_error(mid, "Removing the progress pool is not allowed");
        ret = HG_OTHER_ERROR;
        goto finish;
    }
    if (mid->abt.pools[index].pool == MARGO_RPC_POOL(mid)) {
        margo_error(mid, "Removing the default handler pool is not allowed");
        ret = HG_OTHER_ERROR;
        goto finish;
    }

    /* monitoring */
    struct margo_pool_info m_info = {
        .index = index,
        .name  = mid->abt.pools[index].name,
        .pool  = mid->abt.pools[index].pool,
    };
    struct margo_monitor_remove_pool_args monitoring_args
        = {.info = &m_info, .ret = HG_SUCCESS};
    __MARGO_MONITOR(mid, FN_START, remove_pool, monitoring_args);

    ret = __margo_abt_remove_pool(&mid->abt, index);

    /* monitoring */
    m_info.name         = NULL;
    m_info.pool         = ABT_POOL_NULL;
    monitoring_args.ret = ret;
    __MARGO_MONITOR(mid, FN_END, remove_pool, monitoring_args);

finish:
    __margo_abt_unlock(&mid->abt);
    return ret;
}

hg_return_t margo_find_xstream_by_handle(margo_instance_id          mid,
                                         ABT_xstream                handle,
                                         struct margo_xstream_info* info)
{
    if (mid == MARGO_INSTANCE_NULL || handle == ABT_XSTREAM_NULL)
        return HG_INVALID_ARG;
    hg_return_t ret = HG_NOENTRY;
    __margo_abt_lock(&mid->abt);
    for (uint32_t i = 0; i < mid->abt.xstreams_len; ++i) {
        if (mid->abt.xstreams[i].xstream == handle) {
            if (info) {
                info->name    = mid->abt.xstreams[i].name;
                info->xstream = mid->abt.xstreams[i].xstream;
                info->index   = i;
            }
            ret = HG_SUCCESS;
            break;
        }
    }
    __margo_abt_unlock(&mid->abt);
    return ret;
}

hg_return_t margo_find_xstream_by_name(margo_instance_id          mid,
                                       const char*                name,
                                       struct margo_xstream_info* info)
{
    if (mid == MARGO_INSTANCE_NULL || name == NULL) return HG_INVALID_ARG;
    hg_return_t ret = HG_NOENTRY;
    __margo_abt_lock(&mid->abt);
    for (uint32_t i = 0; i < mid->abt.xstreams_len; ++i) {
        if (mid->abt.xstreams[i].name == NULL) continue;
        if (strcmp(mid->abt.xstreams[i].name, name) == 0) {
            if (info) {
                info->name    = mid->abt.xstreams[i].name;
                info->xstream = mid->abt.xstreams[i].xstream;
                info->index   = i;
            }
            ret = HG_SUCCESS;
            break;
        }
    }
    __margo_abt_unlock(&mid->abt);
    return ret;
}

hg_return_t margo_find_xstream_by_index(margo_instance_id          mid,
                                        uint32_t                   index,
                                        struct margo_xstream_info* info)
{
    if (!mid) return HG_INVALID_ARG;
    __margo_abt_lock(&mid->abt);
    if (index >= mid->abt.xstreams_len) {
        __margo_abt_unlock(&mid->abt);
        return HG_INVALID_ARG;
    }
    if (info) {
        info->name    = mid->abt.xstreams[index].name;
        info->xstream = mid->abt.xstreams[index].xstream;
        info->index   = index;
    }
    __margo_abt_unlock(&mid->abt);
    return HG_SUCCESS;
}

hg_return_t margo_xstream_ref_incr_by_handle(margo_instance_id mid,
                                             ABT_xstream       handle)
{
    if (mid == MARGO_INSTANCE_NULL || handle == ABT_XSTREAM_NULL)
        return HG_INVALID_ARG;
    hg_return_t ret = HG_NOENTRY;
    __margo_abt_lock(&mid->abt);
    for (uint32_t i = 0; i < mid->abt.xstreams_len; ++i) {
        if (mid->abt.xstreams[i].xstream == handle) {
            mid->abt.xstreams[i].refcount++;
            ret = HG_SUCCESS;
            break;
        }
    }
    __margo_abt_unlock(&mid->abt);
    return ret;
}

hg_return_t margo_xstream_ref_incr_by_name(margo_instance_id mid,
                                           const char*       name)
{
    if (mid == MARGO_INSTANCE_NULL || name == NULL) return HG_INVALID_ARG;
    hg_return_t ret = HG_NOENTRY;
    __margo_abt_lock(&mid->abt);
    for (uint32_t i = 0; i < mid->abt.xstreams_len; ++i) {
        if (mid->abt.xstreams[i].name == NULL) continue;
        if (strcmp(mid->abt.xstreams[i].name, name) == 0) {
            mid->abt.xstreams[i].refcount++;
            ret = HG_SUCCESS;
            break;
        }
    }
    __margo_abt_unlock(&mid->abt);
    return ret;
}

hg_return_t margo_xstream_ref_incr_by_index(margo_instance_id mid,
                                            uint32_t          index)
{
    if (!mid) return HG_INVALID_ARG;
    __margo_abt_lock(&mid->abt);
    if (index >= mid->abt.xstreams_len) {
        __margo_abt_unlock(&mid->abt);
        return HG_INVALID_ARG;
    }
    mid->abt.xstreams[index].refcount++;
    __margo_abt_unlock(&mid->abt);
    return HG_SUCCESS;
}

hg_return_t margo_xstream_ref_count_by_handle(margo_instance_id mid,
                                              ABT_xstream       handle,
                                              unsigned*         refcount)
{
    if (mid == MARGO_INSTANCE_NULL || handle == ABT_XSTREAM_NULL)
        return HG_INVALID_ARG;
    hg_return_t ret = HG_NOENTRY;
    __margo_abt_lock(&mid->abt);
    for (uint32_t i = 0; i < mid->abt.xstreams_len; ++i) {
        if (mid->abt.xstreams[i].xstream == handle) {
            *refcount = mid->abt.xstreams[i].refcount;
            ret       = HG_SUCCESS;
            break;
        }
    }
    __margo_abt_unlock(&mid->abt);
    return ret;
}

hg_return_t margo_xstream_ref_count_by_name(margo_instance_id mid,
                                            const char*       name,
                                            unsigned*         refcount)
{
    if (mid == MARGO_INSTANCE_NULL || name == NULL) return HG_INVALID_ARG;
    hg_return_t ret = HG_NOENTRY;
    __margo_abt_lock(&mid->abt);
    for (uint32_t i = 0; i < mid->abt.xstreams_len; ++i) {
        if (mid->abt.xstreams[i].name == NULL) continue;
        if (strcmp(mid->abt.xstreams[i].name, name) == 0) {
            *refcount = mid->abt.xstreams[i].refcount;
            ret       = HG_SUCCESS;
            break;
        }
    }
    __margo_abt_unlock(&mid->abt);
    return ret;
}

hg_return_t margo_xstream_ref_count_by_index(margo_instance_id mid,
                                             uint32_t          index,
                                             unsigned*         refcount)
{
    if (!mid) return HG_INVALID_ARG;
    __margo_abt_lock(&mid->abt);
    if (index >= mid->abt.xstreams_len) {
        __margo_abt_unlock(&mid->abt);
        return HG_INVALID_ARG;
    }
    *refcount = mid->abt.xstreams[index].refcount;
    __margo_abt_unlock(&mid->abt);
    return HG_SUCCESS;
}

hg_return_t margo_xstream_release_by_handle(margo_instance_id mid,
                                            ABT_xstream       handle)
{
    if (mid == MARGO_INSTANCE_NULL || handle == ABT_XSTREAM_NULL)
        return HG_INVALID_ARG;
    hg_return_t ret = HG_NOENTRY;
    __margo_abt_lock(&mid->abt);
    for (uint32_t i = 0; i < mid->abt.xstreams_len; ++i) {
        if (mid->abt.xstreams[i].xstream == handle) {
            if (mid->abt.xstreams[i].refcount == 0) {
                __margo_abt_unlock(&mid->abt);
                ret = HG_PERMISSION;
                break;
            }
            mid->abt.xstreams[i].refcount--;
            ret = HG_SUCCESS;
            break;
        }
    }
    __margo_abt_unlock(&mid->abt);
    return ret;
}

hg_return_t margo_xstream_release_by_name(margo_instance_id mid,
                                          const char*       name)
{
    if (mid == MARGO_INSTANCE_NULL || name == NULL) return HG_INVALID_ARG;
    hg_return_t ret = HG_NOENTRY;
    __margo_abt_lock(&mid->abt);
    for (uint32_t i = 0; i < mid->abt.xstreams_len; ++i) {
        if (mid->abt.xstreams[i].name == NULL) continue;
        if (strcmp(mid->abt.xstreams[i].name, name) == 0) {
            if (mid->abt.xstreams[i].refcount == 0) {
                __margo_abt_unlock(&mid->abt);
                ret = HG_PERMISSION;
                break;
            }
            mid->abt.xstreams[i].refcount--;
            ret = HG_SUCCESS;
            break;
        }
    }
    __margo_abt_unlock(&mid->abt);
    return ret;
}

hg_return_t margo_xstream_release_by_index(margo_instance_id mid,
                                           uint32_t          index)
{
    if (!mid) return HG_INVALID_ARG;
    __margo_abt_lock(&mid->abt);
    if (index >= mid->abt.xstreams_len) {
        __margo_abt_unlock(&mid->abt);
        return HG_INVALID_ARG;
    }
    if (mid->abt.xstreams[index].refcount == 0) {
        __margo_abt_unlock(&mid->abt);
        return HG_PERMISSION;
    }
    mid->abt.xstreams[index].refcount--;
    __margo_abt_unlock(&mid->abt);
    return HG_SUCCESS;
}

hg_return_t margo_add_xstream_from_json(margo_instance_id          mid,
                                        const char*                json_str,
                                        struct margo_xstream_info* info)
{
    if (!mid) return HG_INVALID_ARG;
    struct json_object*     json    = NULL;
    struct json_tokener*    tokener = json_tokener_new();
    enum json_tokener_error jerr;
    if (json_str && json_str[0]) {
        json = json_tokener_parse_ex(tokener, json_str, strlen(json_str));
        if (!json) {
            jerr = json_tokener_get_error(tokener);
            margo_error(mid, "JSON parse error: %s",
                        json_tokener_error_desc(jerr));
            json_tokener_free(tokener);
            return HG_INVALID_ARG;
        }
    }
    json_tokener_free(tokener);
    __margo_abt_lock(&mid->abt);

    /* monitoring */
    struct margo_xstream_info             m_info = {0};
    struct margo_monitor_add_xstream_args monitoring_args
        = {.info = &m_info, .ret = HG_SUCCESS};
    __MARGO_MONITOR(mid, FN_START, add_xstream, monitoring_args);

    bool b = __margo_abt_add_xstream_from_json(&mid->abt, json);
    json_object_put(json);
    hg_return_t ret = HG_SUCCESS;
    if (b) {
        m_info.index   = mid->abt.xstreams_len - 1;
        m_info.name    = mid->abt.xstreams[m_info.index].name;
        m_info.xstream = mid->abt.xstreams[m_info.index].xstream;
        if (info) memcpy(info, &m_info, sizeof(m_info));
    } else {
        ret = HG_INVALID_ARG;
    }

    /* monitoring */
    __MARGO_MONITOR(mid, FN_END, add_xstream, monitoring_args);
    monitoring_args.ret = ret;

    __margo_abt_unlock(&mid->abt);
    return ret;
}

hg_return_t margo_add_xstream_external(margo_instance_id mid,
                                       const char*       name,
                                       ABT_xstream       xstream,
                                       ABT_bool          take_ownership,
                                       struct margo_xstream_info* info)
{
    if (!mid) return HG_INVALID_ARG;
    __margo_abt_lock(&mid->abt);

    /* monitoring */
    struct margo_xstream_info             m_info = {0};
    struct margo_monitor_add_xstream_args monitoring_args
        = {.info = &m_info, .ret = HG_SUCCESS};
    __MARGO_MONITOR(mid, FN_START, add_xstream, monitoring_args);

    hg_return_t ret = HG_SUCCESS;
    bool        b = __margo_abt_add_external_xstream(&mid->abt, name, xstream);
    if (b) {
        mid->abt.xstreams[mid->abt.xstreams_len - 1].margo_free_flag
            = take_ownership;
        m_info.index   = mid->abt.xstreams_len - 1;
        m_info.name    = mid->abt.xstreams[m_info.index].name;
        m_info.xstream = mid->abt.xstreams[m_info.index].xstream;
        if (info) memcpy(info, &m_info, sizeof(m_info));
    } else {
        ret = HG_INVALID_ARG;
    }

    /* monitoring */
    __MARGO_MONITOR(mid, FN_END, add_xstream, monitoring_args);
    monitoring_args.ret = ret;

    __margo_abt_unlock(&mid->abt);
    return ret;
}

hg_return_t margo_remove_xstream_by_index(margo_instance_id mid, uint32_t index)
{
    if (!mid) return HG_INVALID_ARG;
    __margo_abt_lock(&mid->abt);

    if (index > mid->abt.xstreams_len) {
        __margo_abt_unlock(&mid->abt);
        return HG_OTHER_ERROR;
    }

    /* monitoring */
    struct margo_xstream_info m_info = {
        .index   = index,
        .name    = mid->abt.xstreams[index].name,
        .xstream = mid->abt.xstreams[index].xstream,
    };
    struct margo_monitor_remove_xstream_args monitoring_args
        = {.info = &m_info, .ret = HG_SUCCESS};
    __MARGO_MONITOR(mid, FN_START, remove_xstream, monitoring_args);

    hg_return_t ret = __margo_abt_remove_xstream(&mid->abt, index);

    /* monitoring */
    m_info.name         = NULL;
    m_info.xstream      = ABT_XSTREAM_NULL;
    monitoring_args.ret = ret;
    __MARGO_MONITOR(mid, FN_END, remove_xstream, monitoring_args);

    __margo_abt_unlock(&mid->abt);
    return ret;
}

hg_return_t margo_remove_xstream_by_name(margo_instance_id mid,
                                         const char*       name)
{
    if (!mid) return HG_INVALID_ARG;
    __margo_abt_lock(&mid->abt);
    int32_t index = __margo_abt_find_xstream_by_name(&mid->abt, name);
    if (index < 0) {
        __margo_abt_unlock(&mid->abt);
        return HG_INVALID_ARG;
    }

    /* monitoring */
    struct margo_xstream_info m_info = {
        .index   = index,
        .name    = mid->abt.xstreams[index].name,
        .xstream = mid->abt.xstreams[index].xstream,
    };
    struct margo_monitor_remove_xstream_args monitoring_args
        = {.info = &m_info, .ret = HG_SUCCESS};
    __MARGO_MONITOR(mid, FN_START, remove_xstream, monitoring_args);

    hg_return_t ret = __margo_abt_remove_xstream(&mid->abt, index);

    /* monitoring */
    m_info.name         = NULL;
    m_info.xstream      = ABT_XSTREAM_NULL;
    monitoring_args.ret = ret;
    __MARGO_MONITOR(mid, FN_END, remove_xstream, monitoring_args);

    __margo_abt_unlock(&mid->abt);
    return ret;
}

hg_return_t margo_remove_xstream_by_handle(margo_instance_id mid,
                                           ABT_xstream       handle)
{
    if (!mid) return HG_INVALID_ARG;
    __margo_abt_lock(&mid->abt);
    int32_t index = __margo_abt_find_xstream_by_handle(&mid->abt, handle);
    if (index < 0) {
        __margo_abt_unlock(&mid->abt);
        return HG_INVALID_ARG;
    }

    /* monitoring */
    struct margo_xstream_info m_info = {
        .index   = index,
        .name    = mid->abt.xstreams[index].name,
        .xstream = mid->abt.xstreams[index].xstream,
    };
    struct margo_monitor_remove_xstream_args monitoring_args
        = {.info = &m_info, .ret = HG_SUCCESS};
    __MARGO_MONITOR(mid, FN_START, remove_xstream, monitoring_args);

    hg_return_t ret = __margo_abt_remove_xstream(&mid->abt, index);

    __margo_abt_unlock(&mid->abt);

    /* monitoring */
    m_info.name         = NULL;
    m_info.xstream      = ABT_XSTREAM_NULL;
    monitoring_args.ret = ret;
    __MARGO_MONITOR(mid, FN_END, remove_xstream, monitoring_args);

    return ret;
}

hg_return_t margo_transfer_pool_content(ABT_pool origin_pool,
                                        ABT_pool target_pool)
{
#ifdef HAVE_ABT_POOL_POP_THREADS
    while (1) {
        ABT_thread threads[64];
        size_t     num = 0;
        ABT_pool_pop_threads(origin_pool, threads, 64, &num);
        if (num == 0) break;
        ABT_pool_push_threads(target_pool, threads, num);
    }
#else
    ABT_unit unit;
    size_t   pool_size;
    while (1) {
        ABT_pool_get_size(origin_pool, &pool_size);
        if (pool_size == 0) break;
        ABT_pool_pop(origin_pool, &unit);
        ABT_pool_push(target_pool, unit);
    }
#endif
    return HG_SUCCESS;
}
