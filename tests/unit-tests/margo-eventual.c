
#include <string.h>
#include <margo.h>
#include "helper-server.h"
#include "munit/munit.h"

/* the intent of these unit tests is to verify correct operation of the
 * margo eventual constructs in different configurations
 */

struct test_context {
    margo_instance_id mid;
};

static void* test_context_setup(const MunitParameter params[], void* user_data)
{
    (void) params;
    (void) user_data;
    struct test_context* ctx = calloc(1, sizeof(*ctx));

    return ctx;
}

static void test_context_tear_down(void *data)
{
    struct test_context *ctx = (struct test_context*)data;

    free(ctx);
}

static MunitResult margo_eventual(const MunitParameter params[], void* data)
{
    const char * protocol = "na+sm";
    struct margo_init_info mii = {0};
    struct test_context* ctx = (struct test_context*)data;

    mii.json_config = munit_parameters_get(params, "json");

    ctx->mid = margo_init_ext(protocol, MARGO_SERVER_MODE, &mii);
    munit_assert_not_null(ctx->mid);

    margo_finalize(ctx->mid);

    return MUNIT_OK;
}

static char * json_params[] = {
    "{}", /* no dedicated rpc pool */
    "{\"rpc_thread_count\":2}", /* 2 ESes for RPCs */
    "{\"rpc_thread_count\":4}", /* 4 ESes for RPCs */
    "{\"rpc_thread_count\":8}", /* 8 ESes for RPCs */
    NULL
};

static MunitParameterEnum margo_eventual_params[] = {
    { "json", json_params},
    {NULL, NULL}
};

static MunitTest tests[] = {
    { "/rpc-pool-kind", margo_eventual, test_context_setup, test_context_tear_down, MUNIT_TEST_OPTION_NONE, margo_eventual_params},
    { NULL, NULL, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL }
};

static const MunitSuite test_suite = {
    "/margo", tests, NULL, 1, MUNIT_SUITE_OPTION_NONE
};


int main(int argc, char **argv)
{
    return munit_suite_main(&test_suite, NULL, argc, argv);
}
