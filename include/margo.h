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

#include <mercury.h>
#include <mercury_types.h>
#include <mercury_bulk.h>
#include <mercury_macros.h>
#include <abt.h>
#include <margo-diag.h>
#include <margo-logging.h>

#define DEPRECATED(msg) __attribute__((deprecated(msg)))

/* determine how much of the Mercury ID space to use for Margo provider IDs */
#define __MARGO_PROVIDER_ID_SIZE (sizeof(hg_id_t) / 4)
#define __MARGO_RPC_HASH_SIZE    (__MARGO_PROVIDER_ID_SIZE * 3)

/* This is to prevent the user from usin HG_Register_data
 * and HG_Registered_data, which are replaced with
 * margo_register_data and margo_registered_data
 * respecively.
 */
#undef MERCURY_REGISTER

struct margo_instance;
typedef struct margo_instance*       margo_instance_id;
typedef struct margo_data*           margo_data_ptr;
typedef struct margo_request_struct* margo_request;
typedef void (*margo_finalize_callback_t)(void*);

#define MARGO_INSTANCE_NULL       ((margo_instance_id)NULL)
#define MARGO_REQUEST_NULL        ((margo_request)NULL)
#define MARGO_CLIENT_MODE         0
#define MARGO_SERVER_MODE         1
#define MARGO_DEFAULT_PROVIDER_ID 0
#define MARGO_MAX_PROVIDER_ID     ((1 << (8 * __MARGO_PROVIDER_ID_SIZE)) - 1)

/**
 * The margo_init_info structure should be passed to margo_init_ext
 * to finely configure Margo. The structure can be memset to 0 to have
 * margo use default values (no progress thread, no rpc thread, default
 * initialization of mercury, etc.). For any field that is not NULL,
 * margo_init_ext will first look for a configuration in the json_config
 * string. If no configuration is found or of json_config is NULL, margo
 * will fall back to default.
 */
struct margo_init_info {
    const char*          json_config;   /* JSON-formatted string */
    ABT_pool             progress_pool; /* Progress pool */
    ABT_pool             rpc_pool;      /* RPC handler pool */
    hg_class_t*          hg_class;      /* Mercury class */
    hg_context_t*        hg_context;    /* Mercury context */
    struct hg_init_info* hg_init_info;  /* Mercury init info */
};

#define MARGO_INIT_INFO_INITIALIZER \
    {                               \
        0                           \
    }

/**
 * Example JSON configuration:
 * ------------------------------------------------
{
    "mercury" : {
        "address" : "na+sm://",
        "listening" : false,
        "auto_sm" : true,
        "version" : "2.0.0",
        "stats" : false,
        "na_no_block" : false,
        "na_no_retry" : false,
        "max_contexts" : 1,
        "ip_subnet" : "",
        "auth_key" : ""
    },
    "argobots" : {
        "abt_mem_max_num_stacks" : 8,
        "abt_thread_stacksize" : 2097152,
        "version" : "1.0.0",
        "pools" : [
            {
                "name" : "my_progress_pool",
                "kind" : "fifo_wait",
                "access" : "mpmc"
            },
            {
                "name" : "my_rpc_pool",
                "kind" : "fifo_wait",
                "access" : "mpmc"
            }
        ],
        "xstreams" : [
            {
                "name" : "my_progress_xstream",
                "cpubind" : 0,
                "affinity" : [ 0, 1 ],
                "scheduler" : {
                    "type" : "basic_wait",
                    "pools" : [ "my_progress_pool" ]
                }
            },
            {
                "name" : "my_rpc_xstream_0",
                "cpubind" : 2,
                "affinity" : [ 2, 3, 4, 5 ],
                "scheduler" : {
                    "type" : "basic_wait",
                    "pools" : [ "my_rpc_pool" ]
                }
            },
            {
                "name" : "my_rpc_xstream_1",
                "cpubind" : 6,
                "affinity" : [ 6, 7 ],
                "scheduler" : {
                    "type" : "basic_wait",
                    "pools" : [ "my_rpc_pool" ]
                }
            }
        ]
    },
    "handle_cache_size" : 32,
    "profile_sparkline_timeslice_msec" : 1000,
    "progress_timeout_ub_msec" : 100,
    "enable_profiling" : false,
    "enable_diagnostics" : false
}

* The margo json configuration also supports the following convenience
* parameters at input time (the resulting runtime json will contain a fully
* resolved pool configuration):

use_progress_thread: bool (default false)
rpc_thread_count: integer (default 0)

* Note that supported kinds of pools are fifo_wait (default) fifo (for use
* with basic scheduler; will busy spin when idle) and prio_wait (custom pool
* implementation for Margo that favors existing ULTs over newly created ULTs
* when possible)
*
 * ------------------------------------------------
 */

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
margo_instance_id margo_init(const char* addr_str,
                             int         mode,
                             int         use_progress_thread,
                             int         rpc_thread_count);

/**
 * Initializes a margo instance using a margo_init_info struct to provide
 * arguments.
 *
 * @param args Arguments
 * @param address Address or protocol
 * @param mode MARGO_CLIENT_MODE or MARGO_SERVER_MODE
 *
 * @return a margo_instance_id or MARGO_INSTANCE_NULL in case of failure.
 *
 * NOTE: if you are configuring Argobots pools yourself before
 * passing them into this function, please consider setting
 * ABT_MEM_MAX_NUM_STACKS to a low value (like 8) either in your
 * environment or programmatically with putenv() in your code before
 * creating the pools to prevent excess memory consumption under
 * load from producer/consumer patterns across execution streams that
 * fail to utilize per-execution stream stack caches.  See
 * https://xgitlab.cels.anl.gov/sds/margo/issues/40 for details.
 * The margo_init() function does this automatically.
 */
