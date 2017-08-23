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

#include <mercury.h>
#include <mercury_types.h>
#include <mercury_bulk.h>
#include <mercury_macros.h>
#include <abt.h>

#undef MERCURY_REGISTER

struct margo_instance;
typedef struct margo_instance* margo_instance_id;
typedef struct margo_data* margo_data_ptr;

#define MARGO_INSTANCE_NULL ((margo_instance_id)NULL)
#define MARGO_CLIENT_MODE 0
#define MARGO_SERVER_MODE 1
#define MARGO_DEFAULT_MPLEX_ID 0

/**
 * Initializes margo library.
 * @param [in] addr_str            Mercury host address with port number
 * @param [in] mode                Mode to run Margo in:
 *                                     - MARGO_CLIENT_MODE
 *                                     - MARGO_SERVER_MODE
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
    int mode,
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
void margo_finalize(
    margo_instance_id mid);

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
void margo_wait_for_finalize(
    margo_instance_id mid);

/** 
 * Registers an RPC with margo
 *
 * \param [in] mid Margo instance
 * \param [in] func_name unique function name for RPC
 * \param [in] in_proc_cb pointer to input proc callback
 * \param [in] out_proc_cb pointer to output proc callback
 * \param [in] rpc_cb RPC callback
 *
 * \return unique ID associated to the registered function
 */
hg_id_t margo_register_name(
    margo_instance_id mid,
    const char *func_name,
    hg_proc_cb_t in_proc_cb,
    hg_proc_cb_t out_proc_cb,
    hg_rpc_cb_t rpc_cb);

/** 
 * Registers an RPC with margo that is associated with a multiplexed service
 *
 * \param [in] mid Margo instance
 * \param [in] func_name unique function name for RPC
 * \param [in] in_proc_cb pointer to input proc callback
 * \param [in] out_proc_cb pointer to output proc callback
 * \param [in] rpc_cb RPC callback
 * \param [in] mplex_id multiplexing identifier
 * \param [in] pool Argobots pool the handler will execute in
 *
 * \return unique ID associated to the registered function
 */
hg_id_t margo_register_name_mplex(
    margo_instance_id mid,
    const char *func_name,
    hg_proc_cb_t in_proc_cb,
    hg_proc_cb_t out_proc_cb,
    hg_rpc_cb_t rpc_cb,
    uint32_t mplex_id,
    ABT_pool pool);

/*
 * Indicate whether margo_register_name() has been called for the RPC specified by
 * func_name.
 *
 * \param [in] mid Margo instance
 * \param [in] func_name function name
 * \param [out] id registered RPC ID
 * \param [out] flag pointer to boolean
 *
 * \return HG_SUCCESS or corresponding HG error code
 */
hg_return_t margo_registered_name(
    margo_instance_id mid,
    const char *func_name,
    hg_id_t *id,
    hg_bool_t *flag);

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
void* margo_registered_data(
    margo_instance_id mid,
    hg_id_t id);

/**
 * Disable response for a given RPC ID.
 *
 * \param [in] mid          Margo instance 
 * \param [in] id           registered function ID
 * \param [in] disable_flag flag to disable (1) or re-enable (0) responses
 *
 * \return HG_SUCCESS or corresponding HG error code
 */
hg_return_t margo_registered_disable_response(
    margo_instance_id mid,
    hg_id_t id,
    int disable_flag);

/**
 * Lookup an addr from a peer address/name.
 * \param [in] name     lookup name
 * \param [out] addr    return address
 *
 * \return HG_SUCCESS or corresponding HG error code
 */
hg_return_t margo_addr_lookup(
    margo_instance_id mid,
    const char *name,
    hg_addr_t *addr);

/**
 * Free the given Mercury addr.
 *
 * \param [in] mid  Margo instance 
 * \param [in] addr Mercury address
 *
 * \return HG_SUCCESS or corresponding HG error code
 */
