
#include <margo.h>
#include <json-c/json.h>

#include "helper-server.h"
#include "munit/munit.h"

struct test_context {};

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

#define JSON_POOL_CONFIG(__name__, __type__, __access__) \
    "{\"name\":\"" #__name__ "\",\"kind\":\"" #__type__  \
    "\",\"access\":\"" #__access__ "\"}"

#define JSON_XSTREAM_CONFIG(__name__, __pools__) \
    "{\"name\":\"" #__name__                     \
    "\",\"scheduler\":{\"type\":\"basic_wait\",\"pools\":" __pools__ "}}"

static MunitResult test_abt_config(const MunitParameter params[], void* data)
{
    (void)params;
    struct test_context* ctx = (struct test_context*)data;
    (void)ctx;
    struct margo_init_info init_info = {0};
    init_info.json_config =
        "{"
            "\"argobots\":{"
                "\"pools\":["
                    JSON_POOL_CONFIG(my_pool_1, fifo, mpmc) ","
                    JSON_POOL_CONFIG(my_pool_2, fifo, mpmc)
                "],"
                "\"xstreams\":["
                    JSON_XSTREAM_CONFIG(my_es_1, "[\"my_pool_1\", \"my_pool_2\"]") ","
                    JSON_XSTREAM_CONFIG(my_es_2, "[ \"my_pool_2\" ]") ","
                    JSON_XSTREAM_CONFIG(my_es_3, "[ 1, 0 ]")
                "]"
            "}"
        "}";
    margo_instance_id mid
        = margo_init_ext("na+sm", MARGO_SERVER_MODE, &init_info);
    munit_assert_not_null(mid);

    // Note: with the above configuration, margo will have added
    // a __primary__ pool and a __primary__ execution stream

    munit_assert_int(margo_get_num_pools(mid), ==, 3);
    munit_assert_int(margo_get_num_xstreams(mid), ==, 4);

    // let's look at the pools
    const char* pool_names[] = {"my_pool_1", "my_pool_2", "__primary__"};
    for (int i = 0; i < 3; i++) {
        struct margo_pool_info info = {0};
        munit_assert_int(HG_SUCCESS, ==,
                         margo_find_pool_by_index(mid, i, NULL));
        munit_assert_int(HG_SUCCESS, ==,
                         margo_find_pool_by_index(mid, i, &info));
        munit_assert_string_equal(info.name, pool_names[i]);

        ABT_pool handle = info.pool;
        memset(&info, 0, sizeof(info));

        munit_assert_int(HG_SUCCESS, ==,
                         margo_find_pool_by_handle(mid, handle, NULL));
        munit_assert_int(HG_SUCCESS, ==,
                         margo_find_pool_by_handle(mid, handle, &info));
        munit_assert_string_equal(info.name, pool_names[i]);
        munit_assert_int(info.index, ==, i);

        memset(&info, 0, sizeof(info));
        munit_assert_int(HG_SUCCESS, ==,
                         margo_find_pool_by_name(mid, pool_names[i], NULL));
        munit_assert_int(HG_SUCCESS, ==,
                         margo_find_pool_by_name(mid, pool_names[i], &info));
        munit_assert_int(info.index, ==, i);
        munit_assert(info.pool == handle);
    }

    // failing calls for pools
    munit_assert_int(HG_INVALID_ARG, ==,
                     margo_find_pool_by_index(MARGO_INSTANCE_NULL, 0, NULL));
    munit_assert_int(
        HG_INVALID_ARG, ==,
        margo_find_pool_by_handle(MARGO_INSTANCE_NULL, ABT_POOL_NULL, NULL));
    munit_assert_int(
        HG_INVALID_ARG, ==,
        margo_find_pool_by_name(MARGO_INSTANCE_NULL, "my_pool_1", NULL));
    munit_assert_int(HG_INVALID_ARG, ==,
                     margo_find_pool_by_index(mid, 4, NULL));
    munit_assert_int(HG_NOENTRY, ==,
                     margo_find_pool_by_handle(mid, (ABT_pool)0x1234, NULL));
    munit_assert_int(HG_NOENTRY, ==,
                     margo_find_pool_by_name(mid, "my_pool_42", NULL));

    // let's look at the xstreams
    const char* es_names[] = {"my_es_1", "my_es_2", "my_es_3", "__primary__"};
    for (int i = 0; i < 4; i++) {
        struct margo_xstream_info info = {0};
        munit_assert_int(HG_SUCCESS, ==,
                         margo_find_xstream_by_index(mid, i, NULL));
        munit_assert_int(HG_SUCCESS, ==,
                         margo_find_xstream_by_index(mid, i, &info));
        munit_assert_string_equal(info.name, es_names[i]);

        ABT_xstream handle = info.xstream;
        memset(&info, 0, sizeof(info));

        munit_assert_int(HG_SUCCESS, ==,
                         margo_find_xstream_by_handle(mid, handle, NULL));
        munit_assert_int(HG_SUCCESS, ==,
                         margo_find_xstream_by_handle(mid, handle, &info));
        munit_assert_string_equal(info.name, es_names[i]);
        munit_assert_int(info.index, ==, i);

        memset(&info, 0, sizeof(info));
        munit_assert_int(HG_SUCCESS, ==,
                         margo_find_xstream_by_name(mid, es_names[i], NULL));
        munit_assert_int(HG_SUCCESS, ==,
                         margo_find_xstream_by_name(mid, es_names[i], &info));
        munit_assert_int(info.index, ==, i);
        munit_assert(info.xstream == handle);
    }

    // failing calls for xstreams
    munit_assert_int(HG_INVALID_ARG, ==,
                     margo_find_xstream_by_index(MARGO_INSTANCE_NULL, 0, NULL));
    munit_assert_int(HG_INVALID_ARG, ==,
                     margo_find_xstream_by_handle(MARGO_INSTANCE_NULL,
                                                  ABT_XSTREAM_NULL, NULL));
    munit_assert_int(
        HG_INVALID_ARG, ==,
        margo_find_xstream_by_name(MARGO_INSTANCE_NULL, "my_pool_1", NULL));
    munit_assert_int(HG_INVALID_ARG, ==,
                     margo_find_xstream_by_index(mid, 4, NULL));
    munit_assert_int(
        HG_NOENTRY, ==,
        margo_find_xstream_by_handle(mid, (ABT_xstream)0x1234, NULL));
    munit_assert_int(HG_NOENTRY, ==,
                     margo_find_xstream_by_name(mid, "my_es_42", NULL));

    margo_finalize(mid);
    return MUNIT_OK;
}

