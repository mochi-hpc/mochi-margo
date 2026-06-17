/*
 * (C) 2020 The University of Chicago
 *
 * See COPYRIGHT in top-level directory.
 */
#ifndef __MARGO_HANDLE_CACHE_H
#define __MARGO_HANDLE_CACHE_H

#include <margo.h>

// private functions that initialize the handle cache for a margo instance,
// and access cached handles.

struct margo_handle_cache_el; /* defined in margo-handle-cache.c */

hg_return_t __margo_handle_cache_init(margo_instance_id mid,
                                      size_t            handle_cache_size);

void __margo_handle_cache_destroy(margo_instance_id mid);

/* Returns a recycled, reset handle from the cache (or HG_OTHER_ERROR if the
 * cache is empty, in which case the caller should HG_Create one). The handle
 * carries an internal back-pointer to its cache element, so the caller does no
 * cache bookkeeping. */
hg_return_t __margo_handle_cache_get(margo_instance_id mid,
                                     hg_addr_t         addr,
                                     hg_id_t           id,
                                     hg_handle_t*      handle);

/* If the handle came from the cache, resets its data and returns it to the
 * free list in O(1) (no lookup), keeping the handle alive for reuse. Returns
 * HG_OTHER_ERROR for a manually-allocated handle so the caller can HG_Destroy
 * it instead. */
hg_return_t __margo_handle_cache_put(margo_instance_id mid, hg_handle_t handle);

#endif
