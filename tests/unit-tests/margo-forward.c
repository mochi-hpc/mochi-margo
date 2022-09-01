/*
 * (C) 2020 The University of Chicago
 *
 * See COPYRIGHT in top-level directory.
 */
#include <stdio.h>
#include <margo.h>
#include "helper-server.h"
#include "munit/munit.h"
#include "munit/munit-goto.h"


DECLARE_MARGO_RPC_HANDLER(rpc_ult)
static void rpc_ult(hg_handle_t handle)
{
    margo_respond(handle, NULL);
    margo_destroy(handle);
    return;
}
DEFINE_MARGO_RPC_HANDLER(rpc_ult)

static int svr_init_fn(margo_instance_id mid, void* arg)
{
    MARGO_REGISTER(mid, "rpc", void, void, rpc_ult);
    MARGO_REGISTER(mid, "null_rpc", void, void, NULL);
    MARGO_REGISTER_PROVIDER(mid, "provider_rpc", void, void, rpc_ult, 42, ABT_POOL_NULL);
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

    ctx->mid = margo_init(protocol, MARGO_SERVER_MODE, 0, 0);
    if(!ctx->mid) {
        HS_stop(ctx->remote_pid, 0);
    }
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
    hg_return_t hret[5] = {0,0,0,0,0};
    hg_handle_t handle = HG_HANDLE_NULL;
    hg_addr_t   addr = HG_ADDR_NULL;

    struct test_context* ctx = (struct test_context*)data;

    // "rpc" is registered on the server, everything should be fine
    hg_id_t rpc_id = MARGO_REGISTER(ctx->mid, "rpc", void, void, NULL);

    hret[0] = margo_addr_lookup(ctx->mid, ctx->remote_addr, &addr);
    if(hret[0] != HG_SUCCESS) goto cleanup;

    hret[1] = margo_create(ctx->mid, addr, rpc_id, &handle);
    if(hret[1] != HG_SUCCESS) goto cleanup;

    hret[2] = margo_forward(handle, NULL);

cleanup:
    hret[3] = margo_destroy(handle);

    hret[4] = margo_addr_free(ctx->mid, addr);

check:
    munit_assert_int_goto(hret[0], ==, HG_SUCCESS, error);
    munit_assert_int_goto(hret[1], ==, HG_SUCCESS, error);
    munit_assert_int_goto(hret[2], ==, HG_SUCCESS, error);
    munit_assert_int_goto(hret[3], ==, HG_SUCCESS, error);
    munit_assert_int_goto(hret[4], ==, HG_SUCCESS, error);
    return MUNIT_OK;

error:
    return MUNIT_FAIL;
}

static MunitResult test_stress_handle_cache(const MunitParameter params[],
                                            void*                data)
{
    (void)params;
    (void)data;
    hg_return_t hret = HG_SUCCESS;
    hg_addr_t   addr = HG_ADDR_NULL;
    hg_handle_t handles[128];
    margo_request reqs[128];
    memset(handles, 0, 128*sizeof(hg_handle_t));
    memset(reqs, 0, 128*sizeof(margo_request));

    struct test_context* ctx = (struct test_context*)data;

    // "rpc" is registered on the server, everything should be fine
    hg_id_t rpc_id = MARGO_REGISTER(ctx->mid, "rpc", void, void, NULL);

    hret = margo_addr_lookup(ctx->mid, ctx->remote_addr, &addr);
    munit_assert_int_goto(hret, ==, HG_SUCCESS, error);

    for(int i=0; i < 128; i++) {
        hret = margo_create(ctx->mid, addr, rpc_id, &handles[i]);
        munit_assert_int_goto(hret, ==, HG_SUCCESS, error);

        hret = margo_iforward(handles[i], NULL, &reqs[i]);
        munit_assert_int_goto(hret, ==, HG_SUCCESS, error);
    }

    for(int i=0; i < 128; i++) {
        hret = margo_wait(reqs[i]);
        reqs[i] = NULL;
        munit_assert_int_goto(hret, ==, HG_SUCCESS, error);
        hret = margo_destroy(handles[i]);
        handles[i] = NULL;
        munit_assert_int_goto(hret, ==, HG_SUCCESS, error);
    }

    hret = margo_addr_free(ctx->mid, addr);
    addr = NULL;
    munit_assert_int_goto(hret, ==, HG_SUCCESS, error);

    return MUNIT_OK;

error:
    margo_addr_free(ctx->mid, addr);
    return MUNIT_FAIL;
}

