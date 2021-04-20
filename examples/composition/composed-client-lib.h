/*
 * (C) 2015 The University of Chicago
 *
 * See COPYRIGHT in top-level directory.
 */

#ifndef __COMPOSED_CLIENT_LIB
#define __COMPOSED_CLIENT_LIB

#include <margo.h>

int  composed_register_client(margo_instance_id mid);
void composed_read(margo_instance_id mid,
                   hg_addr_t         svr_addr,
                   void*             buffer,
                   hg_size_t         buffer_sz,
                   char*             data_xfer_svc_addr_string);

int  data_xfer_register_client(margo_instance_id mid);
void data_xfer_read(margo_instance_id mid,
                    hg_addr_t         svr_addr,
                    void*             buffer,
                    hg_size_t         buffer_sz);

#endif /* __COMPOSED_CLIENT_LIB */
