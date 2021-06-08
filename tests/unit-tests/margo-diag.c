
#include <margo.h>
#include "helper-server.h"
#include "munit/munit.h"

struct test_context {
    margo_instance_id mid;
    int               remote_pid;
    char              remote_addr[256];
    hg_addr_t         remote_address;
    hg_id_t           sum_rpc_id;
};

MERCURY_GEN_PROC(sum_in_t, ((int32_t)(x)) ((int32_t)(y)))
MERCURY_GEN_PROC(sum_out_t, ((int32_t)(ret)))


static void sum(hg_handle_t h)
{
    hg_return_t ret;
    sum_in_t in;
    sum_out_t out;

    margo_instance_id mid = margo_hg_handle_get_instance(h);

    ret = margo_get_input(h, &in);

    out.ret = in.x + in.y;
    margo_thread_sleep(mid, 250);

    ret = margo_respond(h, &out);
    munit_assert_int(ret, ==, HG_SUCCESS);

    ret = margo_free_input(h, &in);
    munit_assert_int(ret, ==, HG_SUCCESS);

    ret = margo_destroy(h);
    munit_assert_int(ret, ==, HG_SUCCESS);
}

DEFINE_MARGO_RPC_HANDLER(sum)

static int simple_sum(margo_instance_id mid, void * data)
{
    hg_id_t rpc_id = MARGO_REGISTER(mid, "sum", sum_in_t, sum_out_t, sum);
    munit_assert_int(rpc_id, !=, 0);
    margo_wait_for_finalize(mid);
    return 0;
}
static void* test_context_setup(const MunitParameter params[], void* user_data)
{
    (void) params;
    (void) user_data;
    struct test_context* ctx = calloc(1, sizeof(*ctx));

    char * protocol = "na+sm";
    hg_size_t remote_addr_size = sizeof(ctx->remote_addr);
    ctx->remote_pid = HS_start(protocol, NULL, NULL, simple_sum, NULL, &(ctx->remote_addr[0]), &remote_addr_size);
    munit_assert_int(ctx->remote_pid, >, 0);

    ctx->mid = margo_init(protocol, MARGO_CLIENT_MODE, 0, 0);
    munit_assert_not_null(ctx->mid);

    ctx->sum_rpc_id = MARGO_REGISTER(ctx->mid, "sum", sum_in_t, sum_out_t, NULL);

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

static char * name_params[] = {
    "-", "dummy-profile", "/tmp/dummy-profile", "../tooth/fairy/dummy-profile", NULL
};
static char * uniqified_params[] = {
    "0", "1", NULL
};

static MunitParameterEnum dump_params[] = {
    { "name", name_params},
    { "unique", uniqified_params},
    {NULL, NULL}
};

static MunitResult profile_dump_file(const MunitParameter params[], void *data)
{
    const char *name;
    const char *unique;
    char * resolved_name = NULL;

    struct test_context *ctx = (struct test_context *)data;

    name = munit_parameters_get(params, "name");
    unique = munit_parameters_get(params, "unique");
    margo_state_dump(ctx->mid, name, atoi(unique), &resolved_name);

    if (resolved_name != NULL) free(resolved_name);

    return MUNIT_OK;
}

static MunitResult breadcrumb_snapshot(const MunitParameter params[], void *data)
{
    struct test_context *ctx = (struct test_context *)data;
    struct margo_breadcrumb_snapshot snap;
    margo_breadcrumb_snapshot(ctx->mid,  &snap);
    margo_breadcrumb_snapshot_destroy(ctx->mid,  &snap);
    return MUNIT_OK;
}


static MunitResult profile_dump(const MunitParameter params[], void *data)
{
    struct test_context *ctx = (struct test_context*)data;
    margo_profile_start(ctx->mid);
    margo_diag_start(ctx->mid);

    margo_addr_lookup(ctx->mid, ctx->remote_addr, &(ctx->remote_address));

    int i;
    sum_in_t args;
    for (i=0; i< 10; i++) {
        args.x = 42+i*2;
        args.y = 42+i*2+1;
        int compare = args.x + args.y;

        hg_handle_t h;
        margo_create(ctx->mid, ctx->remote_address, ctx->sum_rpc_id, &h);
        margo_forward(h, &args);

        sum_out_t resp;
        margo_get_output(h, &resp);
        munit_assert_int(resp.ret, ==, compare);

        margo_free_output(h, &resp);
        margo_destroy(h);
        margo_thread_sleep(ctx->mid, 1*1000);
    }
    margo_addr_free(ctx->mid, ctx->remote_address);

    margo_state_dump(ctx->mid, "-", 0, NULL);

    margo_profile_stop(ctx->mid);
    margo_diag_stop(ctx->mid);

    return MUNIT_OK;
}


static MunitTest tests[] = {
    { "/diag_stop", diag_stop, test_context_setup, test_context_tear_down, MUNIT_TEST_OPTION_NONE, NULL},
    { "/diag_start", diag_start, test_context_setup, test_context_tear_down, MUNIT_TEST_OPTION_NONE, NULL},
    { "/profile_start", profile_start, test_context_setup, test_context_tear_down, MUNIT_TEST_OPTION_NONE, NULL},
    { "/profile_stop", profile_stop, test_context_setup, test_context_tear_down, MUNIT_TEST_OPTION_NONE, NULL},
    {"/profile_dump_file", profile_dump_file, test_context_setup, test_context_tear_down, MUNIT_TEST_OPTION_NONE, dump_params},
    {"/profile_dump", profile_dump, test_context_setup, test_context_tear_down, MUNIT_TEST_OPTION_NONE, NULL},
    {"/breadcrumb_snapshot", breadcrumb_snapshot, test_context_setup, test_context_tear_down, MUNIT_TEST_OPTION_NONE, NULL},
    { NULL, NULL, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL }
};

static const MunitSuite test_suite = {
    "/margo", tests, NULL, 1, MUNIT_SUITE_OPTION_NONE
};


int main(int argc, char **argv)
{
    return munit_suite_main(&test_suite, NULL, argc, argv);
}
