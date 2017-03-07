/*
 * (C) 2015 The University of Chicago
 * 
 * See COPYRIGHT in top-level directory.
 */

#ifndef __SVC1_CLIENT
#define __SVC1_CLIENT

#include <margo.h>

void svc1_do_thing(margo_instance_id mid, hg_addr_t svr_addr, uint32_t mplex_id);

#endif /* __SVC1_CLIENT */
