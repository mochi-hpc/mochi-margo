/*
 * (C) 2015 The University of Chicago
 * 
 * See COPYRIGHT in top-level directory.
 */

#include <stdio.h>
#include <assert.h>
#include <unistd.h>
#include <abt.h>

#include "hgargo.h"
#include "my-rpc.h"

/* example server program.  Starts HG engine, registers the example RPC type,
 * and then executes indefinitely.
 */

int main(int argc, char **argv) 
{
    int ret;
    ABT_eventual eventual;
    int shutdown;
    ABT_xstream xstream;
    ABT_pool pool;
    ABT_sched sched;
    ABT_pool_def pool_def;
    struct hgargo_sched_data *sched_data;
    struct hgargo_pool_data *pool_data;
    
    ret = ABT_init(argc, argv);
    if(ret != 0)
    {
        fprintf(stderr, "Error: ABT_init()\n");
        return(-1);
    }
    
    ret = ABT_xstream_self(&xstream);
    if(ret != 0)
    {
        fprintf(stderr, "Error: ABT_xstream_self()\n");
        return(-1);
    }

    ret = hgargo_pool_get_def(ABT_POOL_ACCESS_MPMC, &pool_def);
    if(ret != 0)
    {
        fprintf(stderr, "Error: hgargo_pool_get_def()\n");
        return(-1);
    }
    ret = ABT_pool_create(&pool_def, ABT_POOL_CONFIG_NULL, &pool);
    if(ret != 0)
    {
        fprintf(stderr, "Error: ABT_pool_create()\n");
        return(-1);
    }

    hgargo_create_scheds(1, &pool, &sched);

    ABT_sched_get_data(sched, (void**)(&sched_data));
    ABT_pool_get_data(pool, (void**)(&pool_data));

    ret = hgargo_setup_ev(&sched_data->ev);
    if(ret < 0)
    {
        fprintf(stderr, "Error: hgargo_setup_ev()\n");
        return(-1);
    }
    pool_data->ev = sched_data->ev;

    ABT_sched_set_data(sched, sched_data);
    ABT_pool_set_data(pool, pool_data);

    ret = ABT_xstream_set_main_sched(xstream, sched);
    if(ret != 0)
    {
        fprintf(stderr, "Error: ABT_xstream_set_main_sched()\n");
        return(-1);
    }

    hgargo_init(NA_TRUE, "tcp://localhost:1234");

    /* register RPC */
    my_rpc_register();

    /* suspend this ULT until someone tells us to shut down */
    ret = ABT_eventual_create(sizeof(shutdown), &eventual);
    if(ret != 0)
    {
        fprintf(stderr, "Error: ABT_eventual_create()\n");
        return(-1);
    }

    ABT_eventual_wait(eventual, (void**)&shutdown);

    hgargo_finalize();
    ABT_finalize();

    return(0);
}

