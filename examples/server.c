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

    /* create a dedicated ES drive Mercury progress */
    ret = ABT_snoozer_xstream_create(1, &progress_pool, &progress_xstream);
    if(ret != 0)
    {
        fprintf(stderr, "Error: ABT_snoozer_xstream_create()\n");
        return(-1);
    }

    mid = margo_init(NA_TRUE, "tcp://localhost:1234", progress_pool, handler_pool);

    /* register RPC */
    my_rpc_register(mid);

    /* suspend this ULT until someone tells us to shut down */
    ret = ABT_eventual_create(sizeof(*shutdown), &eventual);
    if(ret != 0)
    {
        fprintf(stderr, "Error: ABT_eventual_create()\n");
        return(-1);
    }

    ABT_eventual_wait(eventual, (void**)&shutdown);

    margo_finalize(mid);

    ABT_xstream_join(progress_xstream);
    ABT_xstream_free(&progress_xstream);

    ABT_finalize();

    return(0);
}

