
#include <margo.h>
#include "helper-server.h"
#include "munit/munit.h"

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

/* test repeated init/finalize cycles, server mode */
static MunitResult init_cycle_server(const MunitParameter params[], void* data)
{
    const char* protocol = munit_parameters_get(params, "protocol");
    int use_progress_thread = atoi(munit_parameters_get(params, "use_progress_thread"));
    int rpc_thread_count = atoi(munit_parameters_get(params, "rpc_thread_count"));

    struct test_context* ctx = (struct test_context*)data;

    ctx->mid = margo_init(protocol, MARGO_SERVER_MODE,
                          use_progress_thread, rpc_thread_count);
    munit_assert_not_null(ctx->mid);

    margo_finalize(ctx->mid);

    ctx->mid = margo_init(protocol, MARGO_SERVER_MODE,
                          use_progress_thread, rpc_thread_count);
    munit_assert_not_null(ctx->mid);

    margo_finalize(ctx->mid);

    return MUNIT_OK;
}

/* test repeated init/finalize cycles, client mode */
static MunitResult init_cycle_client(const MunitParameter params[], void* data)
{
    const char* protocol = munit_parameters_get(params, "protocol");
    int use_progress_thread = atoi(munit_parameters_get(params, "use_progress_thread"));
    int rpc_thread_count = atoi(munit_parameters_get(params, "rpc_thread_count"));

    struct test_context* ctx = (struct test_context*)data;

    ctx->mid = margo_init(protocol, MARGO_CLIENT_MODE,
                          use_progress_thread, rpc_thread_count);
    munit_assert_not_null(ctx->mid);

    margo_finalize(ctx->mid);

    ctx->mid = margo_init(protocol, MARGO_CLIENT_MODE,
                          use_progress_thread, rpc_thread_count);
    munit_assert_not_null(ctx->mid);

    margo_finalize(ctx->mid);

    return MUNIT_OK;
}

DECLARE_MARGO_RPC_HANDLER(rpc_ult)
static void rpc_ult(hg_handle_t handle)
{
    margo_instance_id mid = margo_hg_handle_get_instance(handle);
    margo_thread_sleep(mid, 5000.0);
    margo_destroy(handle);
    return;
}
DEFINE_MARGO_RPC_HANDLER(rpc_ult)

static MunitResult finalize_and_wait(const MunitParameter params[], void* data)
{
    const char* protocol = munit_parameters_get(params, "protocol");
    int use_progress_thread = atoi(munit_parameters_get(params, "use_progress_thread"));
    int rpc_thread_count = atoi(munit_parameters_get(params, "rpc_thread_count"));

    struct test_context* ctx = (struct test_context*)data;

    margo_set_environment(NULL);
    ABT_init(0, NULL);

    /* init and finalize_and_wait */
    ctx->mid = margo_init(protocol, MARGO_SERVER_MODE,
                          use_progress_thread, rpc_thread_count);
    munit_assert_not_null(ctx->mid);

    margo_finalize_and_wait(ctx->mid);

    /* init and finalize_and_wait but issue a slow RPC first */
    ctx->mid = margo_init(protocol, MARGO_SERVER_MODE,
                          use_progress_thread, rpc_thread_count);
    munit_assert_not_null(ctx->mid);

    hg_id_t rpc_id = MARGO_REGISTER(ctx->mid, "rpc", void, void, rpc_ult);
    margo_registered_disable_response(ctx->mid, rpc_id, HG_TRUE);

    hg_handle_t handle = HG_HANDLE_NULL;
    hg_addr_t addr = HG_ADDR_NULL;

    margo_addr_self(ctx->mid, &addr);
    margo_create(ctx->mid, addr, rpc_id, &handle);
    margo_addr_free(ctx->mid, addr);
    margo_forward(handle, NULL);
    margo_destroy(handle);

    double t1 = ABT_get_wtime();
    margo_finalize_and_wait(ctx->mid);
    double t2 = ABT_get_wtime();
    munit_assert_double(t2-t1, >=, 0.5);

    ABT_finalize();

    return MUNIT_OK;
}

static char* protocol_params[] = {
    "na+sm", NULL
};

static char* use_progress_thread_params[] = {
    "0","1", NULL
};

static char* rpc_thread_count_params[] = {
    "0", "1", "2", "-1", NULL
};

static MunitParameterEnum test_params[] = {
    { "protocol", protocol_params },
    { "use_progress_thread", use_progress_thread_params },
    { "rpc_thread_count", rpc_thread_count_params },
    { NULL, NULL }
};

static MunitTest tests[] = {
    { "/init-cycle-client", init_cycle_client, test_context_setup, test_context_tear_down, MUNIT_TEST_OPTION_NONE, test_params},
    { "/init-cycle-server", init_cycle_server, test_context_setup, test_context_tear_down, MUNIT_TEST_OPTION_NONE, test_params},
    { "/finalize-and-wait", finalize_and_wait, test_context_setup, test_context_tear_down, MUNIT_TEST_OPTION_NONE, test_params},
    { NULL, NULL, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL }
};

static const MunitSuite test_suite = {
    "/margo", tests, NULL, 1, MUNIT_SUITE_OPTION_NONE
};


int main(int argc, char **argv)
{
    return munit_suite_main(&test_suite, NULL, argc, argv);
}
