/*
 * (C) 2015 The University of Chicago
 *
 * See COPYRIGHT in top-level directory.
 */

#ifndef __SVC1_SERVER
#define __SVC1_SERVER

#include <margo.h>

int  svc1_register(margo_instance_id mid, ABT_pool pool, uint32_t provider_id);
void svc1_deregister(margo_instance_id mid,
                     ABT_pool          pool,
                     uint32_t          provider_id);

#endif /* __SVC1_SERVER */
