/*
 * (C) 2015 The University of Chicago
 * 
 * See COPYRIGHT in top-level directory.
 */

#include <stdio.h>
#include <assert.h>
#include <unistd.h>
#include <abt.h>
#include <abt-snoozer.h>
#include <margo.h>

#include "my-rpc.h"

/* example server program.  Starts HG engine, registers the example RPC type,
 * and then executes indefinitely.
 */

int main(int argc, char **argv) 
{
    int ret;
    margo_instance_id mid;
    hg_context_t *hg_context;
    hg_class_t *hg_class;
    hg_addr_t addr_self;
    char addr_self_string[128];
    hg_size_t addr_self_string_sz = 128;

    if(argc != 2)
    {
        fprintf(stderr, "Usage: ./server <listen_addr>\n");
        fprintf(stderr, "Example: ./server na+sm://\n");
        return(-1);
    }

    /* boilerplate HG initialization steps */
    /***************************************/
    hg_class = HG_Init(argv[1], HG_TRUE);
    if(!hg_class)
    {
        fprintf(stderr, "Error: HG_Init()\n");
        return(-1);
    }
    hg_context = HG_Context_create(hg_class);
    if(!hg_context)
    {
        fprintf(stderr, "Error: HG_Context_create()\n");
        HG_Finalize(hg_class);
        return(-1);
    }

    
    /* figure out what address this server is listening on */
    ret = HG_Addr_self(hg_class, &addr_self);
    if(ret != HG_SUCCESS)
    {
        fprintf(stderr, "Error: HG_Addr_self()\n");
        HG_Context_destroy(hg_context);
        HG_Finalize(hg_class);
        return(-1);
    }
    ret = HG_Addr_to_string(hg_class, addr_self_string, &addr_self_string_sz, addr_self);
    if(ret != HG_SUCCESS)
    {
        fprintf(stderr, "Error: HG_Addr_self()\n");
        HG_Context_destroy(hg_context);
        HG_Finalize(hg_class);
        HG_Addr_free(hg_class, addr_self);
        return(-1);
    }
    HG_Addr_free(hg_class, addr_self);

    printf("# accepting RPCs on address \"%s\"\n", addr_self_string);

    /* set up argobots */
    /***************************************/
    ret = ABT_init(argc, argv);
    if(ret != 0)
    {
        fprintf(stderr, "Error: ABT_init()\n");
        return(-1);
    }

    /* set primary ES to idle without polling */
    ret = ABT_snoozer_xstream_self_set();
    if(ret != 0)
    {
        fprintf(stderr, "Error: ABT_snoozer_xstream_self_set()\n");
        return(-1);
    }

    /* actually start margo */
    mid = margo_init(0, 0, hg_context);
    assert(mid);
    margo_diag_start(mid);

    /* register RPC */
    MARGO_REGISTER(mid, "my_rpc", my_rpc_in_t, my_rpc_out_t, my_rpc_ult, MARGO_RPC_ID_IGNORE);
	MARGO_REGISTER(mid, "my_shutdown_rpc", void, void, my_rpc_shutdown_ult, MARGO_RPC_ID_IGNORE);

    /* NOTE: there isn't anything else for the server to do at this point
     * except wait for itself to be shut down.  The
     * margo_wait_for_finalize() call here yields to let Margo drive
     * progress until that happens.
     */
    margo_wait_for_finalize(mid);

    ABT_finalize();

    HG_Context_destroy(hg_context);
    HG_Finalize(hg_class);

    return(0);
}

