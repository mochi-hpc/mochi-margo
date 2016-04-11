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

typedef struct margo_timed_element_s
{
    margo_timer_cb_fn cb_fn;
    void *cb_dat;
    double expiration;
    struct margo_timed_element_s *next;
    struct margo_timed_element_s *prev;
} margo_timed_element;

void margo_timer_init(
    void);

void margo_timer_cleanup(
    void);

void margo_thread_sleep(
    double timeout_ms);

void margo_create_timer(
    margo_timer_cb_fn cb_fn,
    void *cb_dat,
    double timeout_ms);

void margo_check_timers(
    void);


#ifdef __cplusplus
}
#endif

#endif /* __MARGO_TIMER */
