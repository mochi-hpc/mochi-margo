/*
 * (C) 2022 The University of Chicago
 *
 * See COPYRIGHT in top-level directory.
 */
#ifndef __MARGO_SERIALIZATION_H
#define __MARGO_SERIALIZATION_H

#include "margo.h"
#include "margo-instance.h"
#include "margo-monitoring-internal.h"

// This file provides the serialization mechanism that Margo injects into
// Mercury in order to add a header to every RPC and response.
//
// Instead of calling HG_Register with user-provided proc callbacks, we call
// it with margo_forward_proc and margo_respond_proc, and attach the actual
// user- provided callback using HG_Register_data (see margo_rpc_data struct in
// margo-instance.h).
//
// Once it's time to call HG_Forward or HG_Respond, we extract the user-provided
// callbacks from the RPC data, create a margo_forward_proc_args or a
// margo_respond_proc_args structure, initialize it with the user-provided data
// pointer, and call HG_Forward/Respond with that argument instead.
//
// The margo_respond_proc_args structure carries an error code that allows
// margo to propagate an hg_return_t value to the client. It is used e.g.
// in the __MARGO_INTERNAL_RPC_HANDLER_BODY macro when something happened
// that prevented the RPC from running. It allows to not care about the
// semantics of the user-provided data, since any value other than HG_SUCCESS
// will make serialization stop at the error code.

typedef struct margo_forward_proc_args {
    hg_handle_t   handle;
    margo_request request;
    void*         user_args;
    hg_proc_cb_t  user_cb;
    bool          disable_header;
    struct {
        hg_id_t parent_rpc_id;
    } header;
} * margo_forward_proc_args_t;

typedef struct margo_respond_proc_args {
    hg_handle_t   handle;
    margo_request request;
    void*         user_args;
    hg_proc_cb_t  user_cb;
    bool          disable_header;
    struct {
        hg_return_t hg_ret;
    } header;
} * margo_respond_proc_args_t;

static inline hg_return_t margo_forward_proc(hg_proc_t proc, void* args)
{
    margo_forward_proc_args_t sargs = (margo_forward_proc_args_t)args;
    hg_return_t               hret  = HG_SUCCESS;
    margo_instance_id         mid   = MARGO_INSTANCE_NULL;

    /* monitoring */
    /* Note: sargs->request is set only in the margo_forward path,
     * not in margo_get_input or margo_free_input, so if it set we
     * know we are encoding the input (set_input) and should monitor.
     */
    if (sargs->request) mid = sargs->request->mid;
    struct margo_monitor_set_input_args monitoring_args
        = {.handle  = sargs->handle,
           .request = sargs->request,
           .data    = sargs->user_args,
           .ret     = HG_SUCCESS};
    if (sargs->user_cb) {
        __MARGO_MONITOR(mid, FN_START, set_input, monitoring_args);
    }

    if (!sargs->disable_header) {
        hret = hg_proc_memcpy(proc, (void*)(&sargs->header),
                              sizeof(sargs->header));
        if (hret != HG_SUCCESS) goto finish;
    } else {
        sargs->header.parent_rpc_id = 0;
    }
    if (sargs && sargs->user_cb) {
        hret = sargs->user_cb(proc, sargs->user_args);
        goto finish;
    }

finish:

    /* monitoring */
    monitoring_args.ret = hret;
    if (sargs->user_cb) {
        __MARGO_MONITOR(mid, FN_END, set_input, monitoring_args);
    }

    return hret;
}

static inline hg_return_t margo_respond_proc(hg_proc_t proc, void* args)
{
    margo_respond_proc_args_t sargs = (margo_respond_proc_args_t)args;
    hg_return_t               hret  = HG_SUCCESS;
    margo_instance_id         mid   = MARGO_INSTANCE_NULL;

    /* monitoring */
    /* Note: sargs->request is set only in margo_respond, not in
     * margo_get_output or margo_free_output, so if it set we know
     * we are encoding the output (set_output) and should monitor.
     */
    if (sargs->request) mid = sargs->request->mid;
    struct margo_monitor_set_output_args monitoring_args
        = {.handle  = sargs->handle,
           .request = sargs->request,
           .data    = sargs->user_args,
           .ret     = HG_SUCCESS};
    if (sargs->user_cb) {
        __MARGO_MONITOR(mid, FN_START, set_output, monitoring_args);
    }

    if (!sargs->disable_header) {
        hret = hg_proc_memcpy(proc, (void*)(&sargs->header),
                              sizeof(sargs->header));
        if (hret != HG_SUCCESS) goto finish;
        if (sargs->header.hg_ret != HG_SUCCESS) goto finish;
    } else {
        sargs->header.hg_ret = HG_SUCCESS;
    }
    if (sargs && sargs->user_cb) {
        hret = sargs->user_cb(proc, sargs->user_args);
    }

finish:

    /* monitoring */
    monitoring_args.ret = hret;
    if (sargs->user_cb) {
        __MARGO_MONITOR(mid, FN_END, set_output, monitoring_args);
    }

    return hret;
}

#endif
