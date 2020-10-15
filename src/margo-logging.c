/*
 * (C) 2020 The University of Chicago
 * 
 * See COPYRIGHT in top-level directory.
 */
#include "margo-internal.h"
#include <margo-logging.h>

#define DEFINE_LOG_FN(__name__, __msg__) \
    static void __name__(void* uargs, const char* fmt, ...) { \
    va_list ap; \
    va_start(ap, fmt); \
    printf("[" #__name__ "] "); \
    vprintf(fmt, ap); \
    va_end(ap); \
}

DEFINE_LOG_FN(_margo_trace, trace)
DEFINE_LOG_FN(_margo_debug, debug)
DEFINE_LOG_FN(_margo_info, info)
DEFINE_LOG_FN(_margo_warning, warning)
DEFINE_LOG_FN(_margo_error, error)
DEFINE_LOG_FN(_margo_critical, critical)

static const struct margo_logger _default_logger = {
    .uargs = NULL,
    .trace = _margo_trace,
    .debug = _margo_debug,
    .info = _margo_info,
    .warning = _margo_warning,
    .error = _margo_error,
    .critical = _margo_critical
};

int margo_set_logger(margo_instance_id mid, const struct margo_logger* logger)
{
    if(!logger) {
        mid->logger = _default_logger;
        mid->log_level = MARGO_LOG_WARNING;
    } else {
        mid->logger = *logger;
    }
    return 0;
}

int margo_set_log_level(margo_instance_id mid, margo_log_level level)
{
    if(level < 0 || level > MARGO_LOG_CRITICAL) return -1;
    mid->log_level = level;
    return 0;
}
