/*
 * (C) 2020 The University of Chicago
 *
 * See COPYRIGHT in top-level directory.
 */
#include <stdio.h>
#include <margo.h>
#include "helper-server.h"
#include "munit/munit.h"

static hg_id_t rpc_id;
static hg_id_t provider_rpc_id;

DECLARE_MARGO_RPC_HANDLER(rpc_ult)
static void rpc_ult(hg_handle_t handle)
{
    hg_return_t hret;

    hret = margo_respond(handle, NULL);
    munit_assert_int(hret, ==, HG_SUCCESS);
    margo_destroy(handle);

    return;
}
DEFINE_MARGO_RPC_HANDLER(rpc_ult)

static int svr_init_fn(margo_instance_id mid, void* arg)
{
    rpc_id = MARGO_REGISTER(mid, "rpc", void, void, rpc_ult);
    provider_rpc_id = MARGO_REGISTER_PROVIDER(mid, "provider_rpc", void, void, rpc_ult, 42, ABT_POOL_NULL);
    return (0);
}

struct test_context {
    margo_instance_id mid;
    int               remote_pid;
    char              remote_addr[256];
};

static void* test_context_setup(const MunitParameter params[], void* user_data)
{
    (void)params;
    (void)user_data;
    struct test_context* ctx = calloc(1, sizeof(*ctx));

    const char* protocol         = munit_parameters_get(params, "protocol");
    hg_size_t   remote_addr_size = 256;
    ctx->remote_pid = HS_start(protocol, NULL, svr_init_fn, NULL, NULL,
                               &(ctx->remote_addr[0]), &remote_addr_size);
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

static MunitResult test_forward(const MunitParameter params[],
                                void*                data)
{
    (void)params;
    (void)data;
    hg_return_t hret;
    hg_handle_t handle;
    hg_addr_t   addr;

    struct test_context* ctx = (struct test_context*)data;

    rpc_id = MARGO_REGISTER(ctx->mid, "rpc", void, void, NULL);

    hret = margo_addr_lookup(ctx->mid, ctx->remote_addr, &addr);
    munit_assert_int(hret, ==, HG_SUCCESS);

    hret = margo_create(ctx->mid, addr, rpc_id, &handle);
    munit_assert_int(hret, ==, HG_SUCCESS);

    hret = margo_forward(handle, NULL);
    munit_assert_int(hret, ==, HG_SUCCESS);

    hret = margo_destroy(handle);
    munit_assert_int(hret, ==, HG_SUCCESS);

    hret = margo_addr_free(ctx->mid, addr);
    munit_assert_int(hret, ==, HG_SUCCESS);

    return MUNIT_OK;
}

static MunitResult test_forward_invalid(const MunitParameter params[],
                                        void*                data)
{
    (void)params;
    (void)data;
    hg_return_t hret;
    hg_handle_t handle;
    hg_addr_t   addr;

    struct test_context* ctx = (struct test_context*)data;

    hg_id_t invalid_rpc_id = MARGO_REGISTER(ctx->mid, "invalid_rpc", void, void, NULL);

    hret = margo_addr_lookup(ctx->mid, ctx->remote_addr, &addr);
    munit_assert_int(hret, ==, HG_SUCCESS);

    // invalid RPC id
    hret = margo_create(ctx->mid, addr, invalid_rpc_id, &handle);
    munit_assert_int(hret, ==, HG_SUCCESS);

    hret = margo_forward(handle, NULL);
    munit_assert_int(hret, ==, HG_NO_MATCH);

    hret = margo_destroy(handle);
    munit_assert_int(hret, ==, HG_SUCCESS);

    hret = margo_addr_free(ctx->mid, addr);
    munit_assert_int(hret, ==, HG_SUCCESS);

    return MUNIT_OK;
}

static MunitResult test_provider_forward(const MunitParameter params[],
                                         void*                data)
{
    (void)params;
    (void)data;
    hg_return_t hret;
    hg_handle_t handle;
    hg_addr_t   addr;

    struct test_context* ctx = (struct test_context*)data;

    provider_rpc_id = MARGO_REGISTER(ctx->mid, "provider_rpc", void, void, NULL);

    hret = margo_addr_lookup(ctx->mid, ctx->remote_addr, &addr);
    munit_assert_int(hret, ==, HG_SUCCESS);

    hret = margo_create(ctx->mid, addr, provider_rpc_id, &handle);
    munit_assert_int(hret, ==, HG_SUCCESS);

    hret = margo_provider_forward(42, handle, NULL);
    munit_assert_int(hret, ==, HG_SUCCESS);

    hret = margo_destroy(handle);
    munit_assert_int(hret, ==, HG_SUCCESS);

    hret = margo_addr_free(ctx->mid, addr);
    munit_assert_int(hret, ==, HG_SUCCESS);

    return MUNIT_OK;
}

static MunitResult test_provider_forward_invalid(const MunitParameter params[],
                                                 void*                data)
{
    (void)params;
    (void)data;
    hg_return_t hret;
    hg_handle_t handle;
    hg_addr_t   addr;

    struct test_context* ctx = (struct test_context*)data;

    hg_id_t provider_rpc_id = MARGO_REGISTER(ctx->mid, "povider_rpc", void, void, NULL);

    hret = margo_addr_lookup(ctx->mid, ctx->remote_addr, &addr);
    munit_assert_int(hret, ==, HG_SUCCESS);

    hret = margo_create(ctx->mid, addr, provider_rpc_id, &handle);
    munit_assert_int(hret, ==, HG_SUCCESS);

    hret = margo_provider_forward(43, handle, NULL);
    munit_assert_int(hret, ==, HG_NO_MATCH);

    hret = margo_destroy(handle);
    munit_assert_int(hret, ==, HG_SUCCESS);

    hret = margo_addr_free(ctx->mid, addr);
    munit_assert_int(hret, ==, HG_SUCCESS);

    return MUNIT_OK;
}

static char* protocol_params[] = {"na+sm", NULL};

static MunitParameterEnum test_params[]
    = {{"protocol", protocol_params}, {NULL, NULL}};

static MunitTest test_suite_tests[] = {
    {(char*)"/forward", test_forward, test_context_setup,
     test_context_tear_down, MUNIT_TEST_OPTION_NONE, test_params},
    {(char*)"/forward_invalid", test_forward_invalid, test_context_setup,
     test_context_tear_down, MUNIT_TEST_OPTION_NONE, test_params},
    {(char*)"/provider_forward", test_provider_forward, test_context_setup,
     test_context_tear_down, MUNIT_TEST_OPTION_NONE, test_params},
    {(char*)"/provider_forward_invalid", test_provider_forward_invalid, test_context_setup,
     test_context_tear_down, MUNIT_TEST_OPTION_NONE, test_params},
    {NULL, NULL, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL}};

static const MunitSuite test_suite
    = {(char*)"/margo", test_suite_tests, NULL, 1, MUNIT_SUITE_OPTION_NONE};

int main(int argc, char* argv[MUNIT_ARRAY_PARAM(argc + 1)])
{
    return munit_suite_main(&test_suite, NULL, argc, argv);
}
