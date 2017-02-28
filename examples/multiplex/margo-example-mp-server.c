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

#include "svc1.h"

/* example server program that starts a skeleton for sub-services within
 * this process to register with
 */

/* this is a "common" rpc that is handled by the core daemon
 */

static void my_rpc_shutdown_ult(hg_handle_t handle)
{
    hg_return_t hret;
    struct hg_info *hgi;
    margo_instance_id mid;

    printf("Got RPC request to shutdown\n");

    hgi = HG_Get_info(handle);
    assert(hgi);
    mid = margo_hg_class_to_instance(hgi->hg_class);

    hret = margo_respond(mid, handle, NULL);
    assert(hret == HG_SUCCESS);

    HG_Destroy(handle);

    /* NOTE: we assume that the server daemon is using
     * margo_wait_for_finalize() to suspend until this RPC executes, so there
     * is no need to send any extra signal to notify it.
     */
    margo_finalize(mid);

    return;
}
DEFINE_MARGO_RPC_HANDLER(my_rpc_shutdown_ult)


int main(int argc, char **argv) 
{
    int ret;
    margo_instance_id mid;
    hg_context_t *hg_context;
    hg_class_t *hg_class;
    char proto[12] = {0};
    int i;
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

    for(i=0; i<11 && argv[1][i] != '\0' && argv[1][i] != ':'; i++)
        proto[i] = argv[1][i];
    printf("# accepting RPCs on address \"%s://%s\"\n", proto, addr_self_string);

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
    /***************************************/
    mid = margo_init(0, 0, hg_context);
    assert(mid);

    /* register svc1, with mplex_id 1, to execute on the default handler pool
     * used by Margo
     */
    ret = svc1_register(mid, margo_get_handler_pool(mid), 1);
    assert(ret == 0);

    /* NOTE: there isn't anything else for the server to do at this point
     * except wait for itself to be shut down.  The
     * margo_wait_for_finalize() call here yields to let Margo drive
     * progress until that happens.
     */
    margo_wait_for_finalize(mid);

    svc1_deregister(mid, margo_get_handler_pool(mid), 1);

    ABT_finalize();

    HG_Context_destroy(hg_context);
    HG_Finalize(hg_class);

    return(0);
}