static MunitResult test_forward_to_null(const MunitParameter params[],
                                        void*                data)
{
    (void)params;
    (void)data;
    hg_return_t hret[5] = {0,0,0,0,0};
    hg_handle_t handle = HG_HANDLE_NULL;
    hg_addr_t   addr = HG_ADDR_NULL;

    struct test_context* ctx = (struct test_context*)data;

    // "null_rpc" is registered on the server, but associated with
    // a NULL RPC handler. Forward should return HG_NO_MATCH.
    hg_id_t rpc_id = MARGO_REGISTER(ctx->mid, "null_rpc", void, void, NULL);

    hret[0] = margo_addr_lookup(ctx->mid, ctx->remote_addr, &addr);
    if(hret[0] != HG_SUCCESS) goto cleanup;

    hret[1] = margo_create(ctx->mid, addr, rpc_id, &handle);
    if(hret[1] != HG_SUCCESS) goto cleanup;

    hret[2] = margo_forward(handle, NULL);

cleanup:
    hret[3] = margo_destroy(handle);

    hret[4] = margo_addr_free(ctx->mid, addr);

check:
    munit_assert_int_goto(hret[0], ==, HG_SUCCESS, error);
    munit_assert_int_goto(hret[1], ==, HG_SUCCESS, error);
    munit_assert_int_goto(hret[2], ==, HG_NO_MATCH, error);
    munit_assert_int_goto(hret[3], ==, HG_SUCCESS, error);
    munit_assert_int_goto(hret[4], ==, HG_SUCCESS, error);
    return MUNIT_OK;

error:
    return MUNIT_FAIL;
}

static MunitResult test_self_forward_to_null(const MunitParameter params[],
                                             void*                data)
{
    (void)params;
    (void)data;
    hg_return_t hret[5] = {0,0,0,0,0};
    hg_handle_t handle = HG_HANDLE_NULL;
    hg_addr_t   addr = HG_ADDR_NULL;

    struct test_context* ctx = (struct test_context*)data;

    // Register null_rpc to be NULL handler, forwarding to self
    // should return HG_NO_MATCH
    hg_id_t rpc_id = MARGO_REGISTER(ctx->mid, "null_rpc", void, void, NULL);

    hret[0] = margo_addr_self(ctx->mid, &addr);
    if(hret[0] != HG_SUCCESS) goto cleanup;

    hret[1] = margo_create(ctx->mid, addr, rpc_id, &handle);
    if(hret[1] != HG_SUCCESS) goto cleanup;

    hret[2] = margo_forward(handle, NULL);

cleanup:
    hret[3] = margo_destroy(handle);
    hret[4] = margo_addr_free(ctx->mid, addr);

check:
    munit_assert_int_goto(hret[0], ==, HG_SUCCESS, error);
    munit_assert_int_goto(hret[1], ==, HG_SUCCESS, error);
    munit_assert_int_goto(hret[2], ==, HG_NO_MATCH, error);
    munit_assert_int_goto(hret[3], ==, HG_SUCCESS, error);
    munit_assert_int_goto(hret[4], ==, HG_SUCCESS, error);
    return MUNIT_OK;

error:
    return MUNIT_FAIL;
}