margo_instance_id margo_init_ext(const char*                   address,
                                 int                           mode,
                                 const struct margo_init_info* args);

/**
 * Configures the runtime environment dependencies for use by Margo without
 * initializing Margo.  The primary purpose of this function is to set
 * preferred environment variables for Argobots (e.g., ULT stack size) if
 * Argobots will be initialized before calling margo_init() or
 * margo_init_ext().
 *
 * @param [in] optional_json_config The json-formatted configuration
 *                                  parameters to be used by Margo when it
 *                                  is initialized later (if known).  If not
 *                                  specified, then margo_set_environment()
 *                                  will use default values.
 * @returns returns 0 on success, negative value on failure
 */
int margo_set_environment(const char* optional_json_config);

/**
 * Initializes margo library with custom Mercury options.
 * @param [in] addr_str            Mercury host address with port number
 * @param [in] mode                Mode to run Margo in:
 *                                     - MARGO_CLIENT_MODE
 *                                     - MARGO_SERVER_MODE
 * @param [in] hg_init_info        (Optional) Hg init info, passed directly
 *                                 to Mercury
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
margo_instance_id margo_init_opt(const char*                addr_str,
                                 int                        mode,
                                 const struct hg_init_info* hg_init_info,
                                 int                        use_progress_thread,
                                 int                        rpc_thread_count)
    DEPRECATED("use margo_init_ext instead");

/**
 * Initializes margo library from given argobots and Mercury instances.
 * @param [in] progress_pool Argobots pool to drive communication
 * @param [in] handler_pool Argobots pool to service RPC handlers
 * @param [in] hg_context Mercury context
 * @returns margo instance id on success, MARGO_INSTANCE_NULL upon error
 *
 * NOTE: if you are configuring Argobots pools yourself before
 * passing them into this function, please consider setting
 * ABT_MEM_MAX_NUM_STACKS to a low value (like 8) either in your
 * environment or programmatically with putenv() in your code before
 * creating the pools to prevent excess memory consumption under
 * load from producer/consumer patterns across execution streams that
 * fail to utilize per-execution stream stack caches.  See
 * https://xgitlab.cels.anl.gov/sds/margo/issues/40 for details.
 * The margo_init() function does this automatically.
 */
margo_instance_id margo_init_pool(ABT_pool      progress_pool,
                                  ABT_pool      handler_pool,
                                  hg_context_t* hg_context)
    DEPRECATED("use margo_init_ext instead");

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
 * Checks whether a Margo instance we initialized as a server.
 *
 * @param [in] mid Margo instance
 *
 * @return HG_TRUE if listening or HG_FALSE if not, or not a valid margo
 * instance.
 */
hg_bool_t margo_is_listening(margo_instance_id mid);

/**
 * Installs a callback to be called before the margo instance is finalize,
 * and before the Mercury progress loop is terminated.
 * Callbacks installed will be called in reverse ordered than they have been
 * pushed, and with the user-provider pointer as argument.
 *
 * Note that callbacks may not be called within margo_finalize. They are called
 * when the margo instance is cleaned up, which may happen in
 * margo_wait_for_finalize.
 *
 * Callbacks installed using this function may call RPCs or margo_thread_sleep,
 * but do not guarantee that the process isn't itself going to receive RPCs in
 * the mean time.
 *
 * @param mid The margo instance
 * @param cb Callback to install
 * @param uargs User-provider argument to pass to the callback when called
 */
void margo_push_prefinalize_callback(margo_instance_id         mid,
                                     margo_finalize_callback_t cb,
                                     void*                     uargs);

/**
 * Removes the last pre-finalize callback that was pushed into the margo
 * instance without calling it. If a callback was removed, this function returns
 * 1, otherwise it returns 0.
 *
 * @param mid Margo instance.
 */
int margo_pop_prefinalize_callback(margo_instance_id mid);

/**
 * @brief Get the last pre-finalize callback that was pushed into the margo
 * instance as well as its argument. If a callback is found, this function
 * returns 1, otherwise it returns 0.
 *
 * @param mid Margo instance.
 * @param cb Returned callback.
 * @param uargs Uargs.
 */
int margo_top_prefinalize_callback(margo_instance_id          mid,
                                   margo_finalize_callback_t* cb,
                                   void**                     uargs);

/**
 * @brief Installs a callback to be called before the margo instance is
 * finalized, and before the Mercury progress loop is terminated. The owner
 * pointer allows to identify callbacks installed by particular providers. Note
 * that one can install multiple callbacks with the same owner. If popped, they
 * will be popped in reverse order of installation. If they are not popped, they
 * will be called in reverse order of installation by the margo cleanup
 * procedure.
 *
 * Callbacks installed using this function may call RPCs or margo_thread_sleep,
 * but do not guarantee that the process isn't itself going to receive RPCs in
 * the mean time.
 *
 * @param mid The margo instance
 * @param owner Owner of the callback (to be used when popping callbacks)
 * @param cb Callback to install
 * @param uargs User-provider argument to pass to the callback when called
 */
void margo_provider_push_prefinalize_callback(margo_instance_id         mid,
                                              const void*               owner,
                                              margo_finalize_callback_t cb,
                                              void*                     uargs);

/**
 * Removes the last prefinalize callback that was pushed into the margo instance
 * by the specified owner. If a callback was removed, this function returns 1,
 * otherwise it returns 0.
 *
 * @param mid Margo instance.
 * @param owner Owner of the callback.
 */
int margo_provider_pop_prefinalize_callback(margo_instance_id mid,
                                            const void*       owner);

