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

/* This is an example client program that issues 4 concurrent RPCs, each of
 * which includes a bulk transfer driven by the server.
 *
 * Each client operation executes as an independent ULT in Argobots.
 * The HG forward call is executed using asynchronous operations.
 */

struct run_my_rpc_args
{
    int val;
    margo_instance_id mid;
    na_class_t *network_class;
    na_context_t *na_context;
    hg_context_t *hg_context;
    hg_class_t *hg_class;
};

static void run_my_rpc(void *_arg);

static hg_id_t my_rpc_id;

int main(int argc, char **argv) 
{
    struct run_my_rpc_args args[4];
    ABT_thread threads[4];
    int i;
    int ret;
    ABT_xstream xstream;
    ABT_pool pool;
    margo_instance_id mid;
    ABT_xstream progress_xstream;
    ABT_pool progress_pool;
    na_class_t *network_class;
    na_context_t *na_context;
    hg_context_t *hg_context;
    hg_class_t *hg_class;
        
    /* boilerplate HG initialization steps */
    network_class = NA_Initialize("tcp://localhost:1234", NA_FALSE);
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

    /* create a dedicated ES drive Mercury progress */
    ret = ABT_snoozer_xstream_create(1, &progress_pool, &progress_xstream);
    if(ret != 0)
    {
        fprintf(stderr, "Error: ABT_snoozer_xstream_create()\n");
        return(-1);
    }

    /* actually start margo */
    /*   note: the handler_pool is NULL because this is a client and is not
     *   expected to run rpc handlers.
     */
    mid = margo_init(progress_pool, ABT_POOL_NULL, hg_context, hg_class);

    /* register RPC */
    my_rpc_id = MERCURY_REGISTER(hg_class, "my_rpc", my_rpc_in_t, my_rpc_out_t, 
        my_rpc_ult_handler);

    for(i=0; i<4; i++)
    {
        args[i].val = i;
        args[i].mid = mid;
        args[i].hg_class = hg_class;
        args[i].hg_context = hg_context;
        args[i].na_context = na_context;
        args[i].network_class = network_class;

        /* Each fiber gets a pointer to an element of the array to use
         * as input for the run_my_rpc() function.
         */
        ret = ABT_thread_create(pool, run_my_rpc, &args[i],
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

    margo_finalize(mid);
    
    ABT_xstream_join(progress_xstream);
    ABT_xstream_free(&progress_xstream);

    ABT_finalize();

    HG_Context_destroy(hg_context);
    HG_Finalize(hg_class);
    NA_Context_destroy(network_class, na_context);
    NA_Finalize(network_class);

    return(0);
}

static void run_my_rpc(void *_arg)
{
    struct run_my_rpc_args *arg = _arg;
    na_addr_t svr_addr = NA_ADDR_NULL;
    hg_handle_t handle;
    my_rpc_in_t in;
    my_rpc_out_t out;
    int ret;
    hg_size_t size;
    void* buffer;
    struct hg_info *hgi;

    printf("ULT [%d] running.\n", arg->val);

    /* allocate buffer for bulk transfer */
    size = 512;
    buffer = calloc(1, 512);
    assert(buffer);
    sprintf((char*)buffer, "Hello world!\n");

    /* find addr for server */
    ret = NA_Addr_lookup_wait(arg->network_class, "tcp://localhost:1234", &svr_addr);
    assert(ret == 0);

    /* create handle */
    ret = HG_Create(arg->hg_class, arg->hg_context, svr_addr, my_rpc_id, &handle);
    assert(ret == 0);

    /* register buffer for rdma/bulk access by server */
    hgi = HG_Get_info(handle);
    assert(hgi);
    ret = HG_Bulk_create(hgi->hg_bulk_class, 1, &buffer, &size, 
        HG_BULK_READ_ONLY, &in.bulk_handle);
    assert(ret == 0);

    /* Send rpc. Note that we are also transmitting the bulk handle in the
     * input struct.  It was set above. 
     */ 
    in.input_val = arg->val;
    margo_forward(arg->mid, handle, &in);

    /* decode response */
    ret = HG_Get_output(handle, &out);
    assert(ret == 0);

    printf("Got response ret: %d\n", out.ret);

    /* clean up resources consumed by this rpc */
    HG_Bulk_free(in.bulk_handle);
    HG_Free_output(handle, &out);
    HG_Destroy(handle);
    free(buffer);

    printf("ULT [%d] done.\n", arg->val);
    return;
}

