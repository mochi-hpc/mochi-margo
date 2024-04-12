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
 * has already been submitted as a ULT, this function will
 * block until the ULT has executed. If the ULT hasn't started
 * yet when margo_timer_cancel is called, the ULT won't run
 * the callback and will simply return. If the ULT had started,
 * it will run the callback to completion.
 *
 * This function guarantees that there won't be any invokation
 * of the callback after margo_timer_cancel returns.
 *
 * @param timer Timer to cancel
 *
 * @return 0 on success, negative value on failure
 */
int margo_timer_cancel(margo_timer_t timer);

/**
 * @brief Cancel n timers, blocking until it can ensure that
 * no callback associated with any of the timers will be called.
 *
 * This function is more efficient than calling margo_timer_cancel
 * in a loop because it requests the cancelation of all the timers
 * before blocking.
 *
 * @warning All the timers must be associated with the same margo
 * instance.
 *
 * @param n Number of timers
 * @param timer Array of timers to cancel
 *
 * @return 0 on success, negative value on failure
 */
int margo_timer_cancel_many(size_t n, margo_timer_t* timers);

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
