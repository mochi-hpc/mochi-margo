/*
 * (C) 2020 The University of Chicago
 *
 * See COPYRIGHT in top-level directory.
 */

/* this unit test is meant to try out various rpc scenarios while a server
 * is shutting down
 */

#include <stdio.h>
#include <margo.h>
#include "helper-server.h"
#include "munit/munit.h"
#include "munit/munit-goto.h"

static hg_id_t test_rpc_id;

MERCURY_GEN_PROC(test_rpc_in_t, ((int32_t)(dereg_flag)))

DECLARE_MARGO_RPC_HANDLER(test_rpc_ult)

static void test_rpc_ult(hg_handle_t handle)
{
    hg_return_t hret;
    test_rpc_in_t in;
    margo_instance_id mid = MARGO_INSTANCE_NULL;

    mid = margo_hg_handle_get_instance(handle);
    /* munit_assert_not_null(mid); */

    hret = margo_get_input(handle, &in);
    /* munit_assert_int(hret, ==, HG_SUCCESS); */

    if(in.dereg_flag) {
        hret = margo_deregister(mid, test_rpc_id);
        /* munit_assert_int(hret, ==, HG_SUCCESS); */
    }

    hret = margo_respond(handle, NULL);
    if(hret != HG_SUCCESS)
        fprintf(stderr, "margo_respond() failure (expected).\n");

    margo_destroy(handle);

    return;
}
DEFINE_MARGO_RPC_HANDLER(test_rpc_ult)

static int svr_init_fn(margo_instance_id mid, void* arg)
{
    test_rpc_id
        = MARGO_REGISTER(mid, "test_rpc", test_rpc_in_t, void, test_rpc_ult);

    return (0);
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
    (void)params;
    (void)user_data;
    struct test_context* ctx = calloc(1, sizeof(*ctx));

    const char* protocol         = munit_parameters_get(params, "protocol");
    hg_size_t   remote_addr_size = 256;
    ctx->remote_pid = HS_start(protocol, NULL, svr_init_fn, NULL, NULL,
                               &(ctx->remote_addr[0]), &remote_addr_size);
    munit_assert_int(ctx->remote_pid, >, 0);

    ctx->mid = margo_init(protocol, MARGO_CLIENT_MODE, 0, 0);
    if(!ctx->mid)
        HS_stop(ctx->remote_pid, 0);
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

static MunitResult test_comm_deregister(const MunitParameter params[],
                                        void*                data)
{
    (void)params;
    (void)data;
    hg_return_t   hret;
    hg_handle_t   handle_array[64];
    hg_addr_t     addr;
    test_rpc_in_t in;
    margo_request req_array[64];
    int           i;
    int           fail_count = 0;

    struct test_context* ctx = (struct test_context*)data;

    test_rpc_id
        = MARGO_REGISTER(ctx->mid, "test_rpc", test_rpc_in_t, void, NULL);

    /* should succeed b/c addr is properly formatted */
    hret = margo_addr_lookup(ctx->mid, ctx->remote_addr, &addr);
    munit_assert_int_goto(hret, ==, HG_SUCCESS, error);

    for (i = 0; i < 64; i++) {
        hret = margo_create(ctx->mid, addr, test_rpc_id, &handle_array[i]);
        munit_assert_int_goto(hret, ==, HG_SUCCESS, error);

        if (i == 16)
            in.dereg_flag = 1;
        else
            in.dereg_flag = 0;
        hret
            = margo_iforward_timed(handle_array[i], &in, 2000.0, &req_array[i]);
        munit_assert_int_goto(hret, ==, HG_SUCCESS, error);
    }
    for (i = 0; i < 64; i++) {
        hret = margo_wait(req_array[i]);
        /* NOTE: the above may or may not succeed depending on timing of
         * deregisteration.  We intentionally do not assert here, though the
         * expectation is that at least one will fail
         */
        if(hret != HG_SUCCESS)
            fail_count++;
        margo_destroy(handle_array[i]);
    }

    /* some subset (not all, but at least one) rpc is expected to have
     * failed
     */
    munit_assert_int_goto(fail_count, >, 0, error);
    munit_assert_int_goto(fail_count, <, 64, error);

    hret = margo_addr_free(ctx->mid, addr);
    munit_assert_int_goto(hret, ==, HG_SUCCESS, error);

    return MUNIT_OK;

error:
    margo_addr_free(ctx->mid, addr);
    return MUNIT_FAIL;
}

static char* protocol_params[] = {"na+sm", NULL};

static MunitParameterEnum test_params[]
    = {{"protocol", protocol_params}, {NULL, NULL}};

static MunitTest test_suite_tests[]
    = {{(char*)"/comm_deregister", test_comm_deregister, test_context_setup,
        test_context_tear_down, MUNIT_TEST_OPTION_NONE, test_params},
       {NULL, NULL, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL}};

static const MunitSuite test_suite
    = {(char*)"/margo", test_suite_tests, NULL, 1, MUNIT_SUITE_OPTION_NONE};

int main(int argc, char* argv[MUNIT_ARRAY_PARAM(argc + 1)])
{
    return munit_suite_main(&test_suite, NULL, argc, argv);
}
