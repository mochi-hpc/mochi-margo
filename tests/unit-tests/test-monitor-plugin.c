/*
 * (C) 2024 The University of Chicago
 *
 * See COPYRIGHT in top-level directory.
 */

/* Minimal monitoring plugin used by margo-monitor-plugin.c. It is built as a
 * shared library and loaded at run time through the "monitoring.library" field
 * of the margo configuration. It exports the "margo_monitor_init" symbol that
 * margo calls to obtain the monitor to install. The "name" hook lets the test
 * confirm, via margo_get_config, that this plugin was installed. */

#include <stdlib.h>
#include <margo.h>

static void* test_plugin_initialize(margo_instance_id   mid,
                                    void*               uargs,
                                    struct json_object* config)
{
    (void)mid;
    (void)config;
    return uargs;
}

static void test_plugin_finalize(void* uargs) { (void)uargs; }

static const char* test_plugin_name() { return "test-plugin-monitor"; }

static struct margo_monitor test_plugin_monitor = {
    .uargs      = NULL,
    .initialize = test_plugin_initialize,
    .finalize   = test_plugin_finalize,
    .name       = test_plugin_name,
};

int margo_monitor_init(struct margo_monitor** monitor)
{
    *monitor = &test_plugin_monitor;
    return 0;
}
