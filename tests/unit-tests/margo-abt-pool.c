
#include <string.h>
#include <margo.h>
#include "helper-server.h"
#include "munit/munit.h"

/* the intent of these unit tests is to verify the ability to modify various
 * argobots pool settings
 */

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

/* count how many times the needle string appears within the haystack string */
static int count_occurrence(const char* haystack, const char *needle)
{
    const char *location = haystack;
    int count = 0;

    while(location) {
        location = strstr(location, needle);
        if(location)
        {
            count++;
            location++;
        }
    }

    return(count);
}

/* test different ways of specifying different "kind" for
 * dedicated rpc handler pool
 */
static MunitResult rpc_pool_kind(const MunitParameter params[], void* data)
{
    const char * protocol = "na+sm";
    struct margo_init_info mii = {0};
    struct test_context* ctx = (struct test_context*)data;
    char *runtime_config;
    int count;

    mii.json_config = munit_parameters_get(params, "json");

    ctx->mid = margo_init_ext(protocol, MARGO_SERVER_MODE, &mii);
    munit_assert_not_null(ctx->mid);

    /* check resulting configuration */
    runtime_config = margo_get_config(ctx->mid);

    /* just one pool with the __rpc__ name */
    count = count_occurrence(runtime_config, "__rpc__");
    munit_assert_int(count, ==, 1);

    /* just one pool with the prio_wait kind */
    count = count_occurrence(runtime_config, "prio_wait");
    munit_assert_int(count, ==, 1);

    free(runtime_config);

    margo_finalize(ctx->mid);

    return MUNIT_OK;
}

static char * json_params[] = {
    "{ \"rpc_thread_count\":2, \"argobots\":{ \"pools\":[ { \"name\":\"__rpc__\", \"kind\":\"prio_wait\" } ] } }", NULL
};

static MunitParameterEnum rpc_pool_kind_params[] = {
    { "json", json_params},
    {NULL, NULL}
};

static MunitTest tests[] = {
    { "/rpc-pool-kind", rpc_pool_kind, test_context_setup, test_context_tear_down, MUNIT_TEST_OPTION_NONE, rpc_pool_kind_params},
    { NULL, NULL, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL }
};

static const MunitSuite test_suite = {
    "/margo", tests, NULL, 1, MUNIT_SUITE_OPTION_NONE
};


int main(int argc, char **argv)
{
    return munit_suite_main(&test_suite, NULL, argc, argv);
}
