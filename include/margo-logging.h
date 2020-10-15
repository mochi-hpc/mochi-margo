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

typedef void (*margo_log_fn_t)(void* uargs, const char* format, ...);

typedef enum margo_log_level {
    MARGO_LOG_EXTERNAL, /* level management is handled by the underlying implementation */
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
 * @brief Set a logger structure for Margo to use. The structure will be internally copied
 * and the user may free the input argument after the call. Passing NULL as logger will
 * reset the logger to the default internal logger.
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

#ifdef __cplusplus
}
#endif


#endif
