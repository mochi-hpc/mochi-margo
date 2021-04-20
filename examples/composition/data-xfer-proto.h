/*
 * (C) 2015 The University of Chicago
 *
 * See COPYRIGHT in top-level directory.
 */

#ifndef __DATA_XFER_PROTO
#define __DATA_XFER_PROTO

#include <margo.h>
#include <mercury_proc_string.h>

MERCURY_GEN_PROC(data_xfer_read_out_t, ((int32_t)(ret)))
MERCURY_GEN_PROC(data_xfer_read_in_t,
                 ((hg_string_t)(client_addr))((hg_bulk_t)(bulk_handle)))

#endif /* __DATA_XFER_PROTO */
