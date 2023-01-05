
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

DECLARE_MARGO_RPC_HANDLER(rpc_ult)
static void rpc_ult(hg_handle_t handle)
{
    margo_respond(handle, NULL);
    margo_destroy(handle);
}
DEFINE_MARGO_RPC_HANDLER(rpc_ult)

static void my_ult(void* data)
{
    (void)data;
}

static MunitResult remove_pool(const MunitParameter params[], void* data)
{
    (void)params;
    (void)data;

    margo_instance_id mid = margo_init("na+sm", MARGO_SERVER_MODE, 1, 4);
    munit_assert_not_null(mid);

    hg_return_t ret;

    // note: because pools need to not be attached to any ES to be removed,
    // we can't just act on the pools created by margo_init. We will have
    // to create a few pools attached to nothing so we can remove them.

    struct margo_pool_info pool_info = {0};

    // add a few pools from a JSON string
    for(unsigned i = 0; i < 3; i++) {
        const char* pool_desc_fmt = "{\"name\":\"my_pool_%u\", \"kind\":\"fifo_wait\", \"access\": \"mpmc\"}";
        char pool_desc[1024];
        sprintf(pool_desc, pool_desc_fmt, i);
        ret = margo_add_pool_from_json(mid, pool_desc, &pool_info);
        munit_assert_int(ret, ==, HG_SUCCESS);
        munit_assert_int(pool_info.index, ==, 3 + i);
    }

    // Get my_pool_0 and register an RPC handler with it
    ret = margo_find_pool_by_name(mid, "my_pool_0", &pool_info);
    munit_assert_int(ret, ==, HG_SUCCESS);
    hg_id_t id0 = MARGO_REGISTER_PROVIDER(mid, "rpc_0", void, void, rpc_ult, 42, pool_info.pool);

    // Get my_pool_1 and register an RPC handler with it
    ret = margo_find_pool_by_name(mid, "my_pool_1", &pool_info);
    munit_assert_int(ret, ==, HG_SUCCESS);
    hg_id_t id1 = MARGO_REGISTER_PROVIDER(mid, "rpc_1", void, void, rpc_ult, 42, pool_info.pool);

    int num_pools = margo_get_num_pools(mid);
    munit_assert_int(num_pools, ==, 6);

    // failing case: removing by invalid index
    ret = margo_remove_pool_by_index(mid, num_pools);
    munit_assert_int(ret, !=, HG_SUCCESS);

    // failing case: removing by invalid name
    ret = margo_remove_pool_by_name(mid, "invalid");
    munit_assert_int(ret, !=, HG_SUCCESS);

    // failing case: removing by invalid ABT_pool
    ret = margo_remove_pool_by_handle(mid, (ABT_pool)(0x1234));
    munit_assert_int(ret, !=, HG_SUCCESS);

    // failing case: removing the primary ES's pool
    ret = margo_remove_pool_by_name(mid, "__primary__");
    munit_assert_int(ret, !=, HG_SUCCESS);

    // failing case: removing a pool that is still in use by some ES
    ret = margo_remove_pool_by_name(mid, "__pool_1__");
    munit_assert_int(ret, !=, HG_SUCCESS);

    // check that we can access my_pool_1
    ret = margo_find_pool_by_name(mid, "my_pool_1", &pool_info);
    munit_assert_int(ret, ==, HG_SUCCESS);

    // failing case: removing my_pool_1 not allowed because rpc_1 registered with it
    ret = margo_remove_pool_by_name(mid, "my_pool_1");
    munit_assert_int(ret, !=, HG_SUCCESS);

    // deregister rpc_1 should make it possible to then remove my_pool_1
    margo_deregister(mid, id1);

    // remove my_pool_1 by name
    ret = margo_remove_pool_by_name(mid, "my_pool_1");
    munit_assert_int(ret, ==, HG_SUCCESS);

    // check the number of pools again
    num_pools = margo_get_num_pools(mid);
    munit_assert_int(num_pools, ==, 5);

    // check that my_pool_1 is no longer present
    ret = margo_find_pool_by_name(mid, "my_pool_1", &pool_info);
    munit_assert_int(ret, !=, HG_SUCCESS);

    // check that we can access my_pool_2
    ret = margo_find_pool_by_name(mid, "my_pool_2", &pool_info);
    munit_assert_int(ret, ==, HG_SUCCESS);

    // failing case: put a ULT in my_pool_2 and try to remove the pool
    // Note: because my_pool_2 isn't used by any ES, the thread isn't
    // going to start executing, so we then need to remove it by associating
    // the pool with an ES temporarily to get work done.
    ABT_thread ult = ABT_THREAD_NULL;
    ABT_thread_create(pool_info.pool, my_ult, NULL, ABT_THREAD_ATTR_NULL, &ult);
    ret = margo_remove_pool_by_index(mid, pool_info.index);
    munit_assert_int(ret, !=, HG_SUCCESS);
    ABT_xstream tmp_es = ABT_XSTREAM_NULL;
    ABT_xstream_create_basic(ABT_SCHED_DEFAULT, 1, &pool_info.pool, ABT_SCHED_CONFIG_NULL, &tmp_es);
    ABT_thread_join(ult);
    ABT_thread_free(&ult);
    ABT_xstream_join(tmp_es);
    ABT_xstream_free(&tmp_es);

    // remove my_pool_2 by index
    ret = margo_remove_pool_by_index(mid, pool_info.index);
    munit_assert_int(ret, ==, HG_SUCCESS);

    // check the number of xstreams again
    num_pools = margo_get_num_pools(mid);
    munit_assert_int(num_pools, ==, 4);

    // check that my_pool_2 is no longer present
    ret = margo_find_pool_by_name(mid, "my_pool_2", &pool_info);
    munit_assert_int(ret, !=, HG_SUCCESS);

    // check that we can access my_pool_0
    ret = margo_find_pool_by_name(mid, "my_pool_0", &pool_info);
    munit_assert_int(ret, ==, HG_SUCCESS);

    // failing case: cannot removing my_pool_0 because it is used by rpc_0
    ret = margo_remove_pool_by_handle(mid, pool_info.pool);
    munit_assert_int(ret, !=, HG_SUCCESS);

    // move rpc_0 to another pool (__pool_1__, which is the default handler pool)
    // so we can remove my_pool_0
    ABT_pool handler_pool = ABT_POOL_NULL;
    margo_get_handler_pool(mid, &handler_pool);
    margo_rpc_set_pool(mid, id0, handler_pool);

    // remove it by handle
    ret = margo_remove_pool_by_handle(mid, pool_info.pool);
    munit_assert_int(ret, ==, HG_SUCCESS);

    // check the number of pools again
    num_pools = margo_get_num_pools(mid);
    munit_assert_int(num_pools, ==, 3);

    // check that my_pool_0 is no longer present
    ret = margo_find_pool_by_name(mid, "my_pool_0", &pool_info);
    munit_assert_int(ret, !=, HG_SUCCESS);

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

static MunitResult remove_xstream(const MunitParameter params[], void* data)
{
    (void)params;
    (void)data;
    hg_return_t ret;

    margo_instance_id mid = margo_init("na+sm", MARGO_SERVER_MODE, 1, 4);
    munit_assert_not_null(mid);

    // note: in this setup, margo has a __primary__ ES and __xstream_X__
    // with X = 1 (progress loop), 2, 3, 4, 5 (RPC xstreams).
    // We should NOT remove __xstream_1__ if we don't want the test to
    // deadlock, but we are safe removing 2, 3, 4, and 5.

    int num_xstreams = margo_get_num_xstreams(mid);
    munit_assert_int(num_xstreams, ==, 6);

    // failing case: removing by invalid index
    ret = margo_remove_xstream_by_index(mid, num_xstreams);
    munit_assert_int(ret, !=, HG_SUCCESS);

    // failing case: removing by invalid name
    ret = margo_remove_xstream_by_name(mid, "invalid");
    munit_assert_int(ret, !=, HG_SUCCESS);

    // failing case: removing by invalid ABT_xstream
    ret = margo_remove_xstream_by_handle(mid, (ABT_xstream)(0x1234));
    munit_assert_int(ret, !=, HG_SUCCESS);

    // failing case: removing the primary ES
    ret = margo_remove_xstream_by_name(mid, "__primary__");
    munit_assert_int(ret, !=, HG_SUCCESS);

    // check that we can access __xstream_2__
    struct margo_xstream_info xstream_info = {0};
    ret = margo_find_xstream_by_name(mid, "__xstream_2__", &xstream_info);
    munit_assert_int(ret, ==, HG_SUCCESS);

    // remove __xstream_2__ by name
    ret = margo_remove_xstream_by_name(mid, "__xstream_2__");
    munit_assert_int(ret, ==, HG_SUCCESS);

    // check the number of xstreams again
    num_xstreams = margo_get_num_xstreams(mid);
    munit_assert_int(num_xstreams, ==, 5);

    // check that __xstream_2__ is no longer present
    ret = margo_find_xstream_by_name(mid, "__xstream_2__", &xstream_info);
    munit_assert_int(ret, !=, HG_SUCCESS);

    // check that we can access __xstream_4__
    ret = margo_find_xstream_by_name(mid, "__xstream_4__", &xstream_info);
    munit_assert_int(ret, ==, HG_SUCCESS);

    // remove __xstream_4__ by index
    ret = margo_remove_xstream_by_index(mid, xstream_info.index);
    munit_assert_int(ret, ==, HG_SUCCESS);

    // check the number of xstreams again
    num_xstreams = margo_get_num_xstreams(mid);
    munit_assert_int(num_xstreams, ==, 4);

    // check that __xstream_4__ is no longer present
    ret = margo_find_xstream_by_name(mid, "__xstream_4__", &xstream_info);
    munit_assert_int(ret, !=, HG_SUCCESS);

    // check that we can access __xstream_3__
    ret = margo_find_xstream_by_name(mid, "__xstream_3__", &xstream_info);
    munit_assert_int(ret, ==, HG_SUCCESS);

    // remove it by handle
    ret = margo_remove_xstream_by_handle(mid, xstream_info.xstream);
    munit_assert_int(ret, ==, HG_SUCCESS);

    // check the number of xstreams again
    num_xstreams = margo_get_num_xstreams(mid);
    munit_assert_int(num_xstreams, ==, 3);

    // check that __xstream_3__ is no longer present
    ret = margo_find_xstream_by_name(mid, "__xstream_3__", &xstream_info);
    munit_assert_int(ret, !=, HG_SUCCESS);

    margo_finalize(mid);
    return MUNIT_OK;
}

static MunitTest tests[] = {
    { "/add_pool_from_json", add_pool_from_json, test_context_setup, test_context_tear_down, MUNIT_TEST_OPTION_NONE, NULL},
    { "/add_pool_external", add_pool_external, test_context_setup, test_context_tear_down, MUNIT_TEST_OPTION_NONE, NULL},
    { "/remove_pool", remove_pool, test_context_setup, test_context_tear_down, MUNIT_TEST_OPTION_NONE, NULL},
    { "/add_xstream_from_json", add_xstream_from_json, test_context_setup, test_context_tear_down, MUNIT_TEST_OPTION_NONE, NULL},
    { "/add_xstream_external", add_xstream_external, test_context_setup, test_context_tear_down, MUNIT_TEST_OPTION_NONE, NULL},
    { "/remove_xstream", remove_xstream, test_context_setup, test_context_tear_down, MUNIT_TEST_OPTION_NONE, NULL},
    { NULL, NULL, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL }
};

static const MunitSuite test_suite = {
    "/margo", tests, NULL, 1, MUNIT_SUITE_OPTION_NONE
};


int main(int argc, char **argv)
{
    return munit_suite_main(&test_suite, NULL, argc, argv);
}
