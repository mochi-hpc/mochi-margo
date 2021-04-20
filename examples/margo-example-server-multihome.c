/*
 * (C) 2015 The University of Chicago
 *
 * See COPYRIGHT in top-level directory.
 */

#include <stdio.h>
#include <assert.h>
#include <unistd.h>
#include <mercury.h>
#include <abt.h>
#include <margo.h>

#include "my-rpc.h"

/* example server program.  Starts HG engine, registers the example RPC type,
 * and then executes indefinitely.
 */

int main(int argc, char** argv)
{
    hg_return_t       hret;
    margo_instance_id mid1, mid2;
    hg_addr_t         addr_self;
    char              addr_self_string1[128];
    char              addr_self_string2[128];
    hg_size_t         addr_self_string_sz = 128;

    if (argc != 3) {
        fprintf(stderr, "Usage: ./server <listen_addr1> <listen_addr2>\n");
        fprintf(stderr, "Example: ./server na+sm:// ofi+tcp://\n");
        return (-1);
    }

    /* Start two margo instances.  Each uses it's own dedicated progress
     * thread and executes handlers on the calling context.
     */

    /***************************************/
    mid1 = margo_init(argv[1], MARGO_SERVER_MODE, 1, -1);
    if (mid1 == MARGO_INSTANCE_NULL) {
        fprintf(stderr, "Error: margo_init()\n");
        return (-1);
    }
    mid2 = margo_init(argv[2], MARGO_SERVER_MODE, 1, -1);
    if (mid2 == MARGO_INSTANCE_NULL) {
        fprintf(stderr, "Error: margo_init()\n");
        return (-1);
    }

    /* figure out first listening addr */
    hret = margo_addr_self(mid1, &addr_self);
    if (hret != HG_SUCCESS) {
        fprintf(stderr, "Error: margo_addr_self()\n");
        margo_finalize(mid1);
        return (-1);
    }
    addr_self_string_sz = 128;
    hret = margo_addr_to_string(mid1, addr_self_string1, &addr_self_string_sz,
                                addr_self);
    if (hret != HG_SUCCESS) {
        fprintf(stderr, "Error: margo_addr_to_string()\n");
        margo_addr_free(mid1, addr_self);
        margo_finalize(mid1);
        return (-1);
    }
    margo_addr_free(mid1, addr_self);

    /* figure out second listening addr */
    hret = margo_addr_self(mid2, &addr_self);
    if (hret != HG_SUCCESS) {
        fprintf(stderr, "Error: margo_addr_self()\n");
        margo_finalize(mid2);
        return (-1);
    }
    addr_self_string_sz = 128;
    hret = margo_addr_to_string(mid2, addr_self_string2, &addr_self_string_sz,
                                addr_self);
    if (hret != HG_SUCCESS) {
        fprintf(stderr, "Error: margo_addr_to_string()\n");
        margo_addr_free(mid2, addr_self);
        margo_finalize(mid2);
        return (-1);
    }
    margo_addr_free(mid2, addr_self);

    fprintf(stderr, "# accepting RPCs on address \"%s\" and \"%s\"\n",
            addr_self_string1, addr_self_string2);

    /* register RPC */
    MARGO_REGISTER(mid1, "my_rpc", my_rpc_in_t, my_rpc_out_t, my_rpc_ult);
    MARGO_REGISTER(mid1, "my_shutdown_rpc", void, void, my_rpc_shutdown_ult);
    MARGO_REGISTER(mid2, "my_rpc", my_rpc_in_t, my_rpc_out_t, my_rpc_ult);
    MARGO_REGISTER(mid2, "my_shutdown_rpc", void, void, my_rpc_shutdown_ult);

    /* NOTE: there isn't anything else for the server to do at this point
     * except wait for itself to be shut down.  The
     * margo_wait_for_finalize() call here yields to let Margo drive
     * progress until that happens.
     */
    margo_wait_for_finalize(mid1);
    margo_wait_for_finalize(mid2);

    return (0);
}