static MunitResult test_forward_invalid(const MunitParameter params[],
                                        void*                data)
{
    (void)params;
    (void)data;
    hg_return_t hret[5] = {0, 0, 0, 0, 0};
    hg_handle_t handle = HG_HANDLE_NULL;
    hg_addr_t   addr = HG_ADDR_NULL;

    struct test_context* ctx = (struct test_context*)data;

    // invalid_rpc has not been registered on the server, forward
    // should return HG_NO_MATCH
    hg_id_t rpc_id = MARGO_REGISTER(ctx->mid, "invalid_rpc", void, void, NULL);

    hret[0] = margo_addr_lookup(ctx->mid, ctx->remote_addr, &addr);
    if(hret[0] != HG_SUCCESS) goto cleanup;

    hret[1] = margo_create(ctx->mid, addr, rpc_id, &handle);
    if(hret[1] != HG_SUCCESS) goto cleanup;

    hret[2] = margo_forward(handle, NULL);

cleanup:
    hret[3] = margo_destroy(handle);
    hret[4] = margo_addr_free(ctx->mid, addr);

check:
    munit_assert_int_goto(hret[0], ==, HG_SUCCESS, error);
    munit_assert_int_goto(hret[1], ==, HG_SUCCESS, error);
    munit_assert_int_goto(hret[2], ==, HG_NO_MATCH, error);
    munit_assert_int_goto(hret[3], ==, HG_SUCCESS, error);
    munit_assert_int_goto(hret[4], ==, HG_SUCCESS, error);
    return MUNIT_OK;

error:
    return MUNIT_FAIL;
}

static MunitResult test_provider_forward(const MunitParameter params[],
                                         void*                data)
{
    (void)params;
    (void)data;
    hg_return_t hret[5] = {0,0,0,0,0};
    hg_handle_t handle = HG_HANDLE_NULL;
    hg_addr_t   addr = HG_ADDR_NULL;

    struct test_context* ctx = (struct test_context*)data;

    // provider 42 registered provider_rpc on server, forward
    // to provider 42 should succeed.
    hg_id_t rpc_id = MARGO_REGISTER(ctx->mid, "provider_rpc", void, void, NULL);

    hret[0] = margo_addr_lookup(ctx->mid, ctx->remote_addr, &addr);
    if(hret[0] != HG_SUCCESS) goto cleanup;

    hret[1] = margo_create(ctx->mid, addr, rpc_id, &handle);
    if(hret[1] != HG_SUCCESS) goto cleanup;

    hret[2] = margo_provider_forward(42, handle, NULL);

cleanup:
    hret[3] = margo_destroy(handle);
    hret[4] = margo_addr_free(ctx->mid, addr);

check:
    munit_assert_int_goto(hret[0], ==, HG_SUCCESS, error);
    munit_assert_int_goto(hret[1], ==, HG_SUCCESS, error);
    munit_assert_int_goto(hret[2], ==, HG_SUCCESS, error);
    munit_assert_int_goto(hret[3], ==, HG_SUCCESS, error);
    munit_assert_int_goto(hret[4], ==, HG_SUCCESS, error);

    return MUNIT_OK;

error:
    return MUNIT_FAIL;
}

static MunitResult test_provider_forward_invalid(const MunitParameter params[],
                                                 void*                data)
{
    (void)params;
    (void)data;
    hg_return_t hret[5] = {0,0,0,0,0};
    hg_handle_t handle = HG_HANDLE_NULL;
    hg_addr_t   addr = HG_ADDR_NULL;

    struct test_context* ctx = (struct test_context*)data;

    // "provider_rpc" registered with provider 42, but we will send to 43.
    // Forward should return HG_NO_MATCH.
    hg_id_t rpc_id = MARGO_REGISTER(ctx->mid, "povider_rpc", void, void, NULL);

    hret[0] = margo_addr_lookup(ctx->mid, ctx->remote_addr, &addr);
    if(hret[0] != HG_SUCCESS) goto cleanup;

    hret[1] = margo_create(ctx->mid, addr, rpc_id, &handle);
    if(hret[1] != HG_SUCCESS) goto cleanup;

    hret[2] = margo_provider_forward(43, handle, NULL);

cleanup:
    hret[3] = margo_destroy(handle);
    hret[4] = margo_addr_free(ctx->mid, addr);

check:
    munit_assert_int_goto(hret[0], ==, HG_SUCCESS, error);
    munit_assert_int_goto(hret[1], ==, HG_SUCCESS, error);
    munit_assert_int_goto(hret[2], ==, HG_NO_MATCH, error);
    munit_assert_int_goto(hret[3], ==, HG_SUCCESS, error);
    munit_assert_int_goto(hret[4], ==, HG_SUCCESS, error);
    return MUNIT_OK;

error:
    return MUNIT_FAIL;
}

