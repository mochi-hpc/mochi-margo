/**
 * @file margo-timer.h
 *
 * (C) 2021 The University of Chicago
 *
 * See COPYRIGHT in top-level directory.
 */
#ifndef __MARGO_TIMER_H
#define __MARGO_TIMER_H

#include <margo.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Type of callback called when a timer expires.
 */
typedef void (*margo_timer_callback_fn)(void*);
/**
 * Margo timer type.
 */
typedef struct margo_timer* margo_timer_t;
#define MARGO_TIMER_NULL ((margo_timer_t)NULL)

/**
 * @brief Creates a timer object.
 * The callback will be submitted to the handler pool.
 *
 * @param mid Margo instance
 * @param cb_fn Callback to call when the timer finishes
 * @param cb_dat Callback data
 * @param timer Resulting timer
 *
 * @return 0 on success, negative value on failure
 */
int margo_timer_create(margo_instance_id       mid,
                       margo_timer_callback_fn cb_fn,
                       void*                   cb_dat,
                       margo_timer_t*          timer);

/**
 * @brief Creates a timer object and specifies the pool
 * in which to run the callback.
 *
 * @note Passing ABT_POOL_NULL as the pool is allowed.
 * In this case, the callback will be invoked directly
 * within the ULT that runs the progress loop. This should
 * generally be avoided unless the callback is very short,
 * and does not call any margo_* or HG_* function. A typical
 * example of a valid callback would be one that simply
 * sets the value of an ABT_eventual, or one that submits
 * a ULT and returns.
 *
 * @param mid Margo instance
 * @param cb_fn Callback to call when the timer finishes
 * @param cb_dat Callback data
 * @param pool Pool in which to run the callback
 * @param timer Resulting timer
 *
 * @return 0 on success, negative value on failure
 */
int margo_timer_create_with_pool(margo_instance_id       mid,
                                 margo_timer_callback_fn cb_fn,
                                 void*                   cb_dat,
                                 ABT_pool                pool,
                                 margo_timer_t*          timer);

/**
 * @brief Start the timer with a provided timeout.
 *
 * @param margo_timer_t Timer object to start
 * @param timeout_ms Timeout
 *
 * @return 0 on success, negative value on failure
 */
int margo_timer_start(margo_timer_t timer, double timeout_ms);

/**
 * @brief Cancel a started timer. If the timer's callback
 * has already been submitted as a ULT, this ULT will
 * eventually be executed. Hence, calling margo_timer_cancel
 * does not guarantee that the timer won't actually fire
 * later on. To ensure that no such ULT is pending, call
 * margo_timer_wait_pending().
 *
 * @param timer Timer to cancel
 *
 * @return 0 on success, negative value on failure
 */
int margo_timer_cancel(margo_timer_t timer);

/**
 * @brief Destroys a timer.
 *
 * @important This function will not cancel the timer.
 * If it was started, it will still fire, and the timer's
 * memory will be freed afterward.
 *
 * @param timer Timer to destroy.
 *
 * @return 0 on success, negative value on failure
 */
int margo_timer_destroy(margo_timer_t timer);

#ifdef __cplusplus
}
#endif

#endif /* __MARGO_TIMER_H */
