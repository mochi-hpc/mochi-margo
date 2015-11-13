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

/* This is an example client program that issues 4 concurrent RPCs, each of
 * which includes a bulk transfer driven by the server.
 *
 * Each client operation executes as an independent ULT in Argobots.
 * The HG forward call is executed using asynchronous operations.
 */

static void run_my_rpc(void *_arg);

static hg_id_t my_rpc_id;

int main(int argc, char **argv) 
{
    int values[4];
    ABT_thread threads[4];
    int i;
    int ret;
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

    /* initialize
     *   note: address here is really just being used to identify transport 
     */
    hgargo_init(NA_FALSE, "tcp://localhost:1234");

    /* register RPC */
    my_rpc_id = my_rpc_register();

    for(i=0; i<4; i++)
    {
        values[i] = i;
        /* Each fiber gets a pointer to an element of the values array to use
         * as input for the run_my_rpc() function.
         */
        ret = ABT_thread_create(pool, run_my_rpc, &values[i],
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

    hgargo_finalize();
    ABT_finalize();

    return(0);
}

static void run_my_rpc(void *_arg)
{
    int* val = (int*)_arg;
    na_addr_t svr_addr = NA_ADDR_NULL;
    hg_handle_t handle;
    my_rpc_in_t in;
    my_rpc_out_t out;
    int ret;
    hg_size_t size;
    void* buffer;
    struct hg_info *hgi;

    printf("ULT [%d] running.\n", *val);

    /* allocate buffer for bulk transfer */
    size = 512;
    buffer = calloc(1, 512);
    assert(buffer);
    sprintf((char*)buffer, "Hello world!\n");

    /* find addr for server */
    ret = hgargo_addr_lookup("tcp://localhost:1234", &svr_addr);
    assert(ret == 0);

    /* create handle */
    ret = hgargo_create_handle(svr_addr, my_rpc_id, &handle);
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
    in.input_val = *((int*)(_arg));
    hgargo_forward(handle, &in);

    /* decode response */
    ret = HG_Get_output(handle, &out);
    assert(ret == 0);

    printf("Got response ret: %d\n", out.ret);

    /* clean up resources consumed by this rpc */
    HG_Bulk_free(in.bulk_handle);
    HG_Free_output(handle, &out);
    HG_Destroy(handle);
    free(buffer);

    printf("ULT [%d] done.\n", *val);
    return;
}

