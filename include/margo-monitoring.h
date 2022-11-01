/**
 * @file margo-monitoring.h
 *
 * (C) 2022 The University of Chicago
 *
 * See COPYRIGHT in top-level directory.
 */
#ifndef __MARGO_MONITORING_H
#define __MARGO_MONITORING_H

#include <abt.h>
#include <mercury.h>
#include <mercury_types.h>
#include <mercury_bulk.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Margo's monitoring subsystem consists of a margo_monitor structure
 * with pointers to functions that will be called when executing some
 * Margo functions. These functions all have the following prototype:
 *
 * ```
 * void (*)(void* state, double ts, margo_monitor_event_t type,
 * margo_monitor_X_args_t args)
 * ```
 *
 * For instance the on_forward function will be called at the beginning
 * of a forward call, and at the end, first with MARGO_MONITOR_FN_START
 * as event, type, then with MARGO_MONITOR_FN_END.
 *
 * The margo_monitor_X_args_t (replace X with the function name) argument
 * contains the argument(s) of the function, and its return value(s). The
 * return values (marked as output in the structures hereafter) are only
 * valid when the event type is MARGO_MONITOR_FN_END. Each of these
 * structures also conain a margo_monitor_data_t field that can be used
 * by the monitor to attach data to the margo_monitor_X_args_t argument
 * in between a call with MARGO_MONITOR_FN_START and the corresponding
 * call with MARGO_MONITOR_FN_END. This structure is a union with an int64_t
 * field, a double field, and a void* field. The latter can be used to
 * attach a pointer to data that wouldn't fit in 8 bytes, if necessary.
 * An example of application would be to attach the starting timestamp
 * as a double when the function is called with MARGO_MONITOR_FN_START,
 * and retrieve it to compute a call duration when the function is called
 * with MARGO_MONITOR_FN_END.
 *
 * This header also provides an enum of values of MARGO_MONITOR_ON_X
 * constants. These constants can be useful for indexing an array, for
 * example:
 *
 * ```
 * double call_times[MARGO_MONITOR_MAX];
 * ...
 * call_times[MARGO_MONITOR_ON_FORWARD] += duration;
 * ```
 *
 * Important: blocking functions (margo_forward, margo_provider_forward,
 * margo_respond, margo_bulk_transfer, etc.) are internally implemented
 * using their non-blocking counterpart. Hence, measuring the time between
 * on_forward calls with MARGO_MONITOR_FN_START and MARGO_MONITOR_FN_END
 * will not give the duration of the corresponding margo_forward call.
 * A margo_forward call will lead to the following sequence of monitoring
 * function calls:
 * - on_forward(MARGO_MONITOR_FN_START)
 * - on_set_input(MARGO_MONITOR_FN_START)
 * - on_set_input(MARGO_MONITOR_FN_END)
 * - on_forward(MARGO_MONITOR_FN_END)
 * - on_wait(MARGO_MONITOR_FN_START)
 * - on_forward_cb(MARGO_MONITOR_FN_START)
 * - on_forward_cb(MARGO_MONITOR_FN_END)
 * - on_wait(MARGO_MONITOR_FN_END)
 * A margo_request field in the argument structures of all these
 * function will be common to all of them for a given magro_forward call.
 * Attaching monitoring data to a margo_request is possible via the
 * margo_set_monitoring_data and margo_get_monitoring_data functions.
 *
 * Similarly, a call to margo_respond will trigger a similar sequence
 * of monitoring calls (with on_respond, on_set_output, and on_respond_cb
 * instead of on_forward, on_set_input and on_forward_cb respectively),
 * and a call to margo_bulk_transfer will trigger a sequence of
 * on_bulk_transfer, on_wait, on_bulk_transfer_cb, on_wait.
 *
 * User-defined events: the margo_monitor_call_user function may be
 * used to trigger the on_user callback. Because custom monitor
 * implementations cannot make any assumption on the format of the data
 * being passed to this function by the user, such data has to be
 * a null-terminated string (for instance a name). We recommend
 * that margo_monitor_call_user be used sparingly, e.g. only when
 * debugging or profiling a program, to have extra information appear
 * in the resulting trace file.
 */

struct hg_bulk_attr; /* forward declaration needed for Mercury < 2.2.0 */
struct margo_instance;
typedef struct margo_instance*       margo_instance_id;
typedef struct margo_request_struct* margo_request;
struct json_object; /* forward declaration to avoid including json-c */