/**
 * @brief Get the last prefinalize callback that was pushed into the margo
 * instance by the specified owner. If a callback is found, this function
 * returns 1, otherwise it returns 0.
 *
 * @param mid Margo instance.
 * @param owner Owner of the callback.
 * @param cb Returned callback.
 * @param uargs Returned user arguments.
 */
int margo_provider_top_prefinalize_callback(margo_instance_id          mid,
                                            const void*                owner,
                                            margo_finalize_callback_t* cb,
                                            void**                     uargs);

/**
 * Installs a callback to be called before the margo instance is finalize.
 * Callbacks installed will be called in reverse ordered than they have been
 * pushed, and with the user-provider pointer as argument.
 *
 * Note that callbacks may not be called within margo_finalize. They are called
 * when the margo instance is cleaned up, which may happen in
 * margo_wait_for_finalize.
 *
 * Important: callbacks cannot make RPC calls nor use margo_thread_sleep. If you
 * need to be able to make RPC calls or use margo_thread_sleep, you should use
 * margo_push_prefinalize_callback instead.
 *
 * @param mid The margo instance
 * @param cb Callback to install
 * @param uargs User-provider argument to pass to the callback when called
 */
void margo_push_finalize_callback(margo_instance_id         mid,
                                  margo_finalize_callback_t cb,
                                  void*                     uargs);

/**
 * @brief Removes the last finalize callback that was pushed into the margo
 * instance without calling it. If a callback was removed, this function returns
 * 1, otherwise it returns 0.
 *
 * @param mid Margo instance.
 */
int margo_pop_finalize_callback(margo_instance_id mid);

/**
 * @brief Returns the last finalize callback that was pushed into the margo
 * instance, along with its argument. If successful, this function returns 1.
 * Otherwise it returns 0.
 *
 * @param mid Margo instance
 * @param cb Returned callback
 * @param uargs Returned user arguments
 *
 * @return 1 if successful, 0 otherwise.
 */
int margo_top_finalize_callback(margo_instance_id          mid,
                                margo_finalize_callback_t* cb,
                                void**                     uargs);

/**
 * @brief Installs a callback to be called before the margo instance is
 * finalized. The owner pointer allows to identify callbacks installed by
 * particular providers. Note that one can install multiple callbacks with the
 * same owner. If popped, they will be popped in reverse order of installation.
 * If they are not popped, they will be called in reverse order of installation
 * by the margo cleanup procedure.
 *
 * Important: callbacks cannot make RPC calls nor use margo_thread_sleep. If you
 * need to be able to make RPC calls or use margo_thread_sleep, you should use
 * margo_provider_push_prefinalize_callback instead.
 *
 * @param mid The margo instance
 * @param owner Owner of the callback (to be used when popping callbacks)
 * @param cb Callback to install
 * @param uargs User-provider argument to pass to the callback when called
 */
void margo_provider_push_finalize_callback(margo_instance_id         mid,
                                           const void*               owner,
                                           margo_finalize_callback_t cb,
                                           void*                     uargs);

/**
 * @brief Removes the last finalize callback that was pushed into the margo
 * instance by the specified owner. If a callback was removed, this function
 * returns 1, otherwise it returns 0.
 *
 * @param mid Margo instance.
 * @param owner Owner of the callback.
 */
int margo_provider_pop_finalize_callback(margo_instance_id mid,
                                         const void*       owner);

/**
 * @brief Gets the last finalize callback that was pushed into the margo
 * instance by the specified owner. If successful, this function returns 1,
 * otherwise it returns 0.
 *
 * @param mid Margo instance
 * @param owner Owner of the callback.
 * @param cb Returned callback.
 * @param uargs Returned user agument.
 *
 * @return 1 if successful, 0 otherwise.
 */
int margo_provider_top_finalize_callback(margo_instance_id          mid,
                                         const void*                owner,
                                         margo_finalize_callback_t* cb,
                                         void**                     uargs);

/**
 * Allows the passed Margo instance to be shut down remotely
 * using margo_shutdown_remote_instance().
 *
 * @param mid Margo instance
 */
void margo_enable_remote_shutdown(margo_instance_id mid);

/**
 * Trigger the shutdown of the Margo instance running
 * at remote_addr.
 *
 * @param mid Local Margo instance
 * @param remote_addr Address of the Margo instance to shut down.
 *
 * @return 0 on success, -1 on failure.
 */
int margo_shutdown_remote_instance(margo_instance_id mid,
                                   hg_addr_t         remote_addr);

/**
 * Registers an RPC with margo that is associated with a provider instance
 *
 * \param [in] mid Margo instance
 * \param [in] func_name unique function name for RPC
 * \param [in] in_proc_cb pointer to input proc callback
 * \param [in] out_proc_cb pointer to output proc callback
 * \param [in] rpc_cb RPC callback
 * \param [in] provider_id provider identifier
 * \param [in] pool Argobots pool the handler will execute in
 *
 * \return unique ID associated to the registered function
 */
hg_id_t margo_provider_register_name(margo_instance_id mid,
                                     const char*       func_name,
                                     hg_proc_cb_t      in_proc_cb,
                                     hg_proc_cb_t      out_proc_cb,
                                     hg_rpc_cb_t       rpc_cb,
                                     uint16_t          provider_id,
                                     ABT_pool          pool);

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
static inline hg_id_t margo_register_name(margo_instance_id mid,
                                          const char*       func_name,
                                          hg_proc_cb_t      in_proc_cb,
                                          hg_proc_cb_t      out_proc_cb,
                                          hg_rpc_cb_t       rpc_cb)
{
    return margo_provider_register_name(mid, func_name, in_proc_cb, out_proc_cb,
                                        rpc_cb, 0, ABT_POOL_NULL);
}

