/*
 * (C) 2015 The University of Chicago
 * 
 * See COPYRIGHT in top-level directory.
 */

#include <assert.h>
#include <unistd.h>
#include <errno.h>
#include <abt.h>

#include "margo.h"
#include "margo-bulk-pool.h"

struct margo_bulk_pool
{
    margo_instance_id mid;
    void *buf;
    hg_bulk_t *bulks;
    hg_size_t count;
    hg_size_t size;
    hg_size_t num_free;
    hg_uint8_t flag;
    ABT_mutex mutex;
    ABT_cond cond;
};

struct margo_bulk_poolset
{
    margo_bulk_pool_t **pools;
    hg_size_t npools;
    hg_size_t nbufs;
    hg_size_t first_size;
    hg_size_t size_multiple;
};

hg_return_t margo_bulk_pool_create(
    margo_instance_id mid,
    hg_size_t count,
    hg_size_t size,
    hg_uint8_t flag,
    margo_bulk_pool_t **pool)
{
    int ret;
    hg_return_t hret;
    margo_bulk_pool_t *p;
    hg_size_t i;

    p = malloc(sizeof(*p)); 
    if (p == NULL)
    {
        hret = HG_NOMEM_ERROR;
        goto err;
    }

    p->buf = malloc(count * size);
    if (p->buf == NULL)
    {
        hret = HG_NOMEM_ERROR;
        goto err;
    }

    p->mid = mid;
    p->count = count;
    p->size = size;
    p->num_free = count;
    p->flag = flag;
    p->bulks = malloc(count * sizeof(*p->bulks));
    if (p->bulks == NULL)
    {
        hret = HG_NOMEM_ERROR;
        goto err;
    }
    for (i = 0; i < count; i++)
    {
        unsigned char *tmp = p->buf;
        void *bulk_buf = tmp + i*size; 
        hret = margo_bulk_create(mid, 1, &bulk_buf, &size, flag,
            &p->bulks[i]);
        if (hret != HG_SUCCESS)
        {
            p->bulks[i] = HG_BULK_NULL;
            goto err;
        }
    }

    ret = ABT_mutex_create(&p->mutex);
    if (ret != ABT_SUCCESS)
    {
        hret = HG_OTHER_ERROR;
        goto err;
    }
    ret = ABT_cond_create(&p->cond);
    if (ret != ABT_SUCCESS)
    {
        ABT_mutex_free(&p->mutex);
        hret = HG_OTHER_ERROR;
        goto err;
    }

    *pool = p;
    return HG_SUCCESS;

err:
    if (p != NULL)
    {
        if (p->bulks != NULL)
        {
            for (i = 0; i < p->count && p->bulks[i] != HG_BULK_NULL; i++)
                margo_bulk_free(p->bulks[i]);
            free(p->bulks);
        }
        if(p->buf != NULL) free(p->buf);
        free(p);
    }
    *pool = NULL;
    return hret;
}

void margo_bulk_pool_destroy(
    margo_bulk_pool_t *pool)
{
    hg_size_t i;

    ABT_cond_free(&pool->cond);
    ABT_mutex_free(&pool->mutex);

    for (i = 0; i < pool->count; i++) margo_bulk_free(pool->bulks[i]);
    free(pool->bulks);
    free(pool->buf);
    free(pool);

    return;
}

static inline hg_bulk_t margo_bp_get(margo_bulk_pool_t *pool)
{
    return pool->bulks[pool->count - pool->num_free--];
}

static inline hg_bulk_t margo_bp_tryget(margo_bulk_pool_t *pool)
{
    return pool->num_free == 0 ? HG_BULK_NULL : margo_bp_get(pool);
}

hg_bulk_t margo_bulk_pool_get(
    margo_bulk_pool_t *pool)
{
    hg_bulk_t bulk;

    ABT_mutex_lock(pool->mutex);
    while (pool->num_free == 0)
        ABT_cond_wait(pool->cond, pool->mutex);
    assert(pool->num_free > 0);
    bulk = margo_bp_get(pool);
    ABT_mutex_unlock(pool->mutex);

    return bulk;
}

hg_bulk_t margo_bulk_pool_tryget(
    margo_bulk_pool_t *pool)
{
    hg_bulk_t bulk;

    ABT_mutex_lock(pool->mutex);
    bulk = margo_bp_tryget(pool);
    ABT_mutex_unlock(pool->mutex);

    return bulk;
}

