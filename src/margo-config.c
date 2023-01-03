/*
 * (C) 2022 The University of Chicago
 *
 * See COPYRIGHT in top-level directory.
 */
#include <stdbool.h>
#include <ctype.h>
#include <margo.h>
#include <margo-logging.h>
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
    struct json_object* root = json_object_new_object();
    // margo version
    json_object_object_add_ex(root, "version",
                              json_object_new_string(PACKAGE_VERSION), flags);
    // argobots section
    struct json_object* abt_json = __margo_abt_to_json(&(mid->abt), options);
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
    // progress_timeout_ub_msec
    json_object_object_add_ex(
        root, "progress_timeout_ub_msec",
        json_object_new_uint64(mid->hg_progress_timeout_ub), flags);
    // handle_cache_size
    json_object_object_add_ex(root, "handle_cache_size",
                              json_object_new_uint64(mid->handle_cache_size),
                              flags);
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

size_t margo_get_num_pools(margo_instance_id mid) { return mid->abt.pools_len; }

size_t margo_get_num_xstreams(margo_instance_id mid)
{
    return mid->abt.xstreams_len;
}

hg_return_t margo_find_pool_by_handle(margo_instance_id       mid,
                                      ABT_pool                handle,
                                      struct margo_pool_info* info)
{
    if (mid == MARGO_INSTANCE_NULL || handle == ABT_POOL_NULL)
        return HG_INVALID_ARG;
    for (uint32_t i = 0; i < mid->abt.pools_len; ++i) {
        if (mid->abt.pools[i].pool == handle) {
            if (info) {
                info->index = i;
                info->name  = mid->abt.pools[i].name;
                info->pool  = mid->abt.pools[i].pool;
            }
            return HG_SUCCESS;
        }
    }
    return HG_NOENTRY;
}

hg_return_t margo_find_pool_by_name(margo_instance_id       mid,
                                    const char*             name,
                                    struct margo_pool_info* info)
{
    if (mid == MARGO_INSTANCE_NULL || name == NULL) return HG_INVALID_ARG;
    for (uint32_t i = 0; i < mid->abt.pools_len; ++i) {
        if (mid->abt.pools[i].name == NULL) continue;
        if (strcmp(mid->abt.pools[i].name, name) == 0) {
            if (info) {
                info->index = i;
                info->name  = mid->abt.pools[i].name;
                info->pool  = mid->abt.pools[i].pool;
            }
            return HG_SUCCESS;
        }
    }
    return HG_NOENTRY;
}

hg_return_t margo_find_pool_by_index(margo_instance_id       mid,
                                     uint32_t                index,
                                     struct margo_pool_info* info)
{
    if (mid == MARGO_INSTANCE_NULL || index >= mid->abt.pools_len)
        return HG_INVALID_ARG;
    if (info) {
        info->index = index;
        info->name  = mid->abt.pools[index].name;
        info->pool  = mid->abt.pools[index].pool;
    }
    return HG_SUCCESS;
}

hg_return_t margo_add_pool_from_json(margo_instance_id       mid,
                                     const char*             json_str,
                                     struct margo_pool_info* info)
{
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
    bool b = __margo_abt_add_pool_from_json(&mid->abt, json);
    json_object_put(json);
    if (b) {
        if (info) {
            info->index = mid->abt.pools_len - 1;
            info->name  = mid->abt.pools[info->index].name;
            info->pool  = mid->abt.pools[info->index].pool;
        }
        return HG_SUCCESS;
    } else {
        return HG_INVALID_ARG;
    }
}

hg_return_t margo_add_pool_external(margo_instance_id       mid,
                                    const char*             name,
                                    ABT_pool                pool,
                                    ABT_bool                take_ownership,
                                    struct margo_pool_info* info)
{
    if (!mid) return HG_INVALID_ARG;
    bool b = __margo_abt_add_external_pool(&mid->abt, name, pool);
    if (b) {
        mid->abt.pools[mid->abt.pools_len - 1].margo_free_flag = take_ownership;
        if (info) {
            info->index = mid->abt.pools_len - 1;
            info->name  = mid->abt.pools[info->index].name;
            info->pool  = mid->abt.pools[info->index].pool;
        }
        return HG_SUCCESS;
    } else {
        return HG_INVALID_ARG;
    }
}

hg_return_t margo_find_xstream_by_handle(margo_instance_id          mid,
                                         ABT_xstream                handle,
                                         struct margo_xstream_info* info)
{
    if (mid == MARGO_INSTANCE_NULL || handle == ABT_XSTREAM_NULL)
        return HG_INVALID_ARG;
    for (uint32_t i = 0; i < mid->abt.xstreams_len; ++i) {
        if (mid->abt.xstreams[i].xstream == handle) {
            if (info) {
                info->name    = mid->abt.xstreams[i].name;
                info->xstream = mid->abt.xstreams[i].xstream;
                info->index   = i;
            }
            return HG_SUCCESS;
        }
    }
    return HG_NOENTRY;
}

