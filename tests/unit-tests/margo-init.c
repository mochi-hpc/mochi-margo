
#include <margo.h>
#include <stdlib.h>
#include <unistd.h>
#include "helper-server.h"
#include "munit/munit.h"

struct test_context {
    margo_instance_id mid;
};

static void* test_context_setup(const MunitParameter params[], void* user_data)
{
    (void)params;
    (void)user_data;
    struct test_context* ctx = calloc(1, sizeof(*ctx));

    return ctx;
}

static void test_context_tear_down(void* data)
{
    struct test_context* ctx = (struct test_context*)data;

    free(ctx);
}

/* test repeated init/finalize cycles, server mode */
static MunitResult init_cycle_server(const MunitParameter params[], void* data)
{
    const char* protocol = munit_parameters_get(params, "protocol");
    int         use_progress_thread
        = atoi(munit_parameters_get(params, "use_progress_thread"));
    int rpc_thread_count
        = atoi(munit_parameters_get(params, "rpc_thread_count"));

    struct test_context* ctx = (struct test_context*)data;

    ctx->mid = margo_init(protocol, MARGO_SERVER_MODE, use_progress_thread,
                          rpc_thread_count);
    munit_assert_not_null(ctx->mid);

    margo_finalize(ctx->mid);

    ctx->mid = margo_init(protocol, MARGO_SERVER_MODE, use_progress_thread,
                          rpc_thread_count);
    munit_assert_not_null(ctx->mid);

    margo_finalize(ctx->mid);

    return MUNIT_OK;
}

/* test initialization with misconfigured Slingshot parameters */
static MunitResult init_cxi_misconfig(const MunitParameter params[], void* data)
{
    struct test_context* ctx = (struct test_context*)data;

    /* Deliberately fault VNI environment variable; this should fail even on
     * systems where the cxi protocol is available.  This test exercises the
     * code path in which Margo attempts to auto-select a VNI.
     */
    putenv("SLINGSHOT_VNIS=apple,banana");

    ctx->mid = margo_init("cxi", MARGO_CLIENT_MODE, 0, 0);
    munit_assert_null(ctx->mid);

    return MUNIT_OK;
}

/* test repeated init/finalize cycles, client mode */
static MunitResult init_cycle_client(const MunitParameter params[], void* data)
{
    const char* protocol = munit_parameters_get(params, "protocol");
    int         use_progress_thread
        = atoi(munit_parameters_get(params, "use_progress_thread"));
    int rpc_thread_count
        = atoi(munit_parameters_get(params, "rpc_thread_count"));

    struct test_context* ctx = (struct test_context*)data;

    ctx->mid = margo_init(protocol, MARGO_CLIENT_MODE, use_progress_thread,
                          rpc_thread_count);
    munit_assert_not_null(ctx->mid);

    margo_finalize(ctx->mid);

    ctx->mid = margo_init(protocol, MARGO_CLIENT_MODE, use_progress_thread,
                          rpc_thread_count);
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
    int         use_progress_thread
        = atoi(munit_parameters_get(params, "use_progress_thread"));
    int rpc_thread_count
        = atoi(munit_parameters_get(params, "rpc_thread_count"));

    struct test_context* ctx = (struct test_context*)data;

    margo_set_environment(NULL);
    ABT_init(0, NULL);

    /* init and finalize_and_wait */
    ctx->mid = margo_init(protocol, MARGO_SERVER_MODE, use_progress_thread,
                          rpc_thread_count);
    munit_assert_not_null(ctx->mid);

    margo_finalize_and_wait(ctx->mid);

    /* init and finalize_and_wait but issue a slow RPC first */
    ctx->mid = margo_init(protocol, MARGO_SERVER_MODE, use_progress_thread,
                          rpc_thread_count);
    munit_assert_not_null(ctx->mid);

    hg_id_t rpc_id = MARGO_REGISTER(ctx->mid, "rpc", void, void, rpc_ult);
    margo_registered_disable_response(ctx->mid, rpc_id, HG_TRUE);

    hg_handle_t handle = HG_HANDLE_NULL;
    hg_addr_t   addr   = HG_ADDR_NULL;

    margo_addr_self(ctx->mid, &addr);
    margo_create(ctx->mid, addr, rpc_id, &handle);
    margo_addr_free(ctx->mid, addr);
    margo_forward(handle, NULL);
    margo_destroy(handle);

    // double t1 = ABT_get_wtime();
    margo_finalize_and_wait(ctx->mid);
    // double t2 = ABT_get_wtime();
    // munit_assert_double(t2-t1, >=, 0.5);

    ABT_finalize();

    return MUNIT_OK;
}

