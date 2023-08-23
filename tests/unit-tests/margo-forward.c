/*
 * (C) 2020 The University of Chicago
 *
 * See COPYRIGHT in top-level directory.
 */
#include <stdio.h>
#include <margo.h>
#include <margo-hg-shim.h>
#include <mercury_proc_string.h>
#include <mercury_macros.h>
#include "helper-server.h"
#include "munit/munit.h"
#include "munit/munit-goto.h"

#define P(__msg__) printf("%s\n", __msg__); fflush(stdout)

DECLARE_MARGO_RPC_HANDLER(rpc_ult)
static void rpc_ult(hg_handle_t handle)
{
    margo_respond(handle, NULL);
    margo_destroy(handle);
    return;
}
DEFINE_MARGO_RPC_HANDLER(rpc_ult)

DECLARE_MARGO_RPC_HANDLER(get_name_ult)
static void get_name_ult(hg_handle_t handle)
{
    const char* name = margo_handle_get_name(handle);
    margo_respond(handle, &name);
    margo_destroy(handle);
    return;
}
DEFINE_MARGO_RPC_HANDLER(get_name_ult)

MERCURY_GEN_PROC(sum_in_t,
        ((int32_t)(x))\
        ((int32_t)(y)))

DECLARE_MARGO_RPC_HANDLER(sum_ult)
static void sum_ult(hg_handle_t handle)
{
    sum_in_t in;
    margo_get_input(handle, &in);
    int32_t out = in.x + in.y;
    margo_respond(handle, &out);
    margo_free_input(handle, &in);
    margo_destroy(handle);
    return;
}
DEFINE_MARGO_RPC_HANDLER(sum_ult)


static int svr_init_fn(margo_instance_id mid, void* arg)
{
    (void)arg;
    MARGO_REGISTER(mid, "rpc", void, void, rpc_ult);
    MARGO_REGISTER(mid, "sum", sum_in_t, int32_t, sum_ult);
    MARGO_REGISTER(mid, "null_rpc", void, void, NULL);
    MARGO_REGISTER_PROVIDER(mid, "provider_rpc", void, void, rpc_ult, 42, ABT_POOL_NULL);
    MARGO_REGISTER(mid, "get_name", void, hg_string_t, get_name_ult);
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
    const char* progress_pool    = munit_parameters_get(params, "progress_pool");
    hg_size_t   remote_addr_size = 256;

    char config[4096];
    const char* config_fmt = "{"
          "\"rpc_pool\":\"p\","
          "\"progress_pool\":\"p\","
          "\"argobots\": {"
              "\"pools\": ["
                  "{ \"name\":\"p\", \"kind\":\"%s\" }"
              "],"
              "\"xstreams\": ["
                  "{ \"name\":\"__progress__\","
                    "\"scheduler\": {\"type\":\"basic_wait\", \"pools\":[\"p\"]}"
                  "}"
              "],"
          "}"
      "}";
    sprintf(config, config_fmt, progress_pool);

    struct margo_init_info init_info = {0};
    init_info.json_config = config;
    ctx->remote_pid = HS_start(protocol, &init_info, svr_init_fn, NULL, NULL,
                               &(ctx->remote_addr[0]), &remote_addr_size);
    munit_assert_int(ctx->remote_pid, >, 0);

    ctx->mid = margo_init_ext(protocol, MARGO_SERVER_MODE, &init_info);
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

    munit_assert_int_goto(hret[0], ==, HG_SUCCESS, error);
    munit_assert_int_goto(hret[1], ==, HG_SUCCESS, error);
    munit_assert_int_goto(hret[2], ==, HG_SUCCESS, error);
    munit_assert_int_goto(hret[3], ==, HG_SUCCESS, error);
    munit_assert_int_goto(hret[4], ==, HG_SUCCESS, error);
    return MUNIT_OK;

error:
    return MUNIT_FAIL;
}

static MunitResult test_forward_with_args(const MunitParameter params[],
                                          void*                data)
{
    (void)params;
    (void)data;
    hg_return_t hret[7] = {0,0,0,0,0,0,0};
    hg_handle_t handle = HG_HANDLE_NULL;
    hg_addr_t   addr = HG_ADDR_NULL;

    struct test_context* ctx = (struct test_context*)data;

    hg_id_t rpc_id = MARGO_REGISTER(ctx->mid, "sum", sum_in_t, int32_t, NULL);

    hret[0] = margo_addr_lookup(ctx->mid, ctx->remote_addr, &addr);
    if(hret[0] != HG_SUCCESS) goto cleanup;

    hret[1] = margo_create(ctx->mid, addr, rpc_id, &handle);
    if(hret[1] != HG_SUCCESS) goto cleanup;

    sum_in_t in = {42, 58};

    hret[2] = margo_forward(handle, &in);

    int32_t out = 0;
    hret[3] = margo_get_output(handle, &out);
    if(hret[3] != HG_SUCCESS) goto cleanup;
    if(out != 100) goto cleanup;

    hret[4] = margo_free_output(handle, &out);

cleanup:
    hret[5] = margo_destroy(handle);

    hret[6] = margo_addr_free(ctx->mid, addr);
    munit_assert_int_goto(out, ==, 100, error);
    munit_assert_int_goto(hret[0], ==, HG_SUCCESS, error);
    munit_assert_int_goto(hret[1], ==, HG_SUCCESS, error);
    munit_assert_int_goto(hret[2], ==, HG_SUCCESS, error);
    munit_assert_int_goto(hret[3], ==, HG_SUCCESS, error);
    munit_assert_int_goto(hret[4], ==, HG_SUCCESS, error);
    munit_assert_int_goto(hret[5], ==, HG_SUCCESS, error);
    munit_assert_int_goto(hret[6], ==, HG_SUCCESS, error);
    return MUNIT_OK;

error:
    return MUNIT_FAIL;
}

