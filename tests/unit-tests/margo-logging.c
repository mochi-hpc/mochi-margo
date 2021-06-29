
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

    char* protocol = "na+sm";

    ctx->mid = margo_init(protocol, MARGO_CLIENT_MODE, 0, 0);
    munit_assert_not_null(ctx->mid);

    /* associate the same logger with the instance as well */
    ret = margo_set_logger(ctx->mid, &test_logger);
    munit_assert_int(ret, ==, 0);

    return ctx;
}

static void test_context_tear_down(void* data)
{
    struct test_context* ctx = (struct test_context*)data;

    margo_finalize(ctx->mid);

    free(ctx->log_buffer);
    free(ctx);
}

static MunitResult init_quiet_log(const MunitParameter params[], void* data)
{
    struct test_context* ctx = (struct test_context*)data;
    char* protocol = "na+sm";
    int ret;

    /* finalize and re-initialize margo and make sure that no log messages
     * were emitted at the default level
     */
    margo_finalize(ctx->mid);

    ctx->mid = margo_init(protocol, MARGO_CLIENT_MODE, 0, 0);
    munit_assert_not_null(ctx->mid);

    /* associate logger with the new instance */
    ret = margo_set_logger(ctx->mid, &test_logger);
    munit_assert_int(ret, ==, 0);

    /* check to see if any messages were emitted */
    if(ctx->log_buffer_pos != 0)
        fprintf(stderr, "Test failure; spurious log messages: %s\n", ctx->log_buffer);
    munit_assert_int(ctx->log_buffer_pos, ==, 0);

    return MUNIT_OK;
}

static char* mid_params[] = {"mid", "NULL", NULL};
static MunitParameterEnum get_mid[] = {{"mid", mid_params}, {NULL, NULL}};

static MunitResult default_log_level(const MunitParameter params[], void* data)
{
    struct test_context* ctx = (struct test_context*)data;
    margo_instance_id mid = NULL;
    const char* mid_param;

    /* test both mid-specific logger and global logger.  Both should have
     * same default level.
     */
    mid_param = munit_parameters_get(params, "mid");
    if(strcmp("mid", mid_param) == 0)
        mid = ctx->mid;
    else if(strcmp("NULL", mid_param) == 0)
        mid = NULL;
    else
        munit_assert(0);

    /* Expected result: default log level will record messages of level
     * warning and higher.
     */
    margo_trace(mid, "trace ");
    munit_assert_null(strstr(ctx->log_buffer, "trace"));

    margo_debug(mid, "debug ");
    munit_assert_null(strstr(ctx->log_buffer, "debug"));

    margo_info(mid, "info ");
    munit_assert_null(strstr(ctx->log_buffer, "info"));

    margo_warning(mid, "warning ");
    munit_assert_not_null(strstr(ctx->log_buffer, "warning"));

    margo_error(mid, "error ");
    munit_assert_not_null(strstr(ctx->log_buffer, "error"));

    margo_critical(mid, "critical ");
    munit_assert_not_null(strstr(ctx->log_buffer, "critical"));

    return MUNIT_OK;
}

static char* log_level_params[] = {"trace", "debug", "info", "warning", "error", "critical", NULL};
static MunitParameterEnum get_log_level[] = {{"log_level", log_level_params}, {NULL, NULL}};

static MunitResult vary_log_level(const MunitParameter params[], void* data)
{
    struct test_context* ctx = (struct test_context*)data;
    const char* level;
    int ret;
    margo_log_level mll = 0;

    level = munit_parameters_get(params, "log_level");
    if(strcmp("trace", level) == 0)
        mll = MARGO_LOG_TRACE;
    if(strcmp("debug", level) == 0)
        mll = MARGO_LOG_DEBUG;
    if(strcmp("info", level) == 0)
        mll = MARGO_LOG_INFO;
    if(strcmp("warning", level) == 0)
        mll = MARGO_LOG_WARNING;
    if(strcmp("error", level) == 0)
        mll = MARGO_LOG_ERROR;
    if(strcmp("critical", level) == 0)
        mll = MARGO_LOG_CRITICAL;

    ret = margo_set_log_level(ctx->mid, mll);
    munit_assert_int(ret, ==, 0);

    margo_trace(ctx->mid, "TRACE ");
    margo_debug(ctx->mid, "DEBUG ");
    margo_info(ctx->mid, "INFO ");
    margo_warning(ctx->mid, "WARNING ");
    margo_error(ctx->mid, "ERROR ");
    margo_critical(ctx->mid, "CRITICAL ");

    if(mll > MARGO_LOG_TRACE)
        munit_assert_null(strstr(ctx->log_buffer, "TRACE"));
    else
        munit_assert_not_null(strstr(ctx->log_buffer, "TRACE"));
    if(mll > MARGO_LOG_DEBUG)
        munit_assert_null(strstr(ctx->log_buffer, "DEBUG"));
    else
        munit_assert_not_null(strstr(ctx->log_buffer, "DEBUG"));
    if(mll > MARGO_LOG_INFO)
        munit_assert_null(strstr(ctx->log_buffer, "INFO"));
    else
        munit_assert_not_null(strstr(ctx->log_buffer, "INFO"));
    if(mll > MARGO_LOG_WARNING)
        munit_assert_null(strstr(ctx->log_buffer, "WARNING"));
    else
        munit_assert_not_null(strstr(ctx->log_buffer, "WARNING"));
    if(mll > MARGO_LOG_ERROR)
        munit_assert_null(strstr(ctx->log_buffer, "ERROR"));
    else
        munit_assert_not_null(strstr(ctx->log_buffer, "ERROR"));
    if(mll > MARGO_LOG_CRITICAL)
        munit_assert_null(strstr(ctx->log_buffer, "CRITICAL"));
    else
        munit_assert_not_null(strstr(ctx->log_buffer, "CRITICAL"));

    return MUNIT_OK;
}

static MunitTest tests[]
    = {{"/default_log_level", default_log_level, test_context_setup,
        test_context_tear_down, MUNIT_TEST_OPTION_NONE, get_mid},
       {"/vary_log_level", vary_log_level, test_context_setup,
        test_context_tear_down, MUNIT_TEST_OPTION_NONE, get_log_level},
       {"/init_quiet_log", init_quiet_log, test_context_setup,
        test_context_tear_down, MUNIT_TEST_OPTION_NONE, NULL},
       {NULL, NULL, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL}};

static const MunitSuite test_suite
    = {"/margo", tests, NULL, 1, MUNIT_SUITE_OPTION_NONE};

int main(int argc, char** argv)
{
    return munit_suite_main(&test_suite, NULL, argc, argv);
}
