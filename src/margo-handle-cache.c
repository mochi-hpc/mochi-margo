/*
 * (C) 2020 The University of Chicago
 *
 * See COPYRIGHT in top-level directory.
 */
#include <string.h>
#include "margo-instance.h"
#include "margo-handle-cache.h"

struct margo_handle_cache_el {
    hg_handle_t                   handle;
    struct margo_handle_cache_el* next; /* free list link */
};

hg_return_t __margo_handle_cache_init(margo_instance_id mid,
                                      size_t            handle_cache_size)
{
    struct margo_handle_cache_el* el;
    hg_return_t                   hret = HG_SUCCESS;

    ABT_mutex_create(&(mid->handle_cache_mtx));

    for (unsigned i = 0; i < handle_cache_size; i++) {
        el = malloc(sizeof(*el));
        if (!el) {
            margo_error(mid,
                        "Could not allocate handle cache element (%u/%zu)", i,
                        handle_cache_size);
            hret = HG_NOMEM_ERROR;
            __margo_handle_cache_destroy(mid);
            break;
        }

        /* create handle with NULL_ADDRs, we will reset later to valid addrs */
        hret = HG_Create(mid->hg.hg_context, HG_ADDR_NULL, 0, &el->handle);
        if (hret != HG_SUCCESS) {
            margo_error(mid, "Could not create cached handle: HG_Create: %s",
                        HG_Error_to_string(hret));
            free(el);
            __margo_handle_cache_destroy(mid);
            break;
        }

        /* Pre-attach a margo_handle_data carrying a back-pointer to this cache
         * element. HG_Reset preserves handle data, so this link is permanent
         * for the lifetime of the cached handle and lets __margo_handle_cache_put
         * recycle the handle in O(1) without any lookup or caller bookkeeping. */
        struct margo_handle_data* data = calloc(1, sizeof(*data));
        if (!data) {
            margo_error(mid, "Could not allocate handle data for cached handle");
            HG_Destroy(el->handle);
            free(el);
            hret = HG_NOMEM_ERROR;
            __margo_handle_cache_destroy(mid);
            break;
        }
        data->cache_el = el;
        hret           = HG_Set_data(el->handle, data, __margo_handle_data_free);
        if (hret != HG_SUCCESS) {
            margo_error(mid,
                        "Could not attach data to cached handle: HG_Set_data: %s",
                        HG_Error_to_string(hret));
            free(data);
            HG_Destroy(el->handle);
            free(el);
            __margo_handle_cache_destroy(mid);
            break;
        }

        /* add to the free list */
        LL_PREPEND(mid->free_handle_list, el);
    }

    return hret;
}

void __margo_handle_cache_destroy(margo_instance_id mid)
{
    struct margo_handle_cache_el *el, *tmp;

    /* only free elements still on the free list -- handles currently in use are
     * owned by the application and will be released via margo_destroy.
     * HG_Destroy releases each handle's attached data via __margo_handle_data_free. */
    LL_FOREACH_SAFE(mid->free_handle_list, el, tmp)
    {
        LL_DELETE(mid->free_handle_list, el);
        HG_Destroy(el->handle);
        free(el);
    }

    ABT_mutex_free(&mid->handle_cache_mtx);

    return;
}

hg_return_t __margo_handle_cache_get(margo_instance_id mid,
                                     hg_addr_t         addr,
                                     hg_id_t           id,
                                     hg_handle_t*      handle)
{
    /* pop first element from the free handle list (the only operation that
     * needs the lock; HG_Reset below is done outside the critical section) */
    ABT_mutex_spinlock(mid->handle_cache_mtx);
    struct margo_handle_cache_el* el = mid->free_handle_list;
    if (el) LL_DELETE(mid->free_handle_list, el);
    ABT_mutex_unlock(mid->handle_cache_mtx);

    if (!el) {
        /* no available handles, caller should HG_Create one */
        return HG_OTHER_ERROR;
    }

    /* reset handle (outside the lock: el is now owned by this caller and not
     * reachable by any other thread) */
    hg_return_t hret = HG_Reset(el->handle, addr, id);
    if (hret == HG_SUCCESS) {
        *handle = el->handle;
    } else {
        /* reset failed, return the element to the free list (the caller will
         * fall back to creating a fresh handle) */
        margo_error(mid, "Could not reset cached handle: HG_Reset: %s",
                    HG_Error_to_string(hret));
        ABT_mutex_spinlock(mid->handle_cache_mtx);
        LL_PREPEND(mid->free_handle_list, el);
        ABT_mutex_unlock(mid->handle_cache_mtx);
    }

    return hret;
}

hg_return_t __margo_handle_cache_put(margo_instance_id mid, hg_handle_t handle)
{
    /* recover the cache element from the handle's own data (set once when the
     * cache attached the data); NULL means the handle wasn't from the cache */
    struct margo_handle_data* data
        = (struct margo_handle_data*)HG_Get_data(handle);
    struct margo_handle_cache_el* el = data ? data->cache_el : NULL;
    if (!el) {
        /* this handle was manually allocated -- caller should HG_Destroy it */
        return HG_OTHER_ERROR;
    }

    /* run the user free callback and reset the data in place for reuse, keeping
     * it attached and preserving the cache back-pointer */
    if (data->user_free_callback) data->user_free_callback(data->user_data);
    memset(data, 0, sizeof(*data));
    data->cache_el = el;

    /* return the element to the free list in O(1), no lookup required */
    ABT_mutex_spinlock(mid->handle_cache_mtx);
    LL_PREPEND(mid->free_handle_list, el);
    ABT_mutex_unlock(mid->handle_cache_mtx);

    return HG_SUCCESS;
}
