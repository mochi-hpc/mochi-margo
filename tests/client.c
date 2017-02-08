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
    hg_context_t *hg_context;
    hg_class_t *hg_class;
    hg_addr_t svr_addr;
};

static void run_my_rpc(void *_arg);

static hg_id_t my_rpc_id;
static hg_id_t my_rpc_shutdown_id;

int main(int argc, char **argv) 
{
    struct run_my_rpc_args args[4];
    ABT_thread threads[4];
    int i;
    int ret;
    ABT_xstream xstream;
    ABT_pool pool;
    margo_instance_id mid;
    hg_context_t *hg_context;
    hg_class_t *hg_class;
    hg_addr_t svr_addr = HG_ADDR_NULL;
    hg_handle_t handle;
    char proto[12] = {0};
  
    if(argc != 2)
    {
        fprintf(stderr, "Usage: ./client <server_addr>\n");
        return(-1);
    }
       
    /* boilerplate HG initialization steps */
    /***************************************/

    /* initialize Mercury using the transport portion of the destination
     * address (i.e., the part before the first : character if present)
     */
    for(i=0; i<11 && argv[1][i] != '\0' && argv[1][i] != ':'; i++)
        proto[i] = argv[1][i];
    hg_class = HG_Init(proto, HG_FALSE);
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
    } ret = ABT_xstream_get_main_pools(xstream, 1, &pool);
    if(ret != 0)
    {
        fprintf(stderr, "Error: ABT_xstream_get_main_pools()\n");
        return(-1);
    }

    /* actually start margo */
    /* Use main process to drive progress (it will relinquish control to
     * Mercury during blocking communication calls).  The rpc handler pool 
     * is null in this example program because this is a pure client that 
     * will not be servicing rpc requests.
     */
    /***************************************/
    mid = margo_init_pool(pool, ABT_POOL_NULL, hg_context);

    /* register RPC */
    my_rpc_id = MERCURY_REGISTER(hg_class, "my_rpc", my_rpc_in_t, my_rpc_out_t, 
        NULL);
    my_rpc_shutdown_id = MERCURY_REGISTER(hg_class, "my_shutdown_rpc", void, void, 
        NULL);

    /* find addr for server */
    ret = margo_addr_lookup(mid, argv[1], &svr_addr);
    assert(ret == 0);

    for(i=0; i<4; i++)
    {
        args[i].val = i;
        args[i].mid = mid;
        args[i].hg_class = hg_class;
        args[i].hg_context = hg_context;
        args[i].svr_addr = svr_addr;

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

    /* send one rpc to server to shut it down */

    /* create handle */
    ret = HG_Create(hg_context, svr_addr, my_rpc_shutdown_id, &handle);
    assert(ret == 0);

    margo_forward(mid, handle, NULL);

    HG_Addr_free(hg_class, svr_addr);

    /* shut down everything */
    margo_finalize(mid);
    
    ABT_finalize();

    HG_Context_destroy(hg_context);
    HG_Finalize(hg_class);

    return(0);
}

static void run_my_rpc(void *_arg)
{
    struct run_my_rpc_args *arg = _arg;
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

    /* create handle */
    ret = HG_Create(arg->hg_context, arg->svr_addr, my_rpc_id, &handle);
    assert(ret == 0);

    /* register buffer for rdma/bulk access by server */
    hgi = HG_Get_info(handle);
    assert(hgi);
    ret = HG_Bulk_create(hgi->hg_class, 1, &buffer, &size, 
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

