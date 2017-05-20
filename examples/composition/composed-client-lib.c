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

    delegator_read_id = MERCURY_REGISTER(margo_get_class(mid), "delegator_read", 
        delegator_read_in_t, delegator_read_out_t, NULL);

    return(0);
}

int data_xfer_register_client(margo_instance_id mid)
{

    data_xfer_read_id = MERCURY_REGISTER(margo_get_class(mid), "data_xfer_read", 
        data_xfer_read_in_t, data_xfer_read_out_t, NULL);

    return(0);
}

void composed_read(margo_instance_id mid, hg_addr_t svr_addr, void *buffer, hg_size_t buffer_sz)
{
    hg_handle_t handle;
    delegator_read_in_t in;
    delegator_read_out_t out;
    int ret;
    const struct hg_info *hgi;

    /* create handle */
    ret = HG_Create(margo_get_context(mid), svr_addr, delegator_read_id, &handle);
    assert(ret == 0);

    /* register buffer for rdma/bulk access by server */
    hgi = HG_Get_info(handle);
    assert(hgi);
    ret = HG_Bulk_create(hgi->hg_class, 1, &buffer, &buffer_sz, 
        HG_BULK_WRITE_ONLY, &in.bulk_handle);
    assert(ret == 0);

#if 0
    HG_Set_target_id(handle, mplex_id);
#endif

    /* Send rpc. Note that we are also transmitting the bulk handle in the
     * input struct.  It was set above. 
     */ 
    margo_forward(mid, handle, &in);

    /* decode response */
    ret = HG_Get_output(handle, &out);
    assert(ret == 0);

    /* clean up resources consumed by this rpc */
    HG_Bulk_free(in.bulk_handle);
    HG_Free_output(handle, &out);
    HG_Destroy(handle);

    return;
}

void data_xfer_read(margo_instance_id mid, hg_addr_t svr_addr, void *buffer, hg_size_t buffer_sz)
{
    hg_handle_t handle;
    data_xfer_read_in_t in;
    data_xfer_read_out_t out;
    int ret;
    const struct hg_info *hgi;

    /* create handle */
    ret = HG_Create(margo_get_context(mid), svr_addr, data_xfer_read_id, &handle);
    assert(ret == 0);

    /* register buffer for rdma/bulk access by server */
    hgi = HG_Get_info(handle);
    assert(hgi);
    ret = HG_Bulk_create(hgi->hg_class, 1, &buffer, &buffer_sz, 
        HG_BULK_WRITE_ONLY, &in.bulk_handle);
    assert(ret == 0);

#if 0
    HG_Set_target_id(handle, mplex_id);
#endif

    /* Send rpc. Note that we are also transmitting the bulk handle in the
     * input struct.  It was set above. 
     */ 
    margo_forward(mid, handle, &in);

    /* decode response */
    ret = HG_Get_output(handle, &out);
    assert(ret == 0);

    /* clean up resources consumed by this rpc */
    HG_Bulk_free(in.bulk_handle);
    HG_Free_output(handle, &out);
    HG_Destroy(handle);

    return;
}

