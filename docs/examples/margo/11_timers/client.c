#include <assert.h>
#include <stdio.h>
#include <margo.h>
#include <margo-timer.h>
#include <margo-logging.h>

void my_callback(void* uargs) {
    margo_instance_id mid = (margo_instance_id)uargs;
    margo_info(mid, "Callback called");
}

int main(int argc, char** argv)
{
    margo_instance_id mid = margo_init("tcp",MARGO_CLIENT_MODE, 0, 0);
    margo_set_log_level(mid, MARGO_LOG_INFO);

    margo_timer_t timer = MARGO_TIMER_NULL;

    margo_timer_create(mid, my_callback, (void*)mid, &timer);
    margo_info(mid, "Timer created");

    margo_timer_start(timer, 1000);
    margo_info(mid, "Timer submitted");

    margo_thread_sleep(mid, 500);
    margo_info(mid, "This is printed before the callback");

    margo_thread_sleep(mid, 700);
    margo_info(mid, "This is printed after the callback");

    margo_timer_start(timer, 1000);
    margo_info(mid, "Timer resubmitted");

    margo_thread_sleep(mid, 500);

    margo_timer_cancel(timer);
    margo_info(mid, "Timer was cancelled");

    margo_thread_sleep(mid, 700);
    margo_info(mid, "No callback should have been printed");

    margo_timer_destroy(timer);

    margo_finalize(mid);

    return 0;
}
