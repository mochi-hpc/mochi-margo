/*
 * (C) 2015 The University of Chicago
 * 
 * See COPYRIGHT in top-level directory.
 */

#ifndef __DELEGATOR_SERVICE
#define __DELEGATOR_SERVICE

#include <margo.h>

int delegator_service_register(margo_instance_id mid, ABT_pool pool, uint32_t mplex_id, hg_addr_t data_xfer_svc_addr);
void delegator_service_deregister(margo_instance_id mid, ABT_pool pool, uint32_t mplex_id);

#endif /* __DELEGATOR_SERVICE */
