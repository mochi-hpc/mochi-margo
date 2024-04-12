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

struct margo_timer_list;

/**
 * Creates and initializes the margo_timer_list associated with the
 * margo instance.
 */
struct margo_timer_list* __margo_timer_list_create();

/**
 * Frees the timer list
 * @param [in] timer_lst timer list to free
 */
void __margo_timer_list_free(margo_instance_id mid);

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

#ifdef __cplusplus
}
#endif

#endif /* __MARGO_TIMER */
