/**
 * @file margo-monitoring.h
 *
 * (C) 2022 The University of Chicago
 *
 * See COPYRIGHT in top-level directory.
 */
#ifndef __MARGO_MONITORING_H
#define __MARGO_MONITORING_H

#include <mercury.h>
#include <mercury_types.h>
#include <mercury_bulk.h>

#ifdef __cplusplus
extern "C" {
#endif

struct margo_instance;
typedef struct margo_instance*       margo_instance_id;
typedef struct margo_request_struct* margo_request;

typedef enum margo_monitor_event_t
{
    MARGO_MONITOR_FN_START,
    MARGO_MONITOR_FN_END,
    MARGO_MONITOR_POINT
} margo_monitor_event_t;

/* clang-format off */
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
/* clang-format on */

/* clang-format off */
struct margo_monitor {
    void* uargs;
    void* (*init)(margo_instance_id, const char*, void*);
    char* (*get_config)(void*);
    void (*free)(void*);
    void (*on_register)(void*, double, margo_monitor_event_t, margo_monitor_register_args_t);
    void (*on_deregister)(void*, double, margo_monitor_event_t, margo_monitor_deregister_args_t);
    void (*on_lookup)(void*, double, margo_monitor_event_t, margo_monitor_lookup_args_t);
    void (*on_create)(void*, double, margo_monitor_event_t, margo_monitor_create_args_t);
    void (*on_forward)(void*, double, margo_monitor_event_t, margo_monitor_forward_args_t);
    void (*on_forward_cb)(void*, double, margo_monitor_event_t, margo_monitor_forward_cb_args_t);
    void (*on_respond)(void*, double, margo_monitor_event_t, margo_monitor_respond_args_t);
    void (*on_respond_cb)(void*, double, margo_monitor_event_t, margo_monitor_respond_cb_args_t);
    void (*on_destroy)(void*, double, margo_monitor_event_t, margo_monitor_destroy_args_t);
    void (*on_bulk_create)(void*, double, margo_monitor_event_t, margo_monitor_bulk_create_args_t);
    void (*on_bulk_transfer)(void*, double, margo_monitor_event_t, margo_monitor_bulk_transfer_args_t);
    void (*on_bulk_transfer_cb)(void*, double, margo_monitor_event_t, margo_monitor_bulk_transfer_cb_args_t);
    void (*on_bulk_free)(void*, double, margo_monitor_event_t, margo_monitor_bulk_free_args_t);
    void (*on_rpc_handler)(void*, double, margo_monitor_event_t, margo_monitor_rpc_handler_args_t);
    void (*on_rpc_ult)(void*, double, margo_monitor_event_t, margo_monitor_rpc_ult_args_t);
    void (*on_wait)(void*, double, margo_monitor_event_t, margo_monitor_wait_args_t);
    void (*on_sleep)(void*, double, margo_monitor_event_t, margo_monitor_sleep_args_t);
    void (*on_set_input)(void*, double, margo_monitor_event_t, margo_monitor_set_input_args_t);
    void (*on_set_output)(void*, double, margo_monitor_event_t, margo_monitor_set_output_args_t);
    void (*on_get_input)(void*, double, margo_monitor_event_t, margo_monitor_get_input_args_t);
    void (*on_get_output)(void*, double, margo_monitor_event_t, margo_monitor_get_output_args_t);
    void (*on_free_input)(void*, double, margo_monitor_event_t, margo_monitor_free_input_args_t);
    void (*on_free_output)(void*, double, margo_monitor_event_t, margo_monitor_free_output_args_t);
    void (*on_prefinalize)(void*, double, margo_monitor_event_t, margo_monitor_prefinalize_args_t);
    void (*on_finalize)(void*, double, margo_monitor_event_t, margo_monitor_finalize_args_t);
    void (*on_user)(void*, double, margo_monitor_event_t, void*);
};
/* clang-format on */

struct margo_monitor_register_args {
    /* input */
    const char* name;
    ABT_pool    pool;
    hg_id_t     id;
    /* output */
    hg_return_t ret;
};

struct margo_monitor_deregister_args {
    /* input */
    hg_id_t id;
    /* output */
    hg_return_t ret;
};

struct margo_monitor_lookup_args {
    /* input */
    const char* name;
    /* output */
    hg_addr_t   addr;
    hg_return_t ret;
};

struct margo_monitor_create_args {
    /* input */
    hg_addr_t addr;
    hg_id_t   id;
    /* output */
    hg_handle_t handle;
    hg_return_t ret;
};

struct margo_monitor_forward_args {
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
    /* input */
    hg_handle_t handle;
    /* output */
    hg_return_t ret;
};

struct margo_monitor_bulk_create_args {
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
    /* input */
    hg_bulk_t handle;
    /* output */
    hg_return_t ret;
};

struct margo_monitor_rpc_handler_args {
    /* input */
    hg_handle_t handle;
    /* output */
    ABT_pool    pool;
    hg_return_t ret;
};

struct margo_monitor_rpc_ult_args {
    /* input */
    hg_handle_t handle;
    /* output */
};

struct margo_monitor_wait_args {
    /* input */
    margo_request request;
    /* output */
    hg_return_t ret;
};

struct margo_monitor_sleep_args {
    /* input */
    double timeout_ms;
    /* output */
};

struct margo_monitor_set_input_args {
    /* input */
    hg_handle_t   handle;
    margo_request request;
    const void*   data;
    /* output */
    hg_return_t ret;
};

struct margo_monitor_set_output_args {
    /* input */
    hg_handle_t   handle;
    margo_request request;
    const void*   data;
    /* output */
    hg_return_t ret;
};

struct margo_monitor_get_input_args {
    /* input */
    hg_handle_t handle;
    const void* data;
    /* output */
    hg_return_t ret;
};

struct margo_monitor_get_output_args {
    /* input */
    hg_handle_t handle;
    const void* data;
    /* output */
    hg_return_t ret;
};

struct margo_monitor_free_input_args {
    /* input */
    hg_handle_t handle;
    const void* data;
    /* output */
    hg_return_t ret;
};

struct margo_monitor_free_output_args {
    /* input */
    hg_handle_t handle;
    const void* data;
    /* output */
    hg_return_t ret;
};

struct margo_monitor_prefinalize_args {};

struct margo_monitor_finalize_args {};

struct margo_monitor_cb_args {
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
 * @param mid Margo instance
 * @param monitor Monitor structure
 *
 * @return 0 in case of success, -1 otherwise
 */
int margo_set_monitor(margo_instance_id           mid,
                      const struct margo_monitor* monitor);

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
