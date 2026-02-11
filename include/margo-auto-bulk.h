/**
 * @file margo-auto-bulk.h
 *
 * (C) 2024 The University of Chicago
 *
 * See COPYRIGHT in top-level directory.
 */

#ifndef __MARGO_AUTO_BULK
#define __MARGO_AUTO_BULK

#include <margo.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct margo_auto_bulk* margo_auto_bulk_t;
#define MARGO_AUTO_BULK_NULL ((margo_auto_bulk_t)0)

#define MARGO_PULL_ON_ACCESS  0x1
#define MARGO_PUSH_ON_DESTROY 0x2

/**
 * @brief Create a margo_auto_bulk_t by allocating a buffer of the
 * specified size and exposing it for RDMA operations.
 *
 * @param[in] mid Margo instance
 * @param[in] size Size of the buffer
 * @param[in] flags 0 or bitwise or of any of MARGO_PULL_ON_ACCESS and
 * MARGO_PUSH_ON_DETROY
 * @param[out] autobulk Resulting margo_auto_bulk_t
 *
 * @return HG_SUCCESS or other error codes.
 */
hg_return_t margo_auto_bulk_create(margo_instance_id  mid,
                                   size_t             size,
                                   uint8_t            flags,
                                   margo_auto_bulk_t* autobulk);

/**
 * @brief Create a margo_auto_bulk_t from an hg_bulk_t representing a
 * buffer owned by the process at the specified remote_addr.
 *
 * Note that this function can be used to wrap both a remote or a local
 * hg_bulk_t (in particular if remote_addr is HG_ADDR_NULL, the bulk handle is
 * assumed to be local).
 *
 * @param[in] mid Margo instance
 * @param[in] bulk Bulk handle
 * @param[in] addr Address owning the bulk handle
 * @param[in] offset Offset at which the margo_auto_bulk_t will start
 * @param[in] size Size of the portion of the bulk to wrap
 * @param[in] flags 0 or bitwise or of any of MARGO_PULL_ON_ACCESS and
 * MARGO_PUSH_ON_DETROY
 * @param[out] autobulk Resulting margo_auto_bulk_t
 *
 * @return HG_SUCCESS or other error codes.
 */
hg_return_t margo_auto_bulk_create_from_bulk(margo_instance_id  mid,
                                             hg_bulk_t          bulk,
                                             hg_addr_t          addr,
                                             size_t             offset,
                                             size_t             size,
                                             uint8_t            flags,
                                             margo_auto_bulk_t* autobulk);

/**
 * @brief Create a margo_auto_bulk_t from a local buffer.
 *
 * Note: to create a margo_auto_bulk_t from non-contiguous memory,
 * use margo_bulk_create to create an hg_bulk_t, and pass it to
 * margo_auto_bulk_create_from_bulk.
 *
 * @param[in] mid Margo instance
 * @param[in] buffer Buffer to wrap
 * @param[in] size Size of the buffer
 * @param[in] flags 0 or bitwise or of any of MARGO_PULL_ON_ACCESS and
 * MARGO_PUSH_ON_DETROY
 * @param[out] autobulk Resulting margo_auto_bulk_t
 *
 * @return HG_SUCCESS or other error codes.
 */
hg_return_t margo_auto_bulk_create_from_buffer(margo_instance_id  mid,
                                               void*              buffer,
                                               size_t             size,
                                               uint8_t            flags,
                                               margo_auto_bulk_t* autobulk);

/**
 * @brief Returns a pointer to the local buffer underlying the
 * margo_auto_bulk_t, as well as its size.
 *
 * Note: Even if the autobulk was created from local memory, the pointer
 * returned by this function may not be the same as that local memory (if the
 * local memory was non-contiguous, the margo_auto_bulk_t will create a
 * contiguous version that is synchronized with the initial memory using
 * margo_auto_bulk_pull/push or on access/destruction if flags have been set
 * accordingly during the creation of the margo_auto_bulk_t).
 *
 * If a "mirror" buffer needs to be created, the first call to
 * margo_auto_bulk_access will be allocate this buffer. If MARGO_PULL_ON_ACCESS
 * was specified at creation time, the content of the original buffer (whether
 * remote or local) will be pulled into this mirror buffer.
 *
 * @param[in] autobulk Resulting margo_auto_bulk_t
 * @param[out] buffer Will be set to the address of the buffer
 * @param[out] size Will be set to the size of the buffer
 *
 * @return HG_SUCCESS or other error codes.
 */
hg_return_t
margo_auto_bulk_access(margo_auto_bulk_t autobulk, void** buffer, size_t* size);

/**
 * @brief Get information about the remote counterpart of the margo_auto_bulk_t.
 *
 * Note: any output argument set to NULL will be ignored. If bulk is set, the
 * caller is responsible for calling margo_bulk_free. If the addr is set, the
 * caller is responsible for calling margo_addr_free.
 *
 * @param[in] autobulk margo_auto_bulk_t from which to get information
 * @param[out] bulk Bulk handle representing the remote buffer
 * @param[out] addr Address owning the remote buffer
 * @param[out] offset Offset in the remote bulk
 * @param[out] size Size of the portion wrapped by the margo_auto_bulk_t
 *
 * @return HG_SUCCESS or other error codes.
 */
hg_return_t margo_auto_bulk_info(margo_auto_bulk_t autobulk,
                                 hg_bulk_t*        bulk,
                                 hg_addr_t*        addr,
                                 size_t*           offset,
                                 size_t*           size);

/**
 * @brief Pull the content of the remote buffer info the local buffer.
 *
 * @param autobulk margo_auto_bulk_t instance.
 * @param offset Offset from which to pull.
 * @param size Size to pull.
 *
 * @return HG_SUCCESS or other error codes.
 */
hg_return_t
margo_auto_bulk_pull(margo_auto_bulk_t autobulk, size_t offset, size_t size);

/**
 * @brief Push the content of the local buffer info the remote buffer.
 *
 * @param autobulk margo_auto_bulk_t instance.
 * @param offset Offset from which to push.
 * @param size Size to push.
 *
 * @return HG_SUCCESS or other error codes.
 */
hg_return_t
margo_auto_bulk_push(margo_auto_bulk_t autobulk, size_t offset, size_t size);

/**
 * @brief Free an autobulk object. This may cause a PUSH operation if the
 * margo_auto_bulk_t has been created with the MARGO_PUSH_ON_DESTROY flag.
 *
 * @param autobulk margo_auto_bulk_t instance to free
 *
 * @return HG_SUCCESS or other error codes.
 */
hg_return_t margo_auto_bulk_free(margo_auto_bulk_t autobulk);

#ifdef __cplusplus
}
#endif

#endif /* __MARGO */