/**
 * The margo_default_monitor constant can be used in the margo_init_info
 * structure passed to margo_init_ext to install the default monitoring
 * system in the margo instance. If the monitor field is left NULL, no
 * monitoring will be performed.
 */
extern struct margo_monitor* margo_default_monitor;

/**
 * The margo_monitor_data_t union is used in all the
 * margo_monitor_*_args structures so that developers
 * of a custom monitoring system can pass some data
 * between a MARGO_MONITOR_FN_START event and its
 * corresponding MARGO_MONITOR_FN_END.
 */
typedef union {
    int64_t i;
    double  f;
    void*   p;
} margo_monitor_data_t;

typedef enum margo_monitor_event_t
{
    MARGO_MONITOR_FN_START,
    MARGO_MONITOR_FN_END,
    MARGO_MONITOR_POINT
} margo_monitor_event_t;

/* clang-format off */
typedef struct margo_monitor_progress_args*      margo_monitor_progress_args_t;
typedef struct margo_monitor_trigger_args*       margo_monitor_trigger_args_t;
typedef struct margo_monitor_register_args*      margo_monitor_register_args_t;
typedef struct margo_monitor_deregister_args*    margo_monitor_deregister_args_t;
typedef struct margo_monitor_lookup_args*        margo_monitor_lookup_args_t;
typedef struct margo_monitor_create_args*        margo_monitor_create_args_t;
typedef struct margo_monitor_forward_args*       margo_monitor_forward_args_t;
typedef struct margo_monitor_cb_args*            margo_monitor_forward_cb_args_t;
typedef struct margo_monitor_respond_args*       margo_monitor_respond_args_t;
typedef struct margo_monitor_cb_args*            margo_monitor_respond_cb_args_t;
typedef struct margo_monitor_destroy_args*       margo_monitor_destroy_args_t;
typedef struct margo_monitor_bulk_create_args*   margo_monitor_bulk_create_args_t;
typedef struct margo_monitor_bulk_transfer_args* margo_monitor_bulk_transfer_args_t;
typedef struct margo_monitor_cb_args*            margo_monitor_bulk_transfer_cb_args_t;
typedef struct margo_monitor_bulk_free_args*     margo_monitor_bulk_free_args_t;
typedef struct margo_monitor_rpc_handler_args*   margo_monitor_rpc_handler_args_t;
typedef struct margo_monitor_rpc_ult_args*       margo_monitor_rpc_ult_args_t;
typedef struct margo_monitor_wait_args*          margo_monitor_wait_args_t;
typedef struct margo_monitor_sleep_args*         margo_monitor_sleep_args_t;
typedef struct margo_monitor_set_input_args*     margo_monitor_set_input_args_t;
typedef struct margo_monitor_set_output_args*    margo_monitor_set_output_args_t;
typedef struct margo_monitor_get_input_args*     margo_monitor_get_input_args_t;
typedef struct margo_monitor_get_output_args*    margo_monitor_get_output_args_t;
typedef struct margo_monitor_free_input_args*    margo_monitor_free_input_args_t;
typedef struct margo_monitor_free_output_args*   margo_monitor_free_output_args_t;
typedef struct margo_monitor_prefinalize_args*   margo_monitor_prefinalize_args_t;
typedef struct margo_monitor_finalize_args*      margo_monitor_finalize_args_t;
typedef const char*                              margo_monitor_user_args_t;
/* clang-format on */

/* clang-format off */
#define MARGO_EXPAND_MONITOR_MACROS       \
    X(PROGRESS,         progress)         \
    X(TRIGGER,          trigger)          \
    X(REGISTER,         register)         \
    X(DEREGISTER,       deregister)       \
    X(LOOKUP,           lookup)           \
    X(CREATE,           create)           \
    X(FORWARD,          forward)          \
    X(FORWARD_CB,       forward_cb)       \
    X(RESPOND,          respond)          \
    X(RESPOND_CB,       respond_cb)       \
    X(DESTROY,          destroy)          \
    X(BULK_CREATE,      bulk_create)      \
    X(BULK_TRANSFER,    bulk_transfer)    \
    X(BULK_TRANSFER_CB, bulk_transfer_cb) \
    X(BULK_FREE,        bulk_free)        \
    X(RPC_HANDLER,      rpc_handler)      \
    X(RPC_ULT,          rpc_ult)          \
    X(WAIT,             wait)             \
    X(SLEEP,            sleep)            \
    X(SET_INPUT,        set_input)        \
    X(SET_OUTPUT,       set_output)       \
    X(GET_INPUT,        get_input)        \
    X(GET_OUTPUT,       get_output)       \
    X(FREE_INPUT,       free_input)       \
    X(FREE_OUTPUT,      free_output)      \
    X(PREFINALIZE,      prefinalize)      \
    X(FINALIZE,         finalize)         \
    X(USER,             user)
