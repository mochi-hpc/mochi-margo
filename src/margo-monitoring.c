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

hg_return_t margo_set_monitor(margo_instance_id           mid,
                              const struct margo_monitor* monitor,
                              const char*                 config)
{
    if (!mid) return HG_INVALID_ARG;
    if (mid->monitor) {
        if (mid->monitor->finalize) {
            mid->monitor->finalize(mid->monitor->uargs);
        }
    } else {
        mid->monitor = (struct margo_monitor*)malloc(sizeof(*(mid->monitor)));
    }
    if (!monitor) {
        free(mid->monitor);
        mid->monitor = NULL;
    } else {
        memcpy(mid->monitor, monitor, sizeof(*(mid->monitor)));
        if (mid->monitor->initialize)
            mid->monitor->uargs
                = mid->monitor->initialize(mid, mid->monitor->uargs, config);
    }
    return HG_SUCCESS;
}

hg_return_t margo_set_monitoring_data(margo_request        req,
                                      margo_monitor_data_t data)
{
    if (!req) return HG_INVALID_ARG;
    req->monitor_data = data;
    return HG_SUCCESS;
}

hg_return_t margo_get_monitoring_data(margo_request         req,
                                      margo_monitor_data_t* data)
{

    if (!req) return HG_INVALID_ARG;
    if (data) *data = req->monitor_data;
    return HG_SUCCESS;
}