static MunitResult ref_incr_and_release(const MunitParameter params[],
                                        void*                data)
{
    const char* protocol = munit_parameters_get(params, "protocol");
    int         use_progress_thread
        = atoi(munit_parameters_get(params, "use_progress_thread"));
    int rpc_thread_count
        = atoi(munit_parameters_get(params, "rpc_thread_count"));

    struct test_context* ctx = (struct test_context*)data;

    margo_set_environment(NULL);
    ABT_init(0, NULL);

    /* init and finalize_and_wait */
    ctx->mid = margo_init(protocol, MARGO_SERVER_MODE, use_progress_thread,
                          rpc_thread_count);
    munit_assert_not_null(ctx->mid);

    margo_finalize_and_wait(ctx->mid);

    /* init and finalize_and_wait but issue a slow RPC first */
    ctx->mid = margo_init(protocol, MARGO_SERVER_MODE, use_progress_thread,
                          rpc_thread_count);
    munit_assert_not_null(ctx->mid);

    unsigned    refcount = 1234;
    hg_return_t hret     = margo_instance_ref_count(ctx->mid, &refcount);
    munit_assert_int(hret, ==, HG_SUCCESS);
    munit_assert_int(refcount, ==, 1);

    hret = margo_instance_ref_incr(ctx->mid);
    munit_assert_int(hret, ==, HG_SUCCESS);

    hret = margo_instance_ref_count(ctx->mid, &refcount);
    munit_assert_int(hret, ==, HG_SUCCESS);
    munit_assert_int(refcount, ==, 2);

    bool is_finalized = true;
    hret              = margo_instance_is_finalized(ctx->mid, &is_finalized);
    munit_assert_int(hret, ==, HG_SUCCESS);
    munit_assert(!is_finalized);

    margo_finalize(ctx->mid);

    hret = margo_instance_ref_count(ctx->mid, &refcount);
    munit_assert_int(hret, ==, HG_SUCCESS);
    munit_assert_int(refcount, ==, 1);

    hret = margo_instance_is_finalized(ctx->mid, &is_finalized);
    munit_assert_int(hret, ==, HG_SUCCESS);
    munit_assert(is_finalized);

    hret = margo_instance_release(ctx->mid);
    munit_assert_int(hret, ==, HG_SUCCESS);

    ABT_finalize();

    return MUNIT_OK;
}

static void kill_test(void* args)
{
    volatile int* x = (int*)args;
    double        t = ABT_get_wtime();
    while (ABT_get_wtime() - t < 1.0) { ABT_thread_yield(); }
    if (*x == 1)
        return;
    else {
        x  = NULL;
        *x = 1; // segfault
    }
}

void my_func(void*) {}

static MunitResult multiple_pools_progress_loop(const MunitParameter params[],
                                                void*                data)
{
    const char* config
        = "{\n"
          "\"argobots\": {"
          "\"pools\": ["
          "{\"name\":\"__primary__\",\"access\":\"mpmc\",\"kind\":\"fifo_"
          "wait\"},"
          "{\"name\":\"p1\",\"access\":\"mpmc\",\"kind\":\"fifo_wait\"},"
          "{\"name\":\"p2\",\"access\":\"mpmc\",\"kind\":\"fifo_wait\"}"
          "],"
          "\"xstreams\": ["
          "{\"name\":\"__primary__\","
          "\"scheduler\":{"
          "\"pools\":[\"__primary__\",\"p1\"],"
          "\"type\":\"basic_wait\""
          "}"
          "},"
          "{\"name\":\"es1\","
          "\"scheduler\":{"
          "\"pools\":[\"p2\"],"
          "\"type\":\"basic_wait\""
          "}"
          "}"
          "]"
          "},"
          "\"progress_pool\":\"p1\","
          "\"rpc_pool\":\"p1\""
          "}";

    struct margo_init_info info = MARGO_INIT_INFO_INITIALIZER;
    info.json_config            = config;
    margo_instance_id mid = margo_init_ext("na+sm", MARGO_SERVER_MODE, &info);

    struct margo_pool_info p2 = {0};
    margo_find_pool_by_name(mid, "p2", &p2);

    ABT_thread   ult, killer;
    volatile int x = 0;
    ABT_thread_create(p2.pool, kill_test, (void*)&x, ABT_THREAD_ATTR_NULL,
                      &killer);
    ABT_thread_create(p2.pool, my_func, NULL, ABT_THREAD_ATTR_NULL, &ult);
    ABT_thread_join(ult);
    x = 1;
    ABT_thread_free(&ult);
    ABT_thread_join(killer);
    ABT_thread_free(&killer);

    margo_finalize(mid);
    return MUNIT_OK;
}

