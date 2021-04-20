/*
 * (C) 2015 The University of Chicago
 *
 * See COPYRIGHT in top-level directory.
 */

#include <stdio.h>
#include <assert.h>
#include <unistd.h>
#include <abt.h>

#include "composed-client-lib.h"
#include "delegator-proto.h"
#include "data-xfer-proto.h"

/* NOTE: just making these global for test example, would be cleaner if there
 * were an instance pointer for the client.
 */
static hg_id_t delegator_read_id = -1;
static hg_id_t data_xfer_read_id = -1;

int composed_register_client(margo_instance_id mid)
{

    delegator_read_id = MARGO_REGISTER(
        mid, "delegator_read", delegator_read_in_t, delegator_read_out_t, NULL);

    return (0);
}

int data_xfer_register_client(margo_instance_id mid)
{

    data_xfer_read_id = MARGO_REGISTER(
        mid, "data_xfer_read", data_xfer_read_in_t, data_xfer_read_out_t, NULL);

    return (0);
}

void composed_read(margo_instance_id mid,
                   hg_addr_t         svr_addr,
                   void*             buffer,
                   hg_size_t         buffer_sz,
                   char*             data_xfer_svc_addr_string)
{
    hg_handle_t          handle;
    delegator_read_in_t  in;
    delegator_read_out_t out;
    hg_return_t          hret;

    /* create handle */
    hret = margo_create(mid, svr_addr, delegator_read_id, &handle);
    assert(hret == HG_SUCCESS);

    /* register buffer for rdma/bulk access by server */
    hret = margo_bulk_create(mid, 1, &buffer, &buffer_sz, HG_BULK_WRITE_ONLY,
                             &in.bulk_handle);
    assert(hret == HG_SUCCESS);

    in.data_xfer_svc_addr = data_xfer_svc_addr_string;

    /* Send rpc. Note that we are also transmitting the bulk handle in the
     * input struct.  It was set above.
     */
    hret = margo_forward(handle, &in);
    assert(hret == HG_SUCCESS);

    /* decode response */
    hret = margo_get_output(handle, &out);
    assert(hret == HG_SUCCESS);

    /* clean up resources consumed by this rpc */
    margo_free_output(handle, &out);
    margo_bulk_free(in.bulk_handle);
    margo_destroy(handle);

    return;
}

void data_xfer_read(margo_instance_id mid,
                    hg_addr_t         svr_addr,
                    void*             buffer,
                    hg_size_t         buffer_sz)
{
    hg_handle_t          handle;
    data_xfer_read_in_t  in;
    data_xfer_read_out_t out;
    hg_return_t          hret;
    hg_addr_t            addr_self;
    char                 addr_self_string[128];
    hg_size_t            addr_self_string_sz = 128;

    /* create handle */
    hret = margo_create(mid, svr_addr, data_xfer_read_id, &handle);
    assert(hret == HG_SUCCESS);

    /* register buffer for rdma/bulk access by server */
    hret = margo_bulk_create(mid, 1, &buffer, &buffer_sz, HG_BULK_WRITE_ONLY,
                             &in.bulk_handle);
    assert(hret == HG_SUCCESS);

    /* figure out local address */
    hret = margo_addr_self(mid, &addr_self);
    assert(hret == HG_SUCCESS);

    hret = margo_addr_to_string(mid, addr_self_string, &addr_self_string_sz,
                                addr_self);
    assert(hret == HG_SUCCESS);

    in.client_addr = addr_self_string;

    /* Send rpc. Note that we are also transmitting the bulk handle in the
     * input struct.  It was set above.
     */
    hret = margo_forward(handle, &in);
    assert(hret == HG_SUCCESS);

    /* decode response */
    hret = margo_get_output(handle, &out);
    assert(hret == HG_SUCCESS);

    /* clean up resources consumed by this rpc */
    margo_free_output(handle, &out);
    margo_bulk_free(in.bulk_handle);
    margo_destroy(handle);
    margo_addr_free(mid, addr_self);

    return;
}