static hg_return_t forward_with_shim_cb(const struct hg_cb_info *callback_info) {
    ABT_eventual ev = (ABT_eventual)callback_info->arg;
    ABT_eventual_set(ev, (void*)&callback_info->ret, sizeof(callback_info->ret));
    return HG_SUCCESS;
}

static MunitResult test_forward_with_shim(const MunitParameter params[],
                                          void*                data)
{
    (void)params;
    (void)data;
    hg_return_t hret[8] = {0};
    hg_handle_t handle = HG_HANDLE_NULL;
    hg_addr_t   addr = HG_ADDR_NULL;

    struct test_context* ctx = (struct test_context*)data;

    hg_class_t* hg_class = margo_get_class(ctx->mid);
    hg_context_t* hg_context = margo_get_context(ctx->mid);

    hg_id_t rpc_id = HG_Register_name_for_margo(hg_class, "sum", NULL);

    hret[0] = HG_Addr_lookup2(hg_class, ctx->remote_addr, &addr);
    if(hret[0] != HG_SUCCESS) goto cleanup;

    hret[1] = HG_Create(hg_context, addr, rpc_id, &handle);
    if(hret[1] != HG_SUCCESS) goto cleanup;

    sum_in_t in = {42, 58};

    ABT_eventual ev;
    ABT_eventual_create(sizeof(hg_return_t), &ev);

    hret[2] = HG_Forward_to_margo(handle, forward_with_shim_cb, ev, hg_proc_sum_in_t, &in);

    hg_return_t* rpc_ret = NULL;
    ABT_eventual_wait(ev, (void**)&rpc_ret);
    hret[3] = *rpc_ret;
    ABT_eventual_free(&ev);
    if(hret[3] != HG_SUCCESS) goto cleanup;

    int32_t out = 0;
    hret[4] = HG_Get_output_from_margo(handle, hg_proc_int32_t, &out);
    if(hret[4] != HG_SUCCESS) goto cleanup;
    if(out != 100) goto cleanup;

    hret[5] = HG_Free_output_from_margo(handle, hg_proc_int32_t, &out);

cleanup:
    hret[6] = HG_Destroy(handle);

    hret[7] = HG_Addr_free(hg_class, addr);
    munit_assert_int_goto(hret[0], ==, HG_SUCCESS, error);
    munit_assert_int_goto(hret[1], ==, HG_SUCCESS, error);
    munit_assert_int_goto(hret[2], ==, HG_SUCCESS, error);
    munit_assert_int_goto(hret[3], ==, HG_SUCCESS, error);
    munit_assert_int_goto(hret[4], ==, HG_SUCCESS, error);
    munit_assert_int_goto(out, ==, 100, error);
    munit_assert_int_goto(hret[5], ==, HG_SUCCESS, error);
    munit_assert_int_goto(hret[6], ==, HG_SUCCESS, error);
    munit_assert_int_goto(hret[7], ==, HG_SUCCESS, error);

    return MUNIT_OK;

error:
    return MUNIT_FAIL;
}

