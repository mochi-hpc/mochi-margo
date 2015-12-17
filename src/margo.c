
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
    /* provided by caller */
    hg_context_t *hg_context;
    hg_class_t *hg_class;
    ABT_pool handler_pool;
    ABT_pool progress_pool;

    /* internal to margo for this particular instance */
    ABT_thread hg_progress_tid;
    int hg_progress_shutdown_flag;
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

margo_instance_id margo_init(ABT_pool progress_pool, ABT_pool handler_pool,
    hg_context_t *hg_context, hg_class_t *hg_class)
{
    int ret;
    struct margo_instance *mid;

    if(handler_mapping_table_size >= MAX_HANDLER_MAPPING)
        return(NULL);

    mid = malloc(sizeof(*mid));
    if(!mid)
        return(NULL);
    memset(mid, 0, sizeof(*mid));

    mid->progress_pool = progress_pool;
    mid->handler_pool = handler_pool;
    mid->hg_class = hg_class;
    mid->hg_context = hg_context;

    ret = ABT_thread_create(mid->progress_pool, hg_progress_fn, mid, 
        ABT_THREAD_ATTR_NULL, &mid->hg_progress_tid);
    if(ret != 0)
    {
        fprintf(stderr, "Error: ABT_thread_create()\n");
        free(mid);
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

ABT_pool* margo_get_handler_pool(margo_instance_id mid)
{
    return(&mid->handler_pool);
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

margo_instance_id margo_hg_class_to_instance(hg_class_t *cl)
{
    int i;

    for(i=0; i<handler_mapping_table_size; i++)
    {
        if(handler_mapping_table[i].class == cl)
            return(handler_mapping_table[i].mid);
    }
    return(NULL);
}
