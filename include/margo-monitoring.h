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

struct hg_bulk_attr;
struct margo_instance;
typedef struct margo_instance*       margo_instance_id;
typedef struct margo_request_struct* margo_request;

extern const struct margo_monitor* margo_default_monitor;

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
typedef void*                                    margo_monitor_user_args_t;
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
    void* (*initialize)(margo_instance_id mid, void*, const char*);
    void (*finalize)(void* uargs);
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
    const char* name;
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
 * @brief Set a monitor structure for Margo to use. The structure will be
 * internally copied and the user may free the input argument after the call.
 * Passing NULL as monitor is valid and will disable monitor altogether.
 *
 * Note: if a monitor is already in place, its finalize function pointer will
 * be called before the new monitor is installed. If provided, The initialize
 * function of the new monitor will be called and the monitor's uargs will be
 * replaced internally with the returned value of the initialize call.
 *
 * @param mid Margo instance
 * @param monitor Monitor structure
 * @param config JSON-formatted configuration or NULL
 *
 * @return 0 in case of success, -1 otherwise
 */
int margo_set_monitor(margo_instance_id           mid,
                      const struct margo_monitor* monitor,
                      const char*                 config);

/**
 * @brief Invokes the on_user callback of the monitor registered with the
 * margo instance.
 *
 * @param mid Margo instance
 * @param margo_monitor_event_t Type of event
 * @param args User-defined args for the event
 *
 * @return 0 in case of success, -1 otherwise
 */
int margo_monitor_call_user(margo_instance_id mid,
                            margo_monitor_event_t,
                            void* args);

#ifdef __cplusplus
}
#endif

#endif
