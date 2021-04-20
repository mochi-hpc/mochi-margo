/*
 * (C) 2015 The University of Chicago
 *
 * See COPYRIGHT in top-level directory.
 */

#ifndef __DELEGATOR_PROTO
#define __DELEGATOR_PROTO

#include <margo.h>
#include <mercury_proc_string.h>

MERCURY_GEN_PROC(delegator_read_out_t, ((int32_t)(ret)))
MERCURY_GEN_PROC(delegator_read_in_t,
                 ((hg_string_t)(data_xfer_svc_addr))((hg_bulk_t)(bulk_handle)))

#endif /* __DELEGATOR_PROTO */
