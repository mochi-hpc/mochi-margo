/*
 * (C) 2024 The University of Chicago
 *
 * See COPYRIGHT in top-level directory.
 *
 * Regression test for thread-safety of the (pre)finalize callback lists.
 *
 * margo_provider_push/pop/top_finalize_callback and their prefinalize
 * counterparts mutate a singly-linked list hanging off the margo instance. When
 * several providers are registered on one instance and then destroyed
 * concurrently (each destroy pops its own finalize callback), those pop calls
 * race on the shared list. Before the fix the list was manipulated with no lock,
 * so a lost update could leave a callback whose owner had already been freed on
 * the list, which margo_finalize would then invoke -- a use-after-free.
 *
 * These tests hammer the list from many Argobots ULTs running on a multi-xstream
 * pool. The correctness property is exact: every push is paired with a pop, so
 * after the workload the list must be empty and margo_finalize must invoke zero
 * callbacks. Run under AddressSanitizer/valgrind, a lost update or freed-node
 * traversal is caught directly.
 */
#include <margo.h>
#include <abt.h>
#include <stdlib.h>
#include <stdatomic.h>
#include "munit/munit.h"

struct test_context {
    margo_instance_id mid;
};

static _Atomic int  g_invoked;
static _Atomic int  g_go;      /* start barrier: release all ULTs at once */
static _Atomic int  g_ready;   /* count of ULTs parked on the barrier */

static void count_cb(void* arg)
{
    (void)arg;
    atomic_fetch_add(&g_invoked, 1);
}

/* Park until every worker is ready, so the list operations overlap as much as
 * possible instead of trickling out one ULT at a time. */
static void barrier_wait(void)
{
    atomic_fetch_add(&g_ready, 1);
    while (atomic_load(&g_go) == 0) ABT_thread_yield();
}

static void* test_context_setup(const MunitParameter params[], void* user_data)
{
    (void)params;
    (void)user_data;
    return calloc(1, sizeof(struct test_context));
}

static void test_context_tear_down(void* data)
{
    struct test_context* ctx = (struct test_context*)data;
    if (ctx->mid) margo_finalize(ctx->mid);
    free(ctx);
}

/* Each ULT pops the finalize callback for its own owner, modelling several
 * flock_provider_destroy() calls racing on the same instance. */
struct pop_arg {
    margo_instance_id mid;
    const void*       owner;
};

static void pop_ult(void* a)
{
    struct pop_arg* arg = (struct pop_arg*)a;
    barrier_wait();
    margo_provider_pop_finalize_callback(arg->mid, arg->owner);
}

static MunitResult concurrent_pop(const MunitParameter params[], void* data)
{
    (void)params;
    struct test_context* ctx = (struct test_context*)data;

    ctx->mid = margo_init("na+sm", MARGO_SERVER_MODE, 1, 4);
    munit_assert_not_null(ctx->mid);

    ABT_pool pool = ABT_POOL_NULL;
    munit_assert_int(margo_get_handler_pool(ctx->mid, &pool), ==, HG_SUCCESS);
    munit_assert(pool != ABT_POOL_NULL);

    const int N = 256;
    atomic_store(&g_invoked, 0);
    atomic_store(&g_go, 0);
    atomic_store(&g_ready, 0);

    /* Register N finalize callbacks with distinct, non-NULL owners. */
    for (int i = 0; i < N; i++)
        margo_provider_push_finalize_callback(
            ctx->mid, (const void*)(uintptr_t)(i + 1), count_cb, NULL);

    /* Concurrently pop them all from many ULTs, released together. */
    struct pop_arg args[N];
    ABT_thread     ths[N];
    for (int i = 0; i < N; i++) {
        args[i].mid   = ctx->mid;
        args[i].owner = (const void*)(uintptr_t)(i + 1);
        munit_assert_int(ABT_thread_create(pool, pop_ult, &args[i],
                                           ABT_THREAD_ATTR_NULL, &ths[i]),
                         ==, ABT_SUCCESS);
    }
    while (atomic_load(&g_ready) < N) ABT_thread_yield();
    atomic_store(&g_go, 1);
    for (int i = 0; i < N; i++) {
        ABT_thread_join(ths[i]);
        ABT_thread_free(&ths[i]);
    }

    /* Every callback was popped, so finalize must invoke none of them. */
    margo_finalize(ctx->mid);
    ctx->mid = MARGO_INSTANCE_NULL;
    munit_assert_int(atomic_load(&g_invoked), ==, 0);

    return MUNIT_OK;
}

/* Each ULT repeatedly pushes then pops its own owner, so pushes and pops from
 * different owners interleave on the shared list. */
struct push_pop_arg {
    margo_instance_id mid;
    const void*       owner;
    int               iters;
};

static void push_pop_ult(void* a)
{
    struct push_pop_arg* arg = (struct push_pop_arg*)a;
    barrier_wait();
    for (int k = 0; k < arg->iters; k++) {
        margo_provider_push_finalize_callback(arg->mid, arg->owner, count_cb,
                                              NULL);
        int popped = margo_provider_pop_finalize_callback(arg->mid, arg->owner);
        /* This ULT is the only one using this owner, so its own push must
         * always be found and removed by its own pop. */
        munit_assert_int(popped, ==, 1);
    }
}

static MunitResult concurrent_push_pop(const MunitParameter params[], void* data)
{
    (void)params;
    struct test_context* ctx = (struct test_context*)data;

    ctx->mid = margo_init("na+sm", MARGO_SERVER_MODE, 1, 4);
    munit_assert_not_null(ctx->mid);

    ABT_pool pool = ABT_POOL_NULL;
    munit_assert_int(margo_get_handler_pool(ctx->mid, &pool), ==, HG_SUCCESS);
    munit_assert(pool != ABT_POOL_NULL);

    const int N = 64;
    atomic_store(&g_invoked, 0);
    atomic_store(&g_go, 0);
    atomic_store(&g_ready, 0);

    struct push_pop_arg args[N];
    ABT_thread          ths[N];
    for (int i = 0; i < N; i++) {
        args[i].mid   = ctx->mid;
        args[i].owner = (const void*)(uintptr_t)(i + 1);
        args[i].iters = 200;
        munit_assert_int(ABT_thread_create(pool, push_pop_ult, &args[i],
                                           ABT_THREAD_ATTR_NULL, &ths[i]),
                         ==, ABT_SUCCESS);
    }
    while (atomic_load(&g_ready) < N) ABT_thread_yield();
    atomic_store(&g_go, 1);
    for (int i = 0; i < N; i++) {
        ABT_thread_join(ths[i]);
        ABT_thread_free(&ths[i]);
    }

    /* All pushes were paired with pops, so the list is empty and finalize
     * invokes nothing. */
    margo_finalize(ctx->mid);
    ctx->mid = MARGO_INSTANCE_NULL;
    munit_assert_int(atomic_load(&g_invoked), ==, 0);

    return MUNIT_OK;
}

static MunitTest tests[] = {
    {"/concurrent-pop", concurrent_pop, test_context_setup,
     test_context_tear_down, MUNIT_TEST_OPTION_NONE, NULL},
    {"/concurrent-push-pop", concurrent_push_pop, test_context_setup,
     test_context_tear_down, MUNIT_TEST_OPTION_NONE, NULL},
    {NULL, NULL, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL}};

static const MunitSuite test_suite
    = {"/margo", tests, NULL, 1, MUNIT_SUITE_OPTION_NONE};

int main(int argc, char** argv)
{
    return munit_suite_main(&test_suite, NULL, argc, argv);
}
