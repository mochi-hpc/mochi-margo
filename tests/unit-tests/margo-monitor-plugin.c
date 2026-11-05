/*
 * (C) 2024 The University of Chicago
 *
 * See COPYRIGHT in top-level directory.
 */
#include <stdio.h>
#include <stdlib.h>
#include <margo.h>
#include <json-c/json.h>
#include "munit/munit.h"

/* Path to the test-monitor-plugin shared library, set by CMake. */
#ifndef TEST_MONITOR_PLUGIN
    #error "TEST_MONITOR_PLUGIN must be defined with the plugin library path"
#endif

static void* test_context_setup(const MunitParameter params[], void* user_data)
{
    (void)params;
    (void)user_data;
    return NULL;
}

static void test_context_tear_down(void* fixture) { (void)fixture; }

/* Returns the "monitoring.name" value from the instance configuration, or NULL
 * if no monitoring section is present. The returned string is owned by json. */
static const char* monitoring_name(struct json_object* config)
{
    struct json_object* monitoring
        = json_object_object_get(config, "monitoring");
    if (!monitoring) return NULL;
    struct json_object* name = json_object_object_get(monitoring, "name");
    return name ? json_object_get_string(name) : NULL;
}

/* With MARGO_ENABLE_MONITORING set and a valid "library", the plugin is loaded
 * and installed: its name appears in the instance configuration. */
static MunitResult test_plugin_loaded(const MunitParameter params[], void* data)
{
    (void)params;
    (void)data;
    setenv("MARGO_ENABLE_MONITORING", "1", 1);

    const char* json_config
        = "{\"monitoring\":{\"library\":\"" TEST_MONITOR_PLUGIN "\"}}";
    struct margo_init_info init_info = {0};
    init_info.json_config            = json_config;

    margo_instance_id mid = margo_init_ext("na+sm", MARGO_SERVER_MODE,
                                           &init_info);
    munit_assert_not_null(mid);

    char* config_str = margo_get_config(mid);
    munit_assert_not_null(config_str);
    struct json_object* config = json_tokener_parse(config_str);
    munit_assert_not_null(config);

    munit_assert_string_equal(monitoring_name(config), "test-plugin-monitor");

    json_object_put(config);
    free(config_str);
    margo_finalize(mid);
    return MUNIT_OK;
}

/* A "library" that cannot be loaded makes margo_init_ext fail. */
static MunitResult test_plugin_missing(const MunitParameter params[],
                                       void*                data)
{
    (void)params;
    (void)data;
    setenv("MARGO_ENABLE_MONITORING", "1", 1);

    const char* json_config
        = "{\"monitoring\":{\"library\":\"/nonexistent/libmonitor.so\"}}";
    struct margo_init_info init_info = {0};
    init_info.json_config            = json_config;

    margo_instance_id mid = margo_init_ext("na+sm", MARGO_SERVER_MODE,
                                           &init_info);
    munit_assert_null(mid);
    return MUNIT_OK;
}

/* Without MARGO_ENABLE_MONITORING no monitor is installed, even when a valid
 * "library" is configured. */
static MunitResult test_plugin_disabled(const MunitParameter params[],
                                        void*                data)
{
    (void)params;
    (void)data;
    unsetenv("MARGO_ENABLE_MONITORING");

    const char* json_config
        = "{\"monitoring\":{\"library\":\"" TEST_MONITOR_PLUGIN "\"}}";
    struct margo_init_info init_info = {0};
    init_info.json_config            = json_config;

    margo_instance_id mid = margo_init_ext("na+sm", MARGO_SERVER_MODE,
                                           &init_info);
    munit_assert_not_null(mid);

    char* config_str = margo_get_config(mid);
    munit_assert_not_null(config_str);
    struct json_object* config = json_tokener_parse(config_str);
    munit_assert_not_null(config);

    /* no monitoring section means no monitor was installed */
    munit_assert_null(monitoring_name(config));

    json_object_put(config);
    free(config_str);
    margo_finalize(mid);
    return MUNIT_OK;
}

static MunitTest test_suite_tests[] = {
    {(char*)"/monitoring/plugin/loaded", test_plugin_loaded, test_context_setup,
     test_context_tear_down, MUNIT_TEST_OPTION_NONE, NULL},
    {(char*)"/monitoring/plugin/missing", test_plugin_missing,
     test_context_setup, test_context_tear_down, MUNIT_TEST_OPTION_NONE, NULL},
    {(char*)"/monitoring/plugin/disabled", test_plugin_disabled,
     test_context_setup, test_context_tear_down, MUNIT_TEST_OPTION_NONE, NULL},
    {NULL, NULL, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL}};

static const MunitSuite test_suite
    = {(char*)"/margo", test_suite_tests, NULL, 1, MUNIT_SUITE_OPTION_NONE};

int main(int argc, char* argv[MUNIT_ARRAY_PARAM(argc + 1)])
{
    return munit_suite_main(&test_suite, NULL, argc, argv);
}
