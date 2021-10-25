/*
 * (C) 2015 The University of Chicago
 *
 * See COPYRIGHT in top-level directory.
 */

#include <stdio.h>
#include <assert.h>
#include <unistd.h>
#include <stdlib.h>
#include <mercury.h>
#include <abt.h>
#include <margo.h>

#include "my-rpc.h"

/* This is an example client program that issues 4 concurrent RPCs, each of
 * which includes a bulk transfer driven by the server.
 *
 * Each client operation executes as an independent ULT in Argobots.
 * The HG forward call is executed using asynchronous operations.
 */

struct run_my_rpc_args {
    int               val;
    int               dump_state;
    margo_instance_id mid;
    hg_addr_t         svr_addr;
};

static void run_my_rpc(void* _arg);

static hg_id_t my_rpc_id;
static hg_id_t my_rpc_shutdown_id;

int main(int argc, char** argv)
{
    struct run_my_rpc_args args[4];
    ABT_thread             threads[4];
    int                    i;
    int                    ret;
    hg_return_t            hret;
    ABT_xstream            xstream;
    ABT_pool               pool;
    margo_instance_id      mid;
    hg_addr_t              svr_addr = HG_ADDR_NULL;
    hg_handle_t            handle;
    char*                  proto;
    char*                  colon;
    struct margo_init_info minfo        = {0};
    char*                  starter_json = "{\"output_dir\":\"/tmp\"}";

    if (argc != 2) {
        fprintf(stderr, "Usage: ./client <server_addr>\n");
        return (-1);
    }

    /* initialize Mercury using the transport portion of the destination
     * address (i.e., the part before the first : character if present)
     */
    proto = strdup(argv[1]);
    assert(proto);
    colon = strchr(proto, ':');
    if (colon) *colon = '\0';

    /* actually start margo -- margo_init() encapsulates the Mercury &
     * Argobots initialization, so this step must precede their use. */
    /* Use main process to drive progress (it will relinquish control to
     * Mercury during blocking communication calls). No RPC threads are
     * used because this is a pure client that will not be servicing
     * rpc requests.
     */
    /***************************************/
    minfo.json_config = starter_json;
    mid               = margo_init_ext(proto, MARGO_CLIENT_MODE, &minfo);
    free(proto);
    if (mid == MARGO_INSTANCE_NULL) {
        fprintf(stderr, "Error: margo_init_ext()\n");
        return (-1);
    }
    margo_diag_start(mid);
    /* NOTE: this is necessary for the server to be able to collect complete
     * profile data, even if we don't plan to use the data on the client in
     * this example.
     */
    margo_profile_start(mid);

    /* retrieve current pool to use for ULT creation */
    ret = ABT_xstream_self(&xstream);
    if (ret != 0) {
        fprintf(stderr, "Error: ABT_xstream_self()\n");
        return (-1);
    }
    ret = ABT_xstream_get_main_pools(xstream, 1, &pool);
    if (ret != 0) {
        fprintf(stderr, "Error: ABT_xstream_get_main_pools()\n");
        return (-1);
    }

    /* register RPC */
    my_rpc_id = MARGO_REGISTER(mid, "my_rpc", my_rpc_in_t, my_rpc_out_t, NULL);
    my_rpc_shutdown_id
        = MARGO_REGISTER(mid, "my_shutdown_rpc", void, void, NULL);

    /* find addr for server */
    hret = margo_addr_lookup(mid, argv[1], &svr_addr);
    assert(hret == HG_SUCCESS);

    for (i = 0; i < 4; i++) {
        args[i].val      = i;
        args[i].mid      = mid;
        args[i].svr_addr = svr_addr;
        if (i == 2) /* pick one in the middle to trigger a stack dump on svr */
            args[i].dump_state = 1;
        else
            args[i].dump_state = 0;

        /* Each ult gets a pointer to an element of the array to use
         * as input for the run_my_rpc() function.
         */
        ret = ABT_thread_create(pool, run_my_rpc, &args[i],
                                ABT_THREAD_ATTR_NULL, &threads[i]);
        if (ret != 0) {
            fprintf(stderr, "Error: ABT_thread_create()\n");
            return (-1);
        }
    }

    /* yield to one of the threads */
    ABT_thread_yield_to(threads[0]);

    for (i = 0; i < 4; i++) {
        ret = ABT_thread_join(threads[i]);
        if (ret != 0) {
            fprintf(stderr, "Error: ABT_thread_join()\n");
            return (-1);
        }
        ret = ABT_thread_free(&threads[i]);
        if (ret != 0) {
            fprintf(stderr, "Error: ABT_thread_join()\n");
            return (-1);
        }
    }

    /* send one rpc to server to shut it down */

    /* create handle */
    hret = margo_create(mid, svr_addr, my_rpc_shutdown_id, &handle);
    assert(hret == HG_SUCCESS);

    hret = margo_forward(handle, NULL);
    assert(hret == HG_SUCCESS);

    margo_destroy(handle);
    margo_addr_free(mid, svr_addr);

    /* shut down everything */
    // margo_diag_dump(mid, "-", 0);
    margo_finalize(mid);

    return (0);
}

static void run_my_rpc(void* _arg)
{
    struct run_my_rpc_args* arg = _arg;
    hg_handle_t             handle;
    my_rpc_in_t             in;
    my_rpc_out_t            out;
    hg_return_t             hret;
    hg_size_t               size;
    void*                   buffer;

    printf("ULT [%d] running.\n", arg->val);

    /* allocate buffer for bulk transfer */
    size   = 512;
    buffer = calloc(1, 512);
    assert(buffer);
    sprintf((char*)buffer, "Hello world!\n");

    /* create handle */
    hret = margo_create(arg->mid, arg->svr_addr, my_rpc_id, &handle);
    assert(hret == HG_SUCCESS);

    /* register buffer for rdma/bulk access by server */
    hret = margo_bulk_create(arg->mid, 1, &buffer, &size, HG_BULK_READ_ONLY,
                             &in.bulk_handle);
    assert(hret == HG_SUCCESS);

    /* Send rpc. Note that we are also transmitting the bulk handle in the
     * input struct.  It was set above.
     */
    in.input_val  = arg->val;
    in.dump_state = arg->dump_state;
    hret          = margo_forward(handle, &in);
    assert(hret == HG_SUCCESS);

    /* decode response */
    hret = margo_get_output(handle, &out);
    assert(hret == HG_SUCCESS);

    printf("Got response ret: %d\n", out.ret);

    /* clean up resources consumed by this rpc */
    margo_bulk_free(in.bulk_handle);
    margo_free_output(handle, &out);
    margo_destroy(handle);
    free(buffer);

    printf("ULT [%d] done.\n", arg->val);
    return;
}
