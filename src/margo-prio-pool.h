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

void margo_create_prio_pool_def(ABT_pool_def* p_def);

#ifdef __cplusplus
}
#endif

#endif /* __MARGO_PRIO_POOL */