/* test basic init with parent_mid: child shares parent's pools */
static MunitResult init_with_parent(const MunitParameter params[], void* data)
{
    (void)params;
    struct test_context* ctx = (struct test_context*)data;

    /* create parent instance */
    ctx->mid = margo_init("na+sm", MARGO_SERVER_MODE, 0, 0);
    munit_assert_not_null(ctx->mid);

    /* create child instance sharing the parent's argobots */
    struct margo_init_info child_info = MARGO_INIT_INFO_INITIALIZER;
    child_info.parent_mid             = ctx->mid;
    margo_instance_id child_mid
        = margo_init_ext("na+sm", MARGO_CLIENT_MODE, &child_info);
    munit_assert_not_null(child_mid);

    /* verify both instances resolve __primary__ to the same ABT pool */
    ABT_pool parent_progress = ABT_POOL_NULL;
    ABT_pool child_progress  = ABT_POOL_NULL;
    margo_get_progress_pool(ctx->mid, &parent_progress);
    margo_get_progress_pool(child_mid, &child_progress);
    munit_assert_ptr_equal(parent_progress, child_progress);

    ABT_pool parent_rpc = ABT_POOL_NULL;
    ABT_pool child_rpc  = ABT_POOL_NULL;
    margo_get_handler_pool(ctx->mid, &parent_rpc);
    margo_get_handler_pool(child_mid, &child_rpc);
    munit_assert_ptr_equal(parent_rpc, child_rpc);

    margo_finalize(child_mid);
    margo_finalize(ctx->mid);

    return MUNIT_OK;
}

/* test child init with progress_pool and rpc_pool specified by name */
static MunitResult init_with_parent_pool_by_name(const MunitParameter params[],
                                                  void*                data)
{
    (void)params;
    struct test_context* ctx = (struct test_context*)data;

    const char* parent_config
        = "{"
          "\"argobots\": {"
          "\"pools\": ["
          "{\"name\":\"__primary__\",\"access\":\"mpmc\",\"kind\":\"fifo_wait\"},"
          "{\"name\":\"my_pool\",\"access\":\"mpmc\",\"kind\":\"fifo_wait\"}"
          "],"
          "\"xstreams\": ["
          "{\"name\":\"__primary__\","
          "\"scheduler\":{\"pools\":[\"__primary__\"],"
          "\"type\":\"basic_wait\"}"
          "},"
          "{\"name\":\"es1\","
          "\"scheduler\":{\"pools\":[\"my_pool\"],"
          "\"type\":\"basic_wait\"}"
          "}"
          "]"
          "},"
          "\"progress_pool\":\"__primary__\","
          "\"rpc_pool\":\"__primary__\""
          "}";

    struct margo_init_info parent_info = MARGO_INIT_INFO_INITIALIZER;
    parent_info.json_config            = parent_config;
    ctx->mid = margo_init_ext("na+sm", MARGO_SERVER_MODE, &parent_info);
    munit_assert_not_null(ctx->mid);

    /* child references parent's pool by name */
    const char* child_config
        = "{"
          "\"progress_pool\":\"my_pool\","
          "\"rpc_pool\":\"my_pool\""
          "}";

    struct margo_init_info child_info = MARGO_INIT_INFO_INITIALIZER;
    child_info.parent_mid             = ctx->mid;
    child_info.json_config            = child_config;
    margo_instance_id child_mid
        = margo_init_ext("na+sm", MARGO_CLIENT_MODE, &child_info);
    munit_assert_not_null(child_mid);

    /* verify the child's progress and rpc pools match my_pool from parent */
    struct margo_pool_info my_pool_info = {0};
    hg_return_t hret = margo_find_pool_by_name(ctx->mid, "my_pool", &my_pool_info);
    munit_assert_int(hret, ==, HG_SUCCESS);

    ABT_pool child_progress = ABT_POOL_NULL;
    ABT_pool child_rpc      = ABT_POOL_NULL;
    margo_get_progress_pool(child_mid, &child_progress);
    margo_get_handler_pool(child_mid, &child_rpc);

    munit_assert_ptr_equal(child_progress, my_pool_info.pool);
    munit_assert_ptr_equal(child_rpc, my_pool_info.pool);

    margo_finalize(child_mid);
    margo_finalize(ctx->mid);

    return MUNIT_OK;
}

