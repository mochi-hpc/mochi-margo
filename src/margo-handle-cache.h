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

hg_return_t __margo_handle_cache_init(margo_instance_id mid,
                                      size_t            handle_cache_size);

void __margo_handle_cache_destroy(margo_instance_id mid);

hg_return_t __margo_handle_cache_get(margo_instance_id mid,
                                     hg_addr_t         addr,
                                     hg_id_t           id,
                                     hg_handle_t*      handle);

hg_return_t __margo_handle_cache_put(margo_instance_id mid, hg_handle_t handle);

#endif
