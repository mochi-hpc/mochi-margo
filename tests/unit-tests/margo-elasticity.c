
#include <margo.h>
#include "munit/munit.h"

static void* test_context_setup(const MunitParameter params[], void* user_data)
{
    (void) params;
    (void) user_data;
    return NULL;
}

static void test_context_tear_down(void *data)
{
    (void)data;
}

static MunitResult add_pool_from_json(const MunitParameter params[], void* data)
{
    (void)params;
    (void)data;

    margo_instance_id mid = margo_init("na+sm", MARGO_SERVER_MODE, 1, 4);
    munit_assert_not_null(mid);

    hg_return_t ret;

    // add a pool from a JSON string
    struct margo_pool_info pool_info = {0};
    const char* pool_desc = "{\"name\":\"my_pool\", \"kind\":\"fifo_wait\", \"access\": \"mpmc\"}";
    ret = margo_add_pool_from_json(mid, pool_desc, &pool_info);
    munit_assert_int(ret, ==, HG_SUCCESS);
    munit_assert_int(pool_info.index, ==, 3);
    munit_assert_string_equal(pool_info.name, "my_pool");
    munit_assert_not_null(pool_info.pool);

    // search for it by index
    struct margo_pool_info pool_info2 = {0};
    ret = margo_find_pool_by_index(mid, pool_info.index, &pool_info2);
    munit_assert_int(ret, ==, HG_SUCCESS);
    munit_assert_int(pool_info2.index, ==, pool_info.index);
    munit_assert_string_equal(pool_info2.name, pool_info.name);
    munit_assert_ptr_equal(pool_info2.pool, pool_info.pool);

    // search for it by name
    memset(&pool_info2, 0, sizeof(pool_info2));
    ret = margo_find_pool_by_name(mid, pool_info.name, &pool_info2);
    munit_assert_int(ret, ==, HG_SUCCESS);
    munit_assert_int(pool_info2.index, ==, pool_info.index);
    munit_assert_string_equal(pool_info2.name, pool_info.name);
    munit_assert_ptr_equal(pool_info2.pool, pool_info.pool);

    // search for it by handle
    memset(&pool_info2, 0, sizeof(pool_info2));
    ret = margo_find_pool_by_handle(mid, pool_info.pool, &pool_info2);
    munit_assert_int(ret, ==, HG_SUCCESS);
    munit_assert_int(pool_info2.index, ==, pool_info.index);
    munit_assert_string_equal(pool_info2.name, pool_info.name);
    munit_assert_ptr_equal(pool_info2.pool, pool_info.pool);

    // add a pool with an invalid JSON
    ret = margo_add_pool_from_json(mid, pool_desc, &pool_info);
    munit_assert_int(ret, ==, HG_INVALID_ARG);

    // add a pool with a name already in use (reuse pool_desc)
    ret = margo_add_pool_from_json(mid, pool_desc, &pool_info);
    munit_assert_int(ret, ==, HG_INVALID_ARG);

    // add a pool without a name (name will be generated)
    ret = margo_add_pool_from_json(mid, "{}", &pool_info);
    munit_assert_int(ret, ==, HG_SUCCESS);
    munit_assert_string_equal(pool_info.name, "__pool_4__");

    // add a pool with a NULL config (should be equivalent to {})
    ret = margo_add_pool_from_json(mid, NULL, &pool_info);
    munit_assert_int(ret, ==, HG_SUCCESS);
    munit_assert_string_equal(pool_info.name, "__pool_5__");

    margo_finalize(mid);
    return MUNIT_OK;
}

