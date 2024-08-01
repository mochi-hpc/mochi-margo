
#include <margo.h>
#include "helper-server.h"
#include "munit/munit.h"

static struct margo_logger test_logger = {0};

struct test_context {
    margo_instance_id mid;
    char*             log_buffer;
    int               log_buffer_pos;
    int               log_buffer_size;
};

static void test_log_fn(void* uargs, const char* str)
{
    struct test_context* ctx = uargs;

    /* check for overflow */
    munit_assert_int(strlen(str) + ctx->log_buffer_pos, <,
                     ctx->log_buffer_size);

    /* directly copy msg to buffer */
    strcpy(&ctx->log_buffer[ctx->log_buffer_pos], str);
    ctx->log_buffer_pos += strlen(str);

    return;
}

static void* test_context_setup(const MunitParameter params[], void* user_data)
{
    (void)params;
    (void)user_data;
    int                 ret;

    struct test_context* ctx = calloc(1, sizeof(*ctx));
    ctx->log_buffer          = calloc(102400, 1);
    ctx->log_buffer_size     = 102400;

    /* set up custom logger to make it easier to validate output */
    test_logger.uargs    = ctx;
    test_logger.trace    = test_log_fn;
    test_logger.debug    = test_log_fn;
    test_logger.info     = test_log_fn;
    test_logger.warning  = test_log_fn;
    test_logger.error    = test_log_fn;
    test_logger.critical = test_log_fn;
    ret = margo_set_global_logger(&test_logger);
    munit_assert_int(ret, ==, 0);

    return ctx;
}

static void test_context_tear_down(void* data)
{
    struct test_context* ctx = (struct test_context*)data;

    free(ctx->log_buffer);
    free(ctx);
}

static MunitResult pool_is_not_used(const MunitParameter params[], void* data)
{
    (void)params;
    struct test_context* ctx = (struct test_context*)data;

    const char* config = "{"
        "\"argobots\":{"
            "\"pools\":["
                "{\"name\":\"__primary__\",\"kind\":\"fifo_wait\"},"
                "{\"name\":\"p1\",\"kind\":\"fifo_wait\"}"
            "],"
            "\"xstreams\":["
                "{\"name\":\"__primary__\","
                 "\"scheduler\":{\"pools\":[\"__primary__\"],\"type\":\"basic_wait\"}}"
            "]"
        "},"
        "\"progress_pool\":\"__primary__\""
    "}";

    struct margo_init_info info = {0};
    info.json_config = config;
    margo_instance_id mid = margo_init_ext("na+sm", MARGO_SERVER_MODE, &info);
    munit_assert_not_null(mid);

    munit_assert_int(ctx->log_buffer_pos, !=, 0);
    char* expected_content = "Pool \"p1\" at index 1 is not currently associated"
        " with any ES. ULT pushed into that pool will not get executed.";
    munit_assert_string_equal(ctx->log_buffer, expected_content);
    margo_finalize(mid);

    return MUNIT_OK;
}

static MunitResult pool_is_not_first(const MunitParameter params[], void* data)
{
    (void)params;
    struct test_context* ctx = (struct test_context*)data;

    const char* config = "{"
        "\"argobots\":{"
            "\"pools\":["
                "{\"name\":\"__primary__\",\"kind\":\"fifo_wait\"},"
                "{\"name\":\"p1\",\"kind\":\"fifo_wait\"}"
            "],"
            "\"xstreams\":["
                "{\"name\":\"__primary__\","
                 "\"scheduler\":{\"pools\":[\"__primary__\",\"p1\"],\"type\":\"basic_wait\"}}"
            "]"
        "},"
        "\"use_progress_thread\":true"
    "}";

    struct margo_init_info info = {0};
    info.json_config = config;
    margo_instance_id mid = margo_init_ext("na+sm", MARGO_SERVER_MODE, &info);
    munit_assert_not_null(mid);

    munit_assert_int(ctx->log_buffer_pos, !=, 0);
    char* expected_content = "Pool \"p1\" at index 1 is not the first pool of any ES. "
        "This could cause starvation for ULTs pushed in that pool.";
    munit_assert_string_equal(ctx->log_buffer, expected_content);
    margo_finalize(mid);

    return MUNIT_OK;
}

