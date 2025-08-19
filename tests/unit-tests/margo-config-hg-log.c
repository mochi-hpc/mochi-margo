
#include <unistd.h>
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

static MunitResult test_json_abt_config(const MunitParameter params[], void* data)
{
    struct test_context* ctx = (struct test_context*)data;
    (void)ctx;
    struct margo_init_info init_info = {0};

    init_info.json_config = "{\"mercury\":{\"log_level\":\"debug\",\"log_subsys\":\"na\"}}";

    margo_instance_id mid
        = margo_init_ext("na+sm", MARGO_SERVER_MODE, &init_info);
    munit_assert_not_null(mid);
    char* output_config_str = margo_get_config_opt(mid, 0);
    struct json_object* output_config
        = json_tokener_parse(output_config_str);
    munit_assert_not_null(output_config);

    /* validate that log level settings are propagated */
    struct json_object* mercury = json_object_object_get(output_config, "mercury");
    munit_assert_not_null(mercury);
    struct json_object* log_level = json_object_object_get(mercury, "log_level");
    munit_assert_not_null(log_level);
    munit_assert_string_equal("debug", json_object_get_string(log_level));
    struct json_object* log_subsys = json_object_object_get(mercury, "log_subsys");
    munit_assert_not_null(log_subsys);
    munit_assert_string_equal("na", json_object_get_string(log_subsys));

    json_object_put(output_config);
    free(output_config_str);
    margo_finalize(mid);

    return MUNIT_OK;
}

int main(int argc, char** argv)
{
    MunitParameterEnum test_params[] = {
      { "test-config", NULL },
      { NULL, NULL }
    };
    MunitTest tests[] = {
        {"/json-config-hg-log", test_json_abt_config, test_context_setup,
         test_context_tear_down, MUNIT_TEST_OPTION_NONE, test_params},
        {NULL, NULL, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL}
    };
    MunitSuite test_suite
        = {"/margo", tests, NULL, 1, MUNIT_SUITE_OPTION_NONE};
    int result =  munit_suite_main(&test_suite, NULL, argc, argv);
    return result;
}
