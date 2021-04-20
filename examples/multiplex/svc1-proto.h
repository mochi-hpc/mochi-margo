/*
 * (C) 2015 The University of Chicago
 *
 * See COPYRIGHT in top-level directory.
 */

#ifndef __SVC1_PROTO
#define __SVC1_PROTO

#include <margo.h>

MERCURY_GEN_PROC(svc1_do_thing_out_t, ((int32_t)(ret)))
MERCURY_GEN_PROC(svc1_do_thing_in_t,
                 ((int32_t)(input_val))((hg_bulk_t)(bulk_handle)))

MERCURY_GEN_PROC(svc1_do_other_thing_out_t, ((int32_t)(ret)))
MERCURY_GEN_PROC(svc1_do_other_thing_in_t,
                 ((int32_t)(input_val))((hg_bulk_t)(bulk_handle)))

#endif /* __SVC1_PROTO */
