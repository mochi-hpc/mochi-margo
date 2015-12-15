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

    mid = margo_init(NA_TRUE, "tcp://localhost:1234");

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
    ABT_finalize();

    return(0);
}

