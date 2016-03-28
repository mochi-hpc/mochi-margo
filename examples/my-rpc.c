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

static void my_rpc_ult(void *_arg)
{
    hg_handle_t *handle = _arg;

    hg_return_t hret;
    my_rpc_out_t out;
    my_rpc_in_t in;
    int ret;
    hg_size_t size;
    void *buffer;
    hg_bulk_t bulk_handle;
    struct hg_info *hgi;
    int fd;
    char filename[256];
    margo_instance_id mid;

    ret = HG_Get_input(*handle, &in);
    assert(ret == HG_SUCCESS);

    printf("Got RPC request with input_val: %d\n", in.input_val);
    out.ret = 0;

    /* set up target buffer for bulk transfer */
    size = 512;
    buffer = calloc(1, 512);
    assert(buffer);

    /* register local target buffer for bulk access */
    hgi = HG_Get_info(*handle);
    assert(hgi);
    ret = HG_Bulk_create(hgi->hg_class, 1, &buffer,
        &size, HG_BULK_WRITE_ONLY, &bulk_handle);
    assert(ret == 0);

    mid = margo_hg_class_to_instance(hgi->hg_class);

    /* do bulk transfer from client to server */
    ret = margo_bulk_transfer(mid, hgi->context, HG_BULK_PULL, 
        hgi->addr, in.bulk_handle, 0,
        bulk_handle, 0, size);
    assert(ret == 0);

#if 0

    /* write to a file */
    sprintf(filename, "/tmp/hg-fiber-%d.txt", in.input_val);
    fd = fbr_eio_open(fctx, filename, O_WRONLY|O_CREAT, S_IWUSR|S_IRUSR, 0);
    assert(fd > -1);

    ret = fbr_eio_write(fctx, fd, buffer, 512, 0, 0);
    assert(ret == 512);

    fbr_eio_close(fctx, fd, 0);
#endif

    hret = HG_Respond(*handle, NULL, NULL, &out);
    assert(hret == HG_SUCCESS);

    HG_Bulk_free(bulk_handle);
    HG_Destroy(*handle);
    free(buffer);
    free(handle);

    return;
}
DEFINE_MARGO_RPC_HANDLER(my_rpc_ult)
