
#include <string.h>
#include <margo.h>
#include "helper-server.h"
#include "munit/munit.h"
#include "munit/munit-goto.h"

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

static void thread_func(void*) {}

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

    const char* config_fmt = "{"
        "\"rpc_thread_count\":0,"
        "\"argobots\": {"
            "\"pools\": ["
                "{ \"name\":\"my_pool\", \"kind\":\"%s\" }"
            "],"
            "\"xstreams\": ["
                "{ \"name\":\"my_xstream\", "
                  "\"scheduler\": {"
                    "\"type\":\"basic_wait\","
                    "\"pools\":[\"my_pool\"]"
                  "}"
                "}"
            "]"
        "}"
    "}";

    const char* pool_kind = munit_parameters_get(params, "pool");

    char config[4096] = {0};
    sprintf(config, config_fmt, pool_kind);

    mii.json_config = config;

    ctx->mid = margo_init_ext(protocol, MARGO_SERVER_MODE, &mii);
    munit_assert_not_null_goto(ctx->mid, error);

    /* check resulting configuration */
    runtime_config = margo_get_config(ctx->mid);

    fprintf(stderr, "%s\n", runtime_config);
    /* just one pool with the __rpc__ name */
    count = count_occurrence(runtime_config, "my_pool");
    munit_assert_int_goto(count, ==, 1, error);

    /* just one pool with the prio_wait kind */
    count = count_occurrence(runtime_config, pool_kind);
    munit_assert_int_goto(count, ==, 1, error);

    free(runtime_config);

    struct margo_pool_info info = {0};
    hg_return_t hret = margo_find_pool_by_name(ctx->mid, "my_pool", &info);
    munit_assert_int_goto(hret, ==, HG_SUCCESS, error);

    // try to post 4 ULTs to the pool
    ABT_thread ults[4];
    for(unsigned i=0; i < 4; ++i) {
        ABT_thread_create(info.pool, thread_func, NULL, ABT_THREAD_ATTR_NULL, ults+i);
    }
    for(unsigned i=0; i < 4; ++i) {
        ABT_thread_join(ults[i]);
        ABT_thread_free(ults+i);
    }

    margo_finalize(ctx->mid);

    return MUNIT_OK;

error:
    return MUNIT_FAIL;
}

static char* pool_params[] = {
    "prio_wait",
    "efirst_wait",
    NULL
};

static char * json_params[] = {
    "{ \"rpc_thread_count\":0, \"argobots\":{ \"pools\":[ { \"name\":\"my_pool\", \"kind\":\"efirst_wait\" } ] } }",
    NULL
};

static MunitParameterEnum rpc_pool_kind_params[] = {
    { "pool", pool_params},
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
