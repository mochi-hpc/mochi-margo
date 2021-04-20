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

static hg_id_t g_data_xfer_read_id = -1;

static void delegator_read_ult(hg_handle_t handle)
{
    hg_return_t           hret;
    delegator_read_out_t  out;
    delegator_read_in_t   in;
    data_xfer_read_in_t   in_relay;
    data_xfer_read_out_t  out_relay;
    const struct hg_info* hgi;
    margo_instance_id     mid;
    hg_addr_t             data_xfer_svc_addr;

    hg_handle_t handle_relay;
    char        client_addr_string[64];
    hg_size_t   client_addr_string_sz = 64;
#if 0
    ABT_thread my_ult;
    ABT_xstream my_xstream; 
    pthread_t my_tid;
#endif

    hret = margo_get_input(handle, &in);
    assert(hret == HG_SUCCESS);
    hgi = margo_get_info(handle);
    assert(hgi);
    mid = margo_hg_info_get_instance(hgi);
    assert(mid != MARGO_INSTANCE_NULL);

#if 0
    ABT_xstream_self(&my_xstream);
    ABT_thread_self(&my_ult);
    my_tid = pthread_self();
    printf("svc1: do_thing: provider_id: %u, ult: %p, xstream %p, tid: %lu\n", 
        hgi->target_id, my_ult, my_xstream, my_tid);
#endif

    out.ret = 0;

    hret = margo_addr_lookup(mid, in.data_xfer_svc_addr, &data_xfer_svc_addr);
    assert(hret == HG_SUCCESS);

    /* relay to microservice */
    hret = margo_create(mid, data_xfer_svc_addr, g_data_xfer_read_id,
                        &handle_relay);
    assert(hret == HG_SUCCESS);
    /* pass through bulk handle */
    in_relay.bulk_handle = in.bulk_handle;
    /* get addr of client to relay to data_xfer service */
    hret = margo_addr_to_string(mid, client_addr_string, &client_addr_string_sz,
                                hgi->addr);
    assert(hret == HG_SUCCESS);
    in_relay.client_addr = client_addr_string;
    hret                 = margo_forward(handle_relay, &in_relay);
    assert(hret == HG_SUCCESS);

    hret = margo_get_output(handle_relay, &out_relay);
    assert(hret == HG_SUCCESS);

    margo_free_input(handle, &in);
    margo_free_output(handle_relay, &out);

    hret = margo_respond(handle, &out);
    assert(hret == HG_SUCCESS);

    margo_addr_free(mid, data_xfer_svc_addr);
    margo_destroy(handle);
    margo_destroy(handle_relay);

    return;
}
DEFINE_MARGO_RPC_HANDLER(delegator_read_ult)

int delegator_service_register(margo_instance_id mid,
                               ABT_pool          pool,
                               uint32_t          provider_id)
{
    /* register client-side of function to relay */
    /* NOTE: this RPC may already be registered if this process has already
     * registered a data-xfer service
     */
    g_data_xfer_read_id = MARGO_REGISTER(
        mid, "data_xfer_read", data_xfer_read_in_t, data_xfer_read_out_t, NULL);

    /* register RPC handler */
    MARGO_REGISTER_PROVIDER(mid, "delegator_read", delegator_read_in_t,
                            delegator_read_out_t, delegator_read_ult,
                            provider_id, pool);

    return (0);
}

void delegator_deregister(margo_instance_id mid,
                          ABT_pool          pool,
                          uint32_t          provider_id)
{
    /* TODO: undo what was done in delegator_register() */
    return;
}
