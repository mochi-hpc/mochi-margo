/*
 * (C) 2020 The University of Chicago
 *
 * See COPYRIGHT in top-level directory.
 */

/* The purpose of these tests are to check the behavior of the Argobots
 * scheduler in conjunction with Margo in various scenarios
 */

#include <stdio.h>
#include <margo.h>
#include "helper-server.h"
#include "munit/munit.h"

struct test_context {
    margo_instance_id mid;
};

static void* test_context_setup(const MunitParameter params[], void* user_data)
{
    (void) params;
    (void) user_data;
    struct test_context* ctx = calloc(1, sizeof(*ctx));

    char* protocol = "na+sm";

    ctx->mid = margo_init(protocol, MARGO_CLIENT_MODE, 0, 0);
    munit_assert_not_null(ctx->mid);

    return ctx;
}

static void test_context_tear_down(void* fixture)
{
    struct test_context* ctx = (struct test_context*)fixture;

    margo_finalize(ctx->mid);

    free(ctx);
}

static MunitResult test_abt_mutex_cpu(const MunitParameter params[], void* data)
{
    (void)params;
    (void)data;

    struct test_context* ctx = (struct test_context*)data;

    /* TODO: do something */
    return MUNIT_OK;
}

static MunitTest test_suite_tests[] = {
    { (char*) "/abt_mutex_cpu", test_abt_mutex_cpu,
        test_context_setup, test_context_tear_down, MUNIT_TEST_OPTION_NONE, NULL },
    { NULL, NULL, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL }
};

static const MunitSuite test_suite = {
    (char*) "/margo", test_suite_tests, NULL, 1, MUNIT_SUITE_OPTION_NONE
};

int main(int argc, char* argv[MUNIT_ARRAY_PARAM(argc + 1)]) {
    return munit_suite_main(&test_suite, NULL, argc, argv);
}
