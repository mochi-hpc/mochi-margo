/*
 * (C) 2015 The University of Chicago
 * 
 * See COPYRIGHT in top-level directory.
 */

#include <assert.h>
#include <pthread.h>
#include "svc2-proto.h"
#include "svc2-server.h"

static void svc2_do_thing_ult(hg_handle_t handle)
{
    hg_return_t hret;
    svc2_do_thing_out_t out;
    svc2_do_thing_in_t in;
    int ret;
    hg_size_t size;
    void *buffer;
    hg_bulk_t bulk_handle;
    struct hg_info *hgi;
    margo_instance_id mid;
    ABT_thread my_ult;
    ABT_xstream my_xstream; 
    pthread_t my_tid;

    ret = HG_Get_input(handle, &in);
    assert(ret == HG_SUCCESS);
    hgi = HG_Get_info(handle);
    assert(hgi);

    ABT_xstream_self(&my_xstream);
    ABT_thread_self(&my_ult);
    my_tid = pthread_self();
    printf("svc2: do_thing: mplex_id: %u, ult: %p, xstream %p, tid: %lu\n", 
        hgi->target_id, my_ult, my_xstream, my_tid);

    out.ret = 0;

    /* set up target buffer for bulk transfer */
    size = 512;
    buffer = calloc(1, 512);
    assert(buffer);

    /* register local target buffer for bulk access */
    ret = HG_Bulk_create(hgi->hg_class, 1, &buffer,
        &size, HG_BULK_WRITE_ONLY, &bulk_handle);
    assert(ret == 0);

    mid = margo_hg_class_to_instance(hgi->hg_class);

    /* do bulk transfer from client to server */
    ret = margo_bulk_transfer(mid, HG_BULK_PULL,
        hgi->addr, in.bulk_handle, 0,
        bulk_handle, 0, size);
    assert(ret == 0);

    HG_Free_input(handle, &in);

    hret = HG_Respond(handle, NULL, NULL, &out);
    assert(hret == HG_SUCCESS);

    HG_Bulk_free(bulk_handle);
    HG_Destroy(handle);
    free(buffer);

    return;
}
DEFINE_MARGO_RPC_HANDLER(svc2_do_thing_ult)

static void svc2_do_other_thing_ult(hg_handle_t handle)
{
    hg_return_t hret;
    svc2_do_other_thing_out_t out;
    svc2_do_other_thing_in_t in;
    int ret;
    hg_size_t size;
    void *buffer;
    hg_bulk_t bulk_handle;
    struct hg_info *hgi;
    margo_instance_id mid;
    ABT_thread my_ult;
    ABT_xstream my_xstream; 
    pthread_t my_tid;

    ret = HG_Get_input(handle, &in);
    assert(ret == HG_SUCCESS);
    hgi = HG_Get_info(handle);
    assert(hgi);

    ABT_xstream_self(&my_xstream);
    ABT_thread_self(&my_ult);
    my_tid = pthread_self();
    printf("svc2: do_other_thing: mplex_id: %u, ult: %p, xstream %p, tid: %lu\n", 
        hgi->target_id, my_ult, my_xstream, my_tid);

    out.ret = 0;

    /* set up target buffer for bulk transfer */
    size = 512;
    buffer = calloc(1, 512);
    assert(buffer);

    /* register local target buffer for bulk access */
    ret = HG_Bulk_create(hgi->hg_class, 1, &buffer,
        &size, HG_BULK_WRITE_ONLY, &bulk_handle);
    assert(ret == 0);

    mid = margo_hg_class_to_instance(hgi->hg_class);

    /* do bulk transfer from client to server */
    ret = margo_bulk_transfer(mid, HG_BULK_PULL,
        hgi->addr, in.bulk_handle, 0,
        bulk_handle, 0, size);
    assert(ret == 0);

    HG_Free_input(handle, &in);

    hret = HG_Respond(handle, NULL, NULL, &out);
    assert(hret == HG_SUCCESS);

    HG_Bulk_free(bulk_handle);
    HG_Destroy(handle);
    free(buffer);

    return;
}
DEFINE_MARGO_RPC_HANDLER(svc2_do_other_thing_ult)

int svc2_register(margo_instance_id mid, ABT_pool pool, uint32_t mplex_id)
{
    MARGO_REGISTER(mid, "svc2_do_thing", svc2_do_thing_in_t, svc2_do_thing_out_t, svc2_do_thing_ult_handler, mplex_id, pool);
    MARGO_REGISTER(mid, "svc2_do_other_thing", svc2_do_other_thing_in_t, svc2_do_other_thing_out_t, svc2_do_other_thing_ult_handler, mplex_id, pool);
   
    return(0);
}

void svc2_deregister(margo_instance_id mid, ABT_pool pool, uint32_t mplex_id)
{
    /* TODO: undo what was done in svc2_register() */
    return;
}

