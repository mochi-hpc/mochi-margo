/*
 * (C) 2015 The University of Chicago
 *
 * See COPYRIGHT in top-level directory.
 */

#ifndef __SVC2_SERVER
#define __SVC2_SERVER

#include <margo.h>

int  svc2_register(margo_instance_id mid, ABT_pool pool, uint32_t provider_id);
void svc2_deregister(margo_instance_id mid,
                     ABT_pool          pool,
                     uint32_t          provider_id);

#endif /* __SVC2_SERVER */
