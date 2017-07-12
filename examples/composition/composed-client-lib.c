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

	MARGO_REGISTER(mid, "delegator_read", 
        delegator_read_in_t, delegator_read_out_t, NULL, &delegator_read_id);

    return(0);
}

int data_xfer_register_client(margo_instance_id mid)
{

	MARGO_REGISTER(mid, "data_xfer_read", 
        data_xfer_read_in_t, data_xfer_read_out_t, NULL, &data_xfer_read_id);

    return(0);
}

void composed_read(margo_instance_id mid, hg_addr_t svr_addr, void *buffer, hg_size_t buffer_sz, char *data_xfer_svc_addr_string)
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

    in.data_xfer_svc_addr = data_xfer_svc_addr_string;

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
    hg_addr_t addr_self;
    char addr_self_string[128];
    hg_size_t addr_self_string_sz = 128;

    /* create handle */
    ret = HG_Create(margo_get_context(mid), svr_addr, data_xfer_read_id, &handle);
    assert(ret == 0);

    /* register buffer for rdma/bulk access by server */
    hgi = HG_Get_info(handle);
    assert(hgi);
    ret = HG_Bulk_create(hgi->hg_class, 1, &buffer, &buffer_sz, 
        HG_BULK_WRITE_ONLY, &in.bulk_handle);
    assert(ret == 0);

    /* figure out local address */
    ret = HG_Addr_self(margo_get_class(mid), &addr_self);
    assert(ret == HG_SUCCESS);

    ret = HG_Addr_to_string(margo_get_class(mid), addr_self_string, &addr_self_string_sz, addr_self);
    assert(ret == HG_SUCCESS);

    in.client_addr = addr_self_string;

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
    HG_Addr_free(margo_get_class(mid), addr_self);

    return;
}