/* clang-format on */

struct margo_monitor {
    void* uargs;
    void* (*initialize)(margo_instance_id mid, void*, struct json_object*);
    void (*finalize)(void* uargs);
    const char* (*name)();
    struct json_object* (*config)(void* uargs);

#define X(__x__, __y__)                                      \
    void (*on_##__y__)(void*, double, margo_monitor_event_t, \
                       margo_monitor_##__y__##_args_t);
    MARGO_EXPAND_MONITOR_MACROS
#undef X
};

/* clang-format off */
#define X(__x__, __y__) MARGO_MONITOR_ON_##__x__,
enum
{
    MARGO_EXPAND_MONITOR_MACROS
    MARGO_MONITOR_MAX
};
#undef X
/* clang-format on */

struct margo_monitor_progress_args {
    margo_monitor_data_t uctx;
    /* input */
    unsigned int timeout_ms;
    /* output */
    hg_return_t ret;
};

struct margo_monitor_trigger_args {
    margo_monitor_data_t uctx;
    /* input */
    unsigned int timeout_ms;
    unsigned int max_count;
    /* output */
    unsigned int actual_count;
    hg_return_t  ret;
};

struct margo_monitor_register_args {
    margo_monitor_data_t uctx;
    /* input */
    const char* name;
    ABT_pool    pool;
    hg_id_t     id;
    /* output */
    hg_return_t ret;
};

struct margo_monitor_deregister_args {
    margo_monitor_data_t uctx;
    /* input */
    hg_id_t id;
    /* output */
    hg_return_t ret;
};

struct margo_monitor_lookup_args {
    margo_monitor_data_t uctx;
    /* input */
    const char* name; /* warning: NULL if called from margo_addr_self */
    /* output */
    hg_addr_t   addr;
    hg_return_t ret;
};

struct margo_monitor_create_args {
    margo_monitor_data_t uctx;
    /* input */
    hg_addr_t addr;
    hg_id_t   id;
    /* output */
    hg_handle_t handle;
    hg_return_t ret;
};

struct margo_monitor_forward_args {
    margo_monitor_data_t uctx;
    /* input */
    uint16_t      provider_id;
    hg_handle_t   handle;
    const void*   data;
    double        timeout_ms;
    margo_request request;
    /* output */
    hg_return_t ret;
};

struct margo_monitor_respond_args {
    margo_monitor_data_t uctx;
    /* input */
    hg_handle_t   handle;
    const void*   data;
    double        timeout_ms;
    bool          error;
    margo_request request;
    /* output */
    hg_return_t ret;
};

struct margo_monitor_destroy_args {
    margo_monitor_data_t uctx;
    /* input */
    hg_handle_t handle;
    /* output */
    hg_return_t ret;
};

struct margo_monitor_bulk_create_args {
    margo_monitor_data_t uctx;
    /* input */
    uint32_t                   count;
    const void* const*         ptrs;
    const hg_size_t*           sizes;
    hg_uint8_t                 flags;
    const struct hg_bulk_attr* attrs;
    /* output */
    hg_bulk_t   handle;
    hg_return_t ret;
};

struct margo_monitor_bulk_transfer_args {
    margo_monitor_data_t uctx;
    /* input */
    hg_bulk_op_t  op;
    hg_addr_t     origin_addr;
    hg_bulk_t     origin_handle;
    size_t        origin_offset;
    hg_bulk_t     local_handle;
    size_t        local_offset;
    size_t        size;
    double        timeout_ms;
    margo_request request;
    /* output */
    hg_return_t ret;
};

struct margo_monitor_bulk_free_args {
    margo_monitor_data_t uctx;
    /* input */
    hg_bulk_t handle;
    /* output */
    hg_return_t ret;
};

struct margo_monitor_rpc_handler_args {
    margo_monitor_data_t uctx;
    /* input */
    hg_handle_t handle;
    hg_id_t     parent_rpc_id;
    /* output */
    ABT_pool    pool;
    hg_return_t ret;
};

struct margo_monitor_rpc_ult_args {
    margo_monitor_data_t uctx;
    /* input */
    hg_handle_t handle;
    /* output */
};

struct margo_monitor_wait_args {
    margo_monitor_data_t uctx;
    /* input */
    margo_request request;
    /* output */
    hg_return_t ret;
};

struct margo_monitor_sleep_args {
    margo_monitor_data_t uctx;
    /* input */
    double timeout_ms;
    /* output */
};

struct margo_monitor_set_input_args {
    margo_monitor_data_t uctx;
    /* input */
    hg_handle_t   handle;
    margo_request request;
    const void*   data;
    /* output */
    hg_return_t ret;
};

struct margo_monitor_set_output_args {
    margo_monitor_data_t uctx;
    /* input */
    hg_handle_t   handle;
    margo_request request;
    const void*   data;
    /* output */
    hg_return_t ret;
};

struct margo_monitor_get_input_args {
    margo_monitor_data_t uctx;
    /* input */
    hg_handle_t handle;
    const void* data;
    /* output */
    hg_return_t ret;
};

struct margo_monitor_get_output_args {
    margo_monitor_data_t uctx;
    /* input */
    hg_handle_t handle;
    const void* data;
    /* output */
    hg_return_t ret;
};

struct margo_monitor_free_input_args {
    margo_monitor_data_t uctx;
    /* input */
    hg_handle_t handle;
    const void* data;
    /* output */
    hg_return_t ret;
};

struct margo_monitor_free_output_args {
    margo_monitor_data_t uctx;
    /* input */
    hg_handle_t handle;
    const void* data;
    /* output */
    hg_return_t ret;
};

struct margo_monitor_prefinalize_args {
    margo_monitor_data_t uctx;
};

struct margo_monitor_finalize_args {
    margo_monitor_data_t uctx;
};

struct margo_monitor_cb_args {
    margo_monitor_data_t uctx;
    /* input */
    const struct hg_cb_info* info;
    margo_request            request;
    /* output */
    hg_return_t ret;
};

/**
 * @brief Invokes the on_user callback of the monitor registered with the
 * margo instance.
 *
 * @param mid Margo instance
 * @param margo_monitor_event_t Type of event
 * @param args User-defined args for the event
 *
 * @return HG_SUCCESS or other error code
 */
hg_return_t margo_monitor_call_user(margo_instance_id mid,
                                    margo_monitor_event_t,
                                    margo_monitor_user_args_t args);

/**
 * @brief Attach custom monitoring data to the handle.
 *
 * Note that the last call related to a particular handle
 * before it is freed will always be on_destroy.
 * This information can be used to properly release any attached
 * data if necessary.
 *
 * @param handle Handle to which to attach data.
 * @param data Data to attach.
 *
 * @return HG_SUCCESS or HG_INVALID_ARG if req is NULL.
 */
hg_return_t margo_set_monitoring_data(hg_handle_t          handle,
                                      margo_monitor_data_t data);

/**
 * @brief Retrieve custom monitoring data from the handle.
 *
 * @param req Request to which the data is attached.
 * @param data Pointer to data.
 *
 * @return HG_SUCCESS or HG_INVALID_ARG if req is NULL.
 */
hg_return_t margo_get_monitoring_data(hg_handle_t           handle,
                                      margo_monitor_data_t* data);

/**
 * @brief Attach custom monitoring data to the margo_request.
 *
 * Attaching data to a margo_request shouldn't be necessary for
 * RPC-related monitoring callbacks, but is necessary for bulk-related
 * ones, because these functions do not carry a hg_handle_t handle
 * to which to attach data.
 *
 * Note that the last call related to a particular margo_request
 * before it is freed will always be on_wait(MARGO_MONITOR_FN_END).
 * This information can be used to properly release any attached
 * data if necessary.
 *
 * @param req Request to which to attach data.
 * @param data Data to attach.
 *
 * @return HG_SUCCESS or HG_INVALID_ARG if req is NULL.
 */
hg_return_t margo_request_set_monitoring_data(margo_request        req,
                                              margo_monitor_data_t data);

/**
 * @brief Retrieve custom monitoring data from the margo_request.
 *
 * @param req Request to which the data is attached.
 * @param data Pointer to data.
 *
 * @return HG_SUCCESS or HG_INVALID_ARG if req is NULL.
 */
hg_return_t margo_request_get_monitoring_data(margo_request         req,
                                              margo_monitor_data_t* data);

#ifdef __cplusplus
}
#endif

#endif
