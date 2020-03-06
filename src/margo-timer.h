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
 * Creates a margo_timer_list.
 * @returns a new margo_timer_list, or NULL if failed 
 */
struct margo_timer_list* margo_timer_list_create();

/**
 * Frees the timer list
 * @param [in] timer_lst timer list to free
 */
void margo_timer_list_free(margo_instance_id mid, struct margo_timer_list* timer_lst);

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

/**
 * Determines the amount of time (in seconds) until the next timer
 * is set to expire
 * @param [in] mid Margo instance
 * @param [out] time until next timer expiration
 * @returns 0 when there is a queued timer which will expire, -1 otherwise
 */
int margo_timer_get_next_expiration(
    margo_instance_id mid,
    double *next_timer_exp);

/**
 * Gets the margo_timer_list from the margo instance.
 * This function is defined in margo.c.
 */
struct margo_timer_list *margo_get_timer_list(margo_instance_id mid);

#ifdef __cplusplus
}
#endif

#endif /* __MARGO_TIMER */
