
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

struct margo_instance
{
    na_class_t *network_class;
    na_context_t *na_context;
    hg_context_t *hg_context;
    hg_class_t *hg_class;
    ABT_thread hg_progress_tid;
    int hg_progress_shutdown_flag;
    ABT_pool main_pool;
    ABT_pool engine_pool;
    ABT_xstream engine_xstream;
    int table_index;
};

struct margo_handler_mapping
{
    hg_class_t *class;
    margo_instance_id mid;
};

#define MAX_HANDLER_MAPPING 8
static int handler_mapping_table_size = 0;
static struct margo_handler_mapping handler_mapping_table[MAX_HANDLER_MAPPING] = {0};

static void hg_progress_fn(void* foo);


struct handler_entry
{
    void* fn;
    hg_handle_t handle;
    struct handler_entry *next; 
};

margo_instance_id margo_init(na_bool_t listen, const char* local_addr)
{
    int ret;
    ABT_xstream xstream;
    struct margo_instance *mid;

    if(handler_mapping_table_size >= MAX_HANDLER_MAPPING)
        return(NULL);

    mid = malloc(sizeof(*mid));
    if(!mid)
        return(NULL);
    memset(mid, 0, sizeof(*mid));

    /* boilerplate HG initialization steps */
    mid->network_class = NA_Initialize(local_addr, listen);
    if(!mid->network_class)
    {
        free(mid);
        return(NULL);
    }

    mid->na_context = NA_Context_create(mid->network_class);
    if(!mid->na_context)
    {
        NA_Finalize(mid->network_class);
        free(mid);
        return(NULL);
    }

    mid->hg_class = HG_Init(mid->network_class, mid->na_context, NULL);
    if(!mid->hg_class)
    {
        NA_Context_destroy(mid->network_class, mid->na_context);
        NA_Finalize(mid->network_class);
        free(mid);
        return(NULL);
    }

    mid->hg_context = HG_Context_create(mid->hg_class);
    if(!mid->hg_context)
    {
        HG_Finalize(mid->hg_class);
        NA_Context_destroy(mid->network_class, mid->na_context);
        NA_Finalize(mid->network_class);
        return(NULL);
    }

    /* get the primary pool for the caller, this is where we will run ULTs to
     * handle incoming requests
     */    
    ret = ABT_xstream_self(&xstream);
    if(ret != 0)
    {
        /* TODO: err handling */
        fprintf(stderr, "Error: ABT_xstream_self()\n");
        return(NULL);
    }

    ret = ABT_xstream_get_main_pools(xstream, 1, &mid->main_pool);
    if(ret != 0)
    {
        /* TODO: err handling */
        fprintf(stderr, "Error: ABT_xstream_get_main_pools()\n");
        return(NULL);
    }

    /* create an ES and ULT to drive Mercury progress */
    ret = ABT_snoozer_xstream_create(1, &mid->engine_pool, &mid->engine_xstream);
    if(ret != 0)
    {
        /* TODO: err handling */
        fprintf(stderr, "Error: ABT_snoozer_xstream_create()\n");
        return(NULL);
    }
    ret = ABT_thread_create(mid->engine_pool, hg_progress_fn, mid, 
        ABT_THREAD_ATTR_NULL, &mid->hg_progress_tid);
    if(ret != 0)
    {
        /* TODO: err handling */
        fprintf(stderr, "Error: ABT_thread_create()\n");
        return(NULL);
    }

    handler_mapping_table[handler_mapping_table_size].mid = mid;
    handler_mapping_table[handler_mapping_table_size].class = mid->hg_class;
    mid->table_index = handler_mapping_table_size;
    handler_mapping_table_size++;

    return mid;
}

void margo_finalize(margo_instance_id mid)
{
    int i;

    /* tell progress thread to wrap things up */
    mid->hg_progress_shutdown_flag = 1;

    /* wait for it to shutdown cleanly */
    ABT_thread_join(mid->hg_progress_tid);
    ABT_thread_free(&mid->hg_progress_tid);
    ABT_xstream_join(mid->engine_xstream);
    ABT_xstream_free(&mid->engine_xstream);

    HG_Context_destroy(mid->hg_context);
    HG_Finalize(mid->hg_class);
    NA_Context_destroy(mid->network_class, mid->na_context);
    NA_Finalize(mid->network_class);

    for(i=mid->table_index; i<(handler_mapping_table_size-1); i++)
    {
        handler_mapping_table[i] = handler_mapping_table[i+1];
    }
    handler_mapping_table_size--;

    return;
}

/* dedicated thread function to drive Mercury progress */
static void hg_progress_fn(void* foo)
{
    int ret;
    unsigned int actual_count;
    struct margo_instance *mid = (struct margo_instance *)foo;

    while(!mid->hg_progress_shutdown_flag)
    {
        do {
            ret = HG_Trigger(mid->hg_class, mid->hg_context, 0, 1, &actual_count);
        } while((ret == HG_SUCCESS) && actual_count && !mid->hg_progress_shutdown_flag);

        if(!mid->hg_progress_shutdown_flag)
            HG_Progress(mid->hg_class, mid->hg_context, 100);
    }

    return;
}

hg_class_t* margo_get_class(margo_instance_id mid)
{
    return(mid->hg_class);
}

ABT_pool* margo_get_main_pool(margo_instance_id mid)
{
    return(&mid->main_pool);
}

na_return_t margo_addr_lookup(margo_instance_id mid, const char* name, na_addr_t* addr)
{
    na_return_t ret;

    ret = NA_Addr_lookup_wait(mid->network_class, name, addr);

    return ret;
}

hg_return_t margo_create_handle(margo_instance_id mid, na_addr_t addr, 
    hg_id_t id, hg_handle_t *handle)
{
    hg_return_t ret;

    ret = HG_Create(mid->hg_class, mid->hg_context, addr, id, handle);

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
    margo_instance_id mid,
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
    margo_instance_id mid,
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

margo_instance_id margo_hg_class_to_instance(hg_class_t *class)
{
    int i;

    for(i=0; i<handler_mapping_table_size; i++)
    {
        if(handler_mapping_table[i].class == class)
            return(handler_mapping_table[i].mid);
    }
    return(NULL);
}