hg_return_t margo_addr_free(
    margo_instance_id mid,
    hg_addr_t addr);

/**
 * Access self address. Address must be freed with margo_addr_free().
 *
 * \param [in] mid  Margo instance 
 * \param [in] addr pointer to abstract Mercury address
 *
 * \return HG_SUCCESS or corresponding HG error code
 */
hg_return_t margo_addr_self(
    margo_instance_id mid,
    hg_addr_t *addr);

/**
 * Duplicate an existing Mercury address. 
 *
 * \param [in] mid      Margo instance 
 * \param [in] addr     abstract Mercury address to duplicate
 * \param [in] new_addr pointer to newly allocated abstract Mercury address
 *
 * \return HG_SUCCESS or corresponding HG error code
 */
hg_return_t margo_addr_dup(
    margo_instance_id mid,
    hg_addr_t addr,
    hg_addr_t *new_addr);

/**
 * Convert a Mercury addr to a string (returned string includes the
 * terminating null byte '\0'). If buf is NULL, the address is not
 * converted and only the required size of the buffer is returned.
 * If the input value passed through buf_size is too small, HG_SIZE_ERROR
 * is returned and the buf_size output is set to the minimum size required.
 *
 * \param [in] mid          Margo instance 
 * \param [in/out] buf      pointer to destination buffer
 * \param [in/out] buf_size pointer to buffer size
 * \param [in] addr         abstract Mercury address
 *
 * \return HG_SUCCESS or corresponding HG error code
 */
hg_return_t margo_addr_to_string(
    margo_instance_id mid,
    char *buf,
    hg_size_t *buf_size,
    hg_addr_t addr);

/**
 * Initiate a new Mercury RPC using the specified function ID and the
 * local/remote target defined by addr. The handle created can be used to
 * query input and output, as well as issuing the RPC by calling
 * HG_Forward(). After completion the handle must be freed using HG_Destroy().
 *
 * \param [in] mid      Margo instance 
 * \param [in] addr     abstract Mercury address of destination
 * \param [in] id       registered function ID
 * \param [out] handle  pointer to HG handle
 *
 * \return HG_SUCCESS or corresponding HG error code
 */
hg_return_t margo_create(
    margo_instance_id mid,
    hg_addr_t addr,
    hg_id_t id,
    hg_handle_t *handle);

/**
 * Destroy Mercury handle.
 *
 * \param [in] handle Mercury handle
 *
 * \return HG_SUCCESS or corresponding HG error code
 */
hg_return_t margo_destroy(
    hg_handle_t handle);

/**
 * Increment ref count on a Mercury handle.
 *
 * \param [in] handle Mercury handle
 *
 * \return HG_SUCCESS or corresponding HG error code
 */
hg_return_t margo_ref_incr(
    hg_handle_t handle);

/**
 * Get info from handle.
 *
 * \param [in] handle Mercury handle
 *
 * \return Pointer to info or NULL in case of failure
 */
const struct hg_info *margo_get_info(
    hg_handle_t handle);

/**
 * Get input from handle (requires registration of input proc to deserialize
 * parameters). Input must be freed using margo_free_input().
 *
 * \param [in] handle           Mercury handle
 * \param [in/out] in_struct    pointer to input structure
 *
 * \return HG_SUCCESS or corresponding HG error code
 */
hg_return_t margo_get_input(
    hg_handle_t handle,
    void *in_struct);

/**
 * Free resources allocated when deserializing the input.
 *
 * \param [in] handle           Mercury handle
 * \param [in/out] in_struct    pointer to input structure
 *
 * \return HG_SUCCESS or corresponding HG error code
 */
hg_return_t margo_free_input(
        hg_handle_t handle,
        void *in_struct);

/**
 * Get output from handle (requires registration of output proc to deserialize
 * parameters). Output must be freed using margo_free_output().
 *
 * \param [in] handle           Mercury handle
 * \param [in/out] out_struct   pointer to output structure
 *
 * \return HG_SUCCESS or corresponding HG error code
 */
