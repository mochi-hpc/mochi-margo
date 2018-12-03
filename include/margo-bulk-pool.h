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
typedef struct margo_bulk_pool margo_bulk_pool_t;

/* A collection of margo_bulk_pool's, each of varying sizes */
struct margo_bulk_poolset;
typedef struct margo_bulk_poolset margo_bulk_poolset_t;

hg_return_t margo_bulk_pool_create(
    margo_instance_id mid,
    hg_size_t count,
    hg_size_t size,
    hg_uint8_t flag,
    margo_bulk_pool_t **pool);

void margo_bulk_pool_destroy(
    margo_bulk_pool_t *pool);

hg_bulk_t margo_bulk_pool_get(
    margo_bulk_pool_t *pool);

hg_bulk_t margo_bulk_pool_tryget(
    margo_bulk_pool_t *pool);

void margo_bulk_pool_release(
    margo_bulk_pool_t *pool,
    hg_bulk_t bulk);

hg_return_t margo_bulk_poolset_create(
    margo_instance_id mid,
    hg_size_t npools,
    hg_size_t nbufs,
    hg_size_t first_size,
    hg_size_t size_multiple,
    hg_uint8_t flag,
    margo_bulk_poolset_t **poolset);

void margo_bulk_poolset_destroy(
    margo_bulk_poolset_t *poolset);

hg_bulk_t margo_bulk_poolset_get(
    margo_bulk_poolset_t *poolset,
    hg_size_t size);

hg_bulk_t margo_bulk_poolset_tryget(
    margo_bulk_poolset_t *poolset,
    hg_size_t size,
    hg_bool_t any_flag);

void margo_bulk_poolset_release(
    margo_bulk_poolset_t *poolset,
    hg_bulk_t bulk);

#ifdef __cplusplus
}
#endif

#endif /* __MARGO_BULK_POOL */
