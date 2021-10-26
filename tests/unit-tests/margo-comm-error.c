/*
 * (C) 2020 The University of Chicago
 *
 * See COPYRIGHT in top-level directory.
 */
#include <stdio.h>
#include <margo.h>
#include "helper-server.h"
#include "munit/munit.h"

static hg_id_t null_rpc_id;

DECLARE_MARGO_RPC_HANDLER(null_rpc_ult)

static void null_rpc_ult(hg_handle_t handle)
{
    hg_return_t       hret;

    hret = margo_respond(handle, NULL);
    munit_assert_int(hret, ==, HG_SUCCESS);
    margo_destroy(handle);

    return;
}
DEFINE_MARGO_RPC_HANDLER(null_rpc_ult)

static int svr_init_fn(margo_instance_id mid, void* arg)
{
    null_rpc_id  = MARGO_REGISTER(mid, "null_rpc", void, void, null_rpc_ult);

    return(0);
}

/* The purpose of this unit test is to check error locations and codes for
 * particular communication failures
 */

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
    ctx->remote_pid = HS_start(protocol, NULL, svr_init_fn, NULL, NULL, &(ctx->remote_addr[0]), &remote_addr_size);
    munit_assert_int(ctx->remote_pid, >, 0);

    ctx->mid = margo_init(protocol, MARGO_CLIENT_MODE, 0, 0);
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

static MunitResult test_comm_reachable(const MunitParameter params[], void* data)
{
    (void)params;
    (void)data;
    hg_return_t hret;
    hg_handle_t handle;
    hg_addr_t addr;

    struct test_context* ctx = (struct test_context*)data;

    null_rpc_id  = MARGO_REGISTER(ctx->mid, "null_rpc", void, void, NULL);

    /* should succeed b/c addr is properly formatted */
    hret = margo_addr_lookup(ctx->mid, ctx->remote_addr, &addr);
    munit_assert_int(hret, ==, HG_SUCCESS);

    hret = margo_create(ctx->mid, addr, null_rpc_id, &handle);
    munit_assert_int(hret, ==, HG_SUCCESS);

    /* attempt to send rpc to addr, should succeed */
    hret = margo_forward_timed(handle, NULL, 2000.0);
    munit_assert_int(hret, ==, HG_SUCCESS);

    margo_destroy(handle);

    hret = margo_addr_free(ctx->mid, addr);

    munit_assert_int(hret, ==, HG_SUCCESS);

    return MUNIT_OK;
}


static MunitResult test_comm_unreachable(const MunitParameter params[], void* data)
{
    (void)params;
    (void)data;
    hg_return_t hret;
    hg_addr_t addr = HG_ADDR_NULL;
    hg_handle_t handle;

    struct test_context* ctx = (struct test_context*)data;

    const char* str_addr = munit_parameters_get(params, "addr_unreachable");

    null_rpc_id  = MARGO_REGISTER(ctx->mid, "null_rpc", void, void, NULL);

    /* should succeed b/c addr is properly formatted */
    hret = margo_addr_lookup(ctx->mid, str_addr, &addr);
    munit_assert_int(hret, ==, HG_SUCCESS);

    hret = margo_create(ctx->mid, addr, null_rpc_id, &handle);
    munit_assert_int(hret, ==, HG_SUCCESS);

    /* attempt to send rpc to addr, should fail without timeout */
    hret = margo_forward_timed(handle, NULL, 2000.0);
    munit_assert_int(hret, ==, HG_NODEV);

    margo_destroy(handle);

    hret = margo_addr_free(ctx->mid, addr);

    munit_assert_int(hret, ==, HG_SUCCESS);

    return MUNIT_OK;
}

static char* protocol_params[] = {
    "na+sm", NULL
};

static char* addr_unreachable_params[] = {
#if HG_VERSION_MAJOR > 2 || (HG_VERSION_MAJOR == 2 && HG_VERSION_MINOR >= 1)
    "na+sm://1-1", NULL
#else
    "na+sm://1/1", NULL
#endif
};

static MunitParameterEnum test_params[] = {
    { "protocol", protocol_params },
    { NULL, NULL }
};

static MunitParameterEnum test_unreachable_params[] = {
    { "protocol", protocol_params },
    { "addr_unreachable", addr_unreachable_params },
    { NULL, NULL }
};

static MunitTest test_suite_tests[] = {
    { (char*) "/comm_reachable", test_comm_reachable,
        test_context_setup, test_context_tear_down, MUNIT_TEST_OPTION_NONE, test_params },
    { (char*) "/comm_unreachable", test_comm_unreachable,
        test_context_setup, test_context_tear_down, MUNIT_TEST_OPTION_NONE, test_unreachable_params },
    { NULL, NULL, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL }
};

static const MunitSuite test_suite = {
    (char*) "/margo", test_suite_tests, NULL, 1, MUNIT_SUITE_OPTION_NONE
};

int main(int argc, char* argv[MUNIT_ARRAY_PARAM(argc + 1)]) {
    return munit_suite_main(&test_suite, NULL, argc, argv);
}
