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

/* This is to prevent the user from usin HG_Register_data
 * and HG_Registered_data, which are replaced with
 * margo_register_data and margo_registered_data
 * respecively.
 */

#include <mercury_bulk.h>
#include <mercury.h>
#include <mercury_macros.h>
#include <abt.h>

#undef MERCURY_REGISTER

struct margo_instance;
typedef struct margo_instance* margo_instance_id;
typedef struct margo_data* margo_data_ptr;

#define MARGO_INSTANCE_NULL ((margo_instance_id)NULL)
#define MARGO_DEFAULT_MPLEX_ID 0
#define MARGO_RPC_ID_IGNORE ((hg_id_t*)NULL)

/**
 * Initializes margo library.
 * @param [in] addr_str            Mercury host address with port number
 * @param [in] listen_flag         Boolean flag to listen for incoming connections
 * @param [in] use_progress_thread Boolean flag to use a dedicated thread for
 *                                 running Mercury's progress loop. If false,
 *                                 it will run in the caller's thread context.
 * @param [in] rpc_thread_count    Number of threads to use for running RPC
 *                                 calls. A value of 0 directs Margo to execute
 *                                 RPCs in the caller's thread context.
 *                                 Clients (i.e processes that will *not* 
 *                                 service incoming RPCs) should use a value 
 *                                 of 0. A value of -1 directs Margo to use 
 *                                 the same execution context as that used 
 *                                 for Mercury progress.
 * @returns margo instance id on success, MARGO_INSTANCE_NULL upon error
 *
 * NOTE: Servers (processes expecting to service incoming RPC requests) must
 * specify non-zero values for use_progress_thread and rpc_thread_count *or*
 * call margo_wait_for_finalize() after margo_init() to relinguish control to 
 * Margo.
 */
margo_instance_id margo_init(
    const char *addr_str,
    int listen_flag,
    int use_progress_thread,
    int rpc_thread_count);

/**
 * Initializes margo library from given argobots and Mercury instances.
 * @param [in] progress_pool Argobots pool to drive communication
 * @param [in] handler_pool Argobots pool to service RPC handlers
 * @param [in] hg_context Mercury context
 * @returns margo instance id on success, MARGO_INSTANCE_NULL upon error
 */
margo_instance_id margo_init_pool(
    ABT_pool progress_pool,
    ABT_pool handler_pool,
    hg_context_t *hg_context);

/**
 * Shuts down margo library and its underlying abt and mercury resources
 * @param [in] mid Margo instance
 */
void margo_finalize(margo_instance_id mid);

/**
 * Suspends the caller until some other entity (e.g. an RPC, thread, or
 * signal handler) invokes margo_finalize().
 *
 * NOTE: This informs Margo that the calling thread no longer needs to be 
 * scheduled for execution if it is sharing an Argobots pool with the
 * progress engine.
 *
 * @param [in] mid Margo instance
 */
void margo_wait_for_finalize(margo_instance_id mid);

/** 
 * Registers an RPC with margo
 * @param [in] mid Margo instance
 * @param [in] id Mercury RPC identifier
 */
int margo_register(margo_instance_id mid, hg_id_t id);

/** 
 * Registers an RPC with margo that is associated with a multiplexed service
 * @param [in] mid Margo instance
 * @param [in] id Mercury RPC identifier
 * @param [in] mplex_id multiplexing identifier
 * @param [in] pool Argobots pool the handler will execute in
 */
int margo_register_mplex(margo_instance_id mid, hg_id_t id, uint32_t mplex_id, ABT_pool pool);

/*
 * Indicate whether HG_Register_name() has been called for the RPC specified by
 * func_name.
 *
 * \param hg_class [IN]         pointer to HG class
 * \param func_name [IN]        function name
 * \param id [OUT]              registered RPC ID
 * \param flag [OUT]            pointer to boolean
 *
 * \return HG_SUCCESS or corresponding HG error code
 */
/* XXX
HG_EXPORT hg_return_t
HG_Registered_name(
        hg_class_t *hg_class,
        const char *func_name,
        hg_id_t *id,
        hg_bool_t *flag
        );
*/

/**
 * Register and associate user data to registered function.
 * When HG_Finalize() is called free_callback (if defined) is called 
 * to free the registered data.
 *
 * \param [in] mid            Margo instance
 * \param [in] id             registered function ID
 * \param [in] data           pointer to data
 * \param [in] free_callback  pointer to free function
 *
 * \return HG_SUCCESS or corresponding HG error code
 */
hg_return_t margo_register_data(
    margo_instance_id mid,
    hg_id_t id,
    void *data,
    void (*free_callback)(void *));

