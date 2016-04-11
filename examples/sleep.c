/*
 * (C) 2016 The University of Chicago
 * 
 * See COPYRIGHT in top-level directory.
 */

#include <stdio.h>
#include <assert.h>
#include <unistd.h>
#include <abt.h>
#include <abt-snoozer.h>
#include <margo.h>

static int use_abt_sleep;

static void sleep_fn(void *arg);

int main(int argc, char **argv) 
{
    ABT_thread threads[4];
    int t_ids[4];
    int i;
    int ret;
    ABT_xstream xstream;
    ABT_pool pool;
    margo_instance_id mid;
    hg_context_t *hg_context;
    hg_class_t *hg_class;

    if(argc == 1)
    {
        use_abt_sleep = 0;
    }
    else if((argc == 2) && (strcmp(argv[1], "ABT") == 0))
    {
        use_abt_sleep = 1;
    }
    else
    {
        fprintf(stderr, "Usage: %s [ABT]\n", argv[0]);
        fprintf(stderr, "\tABT: use ABT sleep mechanism, rather than POSIX sleep.\n");
        return(-1);
    }

    /* boilerplate HG initialization steps */
    /***************************************/
    hg_class = HG_Init("tcp://localhost:1234", HG_FALSE);
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

    /* retrieve current pool to use for ULT creation */
    ret = ABT_xstream_self(&xstream);
    if(ret != 0)
    {
        fprintf(stderr, "Error: ABT_xstream_self()\n");
        return(-1);
    }
    ret = ABT_xstream_get_main_pools(xstream, 1, &pool);
    if(ret != 0)
    {
        fprintf(stderr, "Error: ABT_xstream_get_main_pools()\n");
        return(-1);
    }

    /* actually start margo */
    /* provide argobots pools for driving communication progress and
     * executing rpc handlers as well as class and context for Mercury
     * communication.  The rpc handler pool is null in this example program
     * because this is a pure client that will not be servicing rpc requests.
     */
    /***************************************/
    mid = margo_init(pool, ABT_POOL_NULL, hg_context, hg_class);
    for(i=0; i<4; i++)
    {
        t_ids[i] = i;

        /* start up the sleeper threads */
        ret = ABT_thread_create(pool, sleep_fn, &t_ids[i],
            ABT_THREAD_ATTR_NULL, &threads[i]);
        if(ret != 0)
        {
            fprintf(stderr, "Error: ABT_thread_create()\n");
            return(-1);
        }

    }

    /* yield to one of the threads */
    ABT_thread_yield_to(threads[0]);

    for(i=0; i<4; i++)
    {
        ret = ABT_thread_join(threads[i]);
        if(ret != 0)
        {
            fprintf(stderr, "Error: ABT_thread_join()\n");
            return(-1);
        }
        ret = ABT_thread_free(&threads[i]);
        if(ret != 0)
        {
            fprintf(stderr, "Error: ABT_thread_join()\n");
            return(-1);
        }
    }

    /* shut down everything */
    margo_finalize(mid);
    
    ABT_finalize();

    HG_Context_destroy(hg_context);
    HG_Finalize(hg_class);

    return(0);
}

static void sleep_fn(void *arg)
{
    int my_tid = *(int *)arg;

    if(use_abt_sleep)
        margo_thread_sleep(2*1000.0);
    else
        sleep(2);

    printf("TID: %d sleep end\n", my_tid);

    return;
}