/**
 * Deregisters an RPC with margo
 *
 * \param [in] mid Margo instance
 * \param [in] rpc_id Id of the RPC to deregister
 *
 * \return HG_SUCCESS or corresponding error code
 */
hg_return_t margo_deregister(margo_instance_id mid, hg_id_t rpc_id);

/*
 * Indicate whether margo_register_name() has been called for the RPC specified
 * by func_name.
 *
 * \param [in] mid Margo instance
 * \param [in] func_name function name
 * \param [out] id registered RPC ID
 * \param [out] flag pointer to boolean
 *
 * \return HG_SUCCESS or corresponding HG error code
 */
hg_return_t margo_registered_name(margo_instance_id mid,
                                  const char*       func_name,
                                  hg_id_t*          id,
                                  hg_bool_t*        flag);

/**
 * Indicate whether the given RPC name has been registered with the given
 * provider id.
 *
 * @param [in] mid Margo instance
 * @param [in] func_name function name
 * @param [in] provider_id provider id
 * @param [out] id registered RPC ID
 * @param [out] flag pointer to boolean
 *
 * @return HG_SUCCESS or corresponding HG error code
 */
hg_return_t margo_provider_registered_name(margo_instance_id mid,
                                           const char*       func_name,
                                           uint16_t          provider_id,
                                           hg_id_t*          id,
                                           hg_bool_t*        flag);

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
hg_return_t margo_register_data(margo_instance_id mid,
                                hg_id_t           id,
                                void*             data,
                                void (*free_callback)(void*));

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
 * Disable response for a given RPC ID.
 *
 * \param [in] mid          Margo instance
 * \param [in] id           registered function ID
 * \param [in] disable_flag flag to disable (1) or re-enable (0) responses
 *
 * \return HG_SUCCESS or corresponding HG error code
 */
hg_return_t margo_registered_disable_response(margo_instance_id mid,
                                              hg_id_t           id,
                                              int               disable_flag);

/**
 * Checks if response is disabled for a given RPC ID.
 *
 * @param [in] mid           Margo instance
 * @param [in] id            registered function ID
 * @param [ou] disabled_flag flag indicating whether response is disabled (1) or
 * not (0)
 *
 * @return HG_SUCCESS or corresponding HG error code
 */
hg_return_t margo_registered_disabled_response(margo_instance_id mid,
                                               hg_id_t           id,
                                               int*              disabled_flag);

/**
 * Lookup an addr from a peer address/name.
 * \param [in] name     lookup name
 * \param [out] addr    return address
 *
 * \return HG_SUCCESS or corresponding HG error code
 */
hg_return_t
margo_addr_lookup(margo_instance_id mid, const char* name, hg_addr_t* addr);

/**
 * Free the given Mercury addr.
 *
 * \param [in] mid  Margo instance
 * \param [in] addr Mercury address
 *
 * \return HG_SUCCESS or corresponding HG error code
 */
hg_return_t margo_addr_free(margo_instance_id mid, hg_addr_t addr);

/**
 * Access self address. Address must be freed with margo_addr_free().
 *
 * \param [in] mid  Margo instance
 * \param [in] addr pointer to abstract Mercury address
 *
 * \return HG_SUCCESS or corresponding HG error code
 */
hg_return_t margo_addr_self(margo_instance_id mid, hg_addr_t* addr);

/**
 * @brief Compare two addresses.
 *
 * @param mid   Margo instance
 * @param addr1 first address
 * @param addr2 second address
 *
 * @return HG_TRUE if addresses are determined to be equal, HG_FALSE otherwise
 */
hg_bool_t
margo_addr_cmp(margo_instance_id mid, hg_addr_t addr1, hg_addr_t addr2);

/**
 * @brief Hint that the address is no longer valid. This may happen if
 * the peer is no longer responding. This can be used to force removal of
 * the peer address from the list of the peers, before freeing it and
 * reclaim resources.
 *
 * @param mid  Margo instance
 * @param addr address
 *
 * @return HG_SUCCESS or corresponding HG error code
 */
hg_return_t margo_addr_set_remove(margo_instance_id mid, hg_addr_t addr);

/**
 * Duplicate an existing Mercury address.
 *
 * \param [in] mid      Margo instance
 * \param [in] addr     abstract Mercury address to duplicate
 * \param [in] new_addr pointer to newly allocated abstract Mercury address
 *
 * \return HG_SUCCESS or corresponding HG error code
 */
hg_return_t
margo_addr_dup(margo_instance_id mid, hg_addr_t addr, hg_addr_t* new_addr);

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
hg_return_t margo_addr_to_string(margo_instance_id mid,
                                 char*             buf,
                                 hg_size_t*        buf_size,
                                 hg_addr_t         addr);

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
hg_return_t margo_create(margo_instance_id mid,
                         hg_addr_t         addr,
                         hg_id_t           id,
                         hg_handle_t*      handle);

/**
 * Destroy Mercury handle.
 *
 * \param [in] handle   Mercury handle
 *
 * \return HG_SUCCESS or corresponding HG error code
 */
hg_return_t margo_destroy(hg_handle_t handle);

/**
 * Increment ref count on a Mercury handle.
 *
 * \param [in] handle Mercury handle
 *
 * \return HG_SUCCESS or corresponding HG error code
 */
#define margo_ref_incr HG_Ref_incr

/**
 * Get info from handle.
 *
 * \param [in] handle Mercury handle
 *
 * \return Pointer to info or NULL in case of failure
 */
#define margo_get_info HG_Get_info

/**
 * Get input from handle (requires registration of input proc to deserialize
 * parameters). Input must be freed using margo_free_input().
 *
 * \param [in] handle           Mercury handle
 * \param [in/out] in_struct    pointer to input structure
 *
 * \return HG_SUCCESS or corresponding HG error code
 */
