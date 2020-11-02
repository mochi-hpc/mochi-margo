/*
 * (C) 2020 The University of Chicago
 *
 * See COPYRIGHT in top-level directory.
 */
#include "margo-instance.h"
#include "margo-handle-cache.h"

struct margo_handle_cache_el {
    hg_handle_t                   handle;
    UT_hash_handle                hh;   /* in-use hash link */
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
            hret = HG_NOMEM_ERROR;
            __margo_handle_cache_destroy(mid);
            break;
        }

        /* create handle with NULL_ADDRs, we will reset later to valid addrs */
        hret = HG_Create(mid->hg_context, HG_ADDR_NULL, 0, &el->handle);
        if (hret != HG_SUCCESS) {
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

    /* only free handle list elements -- handles in hash are still in use */
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
    struct margo_handle_cache_el* el;
    hg_return_t                   hret = HG_SUCCESS;

    ABT_mutex_lock(mid->handle_cache_mtx);

    if (!mid->free_handle_list) {
        /* if no available handles, just fall through */
        hret = HG_OTHER_ERROR;
        goto finish;
    }

    /* pop first element from the free handle list */
    el = mid->free_handle_list;
    LL_DELETE(mid->free_handle_list, el);

    /* reset handle */
    hret = HG_Reset(el->handle, addr, id);
    if (hret == HG_SUCCESS) {
        /* put on in-use list and pass back handle */
        HASH_ADD(hh, mid->used_handle_hash, handle, sizeof(hg_handle_t), el);
        *handle = el->handle;
    } else {
        /* reset failed, add handle back to the free list */
        LL_APPEND(mid->free_handle_list, el);
    }

finish:
    ABT_mutex_unlock(mid->handle_cache_mtx);
    return hret;
}

hg_return_t __margo_handle_cache_put(margo_instance_id mid, hg_handle_t handle)
{
    struct margo_handle_cache_el* el;
    hg_return_t                   hret = HG_SUCCESS;

    ABT_mutex_lock(mid->handle_cache_mtx);

    /* look for handle in the in-use hash */
    HASH_FIND(hh, mid->used_handle_hash, &handle, sizeof(hg_handle_t), el);
    if (!el) {
        /* this handle was manually allocated -- just fall through */
        hret = HG_OTHER_ERROR;
        goto finish;
    }

    /* remove from the in-use hash */
    HASH_DELETE(hh, mid->used_handle_hash, el);

    /* add to the tail of the free handle list */
    LL_APPEND(mid->free_handle_list, el);

finish:
    ABT_mutex_unlock(mid->handle_cache_mtx);
    return hret;
}
