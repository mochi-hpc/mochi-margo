/*
 * (C) 2020 The University of Chicago
 *
 * See COPYRIGHT in top-level directory.
 */
#include <stdarg.h>
#include "margo-instance.h"
#include <margo-logging.h>

static const char*     global_log_level_env = NULL;
static margo_log_level global_log_level     = -1;

static void __margo_log_trace(void* uargs, const char* str)
{
    (void)uargs;
    fprintf(stderr, "[trace] %s\n", str);
}

static void __margo_log_debug(void* uargs, const char* str)
{
    (void)uargs;
    fprintf(stderr, "[debug] %s\n", str);
}

static void __margo_log_info(void* uargs, const char* str)
{
    (void)uargs;
    fprintf(stderr, "[info] %s\n", str);
}

static void __margo_log_warning(void* uargs, const char* str)
{
    (void)uargs;
    fprintf(stderr, "[warning] %s\n", str);
}

static void __margo_log_error(void* uargs, const char* str)
{
    (void)uargs;
    fprintf(stderr, "[error] %s\n", str);
}

static void __margo_log_critical(void* uargs, const char* str)
{
    (void)uargs;
    fprintf(stderr, "[critical] %s\n", str);
}

static struct margo_logger global_logger = {.uargs    = NULL,
                                            .trace    = __margo_log_trace,
                                            .debug    = __margo_log_debug,
                                            .info     = __margo_log_info,
                                            .warning  = __margo_log_warning,
                                            .error    = __margo_log_error,
                                            .critical = __margo_log_critical};

static margo_log_level log_level_from_string(const char* level)
{
    if (!level) return -1;
    if (strcmp(level, "trace") == 0) return MARGO_LOG_TRACE;
    if (strcmp(level, "debug") == 0) return MARGO_LOG_DEBUG;
    if (strcmp(level, "info") == 0) return MARGO_LOG_INFO;
    if (strcmp(level, "warning") == 0) return MARGO_LOG_WARNING;
    if (strcmp(level, "error") == 0) return MARGO_LOG_ERROR;
    if (strcmp(level, "critical") == 0)
        return MARGO_LOG_CRITICAL;
    else {
        fprintf(stderr, "[warning] unknown log level \"%s\"", level);
        return -1;
    }
}

static const char* log_level_to_string(margo_log_level level)
{
    switch (level) {
    case MARGO_LOG_TRACE:
        return "trace";
    case MARGO_LOG_DEBUG:
        return "debug";
    case MARGO_LOG_INFO:
        return "info";
    case MARGO_LOG_WARNING:
        return "warning";
    case MARGO_LOG_ERROR:
        return "error";
    case MARGO_LOG_CRITICAL:
        return "critical";
    default:
        return "";
    }
}

static inline void set_global_log_level_from_env()
{
    /* the first time the global log level is needed, this function will
     * set global_log_level_env and global_log_level as specified either
     * by the user via margo_set_global_log_level() or via the MARGO_LOG_LEVEL
     * environment variable. */
    if (!global_log_level_env) {
        if (global_log_level == -1) {
            /* the user did not provide a log level via
             * margo_set_global_log_level, try to set if from MARGO_LOG_LEVEL.
             */
            global_log_level_env = getenv("MARGO_LOG_LEVEL");
            global_log_level     = log_level_from_string(global_log_level_env);
            if (global_log_level == -1) {
                if (global_log_level_env) {
                    fprintf(
                        stderr,
                        "[warning] unknown log level \"%s\" in MARGO_LOG_LEVEL,"
                        " defaulting to \"warning\"",
                        global_log_level_env);
                }
                /* the log level in MARGO_LOG_LEVEL is either not present or not
                 * valid, set global_log_level to MARGO_LOG_WARNING and goto the
                 * section setting global_log_level_env from it. */
                global_log_level = MARGO_LOG_WARNING;
            }
        }
        /* here we know global_log_level is defined. */
        global_log_level_env = log_level_to_string(global_log_level);
    }
}

#define DEFINE_LOG_FN(__name__, __level__, __LEVEL__)                        \
    void __name__(margo_instance_id mid, const char* fmt, ...)               \
    {                                                                        \
        static const margo_log_level function_level = MARGO_LOG_##__LEVEL__; \
        set_global_log_level_from_env();                                     \
        margo_log_level logger_level                                         \
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
        else if (!mid && global_logger.__level__)                            \
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
    set_global_log_level_from_env();
    if (!logger) {
        mid->logger.uargs    = NULL;
        mid->logger.trace    = __margo_log_trace;
        mid->logger.debug    = __margo_log_debug;
        mid->logger.info     = __margo_log_info;
        mid->logger.warning  = __margo_log_warning;
        mid->logger.error    = __margo_log_error;
        mid->logger.critical = __margo_log_critical;
    } else {
        mid->logger = *logger;
    }
    mid->log_level = global_log_level;
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
        global_logger.trace    = __margo_log_trace;
        global_logger.debug    = __margo_log_debug;
        global_logger.info     = __margo_log_info;
        global_logger.warning  = __margo_log_warning;
        global_logger.error    = __margo_log_error;
        global_logger.critical = __margo_log_critical;
    } else {
        global_logger = *logger;
    }
    return 0;
}

int margo_set_global_log_level(margo_log_level level)
{
    if (level < 0 || level > MARGO_LOG_CRITICAL) return -1;
    global_log_level     = level;
    global_log_level_env = log_level_to_string(level);
    return 0;
}
