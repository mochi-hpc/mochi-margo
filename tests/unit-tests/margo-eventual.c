
#include <string.h>
#include <unistd.h>
#include <margo.h>
#include "helper-server.h"
#include "munit/munit.h"
/* NOTE: this is testing internal capabilities in Margo; not exposed to
 * end-users normally
 */
#include "../../src/margo-abt-macros.h"

#define N_ITERS 10000
#define N_ULTS  64

/* the intent of these unit tests is to verify correct operation of the
 * margo eventual constructs in different configurations
 */

struct iteration {
    margo_eventual_t ev;
    ABT_thread       waiter_tid;
    ABT_thread       setter_tid;
};

struct test_context {
    margo_instance_id mid;
    struct iteration* iter_array;
};

static void* test_context_setup(const MunitParameter params[], void* user_data)
{
    (void)params;
    (void)user_data;
    struct test_context* ctx = calloc(1, sizeof(*ctx));
    ctx->iter_array          = malloc(N_ITERS * sizeof(*ctx->iter_array));

    return ctx;
}

static void test_context_tear_down(void* data)
{
    struct test_context* ctx = (struct test_context*)data;

    free(ctx->iter_array);
    free(ctx);
}

void setter_fn(void* _arg)
{
    margo_eventual_t* ev = (margo_eventual_t*)_arg;

    MARGO_EVENTUAL_SET(*ev);

    return;
}

void waiter_fn(void* _arg)
{
    margo_eventual_t* ev = (margo_eventual_t*)_arg;

    MARGO_EVENTUAL_CREATE(ev);

    MARGO_EVENTUAL_WAIT(*ev);

    return;
}

void waiter_sub_fn(void)
{

    return;
}

void iter_fn(void* _arg)
{
    int i;

    for (i = 0; i < N_ITERS; i++) {
        waiter_sub_fn();
    }

    return;
}

static MunitResult margo_eventual_iteration(const MunitParameter params[],
                                            void*                data)
{
    const char*            protocol = "na+sm";
    struct margo_init_info mii      = {0};
    struct test_context*   ctx      = (struct test_context*)data;
    int                    i;
    ABT_pool               rpc_pool;
    ABT_thread             tid_array[N_ULTS];

    mii.json_config = munit_parameters_get(params, "json");

    ctx->mid = margo_init_ext(protocol, MARGO_SERVER_MODE, &mii);
    munit_assert_not_null(ctx->mid);
    margo_get_handler_pool(ctx->mid, &rpc_pool);

    for (i = 0; i < N_ULTS; i++) {
        ABT_thread_create(rpc_pool, iter_fn, NULL,
                          ABT_THREAD_ATTR_NULL, &tid_array[i]);
    }

    for (i = 0; i < N_ULTS; i++) { ABT_thread_join(tid_array[i]); }

    margo_finalize(ctx->mid);

    return MUNIT_OK;
}

static MunitResult margo_eventual(const MunitParameter params[], void* data)
{
    const char*            protocol = "na+sm";
    struct margo_init_info mii      = {0};
    struct test_context*   ctx      = (struct test_context*)data;
    int                    i;
    ABT_pool               rpc_pool;

    mii.json_config = munit_parameters_get(params, "json");

    ctx->mid = margo_init_ext(protocol, MARGO_SERVER_MODE, &mii);
    munit_assert_not_null(ctx->mid);
    margo_get_handler_pool(ctx->mid, &rpc_pool);

#if 0
    for(i=0; i<N_ITERS; i++)
    {
        MARGO_EVENTUAL_CREATE(&ctx->iter_array[i].ev);
    }
#endif

    for (i = 0; i < N_ITERS; i++) {
        ABT_thread_create(rpc_pool, waiter_fn, &ctx->iter_array[i].ev,
                          ABT_THREAD_ATTR_NULL, &ctx->iter_array[i].waiter_tid);
    }

    margo_thread_sleep(ctx->mid, 1000);

#if 0
    for(i=0; i<N_ITERS; i++)
    {
        MARGO_EVENTUAL_SET(ctx->iter_array[i].ev);
    }
#else
    for (i = 0; i < N_ITERS; i++) {
        ABT_thread_create(rpc_pool, setter_fn, &ctx->iter_array[i].ev,
                          ABT_THREAD_ATTR_NULL, &ctx->iter_array[i].setter_tid);
    }
#endif

    for (i = 0; i < N_ITERS; i++) {
        ABT_thread_join(ctx->iter_array[i].waiter_tid);
        ABT_thread_join(ctx->iter_array[i].setter_tid);
        MARGO_EVENTUAL_FREE(&ctx->iter_array[i].ev);
    }

    margo_finalize(ctx->mid);

    return MUNIT_OK;
}

static char* json_params[] = {"{}", /* no dedicated rpc pool */
                              "{\"rpc_thread_count\":2}", /* 2 ESes for RPCs */
                              "{\"rpc_thread_count\":4}", /* 4 ESes for RPCs */
                              "{\"rpc_thread_count\":8}", /* 8 ESes for RPCs */
                              NULL};

static MunitParameterEnum margo_eventual_params[]
    = {{"json", json_params}, {NULL, NULL}};

static MunitTest tests[]
    = {{"/eventual_per_ult", margo_eventual, test_context_setup,
        test_context_tear_down, MUNIT_TEST_OPTION_NONE, margo_eventual_params},
       {"/eventual_per_iteration", margo_eventual_iteration, test_context_setup,
        test_context_tear_down, MUNIT_TEST_OPTION_NONE, margo_eventual_params},
       {NULL, NULL, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL}};

static const MunitSuite test_suite
    = {"/margo", tests, NULL, 1, MUNIT_SUITE_OPTION_NONE};

int main(int argc, char** argv)
{
    return munit_suite_main(&test_suite, NULL, argc, argv);
}
