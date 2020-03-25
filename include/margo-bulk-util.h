/*
 * (C) 2020 The University of Chicago
 * 
 * See COPYRIGHT in top-level directory.
 */

#ifndef __MARGO_BULK_UTIL
#define __MARGO_BULK_UTIL

#ifdef __cplusplus
extern "C" {
#endif

#include <margo.h>

/** 
 * Perform a bulk transfer by submitting multiple margo_bulk_transfer
 * in parallel.
 *
 * @param [in] mid Margo instance
 * @param [in] op type of operation to perform
 * @param [in] origin_addr remote Mercury address
 * @param [in] origin_handle remote Mercury bulk memory handle
 * @param [in] origin_offset offset into remote bulk memory to access
 * @param [in] local_handle local bulk memory handle
 * @param [in] local_offset offset into local bulk memory to access
 * @param [in] size size (in bytes) of transfer
 * @param [in] chunk_size size to by transferred by each operation
 * @returns 0 on success, hg_return_t values on error
 */
hg_return_t margo_bulk_parallel_transfer(
    margo_instance_id mid,
    hg_bulk_op_t op,
    hg_addr_t origin_addr,
    hg_bulk_t origin_handle,
    size_t origin_offset,
    hg_bulk_t local_handle,
    size_t local_offset,
    size_t size,
    size_t chunk_size);

#ifdef __cplusplus
}
#endif

#endif /* __MARGO */