/**
 * Indicate whether margo_register_data() has been called and return associated
 * data.
 *
 * \param [in] mid        Margo instance 
 * \param [in] id         registered function ID
 *
 * \return Pointer to data or NULL
 */
void* margo_registered_data(margo_instance_id mid, hg_id_t id);

/**
 * Disable response for a given RPC ID. This allows an origin process to send an
 * RPC to a target without waiting for a response. The RPC completes locally and
 * the callback on the origin is therefore pushed to the completion queue once
 * the RPC send is completed. By default, all RPCs expect a response to
 * be sent back.
 *
 * \param hg_class [IN]         pointer to HG class
 * \param id [IN]               registered function ID
 * \param disable [IN]          boolean (HG_TRUE to disable
 *                                       HG_FALSE to re-enable)
 *
 * \return HG_SUCCESS or corresponding HG error code
 */
/* XXX
HG_EXPORT hg_return_t
HG_Registered_disable_response(
        hg_class_t *hg_class,
        hg_id_t id,
        hg_bool_t disable
        );
*/

/**
 * Lookup an addr from a peer address/name.
 * @param [in] name             lookup name
 * @param [out] addr            return address
 * @returns HG_SUCCESS on success
 */
hg_return_t margo_addr_lookup(
    margo_instance_id mid,
    const char   *name,
    hg_addr_t    *addr);

/**
 * Free the addr from the list of peers.
 *
 * \param hg_class [IN]         pointer to HG class
 * \param addr [IN]             abstract address
 *
 * \return HG_SUCCESS or corresponding HG error code
 */
/* XXX
HG_EXPORT hg_return_t
HG_Addr_free(
        hg_class_t *hg_class,
        hg_addr_t   addr
        );
*/

/**
 * Access self address. Address must be freed with HG_Addr_free().
 *
 * \param hg_class [IN]         pointer to HG class
 * \param addr [OUT]            pointer to abstract address
 *
 * \return HG_SUCCESS or corresponding HG error code
 */
/* XXX 
HG_EXPORT hg_return_t
HG_Addr_self(
        hg_class_t *hg_class,
        hg_addr_t  *addr
        );
*/

/**
 * Duplicate an existing HG abstract address. The duplicated address can be
 * stored for later use and the origin address be freed safely. The duplicated
 * address must be freed with HG_Addr_free().
 *
 * \param hg_class [IN]         pointer to HG class
 * \param addr [IN]             abstract address
 * \param new_addr [OUT]        pointer to abstract address
 *
 * \return HG_SUCCESS or corresponding HG error code
 */
/* XXX
HG_EXPORT hg_return_t
HG_Addr_dup(
        hg_class_t *hg_class,
        hg_addr_t   addr,
        hg_addr_t  *new_addr
        );
*/

/**
 * Convert an addr to a string (returned string includes the terminating
 * null byte '\0'). If buf is NULL, the address is not converted and only
 * the required size of the buffer is returned. If the input value passed
 * through buf_size is too small, HG_SIZE_ERROR is returned and the buf_size
 * output is set to the minimum size required.
 *
 * \param hg_class [IN]         pointer to HG class
 * \param buf [IN/OUT]          pointer to destination buffer
 * \param buf_size [IN/OUT]     pointer to buffer size
 * \param addr [IN]             abstract address
 *
 * \return HG_SUCCESS or corresponding HG error code
 */
/* XXX
HG_EXPORT hg_return_t
HG_Addr_to_string(
        hg_class_t *hg_class,
        char       *buf,
        hg_size_t  *buf_size,
        hg_addr_t   addr
        );
*/

/**
 * Initiate a new HG RPC using the specified function ID and the local/remote
 * target defined by addr. The HG handle created can be used to query input
 * and output, as well as issuing the RPC by calling HG_Forward().
 * After completion the handle must be freed using HG_Destroy().
 *
 * \param context [IN]          pointer to HG context
 * \param addr [IN]             abstract network address of destination
 * \param id [IN]               registered function ID
 * \param handle [OUT]          pointer to HG handle
 *
 * \return HG_SUCCESS or corresponding HG error code
 */
/* XXX
HG_EXPORT hg_return_t
HG_Create(
        hg_context_t *context,
        hg_addr_t addr,
        hg_id_t id,
        hg_handle_t *handle
        );
*/

/**
 * Destroy HG handle. Decrement reference count, resources associated to the
 * handle are freed when the reference count is null.
 *
 * \param handle [IN]           HG handle
 *
 * \return HG_SUCCESS or corresponding HG error code
 */
/* XXX
HG_EXPORT hg_return_t
HG_Destroy(
        hg_handle_t handle
        );
*/

/**
 * Increment ref count on handle.
 *
 * \param handle [IN]           HG handle
 *
 * \return HG_SUCCESS or corresponding HG error code
 */
