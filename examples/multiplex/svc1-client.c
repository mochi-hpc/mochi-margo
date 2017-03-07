/*
 * (C) 2015 The University of Chicago
 * 
 * See COPYRIGHT in top-level directory.
 */

#include <stdio.h>
#include <assert.h>
#include <unistd.h>
#include <abt.h>

#include "svc1-client.h"
#include "svc1-proto.h"

/* NOTE: just making these global for test example, would be cleaner if there
 * were an instance pointer for the client.
 */
static hg_id_t svc1_do_thing_id = -1;
static hg_id_t svc1_do_other_thing_id = -1;

int svc1_register_client(margo_instance_id mid)
{

    svc1_do_thing_id = MERCURY_REGISTER(margo_get_class(mid), "svc1_do_thing", 
        svc1_do_thing_in_t, svc1_do_thing_out_t, NULL);

    svc1_do_other_thing_id = MERCURY_REGISTER(margo_get_class(mid), "svc1_do_other_thing", 
        svc1_do_other_thing_in_t, svc1_do_other_thing_out_t, NULL);

    return(0);
}

void svc1_do_thing(margo_instance_id mid, hg_addr_t svr_addr, uint32_t mplex_id)
{
    hg_handle_t handle;
    svc1_do_thing_in_t in;
    svc1_do_thing_out_t out;
    int ret;
    hg_size_t size;
    void* buffer;
    struct hg_info *hgi;

    /* allocate buffer for bulk transfer */
    size = 512;
    buffer = calloc(1, 512);
    assert(buffer);
    sprintf((char*)buffer, "Hello world!\n");

    /* create handle */
    ret = HG_Create(margo_get_context(mid), svr_addr, svc1_do_thing_id, &handle);
    assert(ret == 0);

    /* register buffer for rdma/bulk access by server */
    hgi = HG_Get_info(handle);
    assert(hgi);
    ret = HG_Bulk_create(hgi->hg_class, 1, &buffer, &size, 
        HG_BULK_READ_ONLY, &in.bulk_handle);
    assert(ret == 0);

    hgi->mplex_id = mplex_id;

    /* Send rpc. Note that we are also transmitting the bulk handle in the
     * input struct.  It was set above. 
     */ 
    in.input_val = 0;
    margo_forward(mid, handle, &in);

    /* decode response */
    ret = HG_Get_output(handle, &out);
    assert(ret == 0);

    /* clean up resources consumed by this rpc */
    HG_Bulk_free(in.bulk_handle);
    HG_Free_output(handle, &out);
    HG_Destroy(handle);
    free(buffer);

    return;
}

