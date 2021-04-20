/*
 * (C) 2015 The University of Chicago
 *
 * See COPYRIGHT in top-level directory.
 */

#include <stdio.h>
#include <assert.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>

#include <abt.h>
#include <margo.h>

#include "data-xfer-service.h"
#include "delegator-service.h"

/* server program that starts a skeleton for sub-services within
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
    margo_diag_dump(mid, "-", 0);
    margo_finalize(mid);

    return;
}
DEFINE_MARGO_RPC_HANDLER(my_rpc_shutdown_ult)

int main(int argc, char** argv)
{
    hg_return_t       hret;
    margo_instance_id mid;
    hg_addr_t         addr_self;
    char              addr_self_string[128];
    hg_size_t         addr_self_string_sz = 128;
    ABT_pool          handler_pool;
    char*             svc_list;
    char*             svc;

    if (argc != 3) {
        fprintf(
            stderr,
            "Usage: ./server <listen_addr> <comma_separated_service_list>\n");
        fprintf(stderr, "Example: ./server na+sm:// delegator,data-xfer\n");
        return (-1);
    }

    svc_list = strdup(argv[2]);
    assert(svc_list);

    /* actually start margo -- this step encapsulates the Mercury and
     * Argobots initialization and must precede their use */
    /* Use the calling xstream to drive progress and execute handlers. */
    /***************************************/
    mid = margo_init(argv[1], MARGO_SERVER_MODE, 0, -1);
    if (mid == MARGO_INSTANCE_NULL) {
        fprintf(stderr, "Error: margo_init()\n");
        return (-1);
    }
    margo_diag_start(mid);

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

    margo_get_handler_pool(mid, &handler_pool);
    svc = strtok(svc_list, ",");
    while (svc) {
        if (!strcmp(svc, "data-xfer")) {
            data_xfer_service_register(mid, handler_pool, 0);
        } else if (!strcmp(svc, "delegator")) {
            delegator_service_register(mid, handler_pool, 0);
        } else
            assert(0);

        svc = strtok(NULL, ",");
    }

    /* shut things down */
    /****************************************/

    /* NOTE: there isn't anything else for the server to do at this point
     * except wait for itself to be shut down.  The
     * margo_wait_for_finalize() call here yields to let Margo drive
     * progress until that happens.
     */
    margo_wait_for_finalize(mid);

    /*  TODO: rethink this; can't touch mid after wait for finalize */
#if 0
    svc1_deregister(mid, *handler_pool, 1);
    svc1_deregister(mid, svc1_pool2, 2);
    svc2_deregister(mid, *handler_pool, 3);
#endif

    return (0);
}
