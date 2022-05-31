/*
 * (C) 2022 The University of Chicago
 *
 * See COPYRIGHT in top-level directory.
 */
#include <stdio.h>
#include <margo.h>
#include "munit/munit.h"

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

#if (HG_VERSION_MAJOR > 2) || (HG_VERSION_MAJOR == 2 && HG_VERSION_MINOR > 1) \
    || (HG_VERSION_MAJOR == 2 && HG_VERSION_MINOR == 1                        \
        && HG_VERSION_PATCH > 0)
static MunitResult test_margo_bulk_create_attr(const MunitParameter params[],
                                               void*                data)
{
    (void)params;
    (void)data;
    int          ret;
    hg_size_t    size;
    void*        buffer;
    hg_bulk_t    bulk_handle;
    struct hg_bulk_attr bulk_attr;

    struct test_context* ctx = (struct test_context*)data;

    size   = 512;
    buffer = calloc(1, 512);
    munit_assert_not_null(buffer);
    bulk_attr.mem_type = NA_MEM_TYPE_HOST;

    ret = margo_bulk_create_attr(ctx->mid, 1, &buffer, &size, HG_BULK_READWRITE,
                                 &bulk_attr, &bulk_handle);
    munit_assert_int(ret, ==, 0);

    ret = margo_bulk_free(bulk_handle);
    munit_assert_int(ret, ==, 0);

    free(buffer);

    return MUNIT_OK;
}
#endif

static MunitResult test_margo_bulk_create(const MunitParameter params[],
                                          void*                data)
{
    (void)params;
    (void)data;
    int       ret;
    hg_size_t size;
    void*     buffer;
    hg_bulk_t bulk_handle;

    struct test_context* ctx = (struct test_context*)data;

    size   = 512;
    buffer = calloc(1, 512);
    munit_assert_not_null(buffer);

    ret = margo_bulk_create(ctx->mid, 1, &buffer, &size, HG_BULK_READWRITE,
                            &bulk_handle);
    munit_assert_int(ret, ==, 0);

    ret = margo_bulk_free(bulk_handle);
    munit_assert_int(ret, ==, 0);

    free(buffer);

    return MUNIT_OK;
}

static char* protocol_params[] = {"na+sm", NULL};

static MunitParameterEnum test_params[]
    = {{"protocol", protocol_params}, {NULL, NULL}};

static MunitTest test_suite_tests[]
    = {{(char*)"/margo_bulk/bulk_create", test_margo_bulk_create,
        test_context_setup, test_context_tear_down, MUNIT_TEST_OPTION_NONE,
        test_params},
#if (HG_VERSION_MAJOR > 2) || (HG_VERSION_MAJOR == 2 && HG_VERSION_MINOR > 1) \
    || (HG_VERSION_MAJOR == 2 && HG_VERSION_MINOR == 1                        \
        && HG_VERSION_PATCH > 0)
       {(char*)"/margo_bulk/bulk_create_attr", test_margo_bulk_create_attr,
        test_context_setup, test_context_tear_down, MUNIT_TEST_OPTION_NONE,
        test_params},
#endif
       {NULL, NULL, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL}};

static const MunitSuite test_suite
    = {(char*)"/margo", test_suite_tests, NULL, 1, MUNIT_SUITE_OPTION_NONE};

int main(int argc, char* argv[MUNIT_ARRAY_PARAM(argc + 1)])
{
    return munit_suite_main(&test_suite, NULL, argc, argv);
}
