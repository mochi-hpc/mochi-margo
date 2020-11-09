/*
 * (C) 2015 The University of Chicago
 *
 * See COPYRIGHT in top-level directory.
 */
#ifndef __MARGO_BULK_POOL
#define __MARGO_BULK_POOL

#ifdef __cplusplus
extern "C" {
#endif

#include <mercury.h>
#include <mercury_types.h>
#include <mercury_bulk.h>
#include <mercury_macros.h>
#include <abt.h>

/* A collection of fixed-size, fixed-permission reusable bulk buffers */
struct margo_bulk_pool;
typedef struct margo_bulk_pool* margo_bulk_pool_t;

#define MARGO_BULK_POOL_NULL ((margo_bulk_pool_t)NULL)

/* A collection of margo_bulk_pool's, each of varying sizes */
struct margo_bulk_poolset;
typedef struct margo_bulk_poolset* margo_bulk_poolset_t;

#define MARGO_BULK_POOLSET_NULL ((margo_bulk_poolset_t)NULL)

/**
 * @brief Creates a pool of buffers and bulk handles of a given size.
 *
 * @param[in] mid Margo instance with which to create the bulk handles
 * @param[in] count Number of bulk handles contained in the pool
 * @param[in] size Size of each bulk buffer
 * @param[in] flag HG_BULK_READ_ONLY, HG_BULK_WRITE_ONLY, or HG_BULK_READWRITE
 * @param[out] pool Resulting pool of bulk handles
 *
 * @return HG_SUCCESS in case of success, or HG error codes in case of failure.
 */
hg_return_t margo_bulk_pool_create(margo_instance_id  mid,
                                   hg_size_t          count,
                                   hg_size_t          size,
                                   hg_uint8_t         flag,
                                   margo_bulk_pool_t* pool);

/**
 * @brief Destroys a margo_bulk_pool_t object, clearing all bulk handles and
 * and freeing the buffers. The pool should not be in use (i.e. none of the
 * internal buffers should be in use) when this call happens.
 *
 * @param pool margo_bulk_pool_t object to destroy.
 *
 * @return 0 in case of success, -1 in case of failure.
 */
int margo_bulk_pool_destroy(margo_bulk_pool_t pool);

/**
 * @brief Gets a free hg_bulk_t object from the pool. This function will block
 * until a bulk object is available.
 *
 * @param[in] pool margo_bulk_pool_t object from which to take the hg_bulk_t.
 * @param[out] bulk hg_bulk_t object (guaranteed not to be HG_BULK_NULL if the
 * returned value is 0).
 *
 * @return 0 in case of success, -1 in case of failure.
 */
int margo_bulk_pool_get(margo_bulk_pool_t pool, hg_bulk_t* bulk);

/**
 * @brief Gets a free hg_bulk_t object from the pool. This function will return
 * HG_BULK_NULL if no hg_bulk_t object is free at the moment of the call.
 *
 * @param pool margo_bulk_pool_t object from which to take the hg_bulk_t.
 * @param bulk resulting bulk handle (can be HG_BULK_NULL).
 *
 * @return 0 in case of success, -1 in case of failure..
 */
int margo_bulk_pool_tryget(margo_bulk_pool_t pool, hg_bulk_t* bulk);

/**
 * @brief Puts a bulk handle back in the pool. Note that the function is
 * expecting the bulk handle to have been taken from the pool in the first
 * place. The function will return -1 if the bulk was not associated with this
 * pool to begin with.
 *
 * @param pool margo_bulk_pool_t object to which to return the bulk handle.
 * @param bulk Bulk handle to release.
 *
 * @return 0 in case of success, -1 in case of failure.
 */
int margo_bulk_pool_release(margo_bulk_pool_t pool, hg_bulk_t bulk);

/**
 * @brief Creates a poolset. A poolset is a set of pools with the same number of
 * buffers in each pool, and buffer sizes increasing exponentially with the pool
 * number.
 *
 * @param[in] mid Margo instance
 * @param[in] npools Number of pools in the poolset.
 * @param[in] nbufs Number of buffers in each pool.
 * @param[in] first_size Size of buffers in the first pool.
 * @param[in] size_multiple Factor by which to multiply the size of the previous
 * pool to get the size of the next.
 * @param[in] flag HG_BULK_READ_ONLY, HG_BULK_WRITE_ONLY, or HG_BULK_READWRITE.
 * @param[out] poolset Resulting poolset.
 *
 * @return HG_SUCCESS of other HG error codes.
 */
hg_return_t margo_bulk_poolset_create(margo_instance_id     mid,
                                      hg_size_t             npools,
                                      hg_size_t             nbufs,
                                      hg_size_t             first_size,
                                      hg_size_t             size_multiple,
                                      hg_uint8_t            flag,
                                      margo_bulk_poolset_t* poolset);

/**
 * @brief Destroy a poolset. The poolset must not be in use when this function
 * is called.
 *
 * @param poolset Poolset to destroy.
 *
 * @return 0 in case of success, -1 in case of failure.
 */
int margo_bulk_poolset_destroy(margo_bulk_poolset_t poolset);

/**
 * @brief Get maximum size supported by a pool set
 *
 * @param poolset Poolset
 * @param max_size Maximum supported buffer size
 *
 */
void margo_bulk_poolset_get_max(margo_bulk_poolset_t poolset,
                                hg_size_t*           max_size);

/**
 * @brief Gets a bulk handle from the pool with the minimum size required to
 * satisfy the provided size. May block until the pool has a bulk handle
 * available.
 *
 * @param poolset Poolset from which to get the bulk handle.
 * @param size Size of the buffer needed.
 * @param bulk Resulting bulk handle.
 *
 * @return 0 in case of success, -1 in case of failure.
 */
int margo_bulk_poolset_get(margo_bulk_poolset_t poolset,
                           hg_size_t            size,
                           hg_bulk_t*           bulk);

/**
 * @brief Try getting a bulk handle from the poolset. If any_flag is HG_TRUE,
 * this function will search in pools of increasingly larger buffers until it
 * finds one (or return HG_BULK_NULL if it doesn't). If any_flag is HG_FALSE,
 * this function will only search in the pool with the minimum required size.
 *
 * @param poolset Poolset in which to get a handle.
 * @param size Size required.
 * @param any_flag Whether to look in increasingly larger pools or not.
 * @param bulk Resulting bulk handle.
 *
 * @return 0 in case of success (bulk = HG_BULK_NULL is also considered
 * success), -1 in case of failure.
 */
int margo_bulk_poolset_tryget(margo_bulk_poolset_t poolset,
                              hg_size_t            size,
                              hg_bool_t            any_flag,
                              hg_bulk_t*           bulk);

/**
 * @brief Puts a bulk handle back in its pool.
 *
 * @param poolset Poolset.
 * @param bulk Bulk to release.
 *
 * @return 0 in case of success, -1 in case of success.
 */
int margo_bulk_poolset_release(margo_bulk_poolset_t poolset, hg_bulk_t bulk);

#ifdef __cplusplus
}
#endif

#endif /* __MARGO_BULK_POOL */
