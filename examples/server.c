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
    ABT_eventual eventual;
    int *shutdown;
    margo_instance_id mid;
    ABT_xstream handler_xstream;
    ABT_pool handler_pool;
    ABT_xstream progress_xstream;
    ABT_pool progress_pool;
    na_class_t *network_class;
    na_context_t *na_context;
    hg_context_t *hg_context;
    hg_class_t *hg_class;
    int single_pool_mode = 0;
    
    if(argc > 2)
    {
        fprintf(stderr, "Usage: ./server <single>\n");
        fprintf(stderr, "   Note: the optional \"single\" argument makes the server use a single ABT pool for both HG progress and RPC handlers.\n");
        return(-1);
    }

    if(argc == 2)
    {
        if(strcmp(argv[1], "single") == 0)
            single_pool_mode = 1;
        else
        {
            fprintf(stderr, "Usage: ./server <single>\n");
            fprintf(stderr, "   Note: the optional \"single\" argument makes the server use a single ABT pool for both HG progress and RPC handlers.\n");
            return(-1);
        }
    }

    /* boilerplate HG initialization steps */
    /***************************************/
    network_class = NA_Initialize("tcp://localhost:1234", NA_TRUE);
    if(!network_class)
    {
        fprintf(stderr, "Error: NA_Initialize()\n");
        return(-1);
    }
    na_context = NA_Context_create(network_class);
    if(!na_context)
    {
        fprintf(stderr, "Error: NA_Context_create()\n");
        NA_Finalize(network_class);
        return(-1);
    }
    hg_class = HG_Init(network_class, na_context, NULL);
    if(!hg_class)
    {
        fprintf(stderr, "Error: HG_Init()\n");
        NA_Context_destroy(network_class, na_context);
        NA_Finalize(network_class);
        return(-1);
    }
    hg_context = HG_Context_create(hg_class);
    if(!hg_context)
    {
        fprintf(stderr, "Error: HG_Context_create()\n");
        HG_Finalize(hg_class);
        NA_Context_destroy(network_class, na_context);
        NA_Finalize(network_class);
        return(-1);
    }

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

    /* Find primary pool to use for running rpc handlers */
    ret = ABT_xstream_self(&handler_xstream);
    if(ret != 0)
    {
        fprintf(stderr, "Error: ABT_xstream_self()\n");
        return(-1);
    }
    ret = ABT_xstream_get_main_pools(handler_xstream, 1, &handler_pool);
    if(ret != 0)
    {
        fprintf(stderr, "Error: ABT_xstream_get_main_pools()\n");
        return(-1);
    }

    if(!single_pool_mode)
    {
        /* create a dedicated ES drive Mercury progress */
        ret = ABT_snoozer_xstream_create(1, &progress_pool, &progress_xstream);
        if(ret != 0)
        {
            fprintf(stderr, "Error: ABT_snoozer_xstream_create()\n");
            return(-1);
        }
    }

    /* actually start margo */
    /* provide argobots pools for driving communication progress and
     * executing rpc handlers as well as class and context for Mercury
     * communication.
     */
    /***************************************/
    if(single_pool_mode)
        mid = margo_init(handler_pool, handler_pool, hg_context, hg_class);
    else
        mid = margo_init(progress_pool, handler_pool, hg_context, hg_class);

    /* register RPC */
    MERCURY_REGISTER(hg_class, "my_rpc", my_rpc_in_t, my_rpc_out_t, 
        my_rpc_ult_handler);

    /* suspend this ULT until someone tells us to shut down */
    ret = ABT_eventual_create(sizeof(*shutdown), &eventual);
    if(ret != 0)
    {
        fprintf(stderr, "Error: ABT_eventual_create()\n");
        return(-1);
    }

    ABT_eventual_wait(eventual, (void**)&shutdown);

    /* shut down everything */
    margo_finalize(mid);

    if(!single_pool_mode)
    {
        ABT_xstream_join(progress_xstream);
        ABT_xstream_free(&progress_xstream);
    }

    ABT_finalize();

    HG_Context_destroy(hg_context);
    HG_Finalize(hg_class);
    NA_Context_destroy(network_class, na_context);
    NA_Finalize(network_class);

    return(0);
}