/* XXX
HG_EXPORT hg_return_t
HG_Ref_incr(
        hg_handle_t hg_handle
        );
*/

/**
 * Get info from handle.
 *
 * \remark Users must call HG_Addr_dup() to safely re-use the addr field.
 *
 * \param handle [IN]           HG handle
 *
 * \return Pointer to info or NULL in case of failure
 */
/* XXX
HG_EXPORT const struct hg_info *
HG_Get_info(
        hg_handle_t handle
        );
*/

/**
 * Get input from handle (requires registration of input proc to deserialize
 * parameters). Input must be freed using HG_Free_input().
 *
 * \remark This is equivalent to:
 *   - HG_Core_get_input()
 *   - Call hg_proc to deserialize parameters
 *
 * \param handle [IN]           HG handle
 * \param in_struct [IN/OUT]    pointer to input structure
 *
 * \return HG_SUCCESS or corresponding HG error code
 */
/* XXX
HG_EXPORT hg_return_t
HG_Get_input(
        hg_handle_t handle,
        void *in_struct
        );
*/

/**
 * Free resources allocated when deserializing the input.
 * User may copy parameters contained in the input structure before calling
 * HG_Free_input().
 *
 * \param handle [IN]           HG handle
 * \param in_struct [IN/OUT]    pointer to input structure
 *
 * \return HG_SUCCESS or corresponding HG error code
 */
/* XXX
HG_EXPORT hg_return_t
HG_Free_input(
        hg_handle_t handle,
        void *in_struct
        );
*/

/**
 * Get output from handle (requires registration of output proc to deserialize
 * parameters). Output must be freed using HG_Free_output().
 *
 * \remark This is equivalent to:
 *   - HG_Core_get_output()
 *   - Call hg_proc to deserialize parameters
 *
 *
 * \param handle [IN]           HG handle
 * \param out_struct [IN/OUT]   pointer to output structure
 *
 * \return HG_SUCCESS or corresponding HG error code
 */
/* XXX
HG_EXPORT hg_return_t
HG_Get_output(
        hg_handle_t handle,
        void *out_struct
        );
*/

/**
 * Free resources allocated when deserializing the output.
 * User may copy parameters contained in the output structure before calling
 * HG_Free_output().
 *
 * \param handle [IN]           HG handle
 * \param out_struct [IN/OUT]   pointer to input structure
 *
 * \return HG_SUCCESS or corresponding HG error code
 */
/* XXX
HG_EXPORT hg_return_t
HG_Free_output(
        hg_handle_t handle,
        void *out_struct
        );
*/

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
 * Forward an RPC request to a remote host with a user-defined timeout
 * @param [in] mid Margo instance
 * @param [in] handle identifier for the RPC to be sent
 * @param [in] in_struct input argument struct for RPC
 * @param [in] timeout_ms timeout in milliseconds
 * @returns 0 on success, hg_return_t values on error
 */
hg_return_t margo_forward_timed(
    margo_instance_id mid,
    hg_handle_t handle,
    void *in_struct,
    double timeout_ms);

/**
 * Send an RPC response, waiting for completion before returning
 * control to the calling ULT.
 * Note: this call is typically not needed as RPC listeners need not concern
 * themselves with what happens after an RPC finishes. However, there are cases
 * when this is useful (deferring resource cleanup, calling margo_finalize()
 * for e.g. a shutdown RPC).
 * @param [in] mid Margo instance
 * @param [in] handle identifier for the RPC for which a response is being
 * sent
 * @param [in] out_struct output argument struct for response
 * @return HG_SUCCESS on success, hg_return_t values on error. See HG_Respond.
 */
hg_return_t margo_respond(
    margo_instance_id mid,
    hg_handle_t handle,
    void *out_struct);

/**
 * Cancel an ongoing operation.
 *
 * \param handle [IN]           HG handle
 *
 * \return HG_SUCCESS or corresponding HG error code
 */
/* XXX
HG_EXPORT hg_return_t
HG_Cancel(
        hg_handle_t handle
        );
*/

/** 
 * Perform a bulk transfer
 * @param [in] mid Margo instance
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
    hg_bulk_op_t op,
    hg_addr_t origin_addr,
    hg_bulk_t origin_handle,
    size_t origin_offset,
    hg_bulk_t local_handle,
    size_t local_offset,
    size_t size);

/* XXX BULK */

/**
 * Retrieve the abt_handler pool that was associated with the instance at 
 *    initialization time
 * @param [in] mid Margo instance
 */
ABT_pool* margo_get_handler_pool(margo_instance_id mid);

