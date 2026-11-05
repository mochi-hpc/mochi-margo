/*
 * (C) 2020 The University of Chicago
 *
 * See COPYRIGHT in top-level directory.
 */

/* The purpose of these tests are to check the behavior of the Argobots
 * scheduler in conjunction with Margo in various scenarios
 */

#include <stdio.h>
#include <unistd.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <pthread.h>

#include <margo.h>
#include "helper-server.h"
#include "munit/munit.h"

struct test_context {
    margo_instance_id mid;
    ABT_mutex         mutex;
    int               value;
    pthread_t         ext_thread_tid;
    pthread_cond_t    ext_thread_cond;
    pthread_mutex_t   ext_thread_mtx;
    int               ext_thread_go;
};

/* External pthread function, to be started *before* margo and abt are
 * initialized. During unit tests it will be awakened to block on abt constructs
 * and confirm CPU utilization
 */
void* ext_thread_fn(void* arg)
{
    struct test_context* ctx = (struct test_context*)arg;

    /* wait until the test is ready */
    while (!ctx->ext_thread_go) {
        pthread_cond_wait(&ctx->ext_thread_cond, &ctx->ext_thread_mtx);
    }

    /* block on acquiring an Argobots mutex */
    ABT_mutex_lock(ctx->mutex);
    ABT_mutex_unlock(ctx->mutex);

    return (NULL);
}

static void* test_context_setup(const MunitParameter params[], void* user_data)
{
    (void)params;
    (void)user_data;
    struct test_context*   ctx = calloc(1, sizeof(*ctx));
    struct margo_init_info mii = {0};

    char* protocol = "na+sm";

    /* Ask margo to create a dedicated pool (with one execution stream) for
     * rpc handling.
     */
    mii.json_config = "{ \"rpc_thread_count\":1}";

    ctx->mid = margo_init_ext(protocol, MARGO_SERVER_MODE, &mii);
    munit_assert_not_null(ctx->mid);

    ABT_mutex_create(&ctx->mutex);
    pthread_cond_init(&ctx->ext_thread_cond, NULL);

    pthread_create(&ctx->ext_thread_tid, NULL, ext_thread_fn, ctx);

    return ctx;
}

static void test_context_tear_down(void* fixture)
{
    struct test_context* ctx = (struct test_context*)fixture;

    pthread_join(ctx->ext_thread_tid, NULL);
    pthread_cond_destroy(&ctx->ext_thread_cond);
    pthread_mutex_destroy(&ctx->ext_thread_mtx);

    ABT_mutex_free(&ctx->mutex);

    margo_finalize(ctx->mid);

    free(ctx);
}

void thread_fn(void* _arg)
{
    struct test_context* ctx = (struct test_context*)_arg;

    ABT_mutex_lock(ctx->mutex);
    ABT_mutex_unlock(ctx->mutex);

    return;
}

static MunitResult test_abt_mutex_cpu_ext_thread(const MunitParameter params[],
                                                 void*                data)
{
    (void)params;
    (void)data;
    int           ret;
    struct rusage usage;
    double        user_cpu_seconds1, user_cpu_seconds2;

    struct test_context* ctx = (struct test_context*)data;

    ret = getrusage(RUSAGE_SELF, &usage);
    munit_assert_int(ret, ==, 0);
    user_cpu_seconds1 = (double)usage.ru_utime.tv_sec
                      + (double)usage.ru_utime.tv_usec / 1000000.0;

    /* acquire abt mutex */
    ABT_mutex_lock(ctx->mutex);

    /* wake up external thread */
    pthread_mutex_lock(&ctx->ext_thread_mtx);
    ctx->ext_thread_go = 1;
    pthread_cond_signal(&ctx->ext_thread_cond);
    pthread_mutex_unlock(&ctx->ext_thread_mtx);

    /* sleep before releasing mutex */
    margo_thread_sleep(ctx->mid, 5000);
    ABT_mutex_unlock(ctx->mutex);

    ret = getrusage(RUSAGE_SELF, &usage);
    munit_assert_int(ret, ==, 0);
    user_cpu_seconds2 = (double)usage.ru_utime.tv_sec
                      + (double)usage.ru_utime.tv_usec / 1000000.0;

    printf("User CPU time used: %f\n", user_cpu_seconds2 - user_cpu_seconds1);
    if (user_cpu_seconds2 - user_cpu_seconds1 > 4.0)
        printf(
            "\tdetected that Argobots mutexes may cause external thread to "
            "busy spin.\n");
    else
        printf(
            "\tdetected that Argobots mutexes will not cause external thread "
            "to busy spin.\n");

    return MUNIT_OK;
}

static MunitResult test_abt_mutex_cpu(const MunitParameter params[], void* data)
{
    (void)params;
    (void)data;
    ABT_pool      rpc_pool;
    ABT_thread    tid;
    int           ret;
    struct rusage usage;
    double        user_cpu_seconds1, user_cpu_seconds2;

    struct test_context* ctx = (struct test_context*)data;

    ret = getrusage(RUSAGE_SELF, &usage);
    munit_assert_int(ret, ==, 0);
    user_cpu_seconds1 = (double)usage.ru_utime.tv_sec
                      + (double)usage.ru_utime.tv_usec / 1000000.0;

    /* hold mutex while creating ULT */
    ABT_mutex_lock(ctx->mutex);

    /* launch test thread in rpc handler pool */
    margo_get_handler_pool(ctx->mid, &rpc_pool);
    ABT_thread_create(rpc_pool, thread_fn, ctx, ABT_THREAD_ATTR_NULL, &tid);

    /* sleep before releasing mutex */
    margo_thread_sleep(ctx->mid, 5000);
    ABT_mutex_unlock(ctx->mutex);

    /* wait for test thread to complete */
    ABT_thread_join(tid);
    ABT_thread_free(&tid);

    ret = getrusage(RUSAGE_SELF, &usage);
    munit_assert_int(ret, ==, 0);
    user_cpu_seconds2 = (double)usage.ru_utime.tv_sec
                      + (double)usage.ru_utime.tv_usec / 1000000.0;

    printf("User CPU time used: %f\n", user_cpu_seconds2 - user_cpu_seconds1);
    if (user_cpu_seconds2 - user_cpu_seconds1 > 4.0)
        printf("\tdetected that Argobots mutexes may busy spin.\n");
    else
        printf("\tdetected that Argobots mutexes will not busy spin.\n");

    /* wake up external thread so that it can exit */
    pthread_mutex_lock(&ctx->ext_thread_mtx);
    ctx->ext_thread_go = 1;
    pthread_cond_signal(&ctx->ext_thread_cond);
    pthread_mutex_unlock(&ctx->ext_thread_mtx);

    return MUNIT_OK;
}

static MunitTest test_suite_tests[] = {
    {(char*)"/abt_mutex_cpu", test_abt_mutex_cpu, test_context_setup,
     test_context_tear_down, MUNIT_TEST_OPTION_NONE, NULL},
    {(char*)"/abt_mutex_cpu/ext-thread", test_abt_mutex_cpu_ext_thread,
     test_context_setup, test_context_tear_down, MUNIT_TEST_OPTION_NONE, NULL},
    {NULL, NULL, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL}};

static const MunitSuite test_suite
    = {(char*)"/margo", test_suite_tests, NULL, 1, MUNIT_SUITE_OPTION_NONE};

int main(int argc, char* argv[MUNIT_ARRAY_PARAM(argc + 1)])
{
    return munit_suite_main(&test_suite, NULL, argc, argv);
}
