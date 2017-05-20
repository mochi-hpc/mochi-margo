/*
 * (C) 2015 The University of Chicago
 * 
 * See COPYRIGHT in top-level directory.
 */

#include <assert.h>
#include <pthread.h>
#include "data-xfer-proto.h"
#include "delegator-proto.h"
#include "delegator-service.h"

static hg_addr_t g_data_xfer_svc_addr = HG_ADDR_NULL;
static hg_id_t g_data_xfer_read_id = -1;

static void delegator_read_ult(hg_handle_t handle)
{
    hg_return_t hret;
    delegator_read_out_t out;
    delegator_read_in_t in;
    data_xfer_read_in_t in_relay;
    data_xfer_read_out_t out_relay;
    int ret;
    const struct hg_info *hgi;
    margo_instance_id mid;
    hg_handle_t handle_relay;
#if 0
    ABT_thread my_ult;
    ABT_xstream my_xstream; 
    pthread_t my_tid;
#endif

    ret = HG_Get_input(handle, &in);
    assert(ret == HG_SUCCESS);
    hgi = HG_Get_info(handle);
    assert(hgi);

#if 0
    ABT_xstream_self(&my_xstream);
    ABT_thread_self(&my_ult);
    my_tid = pthread_self();
    printf("svc1: do_thing: mplex_id: %u, ult: %p, xstream %p, tid: %lu\n", 
        hgi->target_id, my_ult, my_xstream, my_tid);
#endif

    out.ret = 0;

    mid = margo_hg_class_to_instance(hgi->hg_class);
    
    /* relay to microservice */
    hret = HG_Create(margo_get_context(mid), g_data_xfer_svc_addr, g_data_xfer_read_id, &handle_relay);
    assert(hret == HG_SUCCESS);
    /* pass through bulk handle */
    in_relay.bulk_handle = in.bulk_handle; 
    margo_forward(mid, handle_relay, &in_relay);

    hret = HG_Get_output(handle_relay, &out_relay);
    assert(hret == HG_SUCCESS);

    HG_Free_input(handle, &in);
    HG_Free_output(handle_relay, &out);

    hret = HG_Respond(handle, NULL, NULL, &out);
    assert(hret == HG_SUCCESS);

    HG_Destroy(handle);
    HG_Destroy(handle_relay);

    return;
}
DEFINE_MARGO_RPC_HANDLER(delegator_read_ult)

int delegator_register(margo_instance_id mid, ABT_pool pool, uint32_t mplex_id, hg_addr_t data_xfer_svc_addr)
{
    /* save addr to relay to */
    g_data_xfer_svc_addr = data_xfer_svc_addr;

    /* register client-side of function to relay */
    g_data_xfer_read_id = MERCURY_REGISTER(margo_get_class(mid), "data_xfer_read",
        data_xfer_read_in_t, data_xfer_read_out_t, NULL);

    /* register RPC handler */
    MARGO_REGISTER(mid, "delegator_read", delegator_read_in_t, delegator_read_out_t, delegator_read_ult_handler, mplex_id, pool);

    return(0);
}

void delegator_deregister(margo_instance_id mid, ABT_pool pool, uint32_t mplex_id)
{
    HG_Addr_free(margo_get_class(mid), g_data_xfer_svc_addr);

    /* TODO: undo what was done in delegator_register() */
    return;
}

