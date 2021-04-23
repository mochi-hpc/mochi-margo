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
#include <margo-logging.h>

#include "my-rpc.h"

/* example server program.  Starts HG engine, registers the example RPC type,
 * and then executes indefinitely.
 */

int main(int argc, char** argv)
{
    hg_return_t            hret;
    margo_instance_id      mid;
    hg_addr_t              addr_self;
    char                   addr_self_string[128];
    hg_size_t              addr_self_string_sz = 128;
    struct margo_init_info minfo               = {0};
    char*                  starter_json        = "{\"output_dir\":\"/tmp\"}";

    if (argc != 2) {
        fprintf(stderr, "Usage: ./server <listen_addr>\n");
        fprintf(stderr, "Example: ./server na+sm://\n");
        return (-1);
    }

    /* actually start margo -- this step encapsulates the Mercury and
     * Argobots initialization and must precede their use */
    /* Use the calling xstream to drive progress and execute handlers. */
    /***************************************/
    margo_set_global_log_level(MARGO_LOG_TRACE);
    minfo.json_config = starter_json;
    mid               = margo_init_ext(argv[1], MARGO_SERVER_MODE, &minfo);
    if (mid == MARGO_INSTANCE_NULL) {
        fprintf(stderr, "Error: margo_init_ext()\n");
        return (-1);
    }

    /* Start diagnostics and profiling; they will be included in state dump
     * later on during execution.
     */
    margo_diag_start(mid);
    margo_profile_start(mid);

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

    fprintf(stderr, "# accepting RPCs on address \"%s\"\n", addr_self_string);
    fprintf(stderr, "# connect to this server with \"./margo-example-client %s\"\n", addr_self_string);

    /* register RPC */
    MARGO_REGISTER(mid, "my_rpc", my_rpc_in_t, my_rpc_out_t, my_rpc_ult);
    MARGO_REGISTER(mid, "my_shutdown_rpc", void, void, my_rpc_shutdown_ult);

#if 0
    /* this could be used to display json configuration at run time */
    char*             cfg_str;
    cfg_str = margo_get_config(mid);
    printf("%s\n", cfg_str);
    free(cfg_str);
#endif

    /* NOTE: there isn't anything else for the server to do at this point
     * except wait for itself to be shut down.  The
     * margo_wait_for_finalize() call here yields to let Margo drive
     * progress until that happens.
     */
    margo_wait_for_finalize(mid);

    return (0);
}