#define margo_get_input HG_Get_input

/**
 * Free resources allocated when deserializing the input.
 *
 * \param [in] handle           Mercury handle
 * \param [in/out] in_struct    pointer to input structure
 *
 * \return HG_SUCCESS or corresponding HG error code
 */
#define margo_free_input HG_Free_input

/**
 * Get output from handle (requires registration of output proc to deserialize
 * parameters). Output must be freed using margo_free_output().
 *
 * \param [in] handle           Mercury handle
 * \param [in/out] out_struct   pointer to output structure
 *
 * \return HG_SUCCESS or corresponding HG error code
 */
#define margo_get_output HG_Get_output

/**
 * Free resources allocated when deserializing the output.
 *
 * \param [in] handle           Mercury handle
 * \param [in/out] out_struct   pointer to output structure
 *
 * \return HG_SUCCESS or corresponding HG error code
 */
#define margo_free_output HG_Free_output

/**
 * Forward an RPC request to a remote host
 * @param [in] provider ID (may be MARGO_DEFAULT_PROVIDER_ID)
 * @param [in] handle identifier for the RPC to be sent
 * @param [in] in_struct input argument struct for RPC
 * @returns 0 on success, hg_return_t values on error
 */
hg_return_t margo_provider_forward(uint16_t    provider_id,
                                   hg_handle_t handle,
                                   void*       in_struct);

#define margo_forward(__handle, __in_struct) \
    margo_provider_forward(MARGO_DEFAULT_PROVIDER_ID, __handle, __in_struct)

/**
 * Forward (without blocking) an RPC request to a remote host
 * @param [in] provider ID (may be MARGO_DEFAULT_PROVIDER_ID)
 * @param [in] handle identifier for the RPC to be sent
 * @param [in] in_struct input argument struct for RPC
 * @param [out] req request to wait on using margo_wait
 * @returns 0 on success, hg_return_t values on error
 */
hg_return_t margo_provider_iforward(uint16_t       provider_id,
                                    hg_handle_t    handle,
                                    void*          in_struct,
                                    margo_request* req);

#define margo_iforward(__handle, __in_struct, __req)                          \
    margo_provider_iforward(MARGO_DEFAULT_PROVIDER_ID, __handle, __in_struct, \
                            __req)

/**
 * Forward an RPC request to a remote provider with a user-defined timeout
 * @param [in] provider_id provider id
 * @param [in] handle identifier for the RPC to be sent
 * @param [in] in_struct input argument struct for RPC
 * @param [in] timeout_ms timeout in milliseconds
 * @returns 0 on success, hg_return_t values on error
 */
hg_return_t margo_provider_forward_timed(uint16_t    provider_id,
                                         hg_handle_t handle,
                                         void*       in_struct,
                                         double      timeout_ms);

#define margo_forward_timed(__handle, __in_struct, __timeout)         \
    margo_provider_forward_timed(MARGO_DEFAULT_PROVIDER_ID, __handle, \
                                 __in_struct, __timeout)

/**
 * Non-blocking version of margo_provider_forward_timed.
 * @param [in] provider_id provider id
 * @param [in] handle identifier for the RPC to be sent
 * @param [in] in_struct input argument struct for RPC
 * @param [in] timeout_ms timeout in milliseconds
 * @returns 0 on success, hg_return_t values on error
 */
hg_return_t margo_provider_iforward_timed(uint16_t       provider_id,
                                          hg_handle_t    handle,
                                          void*          in_struct,
                                          double         timeout_ms,
                                          margo_request* req);

#define margo_iforward_timed(__handle, __in_struct, __timeout, __req)  \
    margo_provider_iforward_timed(MARGO_DEFAULT_PROVIDER_ID, __handle, \
                                  __in_struct, __timeout, __req)

/**
 * Wait for an operation initiated by a non-blocking
 * margo function (margo_iforward, margo_irespond, etc.)
 * @param [in] req request to wait on
 * @returns 0 on success, hg_return_t values on error
 */
hg_return_t margo_wait(margo_request req);

/**
 * @brief Waits for any of the provided requests to complete.
 * Note that even if an error occures, index will be set to
 * the index of the request for which the error happened.
 * This function will correctly ignore requests that are
 * equal to MARGO_REQUEST_NULL. If all the requests are
 * equal to MARGO_REQUEST_NULL, this function will return
 * HG_SUCCESS and set index to count.
 *
 * @param req Array of requests
 * @param count Number of requests
 * @param index index of the request that completed
 *
 * @return 0 on success, hg_return_t values on error
 */
hg_return_t margo_wait_any(size_t count, margo_request* req, size_t* index);

/**
 * Test if an operation initiated by a non-blocking
 * margo function (margo_iforward, margo_irespond, etc.)
 * has completed.
 *
 * @param [in] req request created by the non-blocking call
 * @param [out] flag 1 if request is completed, 0 otherwise
 *
 * @return 0 on success, ABT error code otherwise
 */
int margo_test(margo_request req, int* flag);

/**
 * Send an RPC response, waiting for completion before returning
 * control to the calling ULT.
 * Note: this call is typically not needed as RPC listeners need not concern
 * themselves with what happens after an RPC finishes. However, there are cases
 * when this is useful (deferring resource cleanup, calling margo_finalize()
 * for e.g. a shutdown RPC).
 * @param [in] handle identifier for the RPC for which a response is being
 * sent
 * @param [in] out_struct output argument struct for response
 * @return HG_SUCCESS on success, hg_return_t values on error. See HG_Respond.
 */
hg_return_t margo_respond(hg_handle_t handle, void* out_struct);