static MunitResult test_self_provider_forward_invalid(const MunitParameter params[],
                                                      void*                data)
{
    (void)params;
    (void)data;
    hg_return_t hret[5] = {0,0,0,0,0};
    hg_handle_t handle = HG_HANDLE_NULL;
    hg_addr_t   addr = HG_ADDR_NULL;

    struct test_context* ctx = (struct test_context*)data;

    // register provider RPC with provider id 42.
    MARGO_REGISTER_PROVIDER(ctx->mid, "provider_rpc", void, void, rpc_ult, 42, ABT_POOL_NULL);
    // register provider RPC with NULL without a provider id
    hg_id_t rpc_id = MARGO_REGISTER(ctx->mid, "povider_rpc", void, void, NULL);

    hret[0] = margo_addr_self(ctx->mid, &addr);
    if(hret[0] != HG_SUCCESS) goto cleanup;

    hret[1] = margo_create(ctx->mid, addr, rpc_id, &handle);
    if(hret[1] != HG_SUCCESS) goto cleanup;

    // try to send to provider id 43, should return HG_NO_MATCH
    hret[2] = margo_provider_forward(43, handle, NULL);

cleanup:
    hret[3] = margo_destroy(handle);
    hret[4] = margo_addr_free(ctx->mid, addr);

check:
    munit_assert_int_goto(hret[0], ==, HG_SUCCESS, error);
    munit_assert_int_goto(hret[1], ==, HG_SUCCESS, error);
    munit_assert_int_goto(hret[2], ==, HG_NO_MATCH, error);
    munit_assert_int_goto(hret[3], ==, HG_SUCCESS, error);
    munit_assert_int_goto(hret[4], ==, HG_SUCCESS, error);
    return MUNIT_OK;

error:
    return MUNIT_FAIL;
}

static char* protocol_params[] = {"ofi+tcp", NULL};

static MunitParameterEnum test_params[]
    = {{"protocol", protocol_params}, {NULL, NULL}};

static MunitTest test_suite_tests[] = {
    {(char*)"/forward", test_forward, test_context_setup,
     test_context_tear_down, MUNIT_TEST_OPTION_NONE, test_params},
    {(char*)"/forward_to_null", test_forward_to_null, test_context_setup,
     test_context_tear_down, MUNIT_TEST_OPTION_NONE, test_params},
    {(char*)"/self_forward_to_null", test_self_forward_to_null, test_context_setup,
     test_context_tear_down, MUNIT_TEST_OPTION_NONE, test_params},
    {(char*)"/forward_invalid", test_forward_invalid, test_context_setup,
     test_context_tear_down, MUNIT_TEST_OPTION_NONE, test_params},
    {(char*)"/provider_forward", test_provider_forward, test_context_setup,
     test_context_tear_down, MUNIT_TEST_OPTION_NONE, test_params},
    {(char*)"/provider_forward_invalid", test_provider_forward_invalid, test_context_setup,
     test_context_tear_down, MUNIT_TEST_OPTION_NONE, test_params},
    {(char*)"/self_provider_forward_invalid", test_self_provider_forward_invalid, test_context_setup,
     test_context_tear_down, MUNIT_TEST_OPTION_NONE, test_params},
    {(char*)"/stress_handle_cache", test_stress_handle_cache, test_context_setup,
     test_context_tear_down, MUNIT_TEST_OPTION_NONE, test_params},
    {NULL, NULL, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL}};

static const MunitSuite test_suite
    = {(char*)"/margo", test_suite_tests, NULL, 1, MUNIT_SUITE_OPTION_NONE};

int main(int argc, char* argv[MUNIT_ARRAY_PARAM(argc + 1)])
{
    return munit_suite_main(&test_suite, NULL, argc, argv);
}
