/*
 * (C) 2015 The University of Chicago
 * 
 * See COPYRIGHT in top-level directory.
 */

#ifndef __DATA_XFER_PROTO
#define __DATA_XFER_PROTO

#include <margo.h>

MERCURY_GEN_PROC(data_xfer_read_out_t, ((int32_t)(ret)))
MERCURY_GEN_PROC(data_xfer_read_in_t,
    ((hg_bulk_t)(bulk_handle)))

#endif /* __DATA_XFER_PROTO */
