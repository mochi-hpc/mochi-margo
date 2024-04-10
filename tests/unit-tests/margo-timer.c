/*
 * (C) 2020 The University of Chicago
 *
 * See COPYRIGHT in top-level directory.
 */
#include <stdio.h>
#include <unistd.h>
#include <margo.h>
#include <margo-timer.h>
#include "munit/munit.h"

inline int to_bool(const char* v)
{
    if (strcmp(v, "true") == 0) return 1;
    if (strcmp(v, "false") == 0) return 0;
    return -1;
}

struct test_context {
    margo_instance_id mid;
    int               flag;
};

static void* test_context_setup(const MunitParameter params[], void* user_data)
{
    (void)params;
    (void)user_data;
    struct test_context* ctx = calloc(1, sizeof(*ctx));

    const char* protocol = munit_parameters_get(params, "protocol");

    ctx->mid = margo_init(protocol, MARGO_SERVER_MODE, 0, 0);
    munit_assert_not_null(ctx->mid);

    return ctx;
}

static void test_context_tear_down(void* fixture)
{
    struct test_context* ctx = (struct test_context*)fixture;
    margo_finalize(ctx->mid);
    free(ctx);
}

static void timer_cb(void* data)
{
    struct test_context* ctx = (struct test_context*)data;
    ctx->flag                = 1;
}

static void timer_with_sleep_cb(void* data)
{
    struct test_context* ctx = (struct test_context*)data;
    margo_thread_sleep(ctx->mid, 1000); // sleep for 1 second
    ctx->flag                = 1;
}

static MunitResult test_margo_timer_start(const MunitParameter params[],
                                          void*                data)
{
    (void)params;
    (void)data;
    int           ret;
    margo_timer_t timer = MARGO_TIMER_NULL;

    struct test_context* ctx = (struct test_context*)data;

    ret = margo_timer_create(ctx->mid, timer_cb, data, &timer);
    munit_assert_int(ret, ==, 0);
    munit_assert_not_null(timer);

    ctx->flag = 0;

    ret = margo_timer_start(timer, 500);
    munit_assert_int(ret, ==, 0);

    // second start should fail
    ret = margo_timer_start(timer, 500);
    munit_assert_int(ret, ==, -1);

    margo_thread_sleep(ctx->mid, 1000);

    munit_assert_int(ctx->flag, ==, 1);

    ret = margo_timer_destroy(timer);
    munit_assert_int(ret, ==, 0);

    return MUNIT_OK;
}

static MunitResult test_margo_timer_cancel_before_ult_submitted(const MunitParameter params[],
                                                                void*                data)
{
    (void)params;
    (void)data;
    int           ret;
    margo_timer_t timer = MARGO_TIMER_NULL;

    struct test_context* ctx = (struct test_context*)data;

    ret = margo_timer_create(ctx->mid, timer_cb, data, &timer);
    munit_assert_int(ret, ==, 0);
    munit_assert_not_null(timer);

    ctx->flag = 0;
    // Start the timer with a 500ms timeout
    ret = margo_timer_start(timer, 500);
    munit_assert_int(ret, ==, 0);

    // Sleep for 100ms (the timer won't have submitted its ULT)
    margo_thread_sleep(ctx->mid, 100);

    // Cancel the timer
    ret = margo_timer_cancel(timer);
    munit_assert_int(ret, ==, 0);

    // Wait until after the timer's deadline
    margo_thread_sleep(ctx->mid, 900);

    // Ensure that the callback hasn't run
    munit_assert_int(ctx->flag, ==, 0);

    // Destroy the timer
    ret = margo_timer_destroy(timer);
    munit_assert_int(ret, ==, 0);

    return MUNIT_OK;
}

static MunitResult test_margo_timer_cancel_after_ult_started(const MunitParameter params[],
                                                             void*                data)
{
    (void)params;
    (void)data;
    int           ret;
    margo_timer_t timer = MARGO_TIMER_NULL;

    struct test_context* ctx = (struct test_context*)data;

    ret = margo_timer_create(ctx->mid, timer_with_sleep_cb, data, &timer);
    munit_assert_int(ret, ==, 0);
    munit_assert_not_null(timer);

    ctx->flag = 0;

    // Start the timer with a 100ms timeout
    ret = margo_timer_start(timer, 100);
    munit_assert_int(ret, ==, 0);

    // Sleep 200ms, ensuring that the ULT has been submitted
    margo_thread_sleep(ctx->mid, 200);

    // The ULT takes 1000ms to complete but it's already started so it won't be cancelled
    ret = margo_timer_cancel(timer);
    munit_assert_int(ret, ==, 0);

    // margo_timer_cancel will have waited for the ULT to complete, so no need to sleep
    // and the flag should have been set to 1
    munit_assert_int(ctx->flag, ==, 1);

    ret = margo_timer_destroy(timer);
    munit_assert_int(ret, ==, 0);

    return MUNIT_OK;
}

static void just_sleep(void* arg) {
    (void)arg;
    sleep(1);
}

