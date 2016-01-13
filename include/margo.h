/*
 * (C) 2015 The University of Chicago
 * 
 * See COPYRIGHT in top-level directory.
 */

#ifndef __MARGO
#define __MARGO

#ifdef __cplusplus
extern "C" {
#endif

#include <mercury_bulk.h>
#include <mercury.h>
#include <mercury_macros.h>
#include <abt.h>
#include <ev.h>


struct margo_instance;
typedef struct margo_instance* margo_instance_id;

#define MARGO_INSTANCE_NULL ((margo_instance_id)NULL)

/**
 * Initializes margo library from given argobots and Mercury instances.
 * @param [in] progress_pool Argobots pool to drive communication
 * @param [in] handler_pool Argobots pool to service RPC handlers
 * @param [in] hg_context Mercury context
 * @param [in] hg_class Mercury class
 * @returns margo instance id on success, NULL upon error
 */
margo_instance_id margo_init(ABT_pool progress_pool, ABT_pool handler_pool,
    hg_context_t *hg_context, hg_class_t *hg_class);

/**
 * Shuts down margo library and its underlying evfibers and mercury resources
 * @param [in] mid Margo instance
 */
void margo_finalize(margo_instance_id mid);

/**
 * Retrieve the abt_handler pool that was associated with the instance at 
 *    initialization time
 * @param [in] mid Margo instance
 */
ABT_pool* margo_get_handler_pool(margo_instance_id mid);

/**
 * Forward an RPC request to a remote host
 * @param [in] mid Margo instance
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
 * @param [in] mid Margo instance
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

/**
 * address lookup
 * @param [in] na_class         pointer to NA class
 * @param [in] context          pointer to context of execution
 * @param [in] name             lookup name
 * @returns NA_SUCCESS on on success
 */
na_return_t margo_na_addr_lookup(
    margo_instance_id mid,
    na_class_t   *na_class,
    na_context_t *context,
    const char   *name,
    na_addr_t    *addr);

/**
 * Retrive the Margo instance that has been associated with a Mercury class
 * @param [in] cl Mercury class
 * @returns Margo instance on success, NULL on error
 */
margo_instance_id margo_hg_class_to_instance(hg_class_t *cl);

/**
 * macro that defines a function to glue an RPC handler to a fiber
 * @param [in] __name name of handler function
 */
#define DEFINE_MARGO_RPC_HANDLER(__name) \
hg_return_t __name##_handler(hg_handle_t handle) { \
    int __ret; \
    ABT_pool* __pool; \
    margo_instance_id __mid; \
    struct hg_info *__hgi; \
    hg_handle_t* __handle = (hg_handle_t*) malloc(sizeof(*__handle)); \
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

/**
 * macro that declares the prototype for a function to glue an RPC 
 * handler to a fiber
 * @param [in] __name name of handler function
 */
#define DECLARE_MARGO_RPC_HANDLER(__name) hg_return_t __name##_handler(hg_handle_t handle);

#ifdef __cplusplus
}
#endif

#endif /* __MARGO */
