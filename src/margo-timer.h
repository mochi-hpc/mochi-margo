/*
 * (C) 2016 The University of Chicago
 * 
 * See COPYRIGHT in top-level directory.
 */

#ifndef __MARGO_TIMER
#define __MARGO_TIMER

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*margo_timer_cb_fn)(void *);

typedef struct margo_timed_element
{
    margo_timer_cb_fn cb_fn;
    void *cb_dat;
    double expiration;
    struct margo_timed_element *next;
    struct margo_timed_element *prev;
} margo_timer_t;

/**
 * Initializes the margo timer interface
 * @param [in] mid Margo instance
 * @returns 0 on success, -1 on failure
 */
int margo_timer_instance_init(
    margo_instance_id mid);

/**
 * Shuts down the margo timer interface
 * @param [in] mid Margo instance
 */
void margo_timer_instance_finalize(
    margo_instance_id mid);

/**
 * Initializes a margo timer object which will perform some action
 * after a specified time duration
 * @param [in] mid Margo instance
 * @param [in] timer pointer to margo timer object to be initialized
 * @param [in] cb_fn callback function for timeout action
 * @param [in] cb_dat callback data passed to the callback function
 * @param [in] timeout_ms timeout duration in milliseconds
 */
void margo_timer_init(
    margo_instance_id mid,
    margo_timer_t *timer,
    margo_timer_cb_fn cb_fn,
    void *cb_dat,
    double timeout_ms);

/**
 * Destroys a margo timer object which was previously initialized
 * @param [in] mid Margo instance
 * @param [in] timer pointer to margo timer object to be destroyed
 */
void margo_timer_destroy(
    margo_instance_id mid,
    margo_timer_t *timer);

/**
 * Checks for expired timers and performs specified timeout action
 * @param [in] mid Margo instance
 */
void margo_check_timers(
    margo_instance_id mid);

#ifdef __cplusplus
}
#endif

#endif /* __MARGO_TIMER */
