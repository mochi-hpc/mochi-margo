
/*
 * (C) 2015 The University of Chicago
 * 
 * See COPYRIGHT in top-level directory.
 */

#include <unistd.h>
#include <errno.h>
#include <abt.h>
#include <stdlib.h>

#include <margo-config.h>
#include <time.h>
#include <math.h>

#include "margo.h"
#include "margo-timer.h"
#include "utlist.h"
#include "uthash.h"

struct diag_data
{
    double min;
    double max;
    double cumulative;
    int count;
};

struct margo_handle_cache_el; /* defined in margo.c */
struct margo_finalize_cb; /* defined in margo.c */
struct margo_timer_list; /* defined in margo-timer.c */

struct margo_instance
{
    /* mercury/argobots state */
    hg_context_t *hg_context;
    hg_class_t *hg_class;
    ABT_pool handler_pool;
    ABT_pool progress_pool;

    /* internal to margo for this particular instance */
    int margo_init;
    int abt_init;
    ABT_thread hg_progress_tid;
    int hg_progress_shutdown_flag;
    ABT_xstream progress_xstream;
    int owns_progress_pool;
    ABT_xstream *rpc_xstreams;
    int num_handler_pool_threads;
    unsigned int hg_progress_timeout_ub;

    /* control logic for callers waiting on margo to be finalized */
    int finalize_flag;
    int refcount;
    ABT_mutex finalize_mutex;
    ABT_cond finalize_cond;
    struct margo_finalize_cb* finalize_cb;

    /* control logic to prevent margo_finalize from destroying
       the instance when some operations are pending */
    unsigned pending_operations;
    ABT_mutex pending_operations_mtx;
    int finalize_requested;

    /* control logic for shutting down */
    hg_id_t shutdown_rpc_id;
    int enable_remote_shutdown;

    /* timer data */
    struct margo_timer_list* timer_list;
    /* linked list of free hg handles and a hash of in-use handles */
    struct margo_handle_cache_el *free_handle_list;
    struct margo_handle_cache_el *used_handle_hash;
    ABT_mutex handle_cache_mtx; /* mutex protecting access to above caches */

    /* optional diagnostics data tracking */
    /* NOTE: technically the following fields are subject to races if they
     * are updated from more than one thread at a time.  We will be careful
     * to only update the counters from the progress_fn,
     * which will serialize access.
     */
    int diag_enabled;
    struct diag_data diag_trigger_elapsed;
    struct diag_data diag_progress_elapsed_zero_timeout;
    struct diag_data diag_progress_elapsed_nonzero_timeout;
    struct diag_data diag_progress_timeout_value;
};
