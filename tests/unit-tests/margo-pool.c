#include "margo.h"
#include <margo-bulk-pool.h>
#include "helper-server.h"
#include "munit/munit.h"

struct test_context {
    margo_instance_id    mid;
    int                  remote_pid;
    char                 remote_addr[256];
    hg_addr_t            remote_address;
    hg_size_t            pool_count;
    hg_size_t            pool_size;
    margo_bulk_pool_t    testpool;
    margo_bulk_poolset_t testpoolset;
    int                  npools;
    int                  nbufs;
    int                  first_size;
    int                  size_multiple;
};

static void* test_context_setup(const MunitParameter params[], void* user_data)
{
    hg_return_t ret;
    (void)params;
    (void)user_data;
    struct test_context* ctx = calloc(1, sizeof(*ctx));

    char*     protocol         = "na+sm";
    hg_size_t remote_addr_size = sizeof(ctx->remote_addr);
    ctx->remote_pid            = HS_start(protocol, NULL, NULL, NULL, NULL,
                               &(ctx->remote_addr[0]), &remote_addr_size);
    munit_assert_int(ctx->remote_pid, >, 0);

    ctx->mid = margo_init(protocol, MARGO_CLIENT_MODE, 0, 0);
    munit_assert_not_null(ctx->mid);

    ctx->pool_count = 5;
    ctx->pool_size  = 1024;
    ret = margo_bulk_pool_create(ctx->mid, ctx->pool_count, ctx->pool_size,
                                 HG_BULK_READWRITE, &(ctx->testpool));
    munit_assert_int(ret, ==, HG_SUCCESS);

    ctx->npools        = 2;
    ctx->nbufs         = 5;
    ctx->first_size    = 1024;
    ctx->size_multiple = 4;

    ret = margo_bulk_poolset_create(ctx->mid, ctx->npools, ctx->nbufs,
                                    ctx->first_size, ctx->size_multiple,
                                    HG_BULK_READWRITE, &(ctx->testpoolset));
    munit_assert_int(ret, ==, HG_SUCCESS);

    return ctx;
}

static void test_context_tear_down(void* data)
{
    struct test_context* ctx = (struct test_context*)data;

    hg_addr_t   remote_addr = HG_ADDR_NULL;
    hg_return_t ret;

    ret = margo_bulk_pool_destroy(ctx->testpool);
    munit_assert_int(ret, ==, HG_SUCCESS);

    ret = margo_bulk_poolset_destroy(ctx->testpoolset);
    munit_assert_int(ret, ==, HG_SUCCESS);

    margo_addr_lookup(ctx->mid, ctx->remote_addr, &remote_addr);
    margo_shutdown_remote_instance(ctx->mid, remote_addr);
    margo_addr_free(ctx->mid, remote_addr);

    HS_stop(ctx->remote_pid, 0);
    margo_finalize(ctx->mid);

    free(ctx);
}

static MunitResult bulk_release(const MunitParameter params[], void* data)
{
    struct test_context* ctx = (struct test_context*)data;
    hg_bulk_t            b   = HG_BULK_NULL;
    int                  ret = margo_bulk_pool_release(ctx->testpool, b);
    munit_assert_int(ret, ==, -1);

    return MUNIT_OK;
}

static char* pool_params[] = {"NULL", "expected", NULL};

static MunitParameterEnum get_params[] = {{"pool", pool_params}, {NULL, NULL}};

static MunitResult bulk_pool(const MunitParameter params[], void* data)
{
    struct test_context* ctx = (struct test_context*)data;
    hg_bulk_t            bulk;
    const char*          kind;
    margo_bulk_pool_t    pool;
    int                  expected = 0;

    kind = munit_parameters_get(params, "pool");
    if (strcmp("NULL", kind) == 0) {
        pool     = MARGO_BULK_POOL_NULL;
        expected = -1;
    } else {
        pool     = ctx->testpool;
        expected = 0;
    }

    int ret = margo_bulk_pool_get(pool, &bulk);
    munit_assert_int(ret, ==, expected);

    ret = margo_bulk_pool_release(pool, bulk);
    munit_assert_int(ret, ==, expected);

    return MUNIT_OK;
}

static MunitResult bulk_max(const MunitParameter params[], void* data)
{
    struct test_context* ctx = (struct test_context*)data;

    hg_size_t max          = 0;
    hg_size_t computed_max = ctx->first_size;
    for (int i = 1; i < ctx->npools; i++) {
        computed_max *= ctx->size_multiple;
    }

    margo_bulk_poolset_get_max(ctx->testpoolset, &max);
    munit_assert_int(max, ==, computed_max);

    return MUNIT_OK;
}

static MunitResult poolset_tryget(const MunitParameter params[], void* data)
{
    const char*          kind;
    int                  expected;
    struct test_context* ctx = (struct test_context*)data;
    hg_bulk_t            bulk;
    margo_bulk_poolset_t poolset;

    kind = munit_parameters_get(params, "pool");
    if (strcmp("NULL", kind) == 0) {
        poolset  = MARGO_BULK_POOLSET_NULL;
        expected = -1;
    } else {
        poolset  = ctx->testpoolset;
        expected = 0;
    }

    int ret = margo_bulk_poolset_tryget(poolset, 2048, HG_TRUE, &bulk);
    munit_assert_int(ret, ==, expected);

    margo_bulk_poolset_release(ctx->testpoolset, bulk);

    return MUNIT_OK;
}

static MunitResult poolset_get(const MunitParameter params[], void* data)
{
    struct test_context* ctx = (struct test_context*)data;
    const char*          kind;
    int                  ret, expected;
    margo_bulk_poolset_t poolset;
    hg_bulk_t            bulk;

    kind = munit_parameters_get(params, "pool");
    if (strcmp("NULL", kind) == 0) {
        poolset  = MARGO_BULK_POOLSET_NULL;
        expected = -1;
    } else {
        poolset  = ctx->testpoolset;
        expected = 0;
    }

    ret = margo_bulk_poolset_get(poolset, 2048, &bulk);
    munit_assert_int(ret, ==, expected);

    margo_bulk_poolset_release(poolset, bulk);
    return MUNIT_OK;
}

static MunitTest tests[]
    = {{"/bulk_poolset_max", bulk_max, test_context_setup,
        test_context_tear_down, MUNIT_TEST_OPTION_NONE, NULL},
       {"/bulk_pool", bulk_pool, test_context_setup, test_context_tear_down,
        MUNIT_TEST_OPTION_NONE, get_params},
       {"/bulk_release", bulk_release, test_context_setup,
        test_context_tear_down, MUNIT_TEST_OPTION_NONE, NULL},
       {"/bulk_poolset_tryget", poolset_tryget, test_context_setup,
        test_context_tear_down, MUNIT_TEST_OPTION_NONE, get_params},
       {"/bulk_poolset_get", poolset_get, test_context_setup,
        test_context_tear_down, MUNIT_TEST_OPTION_NONE, get_params},
       {NULL, NULL, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL}};

static const MunitSuite test_suite
    = {"/margo", tests, NULL, 1, MUNIT_SUITE_OPTION_NONE};

int main(int argc, char** argv)
{
    return munit_suite_main(&test_suite, NULL, argc, argv);
}
