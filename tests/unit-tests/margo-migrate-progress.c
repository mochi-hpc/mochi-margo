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

static int svr_init_fn(margo_instance_id mid, void* arg)
{
    (void)arg;
    MARGO_REGISTER(mid, "rpc", void, void, rpc_ult);
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

    const char* config = "{"
          "\"rpc_pool\":\"my_rpc_pool\","
          "\"progress_pool\":\"my_progress_pool\","
          "\"argobots\": {"
              "\"pools\": ["
                  "{ \"name\":\"my_rpc_pool\", \"kind\":\"fifo_wait\" },"
                  "{ \"name\":\"my_progress_pool\", \"kind\":\"fifo_wait\" }"
              "],"
              "\"xstreams\": ["
                  "{ \"name\":\"my_progress_xstream\","
                    "\"scheduler\": {\"type\":\"basic_wait\", \"pools\":[\"my_progress_pool\"]}"
                  "},"
                  "{ \"name\":\"my_rpc_xstream\","
                    "\"scheduler\": {\"type\":\"basic_wait\", \"pools\":[\"my_rpc_pool\"]}"
                  "}"
              "],"
          "}"
      "}";

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

static MunitResult test_migrate_progress_and_forward(const MunitParameter params[],
                                                     void*                data)
{
    (void)params;
    (void)data;
    hg_return_t hret[5] = {0,0,0,0,0};
    int ret[5] = {0};
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
    if(hret[2] != HG_SUCCESS) goto cleanup;
    hret[3] = margo_destroy(handle);
    if(hret[3] != HG_SUCCESS) goto cleanup;

    // create new pool and ES
    struct margo_pool_info pool_info = {0};
    ret[0] = margo_add_pool_from_json(ctx->mid,
        "{ \"name\":\"my_new_progress_pool\", \"kind\":\"fifo_wait\" }",
        &pool_info);
    struct margo_xstream_info es_info = {0};
    ret[1] = margo_add_xstream_from_json(ctx->mid,
        "{ \"name\":\"my_new_progress_xstream\","
          "\"scheduler\": {\"type\":\"basic_wait\", \"pools\":[\"my_new_progress_pool\"]}"
        "}",
        &es_info);
    // migrate the progress loop
    ret[2] = margo_migrate_progress_loop(ctx->mid, pool_info.index);
    // erase old pool and ES
    ret[3] = margo_remove_xstream_by_name(ctx->mid, "my_progress_xstream");
    ret[4] = margo_remove_pool_by_name(ctx->mid, "my_progress_pool");

    // send another RPC
    hret[1] = margo_create(ctx->mid, addr, rpc_id, &handle);
    if(hret[1] != HG_SUCCESS) goto cleanup;
    hret[2] = margo_forward(handle, NULL);
    if(hret[2] != HG_SUCCESS) goto cleanup;

cleanup:
    hret[3] = margo_destroy(handle);
    hret[4] = margo_addr_free(ctx->mid, addr);

    munit_assert_int_goto(hret[0], ==, HG_SUCCESS, error);
    munit_assert_int_goto(hret[1], ==, HG_SUCCESS, error);
    munit_assert_int_goto(hret[2], ==, HG_SUCCESS, error);
    munit_assert_int_goto(hret[3], ==, HG_SUCCESS, error);
    munit_assert_int_goto(hret[4], ==, HG_SUCCESS, error);
    munit_assert_int_goto(ret[0], ==, 0, error);
    munit_assert_int_goto(ret[1], ==, 0, error);
    munit_assert_int_goto(ret[2], ==, 0, error);
    munit_assert_int_goto(ret[3], ==, 0, error);
    munit_assert_int_goto(ret[4], ==, 0, error);
    return MUNIT_OK;

error:
    return MUNIT_FAIL;
}

static char* protocol_params[] = {"na+sm", NULL};

static MunitParameterEnum test_params[]
    = {{"protocol", protocol_params},
       {NULL, NULL}};

static MunitParameterEnum test_params2[]
    = {{"protocol", protocol_params},
       {NULL, NULL}};

static MunitTest test_suite_tests[] = {
    {(char*)"/forward", test_migrate_progress_and_forward, test_context_setup,
     test_context_tear_down, MUNIT_TEST_OPTION_NONE, test_params},
    {NULL, NULL, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL}};

static const MunitSuite test_suite
    = {(char*)"/margo", test_suite_tests, NULL, 1, MUNIT_SUITE_OPTION_NONE};

int main(int argc, char* argv[MUNIT_ARRAY_PARAM(argc + 1)])
{
    return munit_suite_main(&test_suite, NULL, argc, argv);
}