static MunitResult add_pool_external(const MunitParameter params[], void* data)
{
    (void)params;
    (void)data;

    margo_instance_id mid = margo_init("na+sm", MARGO_SERVER_MODE, 1, 4);
    munit_assert_not_null(mid);

    // create pool
    ABT_pool my_pool = ABT_POOL_NULL;
    int r = ABT_pool_create_basic(ABT_POOL_FIFO, ABT_POOL_ACCESS_MPMC, ABT_FALSE, &my_pool);
    munit_assert_int(r, ==, ABT_SUCCESS);

    hg_return_t ret;

    // add an external pool
    struct margo_pool_info pool_info = {0};
    ret = margo_add_pool_external(mid, "my_pool", my_pool, true, &pool_info);
    munit_assert_int(ret, ==, HG_SUCCESS);
    munit_assert_int(pool_info.index, ==, 3);
    munit_assert_string_equal(pool_info.name, "my_pool");
    munit_assert_ptr_equal(pool_info.pool, my_pool);

    // search for it by index
    struct margo_pool_info pool_info2 = {0};
    ret = margo_find_pool_by_index(mid, pool_info.index, &pool_info2);
    munit_assert_int(ret, ==, HG_SUCCESS);
    munit_assert_int(pool_info2.index, ==, pool_info.index);
    munit_assert_string_equal(pool_info2.name, pool_info.name);
    munit_assert_ptr_equal(pool_info2.pool, pool_info.pool);

    // search for it by name
    memset(&pool_info2, 0, sizeof(pool_info2));
    ret = margo_find_pool_by_name(mid, pool_info.name, &pool_info2);
    munit_assert_int(ret, ==, HG_SUCCESS);
    munit_assert_int(pool_info2.index, ==, pool_info.index);
    munit_assert_string_equal(pool_info2.name, pool_info.name);
    munit_assert_ptr_equal(pool_info2.pool, pool_info.pool);

    // search for it by handle
    memset(&pool_info2, 0, sizeof(pool_info2));
    ret = margo_find_pool_by_handle(mid, pool_info.pool, &pool_info2);
    munit_assert_int(ret, ==, HG_SUCCESS);
    munit_assert_int(pool_info2.index, ==, pool_info.index);
    munit_assert_string_equal(pool_info2.name, pool_info.name);
    munit_assert_ptr_equal(pool_info2.pool, pool_info.pool);

    // try to add the same handle with a different name
    ret = margo_add_pool_external(mid, "my_pool2", my_pool, true, &pool_info);
    munit_assert_int(ret, ==, HG_INVALID_ARG);

    // create second pool
    ABT_pool my_pool2 = ABT_POOL_NULL;
    r = ABT_pool_create_basic(ABT_POOL_FIFO, ABT_POOL_ACCESS_MPMC, ABT_FALSE, &my_pool2);
    munit_assert_int(r, ==, ABT_SUCCESS);

    // try to add it with a name that exists
    ret = margo_add_pool_external(mid, "my_pool", my_pool2, true, &pool_info);
    munit_assert_int(ret, ==, HG_INVALID_ARG);

    // since my_pool2 hasn't been associated with any ES, we should free it manually
    ABT_pool_free(&my_pool2);

    margo_finalize(mid);
    return MUNIT_OK;
}

