/*
 * (C) 2020 The University of Chicago
 *
 * See COPYRIGHT in top-level directory.
 */
#include <stdio.h>
#include <margo.h>
#include "munit/munit.h"

inline int to_bool(const char* v) {
    if(strcmp(v, "true") == 0)
        return 1;
    if(strcmp(v, "false") == 0)
        return 0;
    return -1;
}

struct test_context {
    margo_instance_id mid;
};

static void* test_context_setup(const MunitParameter params[], void* user_data)
{
    (void) params;
    (void) user_data;
    struct test_context* ctx = calloc(1, sizeof(*ctx));

    const char* protocol = munit_parameters_get(params, "protocol");
    int use_progress_thread = to_bool(munit_parameters_get(params, "use_progress_thread"));
    int num_rpc_threads = atoi(munit_parameters_get(params, "num_rpc_threads"));


    ctx->mid = margo_init(protocol, MARGO_SERVER_MODE, use_progress_thread, num_rpc_threads);
    munit_assert_not_null(ctx->mid);
    return ctx;
}

static void test_context_tear_down(void* fixture)
{
    struct test_context* ctx = (struct test_context*)fixture;
    margo_finalize(ctx->mid);
    free(ctx);
}

static MunitResult test_margo_addr_self(const MunitParameter params[], void* data)
{
    (void)params;
    (void)data;
    hg_return_t hret;
    hg_addr_t addr = HG_ADDR_NULL;

    struct test_context* ctx = (struct test_context*)data;

    hret = margo_addr_self(ctx->mid, &addr);
    munit_assert_int(hret, ==, HG_SUCCESS);
    munit_assert_not_null(addr);

    hret = margo_addr_free(ctx->mid, addr);
    munit_assert_int(hret, ==, HG_SUCCESS);

    return MUNIT_OK;
}

static char* protocol_params[] = {
    "na+sm", "ofi+tcp", NULL
};

static char* use_progress_thread_params[] = {
    "true", "false", NULL
};

static char* num_rpc_threads_params[] = {
    "-1", "0", "1", "2", NULL
};

static MunitParameterEnum test_params[] = {
    { "protocol",            protocol_params },
    { "use_progress_thread", use_progress_thread_params },
    { "num_rpc_threads",     num_rpc_threads_params },
    { NULL, NULL }
};

static MunitTest test_suite_tests[] = {
    { (char*) "/margo_addr_self", test_margo_addr_self, test_context_setup, test_context_tear_down, MUNIT_TEST_OPTION_NONE, test_params },
    { NULL, NULL, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL }
};

static const MunitSuite test_suite = {
    (char*) "/margo", test_suite_tests, NULL, 1, MUNIT_SUITE_OPTION_NONE
};

int main(int argc, char* argv[MUNIT_ARRAY_PARAM(argc + 1)]) {
    return munit_suite_main(&test_suite, NULL, argc, argv);
}
