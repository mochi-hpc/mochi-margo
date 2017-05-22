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
#include <abt-snoozer.h>
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
    hg_return_t hret;
    const struct hg_info *hgi;
    margo_instance_id mid;

    //printf("Got RPC request to shutdown\n");

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
    ABT_pool *handler_pool;
    char* svc_list;
    char* svc;

    if(argc != 3)
    {
        fprintf(stderr, "Usage: ./server <listen_addr> <comma_separated_service_list>\n");
        fprintf(stderr, "Example: ./server na+sm:// delegator,data-xfer\n");
        return(-1);
    }

    svc_list = strdup(argv[2]);
    assert(svc_list);

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

    /* register RPCs and services */
    /***************************************/

    /* register a shutdown RPC as just a generic handler; not part of a
     * multiplexed service
     */
    MERCURY_REGISTER(hg_class, "my_shutdown_rpc", void, void,
        my_rpc_shutdown_ult_handler);

    handler_pool = margo_get_handler_pool(mid);
    svc = strtok(svc_list, ",");
    while(svc)
    {
        if(!strcmp(svc, "data-xfer"))
        {
            data_xfer_service_register(mid, *handler_pool, 0);
        }
        else if(!strcmp(svc, "delegator"))
        {
            delegator_service_register(mid, *handler_pool, 0);
        }
        else
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

    ABT_finalize();

    HG_Context_destroy(hg_context);
    HG_Finalize(hg_class);

    return(0);
}

