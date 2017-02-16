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
 *
 * This version is special; it deliberately executes a scenario in which the
 * progress thread will not be able to proceed; this is simply to serve as a
 * test case for timeout.
 */

struct options
{
    char *hostfile;
    char *listen_addr;
};

static void parse_args(int argc, char **argv, struct options *opts);

int main(int argc, char **argv) 
{
    int ret;
    margo_instance_id mid;
    ABT_xstream handler_xstream;
    ABT_pool handler_pool;
    hg_context_t *hg_context;
    hg_class_t *hg_class;
    struct options opts;

    parse_args(argc, argv, &opts);

    /* boilerplate HG initialization steps */
    /***************************************/
    hg_class = HG_Init(opts.listen_addr, HG_TRUE);
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

    if(opts.hostfile)
    {
        FILE *fp;
        char proto[12] = {0};
        int i;
        hg_addr_t addr_self;
        char addr_self_string[128];
        hg_size_t addr_self_string_sz = 128;

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

        fp = fopen(opts.hostfile, "w");
        if(!fp)
        {
            perror("fopen");
            HG_Context_destroy(hg_context);
            HG_Finalize(hg_class);
            HG_Addr_free(hg_class, addr_self);
            return(-1);
        }

        for(i=0; i<11 && opts.listen_addr[i] != '\0' && opts.listen_addr[i] != ':'; i++)
            proto[i] = opts.listen_addr[i];
        fprintf(fp, "%s://%s", proto, addr_self_string);
        fclose(fp);
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

    /* actually start margo */
    /* provide argobots pools for driving communication progress and
     * executing rpc handlers as well as class and context for Mercury
     * communication.
     */
    /***************************************/
    mid = margo_init_pool(handler_pool, handler_pool, hg_context);
    assert(mid);

    /* register RPC */
    MERCURY_REGISTER(hg_class, "my_rpc", my_rpc_in_t, my_rpc_out_t, 
        my_rpc_ult_handler);
    MERCURY_REGISTER(hg_class, "my_shutdown_rpc", void, void, 
        my_rpc_shutdown_ult_handler);

    /* NOTE: this is intentional; because this test program uses the main
     * thread for Mercury progress, we can stall everything with a long sleep
     * here.
     */
    sleep(5000);

    /* NOTE: at this point this server ULT has two options.  It can wait on
     * whatever mechanism it wants to (however long the daemon should run and
     * then call margo_finalize().  Otherwise, it can call
     * margo_wait_for_finalize() on the assumption that it should block until
     * some other entity calls margo_finalize().
     *
     * This example does the latter.  Margo will be finalized by a special
     * RPC from the client.
     *
     * This approach will allow the server to idle gracefully even when
     * executed in "single" mode, in which the main thread of the server
     * daemon and the progress thread for Mercury are executing in the same
     * ABT pool.
     */
    margo_wait_for_finalize(mid);

    ABT_finalize();

    HG_Context_destroy(hg_context);
    HG_Finalize(hg_class);

    return(0);
}

static void usage(int argc, char **argv)
{
    fprintf(stderr, "Usage: %s listen_address [-s] [-f filename]\n",
        argv[0]);
    fprintf(stderr, "   listen_address is the address or protocol for the server to use\n");
    fprintf(stderr, "   [-f filename] to write the server address to a file\n");
    return;
}


static void parse_args(int argc, char **argv, struct options *opts)
{
    int ret, opt;
    
    memset(opts, 0, sizeof(*opts));

    while((opt = getopt(argc, argv, "f:")) != -1)
    {
        switch(opt)
        {
            case 'f':
                opts->hostfile = strdup(optarg);
                break;
            default: 
                usage(argc, argv);
                exit(EXIT_FAILURE);
        }
    }

    if(optind >= argc)
    {
        usage(argc, argv);
        exit(EXIT_FAILURE);
    }

    opts->listen_addr = strdup(argv[optind]);
    
    return;
}
