/*
 * (C) 2020 The University of Chicago
 *
 * See COPYRIGHT in top-level directory.
 */
#ifndef __MARGO_LOGGING_H
#define __MARGO_LOGGING_H

#ifdef __cplusplus
extern "C" {
#endif

struct margo_instance;
typedef struct margo_instance* margo_instance_id;

typedef void (*margo_log_fn_t)(void* uargs, const char* message);

/* trace:    for entering/exiting function or providing additional
 *           detail about what code path was taken
 * debug:    reserve for when you want to actually debug some function;
 *           not meant to remain in the code after the bug is found
 * info:     information you expect users will want to know by default
 *           (e.g. a server address, etc.)
 * warning:  any warning (e.g. if a CPU affinity request in the configuration
 *           cannot be satisfied, but the code will execute anyway)
 * error:    something went wrong, but not wrong enough that the server
 *           should stop
 * critical: right before you force the program to stop because of an
 *           unrecoverable problem
 */
typedef enum margo_log_level
{
    MARGO_LOG_EXTERNAL, /* level management is handled by the underlying
                           implementation */
    MARGO_LOG_TRACE,
    MARGO_LOG_DEBUG,
    MARGO_LOG_INFO,
    MARGO_LOG_WARNING,
    MARGO_LOG_ERROR,
    MARGO_LOG_CRITICAL
} margo_log_level;

struct margo_logger {
    void*          uargs;
    margo_log_fn_t trace;
    margo_log_fn_t debug;
    margo_log_fn_t info;
    margo_log_fn_t warning;
    margo_log_fn_t error;
    margo_log_fn_t critical;
};

/**
 * @brief Set a logger structure for Margo to use. The structure will be
 * internally copied and the user may free the input argument after the call.
 * Passing NULL as logger will reset the logger to an internal logger that only
 * prints errors and critical messages on stderr.
 *
 * @param mid Margo instance
 * @param logger Logger structure
 *
 * @return 0 in case of success, -1 otherwise
 */
int margo_set_logger(margo_instance_id mid, const struct margo_logger* logger);

/**
 * @brief Set the log level of the Margo instance.
 *
 * @param mid Margo instance
 * @param level Log level
 *
 * @return 0 in case of success, -1 otherwise
 */
int margo_set_log_level(margo_instance_id mid, margo_log_level level);

/**
 * @brief Set a logger structure for Margo to use in functions that don't take a
 * margo_instance_id (such as margo_init and its variants).
 *
 * The structure will be internally copied and the user may free the input
 * argument after the call. Passing NULL as logger will reset the global logger
 * to a logger that only prints errors and critical messages on stderr.
 *
 * @param logger Logger structure
 *
 * @return 0 in case of success, -1 otherwise
 */
int margo_set_global_logger(const struct margo_logger* logger);

/**
 * @brief Set the log level of the logger used for global operations.
 *
 * @param level Log level
 *
 * @return 0 in case of success, -1 otherwise
 */
int margo_set_global_log_level(margo_log_level level);

/**
 * @brief Logging functions. These functions will use the logger
 * registered with the margo instance, or the global logger if
 * the margo instance is MARGO_INSTANCE_NULL.
 *
 * @param mid Margo instance
 * @param fmt Format string
 * @param ... Extra arguments
 */
void margo_trace(margo_instance_id mid, const char* fmt, ...);
void margo_debug(margo_instance_id mid, const char* fmt, ...);
void margo_info(margo_instance_id mid, const char* fmt, ...);
void margo_warning(margo_instance_id mid, const char* fmt, ...);
void margo_error(margo_instance_id mid, const char* fmt, ...);
void margo_critical(margo_instance_id mid, const char* fmt, ...);

#ifdef __cplusplus
}
#endif

#endif
