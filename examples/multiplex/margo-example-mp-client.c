/*
 * (C) 2015 The University of Chicago
 *
 * See COPYRIGHT in top-level directory.
 */

#include <stdio.h>
#include <assert.h>
#include <unistd.h>
#include <stdlib.h>
#include <abt.h>
#include <margo.h>

#include "svc1-client.h"
#include "svc2-client.h"

static hg_id_t my_rpc_shutdown_id;

int main(int argc, char** argv)
{
    margo_instance_id mid;
    hg_return_t       hret;
    hg_addr_t         svr_addr = HG_ADDR_NULL;
    hg_handle_t       handle;
    char*             proto;
    char*             colon;

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
    mid = margo_init(proto, MARGO_CLIENT_MODE, 0, 0);
    free(proto);
    if (mid == MARGO_INSTANCE_NULL) {
        fprintf(stderr, "Error: margo_init()\n");
        return (-1);
    }

    /* register core RPC */
    my_rpc_shutdown_id
        = MARGO_REGISTER(mid, "my_shutdown_rpc", void, void, NULL);
    /* register service APIs */
    svc1_register_client(mid);
    svc2_register_client(mid);

    /* find addr for server */
    hret = margo_addr_lookup(mid, argv[1], &svr_addr);
    assert(hret == HG_SUCCESS);

    svc1_do_thing(mid, svr_addr, 1);
    svc1_do_other_thing(mid, svr_addr, 1);
    svc1_do_thing(mid, svr_addr, 2);
    svc1_do_other_thing(mid, svr_addr, 2);
    svc2_do_thing(mid, svr_addr, 3);
    svc2_do_other_thing(mid, svr_addr, 3);

    /* send one rpc to server to shut it down */
    /* create handle */
    hret = margo_create(mid, svr_addr, my_rpc_shutdown_id, &handle);
    assert(hret == HG_SUCCESS);

    hret = margo_forward(handle, NULL);
    assert(hret == HG_SUCCESS);

    margo_destroy(handle);
    margo_addr_free(mid, svr_addr);

    /* shut down everything */
    margo_finalize(mid);

    return (0);
}
