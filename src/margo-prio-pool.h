/*
 * (C) 2021 The University of Chicago
 *
 * See COPYRIGHT in top-level directory.
 */

#ifndef __MARGO_PRIO_POOL
#define __MARGO_PRIO_POOL

#ifdef __cplusplus
extern "C" {
#endif

#include <abt.h>
#include <stdatomic.h>

void margo_create_prio_pool_def(ABT_pool_def* p_def);
#define MARGO_PRIO_POOL_CONFIG_KEY_EFD 0
struct margo_prio_pool_efd {
    _Atomic int efd_count;
    int         efd;
};

#ifdef __cplusplus
}
#endif

#endif /* __MARGO_PRIO_POOL */
