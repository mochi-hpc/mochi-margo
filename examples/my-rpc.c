/*
 * (C) 2015 The University of Chicago
 * 
 * See COPYRIGHT in top-level directory.
 */

#include <assert.h>
#include "my-rpc.h"

/* my-rpc:
 * This is an example RPC operation.  It includes a small bulk transfer, 
 * driven by the server, that moves data from the client to the server.  The
 * server writes the data to a local file in /tmp.
 */

/* The rpc handler is defined as a single ULT in Argobots.  It uses
 * underlying asynchronous operations for the HG transfer, open, write, and
 * close.
 */

static void my_rpc_ult(hg_handle_t handle)
{
    hg_return_t hret;
    my_rpc_out_t out;
    my_rpc_in_t in;
    hg_size_t size;
    void *buffer;
    hg_bulk_t bulk_handle;
    const struct hg_info *hgi;
#if 0
    int ret;
    int fd;
    char filename[256];
#endif
    margo_instance_id mid;

    hret = margo_get_input(handle, &in);
    assert(hret == HG_SUCCESS);

    printf("Got RPC request with input_val: %d\n", in.input_val);
    out.ret = 0;

    /* set up target buffer for bulk transfer */
    size = 512;
    buffer = calloc(1, 512);
    assert(buffer);

    /* get handle info and margo instance */
    hgi = margo_get_info(handle);
    assert(hgi);
    mid = margo_hg_info_get_instance(hgi);
    assert(mid != MARGO_INSTANCE_NULL);

    /* register local target buffer for bulk access */
    hret = margo_bulk_create(mid, 1, &buffer,
        &size, HG_BULK_WRITE_ONLY, &bulk_handle);
    assert(hret == HG_SUCCESS);

    /* do bulk transfer from client to server */
    hret = margo_bulk_transfer(mid, HG_BULK_PULL,
        hgi->addr, in.bulk_handle, 0,
        bulk_handle, 0, size, HG_OP_ID_IGNORE);
    assert(hret == HG_SUCCESS);

    /* write to a file; would be done with abt-io if we enabled it */
#if 0
    sprintf(filename, "/tmp/margo-%d.txt", in.input_val);
    fd = abt_io_open(aid, filename, O_WRONLY|O_CREAT, S_IWUSR|S_IRUSR);
    assert(fd > -1);

    ret = abt_io_pwrite(aid, fd, buffer, 512, 0);
    assert(ret == 512);

    abt_io_close(aid, fd);
#endif

    margo_free_input(handle, &in);

    hret = margo_respond(mid, handle, &out);
    assert(hret == HG_SUCCESS);

    margo_bulk_free(bulk_handle);
    margo_destroy(handle);
    free(buffer);

    return;
}
DEFINE_MARGO_RPC_HANDLER(my_rpc_ult)

static void my_rpc_shutdown_ult(hg_handle_t handle)
{
    hg_return_t hret;
    const struct hg_info *hgi;
    margo_instance_id mid;

    printf("Got RPC request to shutdown\n");

    /* get handle info and margo instance */
    hgi = margo_get_info(handle);
    assert(hgi);
    mid = margo_hg_info_get_instance(hgi);
    assert(mid != MARGO_INSTANCE_NULL);

    hret = margo_respond(mid, handle, NULL);
    assert(hret == HG_SUCCESS);

    margo_destroy(handle);

    margo_diag_dump(mid, "-", 0);

    /* NOTE: we assume that the server daemon is using
     * margo_wait_for_finalize() to suspend until this RPC executes, so there
     * is no need to send any extra signal to notify it.
     */
    margo_finalize(mid);

    return;
}
DEFINE_MARGO_RPC_HANDLER(my_rpc_shutdown_ult)
