/*
 * (C) 2015 The University of Chicago
 *
 * See COPYRIGHT in top-level directory.
 */

#ifndef __SVC2_CLIENT
#define __SVC2_CLIENT

#include <margo.h>

int svc2_register_client(margo_instance_id mid);

void svc2_do_thing(margo_instance_id mid,
                   hg_addr_t         svr_addr,
                   uint32_t          provider_id);
void svc2_do_other_thing(margo_instance_id mid,
                         hg_addr_t         svr_addr,
                         uint32_t          provider_id);

#endif /* __SVC2_CLIENT */
