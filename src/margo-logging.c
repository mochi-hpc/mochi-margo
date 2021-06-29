/*
 * (C) 2020 The University of Chicago
 *
 * See COPYRIGHT in top-level directory.
 */
#include <stdarg.h>
#include "margo-instance.h"
#include <margo-logging.h>

static margo_log_level global_log_level = MARGO_LOG_WARNING;

static void _margo_log_trace(void* uargs, const char* str)
{
    (void)uargs;
    fprintf(stderr, "[trace] %s\n", str);
}

static void _margo_log_debug(void* uargs, const char* str)
{
    (void)uargs;
    fprintf(stderr, "[debug] %s\n", str);
}

static void _margo_log_info(void* uargs, const char* str)
{
    (void)uargs;
    fprintf(stderr, "[info] %s\n", str);
}

static void _margo_log_warning(void* uargs, const char* str)
{
    (void)uargs;
    fprintf(stderr, "[warning] %s\n", str);
}

static void _margo_log_error(void* uargs, const char* str)
{
    (void)uargs;
    fprintf(stderr, "[error] %s\n", str);
}

static void _margo_log_critical(void* uargs, const char* str)
{
    (void)uargs;
    fprintf(stderr, "[critical] %s\n", str);
}

static struct margo_logger global_logger = {.uargs    = NULL,
                                            .trace    = _margo_log_trace,
                                            .debug    = _margo_log_debug,
                                            .info     = _margo_log_info,
                                            .warning  = _margo_log_warning,
                                            .error    = _margo_log_error,
                                            .critical = _margo_log_critical};

#define DEFINE_LOG_FN(__name__, __level__, __LEVEL__)                        \
    void __name__(margo_instance_id mid, const char* fmt, ...)               \
    {                                                                        \
        static const margo_log_level function_level = MARGO_LOG_##__LEVEL__; \
        margo_log_level              logger_level                            \
            = mid ? mid->log_level : global_log_level;                       \
        if (logger_level > function_level) return;                           \
        va_list args1;                                                       \
        va_start(args1, fmt);                                                \
        va_list args2;                                                       \
        va_copy(args2, args1);                                               \
        size_t msg_size = vsnprintf(NULL, 0, fmt, args1);                    \
        char   buf[msg_size + 1];                                            \
        va_end(args1);                                                       \
        vsnprintf(buf, msg_size + 1, fmt, args2);                            \
        if (mid && mid->logger.__level__)                                    \
            mid->logger.__level__(mid->logger.uargs, buf);                   \
        else if (global_logger.__level__)                                    \
            global_logger.__level__(global_logger.uargs, buf);               \
        va_end(args2);                                                       \
    }

DEFINE_LOG_FN(margo_trace, trace, TRACE)
DEFINE_LOG_FN(margo_debug, debug, DEBUG)
DEFINE_LOG_FN(margo_info, info, INFO)
DEFINE_LOG_FN(margo_warning, warning, WARNING)
DEFINE_LOG_FN(margo_error, error, ERROR)
DEFINE_LOG_FN(margo_critical, critical, CRITICAL)

int margo_set_logger(margo_instance_id mid, const struct margo_logger* logger)
{
    if (!logger) {
        mid->logger.uargs    = NULL;
        mid->logger.trace    = _margo_log_trace;
        mid->logger.debug    = _margo_log_debug;
        mid->logger.info     = _margo_log_info;
        mid->logger.warning  = _margo_log_warning;
        mid->logger.error    = _margo_log_error;
        mid->logger.critical = _margo_log_critical;
        mid->log_level       = MARGO_LOG_WARNING;
    } else {
        mid->logger = *logger;
    }
    return 0;
}

int margo_set_log_level(margo_instance_id mid, margo_log_level level)
{
    if (level < 0 || level > MARGO_LOG_CRITICAL) return -1;
    mid->log_level = level;
    return 0;
}

int margo_set_global_logger(const struct margo_logger* logger)
{
    if (!logger) {
        global_logger.uargs    = NULL;
        global_logger.trace    = _margo_log_trace;
        global_logger.debug    = _margo_log_debug;
        global_logger.info     = _margo_log_info;
        global_logger.warning  = _margo_log_warning;
        global_logger.error    = _margo_log_error;
        global_logger.critical = _margo_log_critical;
        global_log_level       = MARGO_LOG_WARNING;
    } else {
        global_logger = *logger;
    }
    return 0;
}

int margo_set_global_log_level(margo_log_level level)
{
    if (level < 0 || level > MARGO_LOG_CRITICAL) return -1;
    global_log_level = level;
    return 0;
}
