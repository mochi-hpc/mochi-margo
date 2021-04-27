/*
 * (C) 2020 The University of Chicago
 *
 * See COPYRIGHT in top-level directory.
 */
#include <stdio.h>
#include <margo.h>
#include "helper-server.h"
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
    int               remote_pid;
    char              remote_addr[256];
};

static void* test_context_setup(const MunitParameter params[], void* user_data)
{
    (void) params;
    (void) user_data;
    struct test_context* ctx = calloc(1, sizeof(*ctx));

    const char* protocol = munit_parameters_get(params, "protocol");
    hg_size_t remote_addr_size = 256;
    ctx->remote_pid = HS_start(protocol, NULL, NULL, NULL, NULL, &(ctx->remote_addr[0]), &remote_addr_size);
    munit_assert_int(ctx->remote_pid, >, 0);

    ctx->mid = margo_init(protocol, MARGO_SERVER_MODE, 0, 0);
    munit_assert_not_null(ctx->mid);

    return ctx;
}

static void test_context_tear_down(void* fixture)
{
    struct test_context* ctx = (struct test_context*)fixture;

    hg_addr_t remote_addr = HG_ADDR_NULL;
    margo_addr_lookup(ctx->mid, ctx->remote_addr, &remote_addr);
    margo_shutdown_remote_instance(ctx->mid, remote_addr);
    margo_addr_free(ctx->mid, remote_addr);

    HS_stop(ctx->remote_pid, 0);
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

    /* margo_addr_self should work */
    hret = margo_addr_self(ctx->mid, &addr);
    munit_assert_int(hret, ==, HG_SUCCESS);
    munit_assert_not_null(addr);

    /* should be able to free the returned address */
    hret = margo_addr_free(ctx->mid, addr);
    munit_assert_int(hret, ==, HG_SUCCESS);

    /* passing NULL as out param should not crash */
    hret = margo_addr_self(ctx->mid, NULL);
    munit_assert_int(hret, ==, HG_INVALID_ARG);

    return MUNIT_OK;
}

static MunitResult test_margo_addr_free(const MunitParameter params[], void* data)
{
    (void)params;
    (void)data;
    hg_return_t hret;
    hg_addr_t addr = HG_ADDR_NULL;

    struct test_context* ctx = (struct test_context*)data;

    hret = margo_addr_self(ctx->mid, &addr);
    munit_assert_int(hret, ==, HG_SUCCESS);
    munit_assert_not_null(addr);

    /* should be able to free the returned address */
    hret = margo_addr_free(ctx->mid, addr);
    munit_assert_int(hret, ==, HG_SUCCESS);

    /* passing NULL as out param should not crash */
    hret = margo_addr_free(ctx->mid, HG_ADDR_NULL);
    munit_assert_int(hret, ==, HG_SUCCESS);

    return MUNIT_OK;
}

static MunitResult test_margo_addr_dup(const MunitParameter params[], void* data)
{
    (void)params;
    (void)data;
    hg_return_t hret;
    hg_addr_t addr = HG_ADDR_NULL;
    hg_addr_t addr_cpy = HG_ADDR_NULL;

    struct test_context* ctx = (struct test_context*)data;

    hret = margo_addr_self(ctx->mid, &addr);
    munit_assert_int(hret, ==, HG_SUCCESS);
    munit_assert_not_null(addr);

    /* we can duplicate the address */
    hret = margo_addr_dup(ctx->mid, addr, &addr_cpy);
    munit_assert_int(hret, ==, HG_SUCCESS);
    munit_assert_not_null(addr);

    hret = margo_addr_free(ctx->mid, addr_cpy);
    munit_assert_int(hret, ==, HG_SUCCESS);

    /* passing NULL as out param should not crash */
    hret = margo_addr_dup(ctx->mid, addr, NULL);
    munit_assert_int(hret, ==, HG_INVALID_ARG);

    /* passing HG_ADDR_NULL as param should not crash */
    hret = margo_addr_dup(ctx->mid, HG_ADDR_NULL, &addr_cpy);
    munit_assert_int(hret, ==, HG_INVALID_ARG);

    hret = margo_addr_free(ctx->mid, addr);
    munit_assert_int(hret, ==, HG_SUCCESS);

    return MUNIT_OK;
}

static MunitResult test_margo_addr_cmp(const MunitParameter params[], void* data)
{
    (void)params;
    (void)data;
    hg_return_t hret;
    hg_addr_t addr = HG_ADDR_NULL;
    hg_addr_t addr_cpy = HG_ADDR_NULL;

    struct test_context* ctx = (struct test_context*)data;

    hret = margo_addr_self(ctx->mid, &addr);
    munit_assert_int(hret, ==, HG_SUCCESS);
    munit_assert_not_null(addr);

    hret = margo_addr_dup(ctx->mid, addr, &addr_cpy);
    munit_assert_int(hret, ==, HG_SUCCESS);
    munit_assert_not_null(addr);

    /* compare the two addresses */
    hg_bool_t b = margo_addr_cmp(ctx->mid, addr, addr_cpy);
    munit_assert_int(b, ==, HG_TRUE);

    /* compare with HG_ADDR_NULL */
    b = margo_addr_cmp(ctx->mid, addr, HG_ADDR_NULL);
    munit_assert_int(b, ==, HG_FALSE);

    /* compare with address of remote server */
    hg_addr_t remote_addr;
    margo_addr_lookup(ctx->mid, ctx->remote_addr, &remote_addr);
    b = margo_addr_cmp(ctx->mid, addr, remote_addr);
    munit_assert_int(b, ==, HG_FALSE);
    hret = margo_addr_free(ctx->mid, remote_addr);
    munit_assert_int(hret, ==, HG_SUCCESS);

    hret = margo_addr_free(ctx->mid, addr_cpy);
    munit_assert_int(hret, ==, HG_SUCCESS);

    hret = margo_addr_free(ctx->mid, addr);
    munit_assert_int(hret, ==, HG_SUCCESS);

    return MUNIT_OK;
}

