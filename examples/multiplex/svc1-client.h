/*
 * (C) 2015 The University of Chicago
 *
 * See COPYRIGHT in top-level directory.
 */

#ifndef __SVC1_CLIENT
#define __SVC1_CLIENT

#include <margo.h>

int svc1_register_client(margo_instance_id mid);

void svc1_do_thing(margo_instance_id mid,
                   hg_addr_t         svr_addr,
                   uint32_t          provider_id);
void svc1_do_other_thing(margo_instance_id mid,
                         hg_addr_t         svr_addr,
                         uint32_t          provider_id);

#endif /* __SVC1_CLIENT */