hg_return_t margo_get_output(
    hg_handle_t handle,
    void *out_struct);

/**
 * Free resources allocated when deserializing the output.
 *
 * \param [in] handle           Mercury handle
 * \param [in/out] out_struct   pointer to output structure
 *
 * \return HG_SUCCESS or corresponding HG error code
 */
hg_return_t margo_free_output(
    hg_handle_t handle,
    void *out_struct);

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
 * \param [in] handle Mercury handle
 *
 * \return HG_SUCCESS or corresponding HG error code
 */
hg_return_t margo_cancel(
    hg_handle_t handle);

/**
 * Create an abstract bulk handle from specified memory segments.
 * Memory allocated is then freed when margo_bulk_free() is called.
 * \remark If NULL is passed to buf_ptrs, i.e.,
 * \verbatim margo_bulk_create(mid, count, NULL, buf_sizes, flags, &handle) \endverbatim
 * memory for the missing buf_ptrs array will be internally allocated.
 *
 * \param [in] mid          Margo instance 
 * \param [in] count        number of segments
 * \param [in] buf_ptrs     array of pointers
 * \param [in] buf_sizes    array of sizes
 * \param [in] flags        permission flag:
 *                             - HG_BULK_READWRITE
 *                             - HG_BULK_READ_ONLY
 *                             - HG_BULK_WRITE_ONLY
 * \param [out] handle      pointer to returned abstract bulk handle
 *
 * \return HG_SUCCESS or corresponding HG error code
 */
hg_return_t margo_bulk_create(
    margo_instance_id mid,
    hg_uint32_t count,
    void **buf_ptrs,
    const hg_size_t *buf_sizes,
    hg_uint8_t flags,
    hg_bulk_t *handle);

/**
 * Free bulk handle.
 *
 * \param [in/out] handle   abstract bulk handle
 *
 * \return HG_SUCCESS or corresponding HG error code
 */
hg_return_t margo_bulk_free(
    hg_bulk_t handle);

/**
 * Increment ref count on bulk handle.
 *
 * \param handle [IN]           abstract bulk handle
 *
 * \return HG_SUCCESS or corresponding HG error code
 */
hg_return_t margo_bulk_ref_incr(
    hg_bulk_t handle);

/**
 * Access bulk handle to retrieve memory segments abstracted by handle.
 *
 * \param [in] handle           abstract bulk handle
 * \param [in] offset           bulk offset
 * \param [in] size             bulk size
 * \param [in] flags            permission flag:
 *                                 - HG_BULK_READWRITE
 *                                 - HG_BULK_READ_ONLY
 * \param [in] max_count        maximum number of segments to be returned
 * \param [in/out] buf_ptrs     array of buffer pointers
 * \param [in/out] buf_sizes    array of buffer sizes
 * \param [out] actual_count    actual number of segments returned
 *
 * \return HG_SUCCESS or corresponding HG error code
 */
hg_return_t margo_bulk_access(
    hg_bulk_t handle,
    hg_size_t offset,
    hg_size_t size,
    hg_uint8_t flags,
    hg_uint32_t max_count,
    void **buf_ptrs,
    hg_size_t *buf_sizes,
    hg_uint32_t *actual_count);

/**
 * Get total size of data abstracted by bulk handle.
 *
 * \param [in] handle   abstract bulk handle
 *
 * \return Non-negative value
 */
hg_size_t margo_bulk_get_size(
    hg_bulk_t handle);

/**
 * Get total number of segments abstracted by bulk handle.
 *
 * \param [in] handle   abstract bulk handle
 *
 * \return Non-negative value
 */
hg_uint32_t margo_bulk_get_segment_count(
    hg_bulk_t handle);

