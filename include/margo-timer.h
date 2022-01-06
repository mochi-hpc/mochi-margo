/*
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

typedef void (*margo_timer_callback_fn)(void*);
typedef struct margo_timer* margo_timer_t;
#define MARGO_TIMER_NULL ((margo_timer_t)NULL)

/**
 * @brief Creates a timer object.
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
 * @brief Start the timer with a provided timeout.
 *
 * @param margo_timer_t Timer object to start
 * @param timeout_ms Timeout
 *
 * @return 0 on success, negative value on failure
 */
int margo_timer_start(margo_timer_t timer, double timeout_ms);

/**
 * @brief Cancel a started timer.
 *
 * @param timer Timer to cancel
 *
 * @return 0 on success, negative value on failure
 */
int margo_timer_cancel(margo_timer_t timer);

/**
 * @brief Destroys a timer. If the timer was started,
 * this function will also cancel it.
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
