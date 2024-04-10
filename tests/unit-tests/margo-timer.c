/*
 * (C) 2020 The University of Chicago
 *
 * See COPYRIGHT in top-level directory.
 */
#include <stdio.h>
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

static MunitResult test_margo_timer_cancel(const MunitParameter params[],
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

    margo_thread_sleep(ctx->mid, 100);

    ret = margo_timer_cancel(timer);
    munit_assert_int(ret, ==, 0);

    margo_thread_sleep(ctx->mid, 900);

    munit_assert_int(ctx->flag, ==, 0);

    ret = margo_timer_destroy(timer);
    munit_assert_int(ret, ==, 0);

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

    ret = margo_timer_start(timer, 500);
    munit_assert_int(ret, ==, 0);

    margo_thread_sleep(ctx->mid, 100);

    ret = margo_timer_destroy(timer);
    munit_assert_int(ret, ==, 0);

    margo_thread_sleep(ctx->mid, 900);

    munit_assert_int(ctx->flag, ==, 0);

    return MUNIT_OK;
}

static MunitResult test_margo_timer_wait_pending(const MunitParameter params[],
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

    // timer will fire in 500 ms
    ret = margo_timer_start(timer, 500);
    munit_assert_int(ret, ==, 0);

    // wait 600 ms, the timer will have fired and a ULT will have been created
    margo_thread_sleep(ctx->mid, 600);

    // wait for pending ULT
    ret = margo_timer_wait_pending(timer);
    munit_assert_int(ret, ==, 0);

    ret = margo_timer_destroy(timer);
    munit_assert_int(ret, ==, 0);

    munit_assert_int(ctx->flag, ==, 1);

    return MUNIT_OK;
}

static char* protocol_params[] = {"na+sm", NULL};

static MunitParameterEnum test_params[]
    = {{"protocol", protocol_params}, {NULL, NULL}};

static MunitTest test_suite_tests[] = {
    {(char*)"/margo_timer/start", test_margo_timer_start, test_context_setup,
     test_context_tear_down, MUNIT_TEST_OPTION_NONE, test_params},
    {(char*)"/margo_timer/cancel", test_margo_timer_cancel, test_context_setup,
     test_context_tear_down, MUNIT_TEST_OPTION_NONE, test_params},
    {(char*)"/margo_timer/destroy", test_margo_timer_destroy,
     test_context_setup, test_context_tear_down, MUNIT_TEST_OPTION_NONE,
     test_params},
    {(char*)"/margo_timer/wait_pending", test_margo_timer_wait_pending,
     test_context_setup, test_context_tear_down, MUNIT_TEST_OPTION_NONE,
     test_params},
    {NULL, NULL, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL}};

static const MunitSuite test_suite
    = {(char*)"/margo", test_suite_tests, NULL, 1, MUNIT_SUITE_OPTION_NONE};

int main(int argc, char* argv[MUNIT_ARRAY_PARAM(argc + 1)])
{
    return munit_suite_main(&test_suite, NULL, argc, argv);
}
