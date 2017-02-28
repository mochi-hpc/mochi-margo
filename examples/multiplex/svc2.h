/*
 * (C) 2015 The University of Chicago
 * 
 * See COPYRIGHT in top-level directory.
 */

#ifndef __SVC2
#define __SVC2

#include <margo.h>

/* this is an example service that will be registered as a unit on a
 * centralized progress engine */

int svc2_register(margo_instance_id mid, ABT_pool pool, uint32_t mplex_id);
void svc2_deregister(margo_instance_id mid, ABT_pool pool, uint32_t mplex_id);

MERCURY_GEN_PROC(svc2_do_thing_out_t, ((int32_t)(ret)))
MERCURY_GEN_PROC(svc2_do_thing_in_t,
    ((int32_t)(input_val))\
    ((hg_bulk_t)(bulk_handle)))
DECLARE_MARGO_RPC_HANDLER(svc2_do_thing_ult)

MERCURY_GEN_PROC(svc2_do_other_thing_out_t, ((int32_t)(ret)))
MERCURY_GEN_PROC(svc2_do_other_thing_in_t,
    ((int32_t)(input_val))\
    ((hg_bulk_t)(bulk_handle)))
DECLARE_MARGO_RPC_HANDLER(svc2_do_other_thing_ult)

#endif /* __SVC2 */
