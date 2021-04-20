/*
 * (C) 2015 The University of Chicago
 *
 * See COPYRIGHT in top-level directory.
 */

#include <stdio.h>
#include <assert.h>
#include <unistd.h>
#include <abt.h>
#include <margo.h>
#include <margo-config.h>
#include "svc1-server.h"
#include "svc2-server.h"

/* example server program that starts a skeleton for sub-services within
 * this process to register with
 */

/* this is a "common" rpc that is handled by the core daemon
 */

static void my_rpc_shutdown_ult(hg_handle_t handle)
{
    hg_return_t       hret;
    margo_instance_id mid;

    // printf("Got RPC request to shutdown\n");

    mid = margo_hg_handle_get_instance(handle);
    assert(mid != MARGO_INSTANCE_NULL);

    hret = margo_respond(handle, NULL);
    assert(hret == HG_SUCCESS);

    margo_destroy(handle);

    /* NOTE: we assume that the server daemon is using
     * margo_wait_for_finalize() to suspend until this RPC executes, so there
     * is no need to send any extra signal to notify it.
     */
    margo_finalize(mid);

    return;
}
DEFINE_MARGO_RPC_HANDLER(my_rpc_shutdown_ult)

int main(int argc, char** argv)
{
    int               ret;
    margo_instance_id mid;
    hg_return_t       hret;
    hg_addr_t         addr_self;
    char              addr_self_string[128];
    hg_size_t         addr_self_string_sz = 128;
    ABT_xstream       svc1_xstream2;
    ABT_pool          svc1_pool2;
    ABT_pool          handler_pool;

    if (argc != 2) {
        fprintf(stderr, "Usage: ./server <listen_addr>\n");
        fprintf(stderr, "Example: ./server na+sm://\n");
        return (-1);
    }

    /* actually start margo -- this step encapsulates the Mercury and
     * Argobots initialization and must precede their use */
    /* Use the calling xstream to drive progress and execute handlers. */
    /***************************************/
    mid = margo_init(argv[1], MARGO_SERVER_MODE, 0, -1);
    if (mid == MARGO_INSTANCE_NULL) {
        fprintf(stderr, "Error: margo_init()\n");
        return (-1);
    }

    /* figure out what address this server is listening on */
    hret = margo_addr_self(mid, &addr_self);
    if (hret != HG_SUCCESS) {
        fprintf(stderr, "Error: margo_addr_self()\n");
        margo_finalize(mid);
        return (-1);
    }
    hret = margo_addr_to_string(mid, addr_self_string, &addr_self_string_sz,
                                addr_self);
    if (hret != HG_SUCCESS) {
        fprintf(stderr, "Error: margo_addr_to_string()\n");
        margo_addr_free(mid, addr_self);
        margo_finalize(mid);
        return (-1);
    }
    margo_addr_free(mid, addr_self);

    printf("# accepting RPCs on address \"%s\"\n", addr_self_string);

    /* register RPCs and services */
    /***************************************/

    /* register a shutdown RPC as just a generic handler; not part of a
     * multiplexed service
     */
    MARGO_REGISTER(mid, "my_shutdown_rpc", void, void, my_rpc_shutdown_ult);

    /* register svc1, with provider_id 1, to execute on the default handler pool
     * used by Margo
     */
    margo_get_handler_pool(mid, &handler_pool);
    ret = svc1_register(mid, handler_pool, 1);
    assert(ret == 0);

    /* create a dedicated xstream and pool for another instance of svc1 */
    ret = ABT_xstream_create(ABT_SCHED_NULL, &svc1_xstream2);
    assert(ret == 0);
    ret = ABT_xstream_get_main_pools(svc1_xstream2, 1, &svc1_pool2);
    assert(ret == 0);

    /* register svc1, with provider_id 2, to execute on a separate pool.  This
     * will result in svc1 being registered twice, with the client being able
     * to dictate which instance they want to target
     */
    ret = svc1_register(mid, svc1_pool2, 2);
    assert(ret == 0);

    /* register svc2, with provider_id 3, to execute on the default handler pool
     * used by Margo
     */
    margo_get_handler_pool(mid, &handler_pool);
    ret = svc2_register(mid, handler_pool, 3);
    assert(ret == 0);

    /* shut things down */
    /****************************************/

    /* NOTE: there isn't anything else for the server to do at this point
     * except wait for itself to be shut down.  The
     * margo_wait_for_finalize() call here yields to let Margo drive
     * progress until that happens.
     */
    margo_wait_for_finalize(mid);

    /*  TODO: rethink this; can't touch mid or use ABT after wait for finalize
     */
#if 0
    svc1_deregister(mid, *handler_pool, 1);
    svc1_deregister(mid, svc1_pool2, 2);
    svc2_deregister(mid, *handler_pool, 3);

    ABT_xstream_join(svc1_xstream2);
    ABT_xstream_free(&svc1_xstream2);
#endif

    return (0);
}
