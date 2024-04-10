/*
 * (C) 2016 The University of Chicago
 *
 * See COPYRIGHT in top-level directory.
 */

#ifndef __MARGO_TIMER
#define __MARGO_TIMER

#include "margo-timer.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct margo_timer {
    margo_instance_id       mid;
    margo_timer_callback_fn cb_fn;
    void*                   cb_dat;
    ABT_pool                pool;

    ABT_mutex_memory mtx_mem;
    ABT_cond_memory  cv_mem;
    size_t           num_pending;
    _Atomic bool     cancelled;
    _Atomic bool     destroy_requested;

    double              expiration;
    struct margo_timer* next;
    struct margo_timer* prev;
} margo_timer;

/**
 * Creates a margo_timer_list.
 * @returns a new margo_timer_list, or NULL if failed
 */
struct margo_timer_list* __margo_timer_list_create();

/**
 * Frees the timer list
 * @param [in] timer_lst timer list to free
 */
void __margo_timer_list_free(margo_instance_id        mid,
                             struct margo_timer_list* timer_lst);

/**
 * Initializes a margo timer object which will perform some action
 * after a specified time duration
 * @param [in] mid Margo instance
 * @param [in] timer pointer to margo timer object to be initialized
 * @param [in] cb_fn callback function for timeout action
 * @param [in] cb_dat callback data passed to the callback function
 * @param [in] timeout_ms timeout duration in milliseconds
 */
void __margo_timer_init(margo_instance_id       mid,
                        margo_timer*            timer,
                        margo_timer_callback_fn cb_fn,
                        void*                   cb_dat,
                        double                  timeout_ms);

/**
 * Destroys a margo timer object which was previously initialized
 * @param [in] mid Margo instance
 * @param [in] timer pointer to margo timer object to be destroyed
 */
void __margo_timer_destroy(margo_instance_id mid, margo_timer* timer);

/**
 * Checks for expired timers and performs specified timeout action
 * @param [in] mid Margo instance
 */
void __margo_check_timers(margo_instance_id mid);

/**
 * Determines the amount of time (in seconds) until the next timer
 * is set to expire
 * @param [in] mid Margo instance
 * @param [out] time until next timer expiration
 * @returns 0 when there is a queued timer which will expire, -1 otherwise
 */
int __margo_timer_get_next_expiration(margo_instance_id mid,
                                      double*           next_timer_exp);

/**
 * Gets the margo_timer_list from the margo instance.
 */
struct margo_timer_list* __margo_get_timer_list(margo_instance_id mid);

#ifdef __cplusplus
}
#endif

#endif /* __MARGO_TIMER */
