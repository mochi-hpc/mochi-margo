/*
 * (C) 2016 The University of Chicago
 *
 * See COPYRIGHT in top-level directory.
 */

#include <stdio.h>
#include <assert.h>
#include <unistd.h>
#include <stdlib.h>
#include <ctype.h>
#include <abt.h>
#include <margo.h>
#include <margo-logging.h>

static int               use_abt_sleep = 0;
static int               sleep_seconds = 2;
static margo_instance_id mid;

static void sleep_fn(void* arg);

int main(int argc, char** argv)
{
    ABT_thread  threads[4];
    int         t_ids[4];
    int         i;
    int         ret;
    ABT_xstream xstream;
    ABT_pool    pool;

    int arg_ndx = 1;
    if (argc > 1) {
        if (isdigit(argv[1][0])) {
            sleep_seconds = atoi(argv[1]);
            arg_ndx++;
        }
        if ((arg_ndx < argc) && (strcmp(argv[arg_ndx], "ABT") == 0)) {
            use_abt_sleep = 1;
            arg_ndx++;
        }
        if (arg_ndx != argc) {
            fprintf(stderr, "Usage: %s [sleep_seconds] [ABT]\n", argv[0]);
            fprintf(stderr,
                    "\tsleep_seconds: number of seconds for each thread to "
                    "sleep.\n");
            fprintf(
                stderr,
                "\tABT: use ABT sleep mechanism, rather than POSIX sleep.\n");
            return (-1);
        }
    }

    /* actually start margo -- this step encapsulates the Mercury and
     * Argobots initialization and must precede their use */
    /* use a single pool for progress and sleeper threads */
    /* NOTE: we don't use RPC handlers, so no need for an RPC pool */
    /***************************************/
    mid = margo_init("tcp", MARGO_CLIENT_MODE, 0, 0);
    //    margo_set_log_level(mid, MARGO_LOG_TRACE);

    if (mid == MARGO_INSTANCE_NULL) {
        /* if tcp didn't work, try sm */
        mid = margo_init("sm", MARGO_CLIENT_MODE, 0, 0);
        if (mid == MARGO_INSTANCE_NULL) {
            fprintf(stderr, "Error: margo_init()\n");
            return (-1);
        }
    }

    /* retrieve current pool to use for ULT creation */
    ret = ABT_xstream_self(&xstream);
    if (ret != 0) {
        fprintf(stderr, "Error: ABT_xstream_self()\n");
        return (-1);
    }
    ret = ABT_xstream_get_main_pools(xstream, 1, &pool);
    if (ret != 0) {
        fprintf(stderr, "Error: ABT_xstream_get_main_pools()\n");
        return (-1);
    }

    for (i = 0; i < 4; i++) {
        t_ids[i] = i;

        /* start up the sleeper threads */
        ret = ABT_thread_create(pool, sleep_fn, &t_ids[i], ABT_THREAD_ATTR_NULL,
                                &threads[i]);
        if (ret != 0) {
            fprintf(stderr, "Error: ABT_thread_create()\n");
            return (-1);
        }
    }

    /* yield to one of the threads */
    ABT_thread_yield_to(threads[0]);

    for (i = 0; i < 4; i++) {
        ret = ABT_thread_join(threads[i]);
        if (ret != 0) {
            fprintf(stderr, "Error: ABT_thread_join()\n");
            return (-1);
        }
        ret = ABT_thread_free(&threads[i]);
        if (ret != 0) {
            fprintf(stderr, "Error: ABT_thread_join()\n");
            return (-1);
        }
    }

    /* shut down everything */
    margo_finalize(mid);

    return (0);
}

static void sleep_fn(void* arg)
{
    int my_tid = *(int*)arg;

    if (use_abt_sleep)
        margo_thread_sleep(mid, sleep_seconds * 1000.0);
    else
        sleep(sleep_seconds);

    printf("TID: %d sleep end\n", my_tid);

    return;
}
