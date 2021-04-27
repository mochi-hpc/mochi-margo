
#include <margo.h>
#include "helper-server.h"
#include "munit/munit.h"

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

    char * protocol = "na+sm";
    hg_size_t remote_addr_size = sizeof(ctx->remote_addr);
    ctx->remote_pid = HS_start(protocol, NULL, NULL, NULL, NULL, &(ctx->remote_addr[0]), &remote_addr_size);
    munit_assert_int(ctx->remote_pid, >, 0);

    ctx->mid = margo_init(protocol, MARGO_CLIENT_MODE, 0, 0);
    munit_assert_not_null(ctx->mid);

    return ctx;
}

static void test_context_tear_down(void *data)
{
    struct test_context *ctx = (struct test_context*)data;

    hg_addr_t remote_addr = HG_ADDR_NULL;
    margo_addr_lookup(ctx->mid, ctx->remote_addr, &remote_addr);
    margo_shutdown_remote_instance(ctx->mid, remote_addr);
    margo_addr_free(ctx->mid, remote_addr);

    HS_stop(ctx->remote_pid, 0);
    margo_finalize(ctx->mid);

    free(ctx);
}


static MunitResult diag_stop(const MunitParameter params[], void* data)
{
    struct test_context* ctx = (struct test_context*)data;

    margo_diag_stop(ctx->mid);
    return MUNIT_OK;

}

static MunitResult diag_start(const MunitParameter params[], void* data)
{
    struct test_context* ctx = (struct test_context*)data;

    margo_diag_start(ctx->mid);
    return MUNIT_OK;
}

static MunitResult profile_stop(const MunitParameter params[], void* data)
{
    struct test_context* ctx = (struct test_context*)data;

    margo_profile_stop(ctx->mid);
    return MUNIT_OK;
}

static MunitResult profile_start(const MunitParameter params[], void* data)
{
    struct test_context* ctx = (struct test_context*)data;

    margo_profile_start(ctx->mid);
    return MUNIT_OK;
}


static MunitTest tests[] = {
    { "/diag_stop", diag_stop, test_context_setup, test_context_tear_down, MUNIT_TEST_OPTION_NONE, NULL},
    { "/diag_start", diag_start, test_context_setup, test_context_tear_down, MUNIT_TEST_OPTION_NONE, NULL},
    { "/profile_start", profile_start, test_context_setup, test_context_tear_down, MUNIT_TEST_OPTION_NONE, NULL},
    { "/profile_stop", profile_stop, test_context_setup, test_context_tear_down, MUNIT_TEST_OPTION_NONE, NULL},
    { NULL, NULL, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL }
};

static const MunitSuite test_suite = {
    "/margo", tests, NULL, 1, MUNIT_SUITE_OPTION_NONE
};


int main(int argc, char **argv)
{
    return munit_suite_main(&test_suite, NULL, argc, argv);
}