hg_return_t margo_find_xstream_by_name(margo_instance_id          mid,
                                       const char*                name,
                                       struct margo_xstream_info* info)
{
    if (mid == MARGO_INSTANCE_NULL || name == NULL) return HG_INVALID_ARG;
    for (uint32_t i = 0; i < mid->abt.xstreams_len; ++i) {
        if (mid->abt.xstreams[i].name == NULL) continue;
        if (strcmp(mid->abt.xstreams[i].name, name) == 0) {
            if (info) {
                info->name    = mid->abt.xstreams[i].name;
                info->xstream = mid->abt.xstreams[i].xstream;
                info->index   = i;
            }
            return HG_SUCCESS;
        }
    }
    return HG_NOENTRY;
}

hg_return_t margo_find_xstream_by_index(margo_instance_id          mid,
                                        uint32_t                   index,
                                        struct margo_xstream_info* info)
{
    if (mid == MARGO_INSTANCE_NULL || index >= mid->abt.xstreams_len)
        return HG_INVALID_ARG;
    if (info) {
        info->name    = mid->abt.xstreams[index].name;
        info->xstream = mid->abt.xstreams[index].xstream;
        info->index   = index;
    }
    return HG_SUCCESS;
}

hg_return_t margo_add_xstream_from_json(margo_instance_id          mid,
                                        const char*                json_str,
                                        struct margo_xstream_info* info)
{
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
    bool b = __margo_abt_add_xstream_from_json(&mid->abt, json);
    json_object_put(json);
    if (b) {
        if (info) {
            info->index   = mid->abt.xstreams_len - 1;
            info->name    = mid->abt.xstreams[info->index].name;
            info->xstream = mid->abt.xstreams[info->index].xstream;
        }
        return HG_SUCCESS;
    } else {
        return HG_INVALID_ARG;
    }
}

hg_return_t margo_add_xstream_external(margo_instance_id mid,
                                       const char*       name,
                                       ABT_xstream       xstream,
                                       ABT_bool          take_ownership,
                                       struct margo_xstream_info* info)
{
    if (!mid) return HG_INVALID_ARG;
    bool b = __margo_abt_add_external_xstream(&mid->abt, name, xstream);
    if (b) {
        mid->abt.xstreams[mid->abt.xstreams_len - 1].margo_free_flag
            = take_ownership;
        if (info) {
            info->index   = mid->abt.xstreams_len - 1;
            info->name    = mid->abt.xstreams[info->index].name;
            info->xstream = mid->abt.xstreams[info->index].xstream;
        }
        return HG_SUCCESS;
    } else {
        return HG_INVALID_ARG;
    }
}

/* DEPRECATED FUNCTIONS */

// LCOV_EXCL_START
int margo_get_pool_by_name(margo_instance_id mid,
                           const char*       name,
                           ABT_pool*         pool)
{
    struct margo_pool_info info;
    hg_return_t            ret = margo_find_pool_by_name(mid, name, &info);
    if (ret != HG_SUCCESS) return -1;
    if (pool) *pool = info.pool;
    return 0;
}

int margo_get_pool_by_index(margo_instance_id mid,
                            unsigned          index,
                            ABT_pool*         pool)
{
    struct margo_pool_info info;
    hg_return_t            ret = margo_find_pool_by_index(mid, index, &info);
    if (ret != HG_SUCCESS) return -1;
    if (pool) *pool = info.pool;
    return 0;
}

const char* margo_get_pool_name(margo_instance_id mid, unsigned index)
{
    struct margo_pool_info info;
    hg_return_t            ret = margo_find_pool_by_index(mid, index, &info);
    if (ret != HG_SUCCESS) return NULL;
    return info.name;
}

int margo_get_pool_index(margo_instance_id mid, const char* name)
{
    struct margo_pool_info info;
    hg_return_t            ret = margo_find_pool_by_name(mid, name, &info);
    if (ret != HG_SUCCESS) return -1;
    return info.index;
}

int margo_get_xstream_by_name(margo_instance_id mid,
                              const char*       name,
                              ABT_xstream*      es)
{
    struct margo_xstream_info info;
    hg_return_t ret = margo_find_xstream_by_name(mid, name, &info);
    if (ret != HG_SUCCESS) return -1;
    if (es) *es = info.xstream;
    return 0;
}

int margo_get_xstream_by_index(margo_instance_id mid,
                               unsigned          index,
                               ABT_xstream*      es)
{
    struct margo_xstream_info info;
    hg_return_t ret = margo_find_xstream_by_index(mid, index, &info);
    if (ret != HG_SUCCESS) return -1;
    if (es) *es = info.xstream;
    return 0;
}

const char* margo_get_xstream_name(margo_instance_id mid, unsigned index)
{
    struct margo_xstream_info info;
    hg_return_t ret = margo_find_xstream_by_index(mid, index, &info);
    if (ret != HG_SUCCESS) return NULL;
    return info.name;
}

int margo_get_xstream_index(margo_instance_id mid, const char* name)
{
    struct margo_xstream_info info;
    hg_return_t ret = margo_find_xstream_by_name(mid, name, &info);
    if (ret != HG_SUCCESS) return -1;
    return info.index;
}
// LCOV_EXCL_END