static MunitResult add_xstream_from_json(const MunitParameter params[], void* data)
{
    (void)params;
    (void)data;

    margo_instance_id mid = margo_init("na+sm", MARGO_SERVER_MODE, 1, 4);
    munit_assert_not_null(mid);

    hg_return_t ret;

    // add an xstream from a JSON string
    struct margo_xstream_info xstream_info = {0};
    const char* xstream_desc = "{\"name\":\"my_es\", \"scheduler\":{\"pools\":[\"__primary__\", \"__pool_1__\"]}}";
    ret = margo_add_xstream_from_json(mid, xstream_desc, &xstream_info);
    munit_assert_int(ret, ==, HG_SUCCESS);
    munit_assert_int(xstream_info.index, ==, 6);
    munit_assert_string_equal(xstream_info.name, "my_es");
    munit_assert_not_null(xstream_info.xstream);

    // search for it by index
    struct margo_xstream_info xstream_info2 = {0};
    ret = margo_find_xstream_by_index(mid, xstream_info.index, &xstream_info2);
    munit_assert_int(ret, ==, HG_SUCCESS);
    munit_assert_int(xstream_info2.index, ==, xstream_info.index);
    munit_assert_string_equal(xstream_info2.name, xstream_info.name);
    munit_assert_ptr_equal(xstream_info2.xstream, xstream_info.xstream);

    // search for it by name
    memset(&xstream_info2, 0, sizeof(xstream_info2));
    ret = margo_find_xstream_by_name(mid, xstream_info.name, &xstream_info2);
    munit_assert_int(ret, ==, HG_SUCCESS);
    munit_assert_int(xstream_info2.index, ==, xstream_info.index);
    munit_assert_string_equal(xstream_info2.name, xstream_info.name);
    munit_assert_ptr_equal(xstream_info2.xstream, xstream_info.xstream);

    // search for it by handle
    memset(&xstream_info2, 0, sizeof(xstream_info2));
    ret = margo_find_xstream_by_handle(mid, xstream_info.xstream, &xstream_info2);
    munit_assert_int(ret, ==, HG_SUCCESS);
    munit_assert_int(xstream_info2.index, ==, xstream_info.index);
    munit_assert_string_equal(xstream_info2.name, xstream_info.name);
    munit_assert_ptr_equal(xstream_info2.xstream, xstream_info.xstream);

    // add a xstream with an invalid JSON
    ret = margo_add_xstream_from_json(mid, xstream_desc, &xstream_info);
    munit_assert_int(ret, ==, HG_INVALID_ARG);

    // add a xstream with a name already in use (reuse xstream_desc)
    ret = margo_add_xstream_from_json(mid, xstream_desc, &xstream_info);
    munit_assert_int(ret, ==, HG_INVALID_ARG);

    // add a xstream without a name (name will be generated)
    ret = margo_add_xstream_from_json(mid, "{\"scheduler\":{\"pools\":[\"__primary__\"]}}", &xstream_info);
    munit_assert_int(ret, ==, HG_SUCCESS);
    munit_assert_string_equal(xstream_info.name, "__xstream_7__");

    // add a xstream with a NULL config (not allowed)
    ret = margo_add_xstream_from_json(mid, NULL, &xstream_info);
    munit_assert_int(ret, ==, HG_INVALID_ARG);

    margo_finalize(mid);
    return MUNIT_OK;
}