static MunitResult test_margo_timer_cancel_before_ult_started(const MunitParameter params[],
                                                              void*                data)
{
    (void)params;
    (void)data;
    int           ret;
    margo_timer_t timer = MARGO_TIMER_NULL;

    struct test_context* ctx = (struct test_context*)data;

    // Create a pool that will be associated with an ES only later, so
    // we can submit timers to it but the timer ULTs won't be executed
    // until we want them to.
    ABT_pool pool;
    ret = ABT_pool_create_basic(ABT_POOL_FIFO, ABT_POOL_ACCESS_MPMC, true, &pool);
    munit_assert_int(ret, ==, 0);

    // As the first ULT in this pool, we push a "just_sleep" to give us some
    // time to call margo_timer_cancel from the main ES before the ES that
    // runs the timers ULT start executing them.
    ret = ABT_thread_create(pool, just_sleep, NULL, ABT_THREAD_ATTR_NULL, NULL);
    munit_assert_int(ret, ==, 0);

    // Create a timer that will submit its ULT on the above pool
    ret = margo_timer_create_with_pool(ctx->mid, timer_with_sleep_cb, data, pool, &timer);
    munit_assert_int(ret, ==, 0);
    munit_assert_not_null(timer);

    ctx->flag = 0;

    // Start the timer with a 100ms timeout
    ret = margo_timer_start(timer, 100);
    munit_assert_int(ret, ==, 0);

    // Sleep 200ms, ensuring that the ULT has been submitted to the pool
    margo_thread_sleep(ctx->mid, 200);

    // Create an ES to run the pool's ULTs
    ABT_xstream xstream;
    ret = ABT_xstream_create_basic(ABT_SCHED_BASIC, 1, &pool, ABT_SCHED_CONFIG_NULL, &xstream);
    munit_assert_int(ret, ==, 0);

    // The ULT hasn't had a chance to start when we cancel it
    ret = margo_timer_cancel(timer);
    munit_assert_int(ret, ==, 0);

    // The callback shouldn't have run, flag should be 0
    munit_assert_int(ctx->flag, ==, 0);

    ret = margo_timer_destroy(timer);
    munit_assert_int(ret, ==, 0);

    // Terminate the xstream
    ABT_xstream_join(xstream);
    ABT_xstream_free(&xstream);

    return MUNIT_OK;
}

static MunitResult test_margo_timer_destroy(const MunitParameter params[],
                                            void*                data)
{
    (void)params;
    (void)data;
    int           ret;
    margo_timer_t timer = MARGO_TIMER_NULL;

    struct test_context* ctx = (struct test_context*)data;

    ret = margo_timer_create(ctx->mid, timer_cb, data, &timer);
    munit_assert_int(ret, ==, 0);
    munit_assert_not_null(timer);

    ctx->flag = 0;

    // Start timer with 500ms timeout
    ret = margo_timer_start(timer, 500);
    munit_assert_int(ret, ==, 0);

    // Sleep for 100ms, the timer won't have submitted its ULT yet
    margo_thread_sleep(ctx->mid, 100);

    // Destroy the timer. This won't cancel it.
    ret = margo_timer_destroy(timer);
    munit_assert_int(ret, ==, 0);

    // Sleep long enough for the timer to actually fire.
    margo_thread_sleep(ctx->mid, 900);

    // Ensure the flag is now 1
    munit_assert_int(ctx->flag, ==, 1);

    return MUNIT_OK;
}

static char* protocol_params[] = {"na+sm", NULL};

static MunitParameterEnum test_params[]
    = {{"protocol", protocol_params}, {NULL, NULL}};

static MunitTest test_suite_tests[] = {
    {(char*)"/margo_timer/start", test_margo_timer_start, test_context_setup,
     test_context_tear_down, MUNIT_TEST_OPTION_NONE, test_params},
    {(char*)"/margo_timer/cancel-before-ult-submitted", test_margo_timer_cancel_before_ult_submitted,
     test_context_setup, test_context_tear_down, MUNIT_TEST_OPTION_NONE, test_params},
    {(char*)"/margo_timer/cancel-before-ult-started", test_margo_timer_cancel_after_ult_started,
     test_context_setup, test_context_tear_down, MUNIT_TEST_OPTION_NONE, test_params},
    {(char*)"/margo_timer/cancel-after-ult-started", test_margo_timer_cancel_after_ult_started,
     test_context_setup, test_context_tear_down, MUNIT_TEST_OPTION_NONE, test_params},
    {(char*)"/margo_timer/destroy", test_margo_timer_destroy,
     test_context_setup, test_context_tear_down, MUNIT_TEST_OPTION_NONE,
     test_params},
    {NULL, NULL, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL}};

static const MunitSuite test_suite
    = {(char*)"/margo", test_suite_tests, NULL, 1, MUNIT_SUITE_OPTION_NONE};

int main(int argc, char* argv[MUNIT_ARRAY_PARAM(argc + 1)])
{
    return munit_suite_main(&test_suite, NULL, argc, argv);
}