static MunitResult test_json_config(const MunitParameter params[], void* data)
{
    (void)params;
    struct test_context* ctx = (struct test_context*)data;
    (void)ctx;
    struct margo_init_info init_info = {0};

    // open JSON file with reference configurations
    struct json_object* configs
        = json_object_from_file("tests/unit-tests/test-configs.json");
    munit_assert_not_null(configs);
    munit_assert(json_object_is_type(configs, json_type_object));

    json_object_object_foreach(configs, test_name, config)
    {

        munit_assert(json_object_is_type(config, json_type_object));
        struct json_object* config_in = json_object_object_get(config, "input");
        munit_assert_not_null(config_in);
        munit_assert(json_object_is_type(config_in, json_type_object));
        struct json_object* pass = json_object_object_get(config, "pass");
        munit_assert_not_null(pass);
        munit_assert(json_object_is_type(pass, json_type_boolean));

        munit_logf(MUNIT_LOG_INFO, "initializing margo with config \"%s\"",
                   test_name);
        init_info.json_config = json_object_to_json_string_ext(
            config_in, JSON_C_TO_STRING_NOSLASHESCAPE);

        margo_instance_id mid
            = margo_init_ext("na+sm", MARGO_SERVER_MODE, &init_info);
        if (!json_object_get_boolean(pass)) {
            munit_assert_null(mid);
        } else {
            munit_assert_not_null(mid);
            struct json_object* expected_config
                = json_object_object_get(config, "output");
            munit_assert_not_null(expected_config);
            char* output_config_str = margo_get_config(mid);

            munit_logf(MUNIT_LOG_INFO, "output config is\n%s\n",
                       output_config_str);

            struct json_object* output_config
                = json_tokener_parse(output_config_str);
            munit_assert_not_null(output_config);
            json_object_object_del(output_config, "mercury");

            munit_assert(json_object_equal(output_config, expected_config));

            json_object_put(output_config);
            free(output_config_str);
            margo_finalize(mid);
        }
    }
    json_object_put(configs);

    return MUNIT_OK;
}

static MunitTest tests[]
    = {{"/abt-config", test_abt_config, test_context_setup,
        test_context_tear_down, MUNIT_TEST_OPTION_NONE, NULL},
       {"/json-config", test_json_config, test_context_setup,
        test_context_tear_down, MUNIT_TEST_OPTION_NONE, NULL},
       {NULL, NULL, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL}};

static const MunitSuite test_suite
    = {"/margo", tests, NULL, 1, MUNIT_SUITE_OPTION_NONE};

int main(int argc, char** argv)
{
    return munit_suite_main(&test_suite, NULL, argc, argv);
}
