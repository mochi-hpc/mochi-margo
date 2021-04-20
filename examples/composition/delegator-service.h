/*
 * (C) 2015 The University of Chicago
 *
 * See COPYRIGHT in top-level directory.
 */

#ifndef __DELEGATOR_SERVICE
#define __DELEGATOR_SERVICE

#include <margo.h>

int  delegator_service_register(margo_instance_id mid,
                                ABT_pool          pool,
                                uint32_t          provider_id);
void delegator_service_deregister(margo_instance_id mid,
                                  ABT_pool          pool,
                                  uint32_t          provider_id);

#endif /* __DELEGATOR_SERVICE */