/**
 * Send an RPC response without blocking.
 * @param [in] handle identifier for the RPC for which a response is being
 * sent
 * @param [in] out_struct output argument struct for response
 * @param [out] req request on which to wait using margo_wait
 * @return HG_SUCCESS on success, hg_return_t values on error. See HG_Respond.
 */
hg_return_t
margo_irespond(hg_handle_t handle, void* out_struct, margo_request* req);

/**
 * Create an abstract bulk handle from specified memory segments.
 * Memory allocated is then freed when margo_bulk_free() is called.
 * \remark If NULL is passed to buf_ptrs, i.e.,
 * \verbatim margo_bulk_create(mid, count, NULL, buf_sizes, flags, &handle)
 * \endverbatim memory for the missing buf_ptrs array will be internally
 * allocated.
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
hg_return_t margo_bulk_create(margo_instance_id mid,
                              hg_uint32_t       count,
                              void**            buf_ptrs,
                              const hg_size_t*  buf_sizes,
                              hg_uint8_t        flags,
                              hg_bulk_t*        handle);

/**
 * Free bulk handle.
 *
 * \param [in/out] handle   abstract bulk handle
 *
 * \return HG_SUCCESS or corresponding HG error code
 */
hg_return_t margo_bulk_free(hg_bulk_t handle);

/**
 * Increment ref count on bulk handle.
 *
 * \param handle [IN]           abstract bulk handle
 *
 * \return HG_SUCCESS or corresponding HG error code
 */
#define margo_bulk_ref_incr HG_Bulk_ref_incr

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
#define margo_bulk_access HG_Bulk_access

/**
 * Get total size of data abstracted by bulk handle.
 *
 * \param [in] handle   abstract bulk handle
 *
 * \return Non-negative value
 */
#define margo_bulk_get_size HG_Bulk_get_size

/**
 * Get total number of segments abstracted by bulk handle.
 *
 * \param [in] handle   abstract bulk handle
 *
 * \return Non-negative value
 */
#define margo_bulk_get_segment_count HG_Bulk_get_segment_count

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
#define margo_bulk_get_serialize_size HG_Bulk_get_serialize_size

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
#define margo_bulk_serialize HG_Bulk_serialize

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
hg_return_t margo_bulk_deserialize(margo_instance_id mid,
                                   hg_bulk_t*        handle,
                                   const void*       buf,
                                   hg_size_t         buf_size);

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
hg_return_t margo_bulk_transfer(margo_instance_id mid,
                                hg_bulk_op_t      op,
                                hg_addr_t         origin_addr,
                                hg_bulk_t         origin_handle,
                                size_t            origin_offset,
                                hg_bulk_t         local_handle,
                                size_t            local_offset,
                                size_t            size);

/**
 * Asynchronously performs a bulk transfer
 * @param [in] mid Margo instance
 * @param [in] op type of operation to perform
 * @param [in] origin_addr remote Mercury address
 * @param [in] origin_handle remote Mercury bulk memory handle
 * @param [in] origin_offset offset into remote bulk memory to access
 * @param [in] local_handle local bulk memory handle
 * @param [in] local_offset offset into local bulk memory to access
 * @param [in] size size (in bytes) of transfer
 * @param [out] req request to wait on using margo_wait
 * @returns 0 on success, hg_return_t values on error
 */
hg_return_t margo_bulk_itransfer(margo_instance_id mid,
                                 hg_bulk_op_t      op,
                                 hg_addr_t         origin_addr,
                                 hg_bulk_t         origin_handle,
                                 size_t            origin_offset,
                                 hg_bulk_t         local_handle,
                                 size_t            local_offset,
                                 size_t            size,
                                 margo_request*    req);

/**
 * Suspends the calling ULT for a specified time duration
 * @param [in] mid Margo instance
 * @param [in] timeout_ms timeout duration in milliseconds
 */
void margo_thread_sleep(margo_instance_id mid, double timeout_ms);

/**
 * Retrieve the abt_handler pool that was associated with the instance at
 *    initialization time
 * @param [in] mid Margo instance
 * @param [out] pool handler pool
 * @return 0 on success, error code on failure
 */
int margo_get_handler_pool(margo_instance_id mid, ABT_pool* pool);

/**
 * Retrieve the rpc handler abt pool that is associated with this handle
 * @param [in] h handle
 * @return pool
 */
ABT_pool margo_hg_handle_get_handler_pool(hg_handle_t h);

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
 * Get the margo_instance_id from an hg_info struct
 *
 * \param [in] info       hg_info struct
 *
 * \return Margo instance
 */
margo_instance_id margo_hg_info_get_instance(const struct hg_info* info);

/**
 * Enables diagnostic collection on specified Margo instance
 *
 * @param [in] mid Margo instance
 * @returns void
 */
#define margo_diag_start(__mid) \
    margo_set_param(__mid, "enable_diagnostics", "1")

/**
 * Enables profile data collection on specified Margo instance
 *
 * @param [in] mid Margo instance
 * @returns void
 */
#define margo_profile_start(__mid) \
    margo_set_param(__mid, "enable_profiling", "1")

/**
 * Disables diagnostic collection on specified Margo instance
 *
 * @param [in] mid Margo instance
 * @returns void
 */
#define margo_diag_stop(__mid) margo_set_param(__mid, "enable_diagnostics", "0")

/**
 * Disables profile data collection on specified Margo instance
 *
 * @param [in] mid Margo instance
 * @returns void
 */
#define margo_profile_stop(__mid) \
    margo_set_param(__mid, "enable_profiling", "0")

