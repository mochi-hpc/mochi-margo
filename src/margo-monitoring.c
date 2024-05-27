/**
 * (C) 2022 The University of Chicago
 *
 * See COPYRIGHT in top-level directory.
 */
#include <string.h>
#include "margo-instance.h"
#include "margo-monitoring.h"

hg_return_t margo_monitor_call_user(margo_instance_id         mid,
                                    margo_monitor_event_t     ev,
                                    margo_monitor_user_args_t args)
{
    if (!mid) return HG_INVALID_ARG;
    if (!mid->monitor) return HG_SUCCESS;
    if (!mid->monitor->on_user) return HG_SUCCESS;
    mid->monitor->on_user(mid->monitor->uargs, ABT_get_wtime(), ev, args);
    return HG_SUCCESS;
}

hg_return_t margo_set_monitoring_data(hg_handle_t          handle,
                                      margo_monitor_data_t data)
{
    if (!handle) return HG_INVALID_ARG;
    struct margo_handle_data* handle_data = HG_Get_data(handle);
    if (!handle_data) return HG_OTHER_ERROR;
    handle_data->monitor_data = data;
    return HG_SUCCESS;
}

hg_return_t margo_get_monitoring_data(hg_handle_t           handle,
                                      margo_monitor_data_t* data)
{

    if (!handle) return HG_INVALID_ARG;
    struct margo_handle_data* handle_data = HG_Get_data(handle);
    if (!handle_data) return HG_OTHER_ERROR;
    if (data) *data = handle_data->monitor_data;
    return HG_SUCCESS;
}

hg_return_t margo_request_set_monitoring_data(margo_request        req,
                                              margo_monitor_data_t data)
{
    if (!req) return HG_INVALID_ARG;
    req->monitor_data = data;
    return HG_SUCCESS;
}

hg_return_t margo_request_get_monitoring_data(margo_request         req,
                                              margo_monitor_data_t* data)
{

    if (!req) return HG_INVALID_ARG;
    if (data) *data = req->monitor_data;
    return HG_SUCCESS;
}

hg_return_t margo_monitor_dump(margo_instance_id     mid,
                               margo_monitor_dump_fn dump_fn,
                               void*                 uargs,
                               bool                  reset)
{
    if (!mid) return HG_INVALID_ARG;
    if (!mid->monitor) return HG_SUCCESS;
    if (!mid->monitor->dump) return HG_NOENTRY;
    mid->monitor->dump(mid->monitor->uargs, dump_fn, uargs, reset);
    return HG_SUCCESS;
}