static MunitResult add_xstream_external(const MunitParameter params[], void* data)
{
    (void)params;
    (void)data;
    hg_return_t ret;

    margo_instance_id mid = margo_init("na+sm", MARGO_SERVER_MODE, 1, 4);
    munit_assert_not_null(mid);

    // we will need to use pools that are known by Margo
    ABT_pool known_pools[3];
    for(int i = 0; i < 3; i++) {
        struct margo_pool_info pool_info = {0};
        ret = margo_find_pool_by_index(mid, i, &pool_info);
        munit_assert_int(ret, ==, HG_SUCCESS);
        known_pools[i] = pool_info.pool;
    }

    // create xstream
    ABT_xstream my_xstream = ABT_XSTREAM_NULL;
    int r = ABT_xstream_create_basic(ABT_SCHED_DEFAULT, 1, &known_pools[2], ABT_SCHED_CONFIG_NULL, &my_xstream);
    munit_assert_int(r, ==, ABT_SUCCESS);

    // add external xstream
    struct margo_xstream_info xstream_info = {0};
    ret = margo_add_xstream_external(mid, "my_xstream", my_xstream, true, &xstream_info);
    munit_assert_int(ret, ==, HG_SUCCESS);
    munit_assert_int(xstream_info.index, ==, 6);
    munit_assert_string_equal(xstream_info.name, "my_xstream");
    munit_assert_ptr_equal(xstream_info.xstream, my_xstream);

    // search for it by index
    struct margo_xstream_info xstream_info2 = {0};
    ret = margo_find_xstream_by_index(mid, xstream_info.index, &xstream_info2);
    munit_assert_int(ret, ==, HG_SUCCESS);
    munit_assert_int(xstream_info2.index, ==, xstream_info.index);
    munit_assert_string_equal(xstream_info2.name, xstream_info.name);
    munit_assert_ptr_equal(xstream_info2.xstream, xstream_info.xstream);

    // search for it by name
    memset(&xstream_info2, 0, sizeof(xstream_info2));
    ret = margo_find_xstream_by_name(mid, xstream_info.name, &xstream_info2);
    munit_assert_int(ret, ==, HG_SUCCESS);
    munit_assert_int(xstream_info2.index, ==, xstream_info.index);
    munit_assert_string_equal(xstream_info2.name, xstream_info.name);
    munit_assert_ptr_equal(xstream_info2.xstream, xstream_info.xstream);

    // search for it by handle
    memset(&xstream_info2, 0, sizeof(xstream_info2));
    ret = margo_find_xstream_by_handle(mid, xstream_info.xstream, &xstream_info2);
    munit_assert_int(ret, ==, HG_SUCCESS);
    munit_assert_int(xstream_info2.index, ==, xstream_info.index);
    munit_assert_string_equal(xstream_info2.name, xstream_info.name);
    munit_assert_ptr_equal(xstream_info2.xstream, xstream_info.xstream);

    // try to add the same handle with a different name
    ret = margo_add_xstream_external(mid, "my_xstream2", my_xstream, true, &xstream_info);
    munit_assert_int(ret, ==, HG_INVALID_ARG);

    // create second xstream
    ABT_xstream my_xstream2 = ABT_XSTREAM_NULL;
    r = ABT_xstream_create_basic(ABT_SCHED_DEFAULT, 1, &known_pools[2], ABT_SCHED_CONFIG_NULL, &my_xstream2);
    munit_assert_int(r, ==, ABT_SUCCESS);

    // try to add it with a name that exists
    ret = margo_add_xstream_external(mid, "my_xstream", my_xstream2, true, &xstream_info);
    munit_assert_int(ret, ==, HG_INVALID_ARG);

    // create an xstream with a pool that hasn't been registed and try to add it
    ABT_pool my_pool = ABT_POOL_NULL;
    r = ABT_pool_create_basic(ABT_POOL_FIFO, ABT_POOL_ACCESS_MPMC, ABT_TRUE, &my_pool);
    munit_assert_int(r, ==, ABT_SUCCESS);
    ABT_xstream my_xstream3 = ABT_XSTREAM_NULL;
    r = ABT_xstream_create_basic(ABT_SCHED_PRIO, 1, &my_pool, ABT_SCHED_CONFIG_NULL, &my_xstream3);
    munit_assert_int(r, ==, ABT_SUCCESS);
    ret = margo_add_xstream_external(mid, "my_xstream_3", my_xstream3, true, &xstream_info);
    munit_assert_int(ret, ==, HG_INVALID_ARG);

    // since my_xstream3 hasn't been added, free it manually
    ABT_xstream_join(my_xstream3);
    ABT_xstream_free(&my_xstream3);

    // since my_xstream2 hasn't been added, free it manually
    ABT_xstream_join(my_xstream2);
    ABT_xstream_free(&my_xstream2);

    margo_finalize(mid);
    return MUNIT_OK;
}

static MunitTest tests[] = {
    { "/add_pool_from_json", add_pool_from_json, test_context_setup, test_context_tear_down, MUNIT_TEST_OPTION_NONE, NULL},
    { "/add_pool_external", add_pool_external, test_context_setup, test_context_tear_down, MUNIT_TEST_OPTION_NONE, NULL},
    { "/add_xstream_from_json", add_xstream_from_json, test_context_setup, test_context_tear_down, MUNIT_TEST_OPTION_NONE, NULL},
    { "/add_xstream_external", add_xstream_external, test_context_setup, test_context_tear_down, MUNIT_TEST_OPTION_NONE, NULL},
    { NULL, NULL, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL }
};

static const MunitSuite test_suite = {
    "/margo", tests, NULL, 1, MUNIT_SUITE_OPTION_NONE
};


int main(int argc, char **argv)
{
    return munit_suite_main(&test_suite, NULL, argc, argv);
}
