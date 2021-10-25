/*
 * (C) 2015 The University of Chicago
 *
 * See COPYRIGHT in top-level directory.
 */

#include <assert.h>
#include <stdlib.h>
#include <pthread.h>
#include "svc1-proto.h"
#include "svc1-server.h"

static void svc1_do_thing_ult(hg_handle_t handle)
{
    hg_return_t           hret;
    svc1_do_thing_out_t   out;
    svc1_do_thing_in_t    in;
    hg_size_t             size;
    void*                 buffer;
    hg_bulk_t             bulk_handle;
    const struct hg_info* hgi;
    margo_instance_id     mid;
    ABT_thread            my_ult;
    ABT_xstream           my_xstream;
    pthread_t             my_tid;

    hret = margo_get_input(handle, &in);
    assert(hret == HG_SUCCESS);
    hgi = margo_get_info(handle);
    assert(hgi);
    mid = margo_hg_info_get_instance(hgi);
    assert(mid != MARGO_INSTANCE_NULL);

    ABT_xstream_self(&my_xstream);
    ABT_thread_self(&my_ult);
    my_tid = pthread_self();
    printf("svc1: do_thing: ult: %p, xstream %p, tid: %lu\n", my_ult,
           my_xstream, my_tid);

    out.ret = 0;

    /* set up target buffer for bulk transfer */
    size   = 512;
    buffer = calloc(1, 512);
    assert(buffer);

    /* register local target buffer for bulk access */
    hret = margo_bulk_create(mid, 1, &buffer, &size, HG_BULK_WRITE_ONLY,
                             &bulk_handle);
    assert(hret == HG_SUCCESS);

    /* do bulk transfer from client to server */
    hret = margo_bulk_transfer(mid, HG_BULK_PULL, hgi->addr, in.bulk_handle, 0,
                               bulk_handle, 0, size);
    assert(hret == HG_SUCCESS);

    margo_free_input(handle, &in);

    hret = margo_respond(handle, &out);
    assert(hret == HG_SUCCESS);

    margo_bulk_free(bulk_handle);
    margo_destroy(handle);
    free(buffer);

    return;
}
DEFINE_MARGO_RPC_HANDLER(svc1_do_thing_ult)

static void svc1_do_other_thing_ult(hg_handle_t handle)
{
    hg_return_t               hret;
    svc1_do_other_thing_out_t out;
    svc1_do_other_thing_in_t  in;
    hg_size_t                 size;
    void*                     buffer;
    hg_bulk_t                 bulk_handle;
    const struct hg_info*     hgi;
    margo_instance_id         mid;
    ABT_thread                my_ult;
    ABT_xstream               my_xstream;
    pthread_t                 my_tid;

    hret = margo_get_input(handle, &in);
    assert(hret == HG_SUCCESS);
    hgi = margo_get_info(handle);
    assert(hgi);
    mid = margo_hg_info_get_instance(hgi);
    assert(mid != MARGO_INSTANCE_NULL);

    ABT_xstream_self(&my_xstream);
    ABT_thread_self(&my_ult);
    my_tid = pthread_self();
    printf("svc1: do_other_thing: ult: %p, xstream %p, tid: %lu\n", my_ult,
           my_xstream, my_tid);

    out.ret = 0;

    /* set up target buffer for bulk transfer */
    size   = 512;
    buffer = calloc(1, 512);
    assert(buffer);

    /* register local target buffer for bulk access */
    hret = margo_bulk_create(mid, 1, &buffer, &size, HG_BULK_WRITE_ONLY,
                             &bulk_handle);
    assert(hret == HG_SUCCESS);

    /* do bulk transfer from client to server */
    hret = margo_bulk_transfer(mid, HG_BULK_PULL, hgi->addr, in.bulk_handle, 0,
                               bulk_handle, 0, size);
    assert(hret == HG_SUCCESS);

    margo_free_input(handle, &in);

    hret = margo_respond(handle, &out);
    assert(hret == HG_SUCCESS);

    margo_bulk_free(bulk_handle);
    margo_destroy(handle);
    free(buffer);

    return;
}
DEFINE_MARGO_RPC_HANDLER(svc1_do_other_thing_ult)

int svc1_register(margo_instance_id mid, ABT_pool pool, uint32_t provider_id)
{
    MARGO_REGISTER_PROVIDER(mid, "svc1_do_thing", svc1_do_thing_in_t,
                            svc1_do_thing_out_t, svc1_do_thing_ult, provider_id,
                            pool);
    MARGO_REGISTER_PROVIDER(mid, "svc1_do_other_thing",
                            svc1_do_other_thing_in_t, svc1_do_other_thing_out_t,
                            svc1_do_other_thing_ult, provider_id, pool);

    return (0);
}

void svc1_deregister(margo_instance_id mid, ABT_pool pool, uint32_t provider_id)
{
    /* TODO: undo what was done in svc1_register() */
    return;
}
