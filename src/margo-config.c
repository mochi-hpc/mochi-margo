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

size_t margo_get_num_pools(margo_instance_id mid) { return mid->num_abt_pools; }

size_t margo_get_num_xstreams(margo_instance_id mid)
{
    return mid->num_abt_xstreams;
}

hg_return_t margo_find_pool_by_handle(margo_instance_id       mid,
                                      ABT_pool                handle,
                                      struct margo_pool_info* info)
{
    if (mid == MARGO_INSTANCE_NULL || handle == ABT_POOL_NULL)
        return HG_INVALID_ARG;
    for (uint32_t i = 0; i < mid->num_abt_pools; ++i) {
        if (mid->abt_pools[i].info.pool == handle) {
            if (info) *info = mid->abt_pools[i].info;
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
    for (uint32_t i = 0; i < mid->num_abt_pools; ++i) {
        if (strcmp(mid->abt_pools[i].info.name, name) == 0) {
            if (info) *info = mid->abt_pools[i].info;
            return HG_SUCCESS;
        }
    }
    return HG_NOENTRY;
}

hg_return_t margo_find_pool_by_index(margo_instance_id       mid,
                                     uint32_t                index,
                                     struct margo_pool_info* info)
{
    if (mid == MARGO_INSTANCE_NULL || index >= mid->num_abt_pools)
        return HG_INVALID_ARG;
    if (info) *info = mid->abt_pools[index].info;
    return HG_SUCCESS;
}

hg_return_t margo_find_xstream_by_handle(margo_instance_id          mid,
                                         ABT_xstream                handle,
                                         struct margo_xstream_info* info)
{
    if (mid == MARGO_INSTANCE_NULL || handle == ABT_XSTREAM_NULL)
        return HG_INVALID_ARG;
    for (uint32_t i = 0; i < mid->num_abt_xstreams; ++i) {
        if (mid->abt_xstreams[i].info.xstream == handle) {
            if (info) *info = mid->abt_xstreams[i].info;
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
    for (uint32_t i = 0; i < mid->num_abt_xstreams; ++i) {
        if (strcmp(mid->abt_xstreams[i].info.name, name) == 0) {
            if (info) *info = mid->abt_xstreams[i].info;
            return HG_SUCCESS;
        }
    }
    return HG_NOENTRY;
}

hg_return_t margo_find_xstream_by_index(margo_instance_id          mid,
                                        uint32_t                   index,
                                        struct margo_xstream_info* info)
{
    if (mid == MARGO_INSTANCE_NULL || index >= mid->num_abt_xstreams)
        return HG_INVALID_ARG;
    if (info) *info = mid->abt_xstreams[index].info;
    return HG_SUCCESS;
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