/**
 * Appends diagnostic statistics (enabled via margo_diag_start()) to specified
 * output file.
 *
 * @param [in] mid Margo instance
 * @param [in] file output file ("-" for stdout).  If string begins with '/'
 *   character then it will be treated as an absolute path.  Otherwise the
 *   file will be placed in the directory specified output_dir or
 *   MARGO_OUTPUT_DIR.
 * @param [in] uniquify flag indicating if file name should have additional
 *   information added to it to make output from different processes unique
 * @returns void
 */
void margo_diag_dump(margo_instance_id mid, const char* file, int uniquify);

/**
 * Appends Margo state information (including Argobots stack information) to
 * the specified file in text format.
 *
 * @param [in] mid Margo instance
 * @param [in] file output file ("-" for stdout)
 * @param [in] uniquify flag indicating if file name should have additional
 *   information added to it to make output from different processes unique
 * @param [out] resolved_file_name (ignored if NULL) pointer to char* that
 *   will be set to point to a string with the fully resolved path to the
 *   state file that was generated.  Must be freed by caller.
 * @returns void
 */
void margo_state_dump(margo_instance_id mid,
                      const char*       file,
                      int               uniquify,
                      char**            resolved_file_name);

/**
 * Appends profile statistics (enabled via margo_profile_start()) to specified
 * output file.
 *
 * @param [in] mid Margo instance
 * @param [in] file output file ("-" for stdout).  If string begins with '/'
 *   character then it will be treated as an absolute path.  Otherwise the
 *   file will be placed in the directory specified output_dir or
 *   MARGO_OUTPUT_DIR.
 * @param [in] uniquify flag indicating if file name should have additional
 *   information added to it to make output from different processes unique
 * @returns void
 */
void margo_profile_dump(margo_instance_id mid, const char* file, int uniquify);

/**
 * Grabs a snapshot of the current state of diagnostics within the margo
 * instance
 *
 * @param [in] mid Margo instance
 * @param [out] snap Margo diagnostics snapshot
 * @returns void
 */
void margo_breadcrumb_snapshot(margo_instance_id                 mid,
                               struct margo_breadcrumb_snapshot* snap);

/**
 * Releases resources associated with a breadcrumb snapshot
 *
 * @param [in] mid Margo instance
 * @param [in] snap Margo diagnostics snapshot
 * @returns void
 */
void margo_breadcrumb_snapshot_destroy(margo_instance_id                 mid,
                                       struct margo_breadcrumb_snapshot* snap);

/**
 * Sets configurable parameters/hints
 *
 * @param [in] mid Margo instance
 * @param [in] key parameter name
 * @param [in] value parameter value
 * @returns 0 on success, -1 on failure
 */
int margo_set_param(margo_instance_id mid, const char* key, const char* value);

/**
 * Retrieves complete configuration of margo instance, incoded as json
 *
 * @param [in] mid Margo instance
 * @returns null terminated string that must be free'd by caller
 */
char* margo_get_config(margo_instance_id mid);

/**
 * @brief Get a pool from the configuration.
 *
 * @param [in] mid Margo instance
 * @param [in] name Name of the pool
 * @param [out] pool Pool
 *
 * @return 0 on success, -1 on failure
 */
int margo_get_pool_by_name(margo_instance_id mid,
                           const char*       name,
                           ABT_pool*         pool);

/**
 * @brief Get a pool from the configuration.
 *
 * @param [in] mid Margo instance
 * @param [in] index Index of the pool
 * @param [out] pool Pool
 *
 * @return 0 on success, -1 on failure
 */
int margo_get_pool_by_index(margo_instance_id mid,
                            unsigned          index,
                            ABT_pool*         pool);

/**
 * @brief Get the name of a pool at a given index.
 *
 * @param mid Margo instance
 * @param index Index of the pool
 *
 * @return the null-terminated name, or NULL if index is invalid
 */
const char* margo_get_pool_name(margo_instance_id mid, unsigned index);

/**
 * @brief Get the index of a pool from a given name.
 *
 * @param mid Margo instance
 * @param name Name of the pool
 *
 * @return the index of the pool, or -1 if the name is invalid
 */
int margo_get_pool_index(margo_instance_id mid, const char* name);

/**
 * @brief Get the number of pools defined in the margo instance.
 *
 * @param mid Margo instance
 *
 * @return the number of pools
 */
size_t margo_get_num_pools(margo_instance_id mid);

/**
 * @brief Get an xstream from the configuration.
 *
 * @param [in] mid Margo instance
 * @param [in] name Name of the ES
 * @param [out] es ES
 *
 * @return 0 on success, -1 on failure
 */
int margo_get_xstream_by_name(margo_instance_id mid,
                              const char*       name,
                              ABT_xstream*      es);

/**
 * @brief Get an xstream from the configuration.
 *
 * @param [in] mid Margo instance
 * @param [in] name Index of the ES
 * @param [out] es ES
 *
 * @return 0 on success, -1 on failure
 */
int margo_get_xstream_by_index(margo_instance_id mid,
                               unsigned          index,
                               ABT_xstream*      es);

/**
 * @brief Get the name of a xstream at a given index.
 *
 * @param mid Margo instance
 * @param index Index of the xstream
 *
 * @return the null-terminated name, or NULL if index is invalid
 */
const char* margo_get_xstream_name(margo_instance_id mid, unsigned index);

/**
 * @brief Get the index of an xstream from a given name.
 *
 * @param mid Margo instance
 * @param name Name of the xstream
 *
 * @return the index of the xstream, or -1 if the name is invalid
 */
int margo_get_xstream_index(margo_instance_id mid, const char* name);

/**
 * @brief Get the number of xstreams defined in the margo instance.
 *
 * @param mid Margo instance
 *
 * @return the number of xstreams
 */
