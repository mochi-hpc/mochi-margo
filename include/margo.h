/*
 * (C) 2015 The University of Chicago
 * 
 * See COPYRIGHT in top-level directory.
 */

#ifndef __MARGO
#define __MARGO

#include <mercury_bulk.h>
#include <mercury.h>
#include <mercury_macros.h>
#include <abt.h>
#include <ev.h>

/* TODO: update doxygen, especially with mid arguments */

struct margo_instance;
typedef struct margo_instance* margo_instance_id;

/**
 * Initializes margo library, including initializing underlying libevfibers
 *    and Mercury instances.
 * @param [in] listen flag indicating whether to accept inbound RPCs or not
 * @param [in] local_addr address to listen on if listen is set
 * @returns margo instance id on success, NULL upon error
 */
margo_instance_id margo_init(ABT_pool progress_pool, ABT_pool handler_pool,
    hg_context_t *hg_context, hg_class_t *hg_class);

/**
 * Shuts down margo library and its underlying evfibers and mercury resources
 */
void margo_finalize(margo_instance_id mid);

/**
 * Retrieve the ABT pool associated with the main caller (whoever invoked the
 * init function); this is where margo will execute RPC handlers.
 */
ABT_pool* margo_get_handler_pool(margo_instance_id mid);

/**
 * Forward an RPC request to a remote host
 * @param [in] handle identifier for the RPC to be sent
 * @param [in] in_struct input argument struct for RPC
 * @returns 0 on success, hg_return_t values on error
 */
hg_return_t margo_forward(
    margo_instance_id mid,
    hg_handle_t handle,
    void *in_struct);

/** 
 * Perform a bulk transfer
 * @param [in] context Mercury bulk context
 * @param [in] op type of operation to perform
 * @param [in] origin_addr remote Mercury address
 * @param [in] origin_handle remote Mercury bulk memory handle
 * @param [in] origin_offset offset into remote bulk memory to access
 * @param [in] local_handle local bulk memory handle
 * @param [in] local_offset offset into local bulk memory to access
 * @param [in] size size (in bytes) of transfer
 * @returns 0 on success, hg_return_t values on error
 */
hg_return_t margo_bulk_transfer(
    margo_instance_id mid,
    hg_bulk_context_t *context,
    hg_bulk_op_t op,
    na_addr_t origin_addr,
    hg_bulk_t origin_handle,
    size_t origin_offset,
    hg_bulk_t local_handle,
    size_t local_offset,
    size_t size);

/* given a particular hg_class, find the ABT pool that we intend to use to
 * execute handlers.
 */
margo_instance_id margo_hg_class_to_instance(hg_class_t *class);

/**
 * macro that defines a function to glue an RPC handler to a fiber
 * @param [in] __name name of handler function
 */
#define DEFINE_ARGO_RPC_HANDLER(__name) \
hg_return_t __name##_handler(hg_handle_t handle) { \
    int __ret; \
    ABT_pool* __pool; \
    margo_instance_id __mid; \
    struct hg_info *__hgi; \
    hg_handle_t* __handle = malloc(sizeof(*__handle)); \
    if(!__handle) return(HG_NOMEM_ERROR); \
    *__handle = handle; \
    __hgi = HG_Get_info(handle); \
    __mid = margo_hg_class_to_instance(__hgi->hg_class); \
    __pool = margo_get_handler_pool(__mid); \
    __ret = ABT_thread_create(*__pool, __name, __handle, ABT_THREAD_ATTR_NULL, NULL); \
    if(__ret != 0) { \
        return(HG_NOMEM_ERROR); \
    } \
    return(HG_SUCCESS); \
}

#define DECLARE_ARGO_RPC_HANDLER(__name) hg_return_t __name##_handler(hg_handle_t handle);

#endif /* __MARGO */
