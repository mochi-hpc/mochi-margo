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

typedef void* margo_timer_handle;

typedef struct margo_timed_element_s
{
    margo_timer_cb_fn cb_fn;
    void *cb_dat;
    double expiration;
    struct margo_timed_element_s *next;
    struct margo_timed_element_s *prev;
} margo_timed_element;

/**
 * Initializes margo's timer interface
 */
void margo_timer_init(
    void);

/**
 * Cleans up margo timer data structures
 */
void margo_timer_cleanup(
    void);

/**
 * Suspends the calling ULT for a specified time duration
 * @param [in] timeout_ms timeout duration in milliseconds
 */
void margo_thread_sleep(
    double timeout_ms);

/**
 * Creates a margo timer object to perform some action after a
 * specified time duration
 * @param [in] cb_fn callback function for timeout action
 * @param [in] cb_dat callback data passed to the callback function
 * @param [in] timeout_ms timeout duration in milliseconds
 * @param [out] handle handle used to reference the created timer object
 */
void margo_timer_create(
    margo_timer_cb_fn cb_fn,
    void *cb_dat,
    double timeout_ms,
    margo_timer_handle *handle);

/**
 * Frees resources used by the referenced timer object
 * @param [in] handle handle of timer object to free
 */
void margo_timer_free(
    margo_timer_handle handle);

/**
 * Checks for expired timers and performs specified timeout action
 */
void margo_check_timers(
    void);


#ifdef __cplusplus
}
#endif

#endif /* __MARGO_TIMER */
