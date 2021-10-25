/*
 * (C) 2015 The University of Chicago
 *
 * See COPYRIGHT in top-level directory.
 */

#include <assert.h>
#include <stdlib.h>
#include <pthread.h>
#include "data-xfer-proto.h"
#include "data-xfer-service.h"

static hg_size_t g_buffer_size = 8 * 1024 * 1024;
static void*     g_buffer;
static hg_bulk_t g_buffer_bulk_handle;

static void data_xfer_read_ult(hg_handle_t handle)
{
    hg_return_t           hret;
    data_xfer_read_out_t  out;
    data_xfer_read_in_t   in;
    const struct hg_info* hgi;
    margo_instance_id     mid;
    hg_addr_t             client_addr;
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

    if (!in.client_addr)
        client_addr = hgi->addr;
    else {
        hret = margo_addr_lookup(mid, in.client_addr, &client_addr);
        assert(hret == HG_SUCCESS);
    }

    /* do bulk transfer from client to server */
    hret = margo_bulk_transfer(mid, HG_BULK_PUSH, client_addr, in.bulk_handle,
                               0, g_buffer_bulk_handle, 0, g_buffer_size);
    assert(hret == HG_SUCCESS);

    if (in.client_addr) margo_addr_free(mid, client_addr);

    margo_free_input(handle, &in);

    hret = margo_respond(handle, &out);
    assert(hret == HG_SUCCESS);

    margo_destroy(handle);

    return;
}
DEFINE_MARGO_RPC_HANDLER(data_xfer_read_ult)

int data_xfer_service_register(margo_instance_id mid,
                               ABT_pool          pool,
                               uint32_t          provider_id)
{
    hg_return_t hret;

    /* set up global target buffer for bulk transfer */
    g_buffer = calloc(1, g_buffer_size);
    assert(g_buffer);

    /* register local target buffer for bulk access */
    hret = margo_bulk_create(mid, 1, &g_buffer, &g_buffer_size,
                             HG_BULK_READ_ONLY, &g_buffer_bulk_handle);
    assert(hret == HG_SUCCESS);

    /* register RPC handler */
    MARGO_REGISTER_PROVIDER(mid, "data_xfer_read", data_xfer_read_in_t,
                            data_xfer_read_out_t, data_xfer_read_ult,
                            provider_id, pool);

    return (0);
}

void data_xfer_deregister(margo_instance_id mid,
                          ABT_pool          pool,
                          uint32_t          provider_id)
{
    margo_bulk_free(g_buffer_bulk_handle);
    free(g_buffer);

    /* TODO: undo what was done in data_xfer_register() */
    return;
}