/**
 * Retrieve the Mercury context that was associated with this instance at
 *    initialization time
 * @param [in] mid Margo instance
 * @return the Mercury context used in margo_init
 */
hg_context_t* margo_get_context(margo_instance_id mid);

/**
 * Retrieve the Mercury class that was associated with this instance at
 *    initialization time
 * @param [in] mid Margo instance
 * @return the Mercury class used in margo_init
 */
hg_class_t* margo_get_class(margo_instance_id mid);

/**
 * Get the margo_instance_id from a received RPC handle.
 *
 * \param [in] h          RPC handle
 * 
 * \return Margo instance
 */
margo_instance_id margo_hg_handle_get_instance(hg_handle_t h);

/**
 * Suspends the calling ULT for a specified time duration
 * @param [in] mid Margo instance
 * @param [in] timeout_ms timeout duration in milliseconds
 */
void margo_thread_sleep(
    margo_instance_id mid,
    double timeout_ms);

/**
 * Maps an RPC id and mplex id to the pool that it should execute on
 * @param [in] mid Margo instance
 * @param [in] id Mercury RPC identifier
 * @param [in] mplex_id multiplexing identifier
 * @param [out] pool Argobots pool the handler will execute in
 */
int margo_lookup_mplex(margo_instance_id mid, hg_id_t id, uint32_t mplex_id, ABT_pool *pool);


/**
 * macro that registers a function as an RPC.
 */
#define MARGO_REGISTER(__mid, __func_name, __in_t, __out_t, __handler, __rpc_id_ptr) do { \
    hg_return_t __hret; \
    hg_id_t __id; \
    hg_bool_t __flag; \
    int __ret; \
    __hret = HG_Registered_name(margo_get_class(__mid), __func_name, &__id, &__flag); \
    assert(__hret == HG_SUCCESS); \
    if(!__flag) \
        __id = HG_Register_name(margo_get_class(__mid), __func_name,\
                   BOOST_PP_CAT(hg_proc_, __in_t),\
                   BOOST_PP_CAT(hg_proc_, __out_t),\
                   __handler##_handler); \
    __ret = margo_register(__mid, __id); \
    assert(__ret == 0); \
    if(__rpc_id_ptr != MARGO_RPC_ID_IGNORE) { \
        *(__rpc_id_ptr) = __id; \
    } \
} while(0)

#define MARGO_REGISTER_MPLEX(__mid, __func_name, __in_t, __out_t, __handler, __mplex_id, __pool, __rpc_id_ptr) do { \
    hg_return_t __hret; \
    hg_id_t __id; \
    hg_bool_t __flag; \
    int __ret; \
    __hret = HG_Registered_name(margo_get_class(__mid), __func_name, &__id, &__flag); \
    assert(__hret == HG_SUCCESS); \
    if(!__flag) \
        __id = HG_Register_name(margo_get_class(__mid), __func_name,\
                   BOOST_PP_CAT(hg_proc_, __in_t),\
                   BOOST_PP_CAT(hg_proc_, __out_t),\
                   __handler##_handler); \
    __ret = margo_register_mplex(__mid, __id, __mplex_id, __pool); \
    assert(__ret == 0); \
    if(__rpc_id_ptr != MARGO_RPC_ID_IGNORE) { \
        *(__rpc_id_ptr) = __id; \
    } \
} while(0)

#define NULL_handler NULL

/**
 * macro that defines a function to glue an RPC handler to a ult handler
 * @param [in] __name name of handler function
 */
#define DEFINE_MARGO_RPC_HANDLER(__name) \
hg_return_t __name##_handler(hg_handle_t handle) { \
    int __ret; \
    ABT_pool __pool; \
    margo_instance_id __mid; \
    const struct hg_info *__hgi; \
    __hgi = HG_Get_info(handle); \
	__mid = margo_hg_handle_get_instance(handle); \
    if(__mid == MARGO_INSTANCE_NULL) { \
        return(HG_OTHER_ERROR); \
    } \
    __ret = margo_lookup_mplex(__mid, __hgi->id, __hgi->target_id, (&__pool)); \
    if(__ret != 0) { \
        return(HG_INVALID_PARAM); \
    }\
    __ret = ABT_thread_create(__pool, (void (*)(void *))__name, handle, ABT_THREAD_ATTR_NULL, NULL); \
    if(__ret != 0) { \
        return(HG_NOMEM_ERROR); \
    } \
    return(HG_SUCCESS); \
}

/**
 * macro that declares the prototype for a function to glue an RPC 
 * handler to a ult
 * @param [in] __name name of handler function
 */
#define DECLARE_MARGO_RPC_HANDLER(__name) hg_return_t __name##_handler(hg_handle_t handle);

#ifdef __cplusplus
}
#endif

#endif /* __MARGO */