/* test that providing "argobots" config with parent_mid fails */
static MunitResult init_with_parent_invalid_argobots(
    const MunitParameter params[], void* data)
{
    (void)params;
    struct test_context* ctx = (struct test_context*)data;

    ctx->mid = margo_init("na+sm", MARGO_SERVER_MODE, 0, 0);
    munit_assert_not_null(ctx->mid);

    const char* child_config
        = "{"
          "\"argobots\": {"
          "\"pools\": [{\"name\":\"bad\",\"access\":\"mpmc\",\"kind\":\"fifo_wait\"}]"
          "}"
          "}";

    struct margo_init_info child_info = MARGO_INIT_INFO_INITIALIZER;
    child_info.parent_mid             = ctx->mid;
    child_info.json_config            = child_config;
    margo_instance_id child_mid
        = margo_init_ext("na+sm", MARGO_CLIENT_MODE, &child_info);
    munit_assert_null(child_mid);

    margo_finalize(ctx->mid);

    return MUNIT_OK;
}

static char* protocol_params[] = {"na+sm", NULL};

static char* use_progress_thread_params[] = {"0", "1", NULL};

static char* rpc_thread_count_params[] = {"0", "1", "2", "-1", NULL};

static MunitParameterEnum test_params[]
    = {{"protocol", protocol_params},
       {"use_progress_thread", use_progress_thread_params},
       {"rpc_thread_count", rpc_thread_count_params},
       {NULL, NULL}};

static MunitTest tests[] = {
    {"/init-cycle-client", init_cycle_client, test_context_setup,
     test_context_tear_down, MUNIT_TEST_OPTION_NONE, test_params},
    {"/init-cycle-server", init_cycle_server, test_context_setup,
     test_context_tear_down, MUNIT_TEST_OPTION_NONE, test_params},
    {"/finalize-and-wait", finalize_and_wait, test_context_setup,
     test_context_tear_down, MUNIT_TEST_OPTION_NONE, test_params},
    {"/ref-incr-and-release", ref_incr_and_release, test_context_setup,
     test_context_tear_down, MUNIT_TEST_OPTION_NONE, test_params},
    {"/multiple-pools-progress-loop", multiple_pools_progress_loop,
     test_context_setup, test_context_tear_down, MUNIT_TEST_OPTION_NONE, NULL},
    {"/init-cxi-misconfig", init_cxi_misconfig, test_context_setup,
     test_context_tear_down, MUNIT_TEST_OPTION_NONE, NULL},
    {"/init-with-parent", init_with_parent, test_context_setup,
     test_context_tear_down, MUNIT_TEST_OPTION_NONE, NULL},
    {"/init-with-parent-pool-by-name", init_with_parent_pool_by_name,
     test_context_setup, test_context_tear_down, MUNIT_TEST_OPTION_NONE, NULL},
    {"/init-with-parent-invalid-argobots", init_with_parent_invalid_argobots,
     test_context_setup, test_context_tear_down, MUNIT_TEST_OPTION_NONE, NULL},
    {NULL, NULL, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL}};

static const MunitSuite test_suite
    = {"/margo", tests, NULL, 1, MUNIT_SUITE_OPTION_NONE};

int main(int argc, char** argv)
{
    return munit_suite_main(&test_suite, NULL, argc, argv);
}