static MunitResult test_get_name(const MunitParameter params[],
                                 void*                data)
{
    (void)params;
    (void)data;
    hg_return_t hret[6] = {0};
    hg_handle_t handle = HG_HANDLE_NULL;
    hg_addr_t   addr = HG_ADDR_NULL;

    struct test_context* ctx = (struct test_context*)data;

    // "rpc" is registered on the server, everything should be fine
    hg_id_t rpc_id = MARGO_REGISTER(ctx->mid, "get_name", void, hg_string_t, NULL);

    munit_assert_string_equal_goto(
        margo_rpc_get_name(ctx->mid, rpc_id),
        "get_name", error);

    hret[0] = margo_addr_lookup(ctx->mid, ctx->remote_addr, &addr);
    if(hret[0] != HG_SUCCESS) goto cleanup;

    hret[1] = margo_create(ctx->mid, addr, rpc_id, &handle);
    if(hret[1] != HG_SUCCESS) goto cleanup;

    munit_assert_string_equal_goto(
        margo_handle_get_name(handle),
        "get_name", error);

    hret[2] = margo_forward(handle, NULL);

    char* rpc_name = NULL;
    hret[3] = margo_get_output(handle, &rpc_name);

    munit_assert_not_null_goto(rpc_name, error);
    munit_assert_string_equal_goto(
        rpc_name, "get_name", error);

    margo_free_output(handle, &rpc_name);

cleanup:
    hret[4] = margo_destroy(handle);

    hret[5] = margo_addr_free(ctx->mid, addr);

    munit_assert_int_goto(hret[0], ==, HG_SUCCESS, error);
    munit_assert_int_goto(hret[1], ==, HG_SUCCESS, error);
    munit_assert_int_goto(hret[2], ==, HG_SUCCESS, error);
    munit_assert_int_goto(hret[3], ==, HG_SUCCESS, error);
    munit_assert_int_goto(hret[4], ==, HG_SUCCESS, error);
    munit_assert_int_goto(hret[5], ==, HG_SUCCESS, error);
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
    hg_return_t hret[5] = {0};
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
    hg_return_t hret[5] = {0};
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
    hg_return_t hret[5] = {0};
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
    hg_return_t hret[5] = {0};
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
    hg_return_t hret[5] = {0};
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
    hg_return_t hret[5] = {0};
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

    munit_assert_int_goto(hret[0], ==, HG_SUCCESS, error);
    munit_assert_int_goto(hret[1], ==, HG_SUCCESS, error);
    munit_assert_int_goto(hret[2], ==, HG_NO_MATCH, error);
    munit_assert_int_goto(hret[3], ==, HG_SUCCESS, error);
    munit_assert_int_goto(hret[4], ==, HG_SUCCESS, error);
    return MUNIT_OK;

error:
    return MUNIT_FAIL;
}

static void on_complete(void* uargs, hg_return_t hret) {
    ABT_eventual ev = (ABT_eventual)uargs;
    ABT_eventual_set(ev, &hret, sizeof(hret));
}

static MunitResult test_provider_cforward(const MunitParameter params[],
                                          void*                data)
{
    (void)params;
    (void)data;
    hg_return_t hret[6] = {0};
    hg_handle_t handle = HG_HANDLE_NULL;
    hg_addr_t   addr = HG_ADDR_NULL;

    struct test_context* ctx = (struct test_context*)data;

    hg_id_t rpc_id = MARGO_REGISTER(ctx->mid, "provider_rpc", void, void, NULL);

    hret[0] = margo_addr_lookup(ctx->mid, ctx->remote_addr, &addr);
    if(hret[0] != HG_SUCCESS) goto cleanup;

    hret[1] = margo_create(ctx->mid, addr, rpc_id, &handle);
    if(hret[1] != HG_SUCCESS) goto cleanup;

    ABT_eventual ev;
    ABT_eventual_create(sizeof(hg_return_t), &ev);

    hret[2] = margo_provider_cforward(42, handle, NULL,
                                      on_complete, ev);
    hg_return_t* h;
    ABT_eventual_wait(ev, (void**)&h);
    hret[3] = *h;
    ABT_eventual_free(&ev);

cleanup:
    hret[4] = margo_destroy(handle);
    hret[5] = margo_addr_free(ctx->mid, addr);

    munit_assert_int_goto(hret[0], ==, HG_SUCCESS, error);
    munit_assert_int_goto(hret[1], ==, HG_SUCCESS, error);
    munit_assert_int_goto(hret[2], ==, HG_SUCCESS, error);
    munit_assert_int_goto(hret[3], ==, HG_SUCCESS, error);
    munit_assert_int_goto(hret[4], ==, HG_SUCCESS, error);
    munit_assert_int_goto(hret[5], ==, HG_SUCCESS, error);

    return MUNIT_OK;

error:
    return MUNIT_FAIL;
}

static char* protocol_params[] = {"na+sm", NULL};
static char* progress_pool_params[] = {"fifo_wait", "prio_wait", "efirst_wait", NULL};

static MunitParameterEnum test_params[]
    = {{"protocol", protocol_params},
       {"progress_pool", progress_pool_params},
       {NULL, NULL}};

static MunitTest test_suite_tests[] = {
    {(char*)"/forward", test_forward, test_context_setup,
     test_context_tear_down, MUNIT_TEST_OPTION_NONE, test_params},
    {(char*)"/forward_with_args", test_forward_with_args, test_context_setup,
     test_context_tear_down, MUNIT_TEST_OPTION_NONE, test_params},
    {(char*)"/forward_with_shim", test_forward_with_shim, test_context_setup,
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
    {(char*)"/get_name", test_get_name, test_context_setup,
     test_context_tear_down, MUNIT_TEST_OPTION_NONE, test_params},
    {(char*)"/provider_cforward", test_provider_cforward, test_context_setup,
     test_context_tear_down, MUNIT_TEST_OPTION_NONE, test_params},
    {NULL, NULL, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL}};

static const MunitSuite test_suite
    = {(char*)"/margo", test_suite_tests, NULL, 1, MUNIT_SUITE_OPTION_NONE};

int main(int argc, char* argv[MUNIT_ARRAY_PARAM(argc + 1)])
{
    return munit_suite_main(&test_suite, NULL, argc, argv);
}
