/*
 * (C) 2015 The University of Chicago
 *
 * See COPYRIGHT in top-level directory.
 */

#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <unistd.h>
#include <abt.h>

#include "svc2-client.h"
#include "svc2-proto.h"

/* NOTE: just making these global for test example, would be cleaner if there
 * were an instance pointer for the client.
 */
static hg_id_t svc2_do_thing_id       = -1;
static hg_id_t svc2_do_other_thing_id = -1;

int svc2_register_client(margo_instance_id mid)
{

    svc2_do_thing_id = MARGO_REGISTER(mid, "svc2_do_thing", svc2_do_thing_in_t,
                                      svc2_do_thing_out_t, NULL);
    svc2_do_other_thing_id
        = MARGO_REGISTER(mid, "svc2_do_other_thing", svc2_do_other_thing_in_t,
                         svc2_do_other_thing_out_t, NULL);

    return (0);
}

void svc2_do_thing(margo_instance_id mid,
                   hg_addr_t         svr_addr,
                   uint32_t          provider_id)
{
    hg_handle_t         handle;
    svc2_do_thing_in_t  in;
    svc2_do_thing_out_t out;
    hg_return_t         hret;
    hg_size_t           size;
    void*               buffer;

    /* allocate buffer for bulk transfer */
    size   = 512;
    buffer = calloc(1, 512);
    assert(buffer);
    sprintf((char*)buffer, "Hello world!\n");

    /* create handle */
    hret = margo_create(mid, svr_addr, svc2_do_thing_id, &handle);
    assert(hret == HG_SUCCESS);

    /* register buffer for rdma/bulk access by server */
    hret = margo_bulk_create(mid, 1, &buffer, &size, HG_BULK_READ_ONLY,
                             &in.bulk_handle);
    assert(hret == HG_SUCCESS);

    /* Send rpc. Note that we are also transmitting the bulk handle in the
     * input struct.  It was set above.
     */
    in.input_val = 0;
    hret         = margo_provider_forward(provider_id, handle, &in);
    assert(hret == HG_SUCCESS);

    /* decode response */
    hret = margo_get_output(handle, &out);
    assert(hret == HG_SUCCESS);

    /* clean up resources consumed by this rpc */
    margo_free_output(handle, &out);
    margo_bulk_free(in.bulk_handle);
    margo_destroy(handle);
    free(buffer);

    return;
}

void svc2_do_other_thing(margo_instance_id mid,
                         hg_addr_t         svr_addr,
                         uint32_t          provider_id)
{
    hg_handle_t               handle;
    svc2_do_other_thing_in_t  in;
    svc2_do_other_thing_out_t out;
    hg_return_t               hret;
    hg_size_t                 size;
    void*                     buffer;

    /* allocate buffer for bulk transfer */
    size   = 512;
    buffer = calloc(1, 512);
    assert(buffer);
    sprintf((char*)buffer, "Hello world!\n");

    /* create handle */
    hret = margo_create(mid, svr_addr, svc2_do_other_thing_id, &handle);
    assert(hret == HG_SUCCESS);

    /* register buffer for rdma/bulk access by server */
    hret = margo_bulk_create(mid, 1, &buffer, &size, HG_BULK_READ_ONLY,
                             &in.bulk_handle);
    assert(hret == HG_SUCCESS);

    /* Send rpc. Note that we are also transmitting the bulk handle in the
     * input struct.  It was set above.
     */
    in.input_val = 0;
    hret         = margo_provider_forward(provider_id, handle, &in);
    assert(hret == HG_SUCCESS);

    /* decode response */
    hret = margo_get_output(handle, &out);
    assert(hret == HG_SUCCESS);

    /* clean up resources consumed by this rpc */
    margo_free_output(handle, &out);
    margo_bulk_free(in.bulk_handle);
    margo_destroy(handle);
    free(buffer);

    return;
}