/**
 * Get size required to serialize bulk handle.
 *
 * \param [in] handle           abstract bulk handle
 * \param [in] request_eager    boolean (passing HG_TRUE adds size of encoding
 *                              actual data along the handle if handle meets
 *                              HG_BULK_READ_ONLY flag condition)
 *
 * \return Non-negative value
 */
hg_size_t margo_bulk_get_serialize_size(
    hg_bulk_t handle,
    hg_bool_t request_eager);

/**
 * Serialize bulk handle into a buffer.
 *
 * \param [in/out] buf          pointer to buffer
 * \param [in] buf_size         buffer size
 * \param [in] request_eager    boolean (passing HG_TRUE encodes actual data
 *                              along the handle, which is more efficient for
 *                              small data, this is only valid if bulk handle
 *                              has HG_BULK_READ_ONLY permission)
 * \param [in] handle           abstract bulk handle
 *
 * \return HG_SUCCESS or corresponding HG error code
 */
hg_return_t margo_bulk_serialize(
    void *buf,
    hg_size_t buf_size,
    hg_bool_t request_eager,
    hg_bulk_t handle);

/**
 * Deserialize bulk handle from an existing buffer.
 *
 * \param [in] mid      Margo instance 
 * \param [out] handle  abstract bulk handle
 * \param [in] buf      pointer to buffer
 * \param [in] buf_size buffer size
 *
 * \return HG_SUCCESS or corresponding HG error code
 */
hg_return_t margo_bulk_deserialize(
    margo_instance_id mid,
    hg_bulk_t *handle,
    const void *buf,
    hg_size_t buf_size);

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
 * @param [out] op_id pointer to returned operation ID
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
    size_t size,
    hg_op_id_t *op_id);

/**
 * Cancel an ongoing operation.
 *
 * \param [in] op_id    operation ID
 *
 * \return HG_SUCCESS or corresponding HG error code
 */
hg_return_t margo_bulk_cancel(
    hg_op_id_t op_id);

/**
 * Suspends the calling ULT for a specified time duration
 * @param [in] mid Margo instance
 * @param [in] timeout_ms timeout duration in milliseconds
 */
void margo_thread_sleep(
    margo_instance_id mid,
    double timeout_ms);

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
 * Maps an RPC id and mplex id to the pool that it should execute on
 * @param [in] mid Margo instance
 * @param [in] id Mercury RPC identifier
 * @param [in] mplex_id multiplexing identifier
 * @param [out] pool Argobots pool the handler will execute in
 */
int margo_lookup_mplex(margo_instance_id mid, hg_id_t id, uint32_t mplex_id, ABT_pool *pool);

/**
 * Enables diagnostic collection on specified Margo instance
 *
 * @param [in] mid Margo instance
 * @returns void
 */
void margo_diag_start(margo_instance_id mid);

/**
 * Appends diagnostic statistics (enabled via margo_diag_start()) to specified 
 * output file.
 *
 * @param [in] mid Margo instance
 * @param [in] file output file ("-" for stdout)
 * @param [in] uniquify flag indicating if file name should have additional
 *   information added to it to make output from different processes unique
 * @returns void
 */
void margo_diag_dump(margo_instance_id mid, const char* file, int uniquify);

/**
 * macro that registers a function as an RPC.
 */
#define MARGO_REGISTER(__mid, __func_name, __in_t, __out_t, __handler) \
    margo_register_name(__mid, __func_name, \
        BOOST_PP_CAT(hg_proc_, __in_t), \
        BOOST_PP_CAT(hg_proc_, __out_t), \
        __handler##_handler);

#define MARGO_REGISTER_MPLEX(__mid, __func_name, __in_t, __out_t, __handler, __mplex_id, __pool) \
    margo_register_name_mplex(__mid, __func_name, \
        BOOST_PP_CAT(hg_proc_, __in_t), \
        BOOST_PP_CAT(hg_proc_, __out_t), \
        __handler##_handler, \
        __mplex_id, __pool);

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