static MunitResult test_margo_addr_to_string(const MunitParameter params[], void* data)
{
    (void)params;
    (void)data;
    hg_return_t hret;
    hg_addr_t addr = HG_ADDR_NULL;

    struct test_context* ctx = (struct test_context*)data;

    hret = margo_addr_self(ctx->mid, &addr);
    munit_assert_int(hret, ==, HG_SUCCESS);
    munit_assert_not_null(addr);

    char addr_str[256];
    hg_size_t addr_str_size = 256;
    memset(addr_str, 0, 256);

    hret = margo_addr_to_string(ctx->mid, addr_str, &addr_str_size, addr);
    munit_assert_int(hret, ==, HG_SUCCESS);
    munit_assert_string_not_equal(addr_str, "\0");

    hret = margo_addr_free(ctx->mid, addr);
    munit_assert_int(hret, ==, HG_SUCCESS);

    addr_str_size = 256;
    memset(addr_str, 0, 256);
    hret = margo_addr_to_string(ctx->mid, addr_str, &addr_str_size, HG_ADDR_NULL);
    munit_assert_int(hret, ==, HG_INVALID_ARG);

    return MUNIT_OK;
}

static MunitResult test_margo_addr_lookup(const MunitParameter params[], void* data)
{
    (void)params;
    (void)data;
    hg_return_t hret;
    hg_addr_t self_addr = HG_ADDR_NULL;
    hg_addr_t lkup_addr = HG_ADDR_NULL;

    struct test_context* ctx = (struct test_context*)data;

    hret = margo_addr_self(ctx->mid, &self_addr);
    munit_assert_int(hret, ==, HG_SUCCESS);
    munit_assert_not_null(self_addr);

    char addr_str[256];
    hg_size_t addr_str_size = 256;
    memset(addr_str, 0, 256);

    hret = margo_addr_to_string(ctx->mid, addr_str, &addr_str_size, self_addr);
    munit_assert_int(hret, ==, HG_SUCCESS);
    munit_assert_string_not_equal(addr_str, "\0");

    /* lookup self address */
    hret = margo_addr_lookup(ctx->mid, addr_str, &lkup_addr);
    munit_assert_int(hret, ==, HG_SUCCESS);

    hret = margo_addr_free(ctx->mid, lkup_addr);
    munit_assert_int(hret, ==, HG_SUCCESS);

    /* lookup remote address */
    hret = margo_addr_lookup(ctx->mid, ctx->remote_addr, &lkup_addr);
    munit_assert_int(hret, ==, HG_SUCCESS);

    hret = margo_addr_free(ctx->mid, lkup_addr);
    munit_assert_int(hret, ==, HG_SUCCESS);

    /* should not crash if we lookup an invalid address */
    // XXX for now this test doesn't pass because of a problem with na+sm
    //hret = margo_addr_lookup(ctx->mid, "dummy", &lkup_addr);
    //munit_assert_int(hret, ==, HG_INVALID_ARG);

    /* should not crash if we pass NULL as string address */
    hret = margo_addr_lookup(ctx->mid, NULL, &lkup_addr);
    munit_assert_int(hret, ==, HG_INVALID_ARG);

    /* should not crash if we pass NULL as result */
    hret = margo_addr_lookup(ctx->mid, addr_str, NULL);
    munit_assert_int(hret, ==, HG_INVALID_ARG);

    hret = margo_addr_free(ctx->mid, self_addr);
    munit_assert_int(hret, ==, HG_SUCCESS);

    return MUNIT_OK;
}

static char* protocol_params[] = {
    "na+sm", "ofi+tcp", NULL
};

static MunitParameterEnum test_params[] = {
    { "protocol", protocol_params },
    { NULL, NULL }
};

static MunitTest test_suite_tests[] = {
    { (char*) "/margo_addr_self", test_margo_addr_self,
        test_context_setup, test_context_tear_down, MUNIT_TEST_OPTION_NONE, test_params },
    { (char*) "/margo_addr_free", test_margo_addr_free,
        test_context_setup, test_context_tear_down, MUNIT_TEST_OPTION_NONE, test_params },
    { (char*) "/margo_addr_dup", test_margo_addr_dup,
        test_context_setup, test_context_tear_down, MUNIT_TEST_OPTION_NONE, test_params },
    { (char*) "/margo_addr_cmp", test_margo_addr_cmp,
        test_context_setup, test_context_tear_down, MUNIT_TEST_OPTION_NONE, test_params },
    { (char*) "/margo_addr_to_string", test_margo_addr_to_string,
        test_context_setup, test_context_tear_down, MUNIT_TEST_OPTION_NONE, test_params },
    { (char*) "/margo_addr_lookup", test_margo_addr_lookup,
        test_context_setup, test_context_tear_down, MUNIT_TEST_OPTION_NONE, test_params },
    { NULL, NULL, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL }
};

static const MunitSuite test_suite = {
    (char*) "/margo", test_suite_tests, NULL, 1, MUNIT_SUITE_OPTION_NONE
};

int main(int argc, char* argv[MUNIT_ARRAY_PARAM(argc + 1)]) {
    return munit_suite_main(&test_suite, NULL, argc, argv);
}
