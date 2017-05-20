/*
 * (C) 2015 The University of Chicago
 * 
 * See COPYRIGHT in top-level directory.
 */

#ifndef __DELEGATOR_PROTO
#define __DELEGATOR_PROTO

#include <margo.h>

MERCURY_GEN_PROC(delegator_read_out_t, ((int32_t)(ret)))
MERCURY_GEN_PROC(delegator_read_in_t,
    ((hg_bulk_t)(bulk_handle)))

#endif /* __DELEGATOR_PROTO */
