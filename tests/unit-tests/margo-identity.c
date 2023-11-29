
#include <margo.h>
#include "helper-server.h"
#include "munit/munit.h"

struct test_context {
    margo_instance_id mid;
    hg_addr_t         address;
};

static void* test_context_setup(const MunitParameter params[], void* user_data)
{
    (void) params;
    (void) user_data;
    struct test_context* ctx = calloc(1, sizeof(*ctx));
    ctx->mid = margo_init("na+sm", MARGO_SERVER_MODE, 0, 0);
    munit_assert_not_null(ctx->mid);

    margo_addr_self(ctx->mid, &ctx->address);

    return ctx;
}

static void test_context_tear_down(void *data)
{
    struct test_context *ctx = (struct test_context*)data;
    margo_addr_free(ctx->mid, ctx->address);
    margo_finalize(ctx->mid);
    free(ctx);
}

static MunitResult test_identity(const MunitParameter params[], void* data)
{
    struct test_context* ctx = (struct test_context*)data;
    hg_return_t hret;

    const char* identity = margo_provider_registered_identity(ctx->mid, 42);
    munit_assert_null(identity);

    char buffer[256];
    size_t bufsize = 256;

    hret = margo_provider_get_identity(ctx->mid, ctx->address, 42, NULL, &bufsize);
    munit_assert_int(hret, ==, HG_INVALID_ARG);

    hret = margo_provider_get_identity(ctx->mid, ctx->address, 42, buffer, NULL);
    munit_assert_int(hret, ==, HG_INVALID_ARG);

    hret = margo_provider_get_identity(ctx->mid, ctx->address, 42, buffer, &bufsize);
    munit_assert_int(hret, ==, HG_NOENTRY);

    hret = margo_provider_register_identity(ctx->mid, 42, "something");
    munit_assert_int(hret, ==, HG_SUCCESS);

    hret = margo_provider_get_identity(ctx->mid, ctx->address, 42, buffer, &bufsize);
    munit_assert_int(hret, ==, HG_SUCCESS);
    munit_assert_string_equal(buffer, "something");
    munit_assert_size(bufsize, == , strlen("something")+1);

    identity = margo_provider_registered_identity(ctx->mid, 42);
    munit_assert_not_null(identity);
    munit_assert_string_equal(identity, "something");

    bufsize = 4;
    hret = margo_provider_get_identity(ctx->mid, ctx->address, 42, buffer, &bufsize);
    munit_assert_int(hret, ==, HG_NOMEM);
    munit_assert_size(bufsize, == , strlen("something")+1);

    hret = margo_provider_deregister_identity(ctx->mid, 42);
    munit_assert_int(hret, ==, HG_SUCCESS);

    identity = margo_provider_registered_identity(ctx->mid, 42);
    munit_assert_null(identity);

    bufsize = 256;
    hret = margo_provider_get_identity(ctx->mid, ctx->address, 42, buffer, &bufsize);
    munit_assert_int(hret, ==, HG_NOENTRY);

    return MUNIT_OK;
}

static MunitParameterEnum test_params[] = {
    { NULL, NULL }
};

static MunitTest tests[] = {
    { "/identity", test_identity, test_context_setup, test_context_tear_down, MUNIT_TEST_OPTION_NONE, test_params},
    { NULL, NULL, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL }
};

static const MunitSuite test_suite = {
    "/margo", tests, NULL, 1, MUNIT_SUITE_OPTION_NONE
};


int main(int argc, char **argv)
{
    return munit_suite_main(&test_suite, NULL, argc, argv);
}
