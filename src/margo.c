
/*
 * (C) 2015 The University of Chicago
 * 
 * See COPYRIGHT in top-level directory.
 */

#include <assert.h>
#include <unistd.h>
#include <errno.h>
#include <abt.h>
#include <abt-snoozer.h>

#include "margo.h"

static na_class_t *network_class = NULL;
static na_context_t *na_context = NULL;
static hg_context_t *hg_context = NULL;
static hg_class_t *hg_class = NULL;

static ABT_thread hg_progress_tid;
static int hg_progress_shutdown_flag = 0;
static void hg_progress_fn(void* foo);

static ABT_pool main_pool;
static ABT_pool engine_pool;
static ABT_xstream engine_xstream;

struct handler_entry
{
    void* fn;
    hg_handle_t handle;
    struct handler_entry *next; 
};

int margo_init(na_bool_t listen, const char* local_addr)
{
    int ret;
    ABT_xstream xstream;

    /* boilerplate HG initialization steps */
    network_class = NA_Initialize(local_addr, listen);
    if(!network_class)
    {
        return(-1);
    }

    na_context = NA_Context_create(network_class);
    if(!na_context)
    {
        NA_Finalize(network_class);
        return(-1);
    }

    hg_class = HG_Init(network_class, na_context, NULL);
    if(!hg_class)
    {
        NA_Context_destroy(network_class, na_context);
        NA_Finalize(network_class);
        return(-1);
    }

    hg_context = HG_Context_create(hg_class);
    if(!hg_context)
    {
        HG_Finalize(hg_class);
        NA_Context_destroy(network_class, na_context);
        NA_Finalize(network_class);
        return(-1);
    }

    /* get the primary pool for the caller, this is where we will run ULTs to
     * handle incoming requests
     */    
    ret = ABT_xstream_self(&xstream);
    if(ret != 0)
    {
        /* TODO: err handling */
        fprintf(stderr, "Error: ABT_xstream_self()\n");
        return(-1);
    }

    ret = ABT_xstream_get_main_pools(xstream, 1, &main_pool);
    if(ret != 0)
    {
        /* TODO: err handling */
        fprintf(stderr, "Error: ABT_xstream_get_main_pools()\n");
        return(-1);
    }

    /* create an ES and ULT to drive Mercury progress */
    ret = ABT_snoozer_xstream_create(&engine_pool, &engine_xstream);
    if(ret != 0)
    {
        /* TODO: err handling */
        fprintf(stderr, "Error: ABT_snoozer_xstream_create()\n");
        return(-1);
    }
    ret = ABT_thread_create(engine_pool, hg_progress_fn, NULL, 
        ABT_THREAD_ATTR_NULL, &hg_progress_tid);
    if(ret != 0)
    {
        /* TODO: err handling */
        fprintf(stderr, "Error: ABT_thread_create()\n");
        return(-1);
    }

    return 0;
}

void margo_finalize(void)
{
    /* tell progress thread to wrap things up */
    hg_progress_shutdown_flag = 1;

    /* wait for it to shutdown cleanly */
    ABT_thread_join(hg_progress_tid);
    ABT_thread_free(&hg_progress_tid);
    ABT_xstream_join(engine_xstream);
    ABT_xstream_free(&engine_xstream);

    HG_Context_destroy(hg_context);
    HG_Finalize(hg_class);
    NA_Context_destroy(network_class, na_context);
    NA_Finalize(network_class);

    return;
}

/* dedicated thread function to drive Mercury progress */
static void hg_progress_fn(void* foo)
{
    int ret;
    unsigned int actual_count;

    while(!hg_progress_shutdown_flag)
    {
        do {
            ret = HG_Trigger(hg_class, hg_context, 0, 1, &actual_count);
        } while((ret == HG_SUCCESS) && actual_count && !hg_progress_shutdown_flag);

        if(!hg_progress_shutdown_flag)
            HG_Progress(hg_class, hg_context, 100);
    }

    return;
}

hg_class_t* margo_get_class(void)
{
    return(hg_class);
}

ABT_pool* margo_get_main_pool(void)
{
    return(&main_pool);
}

na_return_t margo_addr_lookup(const char* name, na_addr_t* addr)
{
    na_return_t ret;

    ret = NA_Addr_lookup_wait(network_class, name, addr);

    return ret;
}

hg_return_t margo_create_handle(na_addr_t addr, hg_id_t id,
    hg_handle_t *handle)
{
    hg_return_t ret;

    ret = HG_Create(hg_class, hg_context, addr, id, handle);

    return ret;
}

static hg_return_t margo_forward_cb(const struct hg_cb_info *info)
{
    hg_return_t hret = info->ret;

    ABT_eventual *eventual = info->arg;
    /* propagate return code out through eventual */
    ABT_eventual_set(*eventual, &hret, sizeof(hret));
    
    return(HG_SUCCESS);
}

hg_return_t margo_forward(
    hg_handle_t handle,
    void *in_struct)
{
    hg_return_t hret = HG_TIMEOUT;
    ABT_eventual eventual;
    int ret;
    hg_return_t* waited_hret;

    ret = ABT_eventual_create(sizeof(hret), &eventual);
    if(ret != 0)
    {
        return(HG_NOMEM_ERROR);        
    }

    hret = HG_Forward(handle, margo_forward_cb, &eventual, in_struct);
    if(hret == 0)
    {
        ABT_eventual_wait(eventual, (void**)&waited_hret);
        hret = *waited_hret;
    }

    ABT_eventual_free(&eventual);

    return(hret);
}

static hg_return_t margo_bulk_transfer_cb(const struct hg_bulk_cb_info *hg_bulk_cb_info)
{
    hg_return_t hret = hg_bulk_cb_info->ret;
    ABT_eventual *eventual = hg_bulk_cb_info->arg;

    /* propagate return code out through eventual */
    ABT_eventual_set(*eventual, &hret, sizeof(hret));
    
    return(HG_SUCCESS);
}

hg_return_t margo_bulk_transfer(
    hg_bulk_context_t *context,
    hg_bulk_op_t op,
    na_addr_t origin_addr,
    hg_bulk_t origin_handle,
    size_t origin_offset,
    hg_bulk_t local_handle,
    size_t local_offset,
    size_t size)
{
    hg_return_t hret = HG_TIMEOUT;
    hg_return_t *waited_hret;
    ABT_eventual eventual;
    int ret;

    ret = ABT_eventual_create(sizeof(hret), &eventual);
    if(ret != 0)
    {
        return(HG_NOMEM_ERROR);        
    }

    hret = HG_Bulk_transfer(context, margo_bulk_transfer_cb, &eventual, op, 
        origin_addr, origin_handle, origin_offset, local_handle, local_offset,
        size, HG_OP_ID_IGNORE);
    if(hret == 0)
    {
        ABT_eventual_wait(eventual, (void**)&waited_hret);
        hret = *waited_hret;
    }

    ABT_eventual_free(&eventual);

    return(hret);
}

