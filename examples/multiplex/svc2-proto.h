/*
 * (C) 2015 The University of Chicago
 *
 * See COPYRIGHT in top-level directory.
 */

#ifndef __SVC2_PROTO
#define __SVC2_PROTO

#include <margo.h>

MERCURY_GEN_PROC(svc2_do_thing_out_t, ((int32_t)(ret)))
MERCURY_GEN_PROC(svc2_do_thing_in_t,
                 ((int32_t)(input_val))((hg_bulk_t)(bulk_handle)))

MERCURY_GEN_PROC(svc2_do_other_thing_out_t, ((int32_t)(ret)))
MERCURY_GEN_PROC(svc2_do_other_thing_in_t,
                 ((int32_t)(input_val))((hg_bulk_t)(bulk_handle)))

#endif /* __SVC2_PROTO */