static inline void margo_bp_release(margo_bulk_pool_t *pool, hg_bulk_t bulk)
{
    pool->bulks[pool->count - ++pool->num_free] = bulk;
}

void margo_bulk_pool_release(
    margo_bulk_pool_t *pool,
    hg_bulk_t bulk)
{
    if (bulk == HG_BULK_NULL) return;

    assert(pool->size == margo_bulk_get_size(bulk));

    ABT_mutex_lock(pool->mutex);
    assert(pool->num_free != pool->count);
    margo_bp_release(pool, bulk);
    ABT_cond_signal(pool->cond);
    ABT_mutex_unlock(pool->mutex);

    return;
}

hg_return_t margo_bulk_poolset_create(
    margo_instance_id mid,
    hg_size_t npools,
    hg_size_t nbufs,
    hg_size_t first_size,
    hg_size_t size_multiple,
    hg_uint8_t flag,
    margo_bulk_poolset_t **poolset)
{
    margo_bulk_poolset_t *s;
    hg_size_t i  = 0, j, size;
    hg_return_t hret;

    assert(npools > 0 && nbufs > 0 && first_size > 0 && size_multiple > 1); 

    s = malloc(sizeof(*s));
    if (s == NULL)
    {
        hret = HG_NOMEM_ERROR;
        goto err;
    }
    s->pools = malloc(npools * sizeof(*s->pools));
    if (s->pools == NULL)
    {
        hret = HG_NOMEM_ERROR;
        goto err;
    }

    s->npools = npools;
    s->nbufs = nbufs;
    s->first_size = first_size;
    s->size_multiple = size_multiple;

    size = first_size;
    for (i = 0; i < npools; i++)
    {
        hret = margo_bulk_pool_create(mid, nbufs, size, flag, &s->pools[i]);
        if (hret != HG_SUCCESS) goto err;
        size *= size_multiple;
    }

    *poolset = s;
    return HG_SUCCESS;

err:
    if (s)
    {
        if (s->pools)
        {
            for (j = 0; j < i; j++)
                margo_bulk_pool_destroy(s->pools[j]);
            free(s->pools);
        }
        free(s);
    }
    *poolset = NULL;
    return hret;
}

void margo_bulk_poolset_destroy(
    margo_bulk_poolset_t *poolset)
{
    hg_size_t i;

    for (i = 0; i < poolset->npools; i++)
        margo_bulk_pool_destroy(poolset->pools[i]);
    free(poolset->pools);
    free(poolset);

    return;
}

hg_bulk_t margo_bulk_poolset_get(
    margo_bulk_poolset_t *poolset,
    hg_size_t size)
{
    hg_size_t i;
    hg_size_t this_size = poolset->first_size;
    hg_size_t size_mult = poolset->size_multiple;

    for (i = 0; i < poolset->npools; i++)
    {
        if (size <= this_size)
            return margo_bulk_pool_get(poolset->pools[i]);
        this_size *= size_mult;
    }

    return HG_BULK_NULL;
}

hg_bulk_t margo_bulk_poolset_tryget(
    margo_bulk_poolset_t *poolset,
    hg_size_t size,
    hg_bool_t any_flag)
{
    hg_bulk_t bulk;
    hg_size_t i;
    hg_size_t this_size = poolset->first_size;
    hg_size_t size_mult = poolset->size_multiple;

    for (i = 0; i < poolset->npools; i++)
    {
        if (size <= this_size)
        {
            bulk = margo_bulk_pool_tryget(poolset->pools[i]);
            if (bulk != HG_BULK_NULL || any_flag == HG_FALSE) return bulk;
        }
        this_size *= size_mult;
    }

    return HG_BULK_NULL;
}

void margo_bulk_poolset_release(
    margo_bulk_poolset_t *poolset,
    hg_bulk_t bulk)
{
    if (bulk == HG_BULK_NULL) return;

    hg_size_t bulk_size = HG_Bulk_get_size(bulk);
    hg_size_t i;
    hg_size_t size = poolset->first_size;
    hg_size_t size_mult = poolset->size_multiple;

    for (i = 0; i < poolset->npools; i++)
    {
        if (bulk_size == size)
        {
            margo_bulk_pool_release(poolset->pools[i], bulk);
            return;
        }
        else size *= size_mult;
    }
    assert(0); /* should not reach this ... */
}