static MunitResult pool_not_before_progress(const MunitParameter params[], void* data)
{
    (void)params;
    struct test_context* ctx = (struct test_context*)data;

    const char* config = "{"
        "\"argobots\":{"
            "\"pools\":["
                "{\"name\":\"__primary__\",\"kind\":\"fifo_wait\"},"
                "{\"name\":\"p1\",\"kind\":\"fifo_wait\"}"
            "],"
            "\"xstreams\":["
                "{\"name\":\"__primary__\","
                 "\"scheduler\":{\"pools\":[\"__primary__\",\"p1\"],\"type\":\"basic_wait\"}}"
            "]"
        "},"
        "\"progress_pool\":\"__primary__\""
    "}";

    struct margo_init_info info = {0};
    info.json_config = config;
    margo_instance_id mid = margo_init_ext("na+sm", MARGO_SERVER_MODE, &info);
    munit_assert_not_null(mid);

    munit_assert_int(ctx->log_buffer_pos, !=, 0);
    char* expected_content =
        "Pool \"p1\" at index 1 is not the first pool of any ES."
        " This could cause starvation for ULTs pushed in that pool."
        "Pool \"p1\" at index 1 does not appear before the progress pool in any ES."
        " Depending on the type of scheduler used, this may cause ULTs pushed in that"
        " pool to never execute because the progress pool will keep the ES busy."
        "Pool \"p1\" at index 1 appears after the progress pool in at least one ES."
        " Depending on the type of scheduler used, this ES may never pull ULTs from"
        " that pool because the progress pool will keep the ES busy."
        "Pool \"p1\" at index 1 is used by an ES that is also associated with the"
        " progress pool. This may cause ULTs pushed into that pool to get unnecessarily delayed.";
    munit_assert_string_equal(ctx->log_buffer, expected_content);
    margo_finalize(mid);
    return MUNIT_OK;
}

static MunitResult progress_pool_is_not_last(const MunitParameter params[], void* data)
{
    (void)params;
    struct test_context* ctx = (struct test_context*)data;

    const char* config = "{"
        "\"argobots\":{"
            "\"pools\":["
                "{\"name\":\"__primary__\",\"kind\":\"fifo_wait\"},"
                "{\"name\":\"p1\",\"kind\":\"fifo_wait\"}"
            "],"
            "\"xstreams\":["
                "{\"name\":\"__primary__\","
                 "\"scheduler\":{\"pools\":[\"__primary__\", \"p1\"],\"type\":\"basic_wait\"}},"
                "{\"name\":\"es1\","
                 "\"scheduler\":{\"pools\":[\"p1\", \"__primary__\"],\"type\":\"basic_wait\"}},"
            "]"
        "},"
        "\"progress_pool\":\"__primary__\""
    "}";

    struct margo_init_info info = {0};
    info.json_config = config;
    margo_instance_id mid = margo_init_ext("na+sm", MARGO_SERVER_MODE, &info);
    munit_assert_not_null(mid);

    munit_assert_int(ctx->log_buffer_pos, !=, 0);
    char* expected_content = "Pool \"p1\" at index 1 appears after the progress pool"
        " in at least one ES. Depending on the type of scheduler used, this ES may "
        "never pull ULTs from that pool because the progress pool will keep the ES busy."
        "Pool \"p1\" at index 1 is used by an ES that is also associated with the progress pool."
        " This may cause ULTs pushed into that pool to get unnecessarily delayed.";
    munit_assert_string_equal(ctx->log_buffer, expected_content);
    margo_finalize(mid);

    return MUNIT_OK;
}

static MunitTest tests[]
    = {{"/pool_is_not_used", pool_is_not_used, test_context_setup,
        test_context_tear_down, MUNIT_TEST_OPTION_NONE, NULL},
       {"/pool_is_not_first", pool_is_not_first, test_context_setup,
        test_context_tear_down, MUNIT_TEST_OPTION_NONE, NULL},
       {"/pool_not_before_progress", pool_not_before_progress, test_context_setup,
        test_context_tear_down, MUNIT_TEST_OPTION_NONE, NULL},
       {"/progress_pool_is_not_last", progress_pool_is_not_last, test_context_setup,
        test_context_tear_down, MUNIT_TEST_OPTION_NONE, NULL},
       {NULL, NULL, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL}};

static const MunitSuite test_suite
    = {"/margo", tests, NULL, 1, MUNIT_SUITE_OPTION_NONE};

int main(int argc, char** argv)
{
    return munit_suite_main(&test_suite, NULL, argc, argv);
}