size_t margo_get_num_xstreams(margo_instance_id mid);

/**
 * @private
 * Internal function used by MARGO_REGISTER, not
 * supposed to be called by users!
 *
 * @param mid Margo instance
 *
 * @return whether margo_finalize() was called.
 */
int __margo_internal_finalize_requested(margo_instance_id mid);

/**
 * @private
 * Internal function used by MARGO_REGISTER, not
 * supposed to be called by users!
 *
 * @param mid Margo instance
 */
void __margo_internal_incr_pending(margo_instance_id mid);

/**
 * @private
 * Internal function used by MARGO_REGISTER, not
 * supposed to be called by users!
 *
 * @param mid Margo instance
 */
void __margo_internal_decr_pending(margo_instance_id mid);

/**
 * @private
 * Internal function used by DEFINE_MARGO_RPC_HANDLER, not supposed to be
 * called by users!
 */
void __margo_internal_pre_wrapper_hooks(margo_instance_id mid,
                                        hg_handle_t       handle);

/**
 * @private
 * Internal function used by DEFINE_MARGO_RPC_HANDLER, not supposed to be
 * called by users!
 */
void __margo_internal_post_wrapper_hooks(margo_instance_id mid);

/**
 * macro that registers a function as an RPC.
 */
#define MARGO_REGISTER(__mid, __func_name, __in_t, __out_t, __handler) \
    margo_provider_register_name(                                      \
        __mid, __func_name, BOOST_PP_CAT(hg_proc_, __in_t),            \
        BOOST_PP_CAT(hg_proc_, __out_t), _handler_for_##__handler,     \
        MARGO_DEFAULT_PROVIDER_ID, ABT_POOL_NULL);

#define MARGO_REGISTER_PROVIDER(__mid, __func_name, __in_t, __out_t, \
                                __handler, __provider_id, __pool)    \
    margo_provider_register_name(                                    \
        __mid, __func_name, BOOST_PP_CAT(hg_proc_, __in_t),          \
        BOOST_PP_CAT(hg_proc_, __out_t), _handler_for_##__handler,   \
        __provider_id, __pool);

#define _handler_for_NULL NULL

#define __MARGO_INTERNAL_RPC_WRAPPER_BODY(__name)                 \
    margo_instance_id __mid;                                      \
    __mid = margo_hg_handle_get_instance(handle);                 \
    __margo_internal_pre_wrapper_hooks(__mid, handle);            \
    margo_trace(__mid, "Starting RPC " #__name " (handle = %p)",  \
                (void*)handle);                                   \
    __name(handle);                                               \
    margo_trace(__mid, "RPC " #__name " completed (handle = %p)", \
                (void*)handle);                                   \
    __margo_internal_post_wrapper_hooks(__mid);

#define __MARGO_INTERNAL_RPC_WRAPPER(__name)       \
    void _wrapper_for_##__name(hg_handle_t handle) \
    {                                              \
        __MARGO_INTERNAL_RPC_WRAPPER_BODY(__name)  \
    }

#define __MARGO_INTERNAL_RPC_HANDLER_BODY(__name)                              \
    int               __ret;                                                   \
    ABT_pool          __pool;                                                  \
    margo_instance_id __mid;                                                   \
    __mid = margo_hg_handle_get_instance(handle);                              \
    if (__mid == MARGO_INSTANCE_NULL) {                                        \
        margo_error(                                                           \
            __mid, "Could not get margo instance when entering RPC " #__name); \
        margo_destroy(handle);                                                 \
        return (HG_OTHER_ERROR);                                               \
    }                                                                          \
    if (__margo_internal_finalize_requested(__mid)) {                          \
        margo_warning(__mid,                                                   \
                      "Ignoring " #__name " RPC because margo is finalizing"); \
        margo_destroy(handle);                                                 \
        return (HG_CANCELED);                                                  \
    }                                                                          \
    __pool = margo_hg_handle_get_handler_pool(handle);                         \
    __margo_internal_incr_pending(__mid);                                      \
    margo_trace(__mid,                                                         \
                "Spawning ULT for " #__name                                    \
                " RPC "                                                        \
                "(handle = %p)",                                               \
                (void*)handle);                                                \
    __ret = ABT_thread_create(__pool, (void (*)(void*))_wrapper_for_##__name,  \
                              handle, ABT_THREAD_ATTR_NULL, NULL);             \
    if (__ret != 0) {                                                          \
        margo_error(__mid,                                                     \
                    "Could not create ULT for " #__name " RPC (ret = %d)",     \
                    __ret);                                                    \
        margo_destroy(handle);                                                 \
        __margo_internal_decr_pending(__mid);                                  \
        return (HG_NOMEM_ERROR);                                               \
    }                                                                          \
    return (HG_SUCCESS);

#define __MARGO_INTERNAL_RPC_HANDLER(__name)              \
    hg_return_t _handler_for_##__name(hg_handle_t handle) \
    {                                                     \
        __MARGO_INTERNAL_RPC_HANDLER_BODY(__name)         \
    }

/**
 * macro that defines a function to glue an RPC handler to a ult handler
 * @param [in] __name name of handler function
 */
#define DEFINE_MARGO_RPC_HANDLER(__name) \
    __MARGO_INTERNAL_RPC_WRAPPER(__name) \
    __MARGO_INTERNAL_RPC_HANDLER(__name)

/**
 * macro that declares the prototype for a function to glue an RPC
 * handler to a ult
 * @param [in] __name name of handler function
 */
#define DECLARE_MARGO_RPC_HANDLER(__name) \
    hg_return_t _handler_for_##__name(hg_handle_t handle);

#ifdef __cplusplus
}
#endif

#endif /* __MARGO */
