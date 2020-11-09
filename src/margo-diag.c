/*
 * (C) 2020 The University of Chicago
 *
 * See COPYRIGHT in top-level directory.
 */
#include <margo.h>
#include "margo-diag-internal.h"
#include "margo-instance.h"

void __margo_sparkline_data_collection_fn(void* foo)
{
    struct margo_instance* mid = (struct margo_instance*)foo;
    struct diag_data *     stat, *tmp;

    /* double check that profile collection should run, else, close this ULT */
    if (!mid->profile_enabled) {
        ABT_thread_join(mid->sparkline_data_collection_tid);
        ABT_thread_free(&mid->sparkline_data_collection_tid);
    }

    int sleep_time_msec = json_object_get_int64(json_object_object_get(
        mid->json_cfg, "profile_sparkline_timeslice_msec"));

    while (!mid->hg_progress_shutdown_flag) {
        margo_thread_sleep(mid, sleep_time_msec);
        HASH_ITER(hh, mid->diag_rpc, stat, tmp)
        {

            if (mid->sparkline_index > 0 && mid->sparkline_index < 100) {
                stat->sparkline_time[mid->sparkline_index]
                    = stat->stats.cumulative
                    - stat->sparkline_time[mid->sparkline_index - 1];
                stat->sparkline_count[mid->sparkline_index]
                    = stat->stats.count
                    - stat->sparkline_count[mid->sparkline_index - 1];
            } else if (mid->sparkline_index == 0) {
                stat->sparkline_time[mid->sparkline_index]
                    = stat->stats.cumulative;
                stat->sparkline_count[mid->sparkline_index] = stat->stats.count;
            } else {
                // Drop!
            }
        }
        mid->sparkline_index++;
        mid->previous_sparkline_data_collection_time = ABT_get_wtime();
    }

    return;
}

void __margo_print_diag_data(margo_instance_id mid,
                             FILE*             file,
                             const char*       name,
                             const char*       description,
                             struct diag_data* data)
{
    (void)mid;
    (void)description; // TODO was this supposed to be used?

    double avg;

    if (data->stats.count != 0)
        avg = data->stats.cumulative / data->stats.count;
    else
        avg = 0;

    fprintf(file, "%s,%.9f,%.9f,%.9f,%.9f,%lu\n", name, avg,
            data->stats.cumulative, data->stats.min, data->stats.max,
            data->stats.count);

    return;
}

void __margo_print_profile_data(margo_instance_id mid,
                                FILE*             file,
                                const char*       name,
                                const char*       description,
                                struct diag_data* data)
{
    (void)description; // TODO was this supposed to be used?
    double avg;
    int    i;

    if (data->stats.count != 0)
        avg = data->stats.cumulative / data->stats.count;
    else
        avg = 0;

    /* first line is breadcrumb data */
    fprintf(file,
            "%s,%.9f,%lu,%lu,%d,%.9f,%.9f,%.9f,%lu,%lu,%lu,%lu,%lu,%lu,%lu\n",
            name, avg, data->key.rpc_breadcrumb, data->key.addr_hash,
            data->type, data->stats.cumulative, data->stats.min,
            data->stats.max, data->stats.count, data->stats.abt_pool_size_hwm,
            data->stats.abt_pool_size_lwm, data->stats.abt_pool_size_cumulative,
            data->stats.abt_pool_total_size_hwm,
            data->stats.abt_pool_total_size_lwm,
            data->stats.abt_pool_total_size_cumulative);

    /* second line is sparkline data for the given breadcrumb*/
    fprintf(file, "%s,%d;", name, data->type);
    for (i = 0; i < mid->sparkline_index; i++)
        fprintf(file, "%.9f,%.9f, %d;", data->sparkline_time[i],
                data->sparkline_count[i], i);
    fprintf(file, "\n");

    return;
}

/* records statistics for a breadcrumb, to be used after completion of an
 * RPC, both on the origin as well as on the target */
void __margo_breadcrumb_measure(margo_instance_id     mid,
                                uint64_t              rpc_breadcrumb,
                                double                start,
                                margo_breadcrumb_type type,
                                uint16_t              provider_id,
                                uint64_t              hash,
                                hg_handle_t           h)
{
    struct diag_data* stat;
    double            end, elapsed;
    uint16_t          t = (type == origin) ? 2 : 1;
    uint64_t          hash_;

    __uint128_t x = 0;

    /* IMPT NOTE: presently not adding provider_id to the breadcrumb,
       thus, the breadcrumb represents cumulative information for all providers
       offering or making a certain RPC call on this Margo instance */

    /* Bake in information about whether or not this was an origin or
     * target-side breadcrumb */
    hash_ = hash;
    hash_ = (hash_ >> 16) << 16;
    hash_ |= t;

    /* add in the server address */
    x = hash_;
    x = x << 64;
    x |= rpc_breadcrumb;

    if (!mid->profile_enabled) return;

    end     = ABT_get_wtime();
    elapsed = end - start;

    ABT_mutex_lock(mid->diag_rpc_mutex);

    HASH_FIND(hh, mid->diag_rpc, &x, sizeof(uint64_t) * 2, stat);

    if (!stat) {
        /* we aren't tracking this breadcrumb yet; add it */
        stat = calloc(1, sizeof(*stat));
        if (!stat) {
            /* best effort; we return gracefully without recording stats if this
             * happens.
             */
            ABT_mutex_unlock(mid->diag_rpc_mutex);
            return;
        }

        stat->rpc_breadcrumb     = rpc_breadcrumb;
        stat->type               = type;
        stat->key.rpc_breadcrumb = rpc_breadcrumb;
        stat->key.addr_hash      = hash;
        stat->key.provider_id    = provider_id;
        stat->x                  = x;

        /* initialize pool stats for breadcrumb */
        stat->stats.abt_pool_size_lwm        = 0x11111111; // Some high value
        stat->stats.abt_pool_size_cumulative = 0;
        stat->stats.abt_pool_size_hwm        = -1;

        stat->stats.abt_pool_total_size_lwm = 0x11111111; // Some high value
        stat->stats.abt_pool_total_size_cumulative = 0;
        stat->stats.abt_pool_total_size_hwm        = -1;

        /* initialize sparkline data */
        memset(stat->sparkline_time, 0.0, 100 * sizeof(double));
        memset(stat->sparkline_count, 0.0, 100 * sizeof(double));

        HASH_ADD(hh, mid->diag_rpc, x, sizeof(x), stat);
    }

    /* Argobots pool info */
    size_t                 s, s1;
    struct margo_rpc_data* margo_data;
    if (type) {
        const struct hg_info* info;
        info       = HG_Get_info(h);
        margo_data = (struct margo_rpc_data*)HG_Registered_data(mid->hg_class,
                                                                info->id);
        if (margo_data && margo_data->pool != ABT_POOL_NULL) {
            ABT_pool_get_total_size(margo_data->pool, &s);
            ABT_pool_get_size(margo_data->pool, &s1);
        } else {
            ABT_pool_get_total_size(mid->rpc_pool, &s);
            ABT_pool_get_size(mid->rpc_pool, &s1);
        }

        stat->stats.abt_pool_size_hwm
            = stat->stats.abt_pool_size_hwm > (double)s1
                ? stat->stats.abt_pool_size_hwm
                : s1;
        stat->stats.abt_pool_size_lwm
            = stat->stats.abt_pool_size_lwm < (double)s1
                ? stat->stats.abt_pool_size_lwm
                : s1;
        stat->stats.abt_pool_size_cumulative += s1;

        stat->stats.abt_pool_total_size_hwm
            = stat->stats.abt_pool_total_size_hwm > (double)s
                ? stat->stats.abt_pool_total_size_hwm
                : s;
        stat->stats.abt_pool_total_size_lwm
            = stat->stats.abt_pool_total_size_lwm < (double)s
                ? stat->stats.abt_pool_total_size_lwm
                : s;
        stat->stats.abt_pool_total_size_cumulative += s;
    }
    /* Argobots pool info */

    stat->stats.count++;
    stat->stats.cumulative += elapsed;
    if (elapsed > stat->stats.max) stat->stats.max = elapsed;
    if (stat->stats.min == 0 || elapsed < stat->stats.min)
        stat->stats.min = elapsed;

    ABT_mutex_unlock(mid->diag_rpc_mutex);

    return;
}

/* sets the value of a breadcrumb, to be called just before issuing an RPC */
uint64_t __margo_breadcrumb_set(hg_id_t rpc_id)
{
    uint64_t* val;
    uint64_t  tmp;

    ABT_key_get(g_margo_rpc_breadcrumb_key, (void**)(&val));
    if (val == NULL) {
        /* key not set yet on this ULT; we need to allocate a new one
         * with all zeroes for initial value of breadcrumb and idx
         */
        /* NOTE: treating this as best effort; just return 0 if it fails */
        val = calloc(1, sizeof(*val));
        if (!val) return (0);
    }

    /* NOTE: an rpc_id (after mux'ing) has provider in low order bits and
     * base rpc_id in high order bits.  After demuxing, a base_id has zeroed
     * out low bits.  So regardless of whether the rpc_id is a base_id or a
     * mux'd id, either way we need to shift right to get either the
     * provider id (or the space reserved for it) out of the way, then mask
     * off 16 bits for use as a breadcrumb.
     */
    tmp = rpc_id >> (__MARGO_PROVIDER_ID_SIZE * 8);
    tmp &= 0xffff;

    /* clear low 16 bits of breadcrumb */
    *val = (*val >> 16) << 16;

    /* combine them, so that we have low order 16 of rpc id and high order
     * bits of previous breadcrumb */
    *val |= tmp;

    ABT_key_set(g_margo_rpc_breadcrumb_key, val);

    return *val;
}

/* copy out the entire list of breadcrumbs on this margo instance */
void margo_breadcrumb_snapshot(margo_instance_id                 mid,
                               struct margo_breadcrumb_snapshot* snap)
{
    assert(mid->profile_enabled);
    struct diag_data *       dd, *tmp;
    struct margo_breadcrumb* tmp_bc;

#if 0
  fprintf(stderr, "Taking a snapshot\n");
#endif

    snap->ptr = calloc(1, sizeof(struct margo_breadcrumb));
    tmp_bc    = snap->ptr;

    HASH_ITER(hh, mid->diag_rpc, dd, tmp)
    {
#if 0
    fprintf(stderr, "Copying out RPC breadcrumb %d\n", dd->rpc_breadcrumb);
#endif
        tmp_bc->stats.min        = dd->stats.min;
        tmp_bc->stats.max        = dd->stats.max;
        tmp_bc->type             = dd->type;
        tmp_bc->key              = dd->key;
        tmp_bc->stats.count      = dd->stats.count;
        tmp_bc->stats.cumulative = dd->stats.cumulative;

        tmp_bc->stats.abt_pool_total_size_hwm
            = dd->stats.abt_pool_total_size_hwm;
        tmp_bc->stats.abt_pool_total_size_lwm
            = dd->stats.abt_pool_total_size_lwm;
        tmp_bc->stats.abt_pool_total_size_cumulative
            = dd->stats.abt_pool_total_size_cumulative;
        tmp_bc->stats.abt_pool_size_hwm = dd->stats.abt_pool_size_hwm;
        tmp_bc->stats.abt_pool_size_lwm = dd->stats.abt_pool_size_lwm;
        tmp_bc->stats.abt_pool_size_cumulative
            = dd->stats.abt_pool_size_cumulative;

        tmp_bc->next = calloc(1, sizeof(struct margo_breadcrumb));
        tmp_bc       = tmp_bc->next;
        tmp_bc->next = NULL;
    }
}

void margo_diag_dump(margo_instance_id mid, const char* file, int uniquify)
{
    FILE*    outfile;
    time_t   ltime;
    char     revised_file_name[256] = {0};
    char*    name;
    uint64_t hash;

    if (!mid->diag_enabled) return;

    if (uniquify) {
        char hostname[128] = {0};
        int  pid;

        gethostname(hostname, 128);
        pid = getpid();

        sprintf(revised_file_name, "%s-%s-%d.diag", file, hostname, pid);
    }

    else {
        sprintf(revised_file_name, "%s.diag", file);
    }

    if (strcmp("-", file) == 0) {
        outfile = stdout;
    } else {
        outfile = fopen(revised_file_name, "a");
        if (!outfile) {
            perror("fopen");
            return;
        }
    }

    /* TODO: support pattern substitution in file name to create unique
     * output files per process
     */
    time(&ltime);

    fprintf(outfile, "# Margo diagnostics\n");
    GET_SELF_ADDR_STR(mid, name);
    HASH_JEN(name, strlen(name),
             hash); /*record own address in the breadcrumb */
    fprintf(outfile, "#Addr Hash and Address Name: %lu,%s\n", hash, name);
    fprintf(outfile, "# %s\n", ctime(&ltime));
    fprintf(outfile,
            "# Function Name, Average Time Per Call, Cumulative Time, "
            "Highwatermark, Lowwatermark, Call Count\n");

    __margo_print_diag_data(mid, outfile, "trigger_elapsed",
                            "Time consumed by HG_Trigger()",
                            &mid->diag_trigger_elapsed);
    __margo_print_diag_data(
        mid, outfile, "progress_elapsed_zero_timeout",
        "Time consumed by HG_Progress() when called with timeout==0",
        &mid->diag_progress_elapsed_zero_timeout);
    __margo_print_diag_data(
        mid, outfile, "progress_elapsed_nonzero_timeout",
        "Time consumed by HG_Progress() when called with timeout!=0",
        &mid->diag_progress_elapsed_nonzero_timeout);
    __margo_print_diag_data(mid, outfile, "bulk_create_elapsed",
                            "Time consumed by HG_Bulk_create()",
                            &mid->diag_bulk_create_elapsed);

    if (outfile != stdout) fclose(outfile);

    return;
}

void margo_profile_dump(margo_instance_id mid, const char* file, int uniquify)
{
    FILE*                        outfile;
    time_t                       ltime;
    char                         revised_file_name[256] = {0};
    struct diag_data *           dd, *tmp;
    char                         rpc_breadcrumb_str[256] = {0};
    struct margo_registered_rpc* tmp_rpc;
    char*                        name;
    uint64_t                     hash;

    assert(mid->profile_enabled);

    if (uniquify) {
        char hostname[128] = {0};
        int  pid;

        gethostname(hostname, 128);
        pid = getpid();

        sprintf(revised_file_name, "%s-%s-%d.csv", file, hostname, pid);
    }

    else {
        sprintf(revised_file_name, "%s.csv", file);
    }

    if (strcmp("-", file) == 0) {
        outfile = stdout;
    } else {
        outfile = fopen(revised_file_name, "a");
        if (!outfile) {
            perror("fopen");
            return;
        }
    }

    /* TODO: support pattern substitution in file name to create unique
     * output files per process
     */
    time(&ltime);

    fprintf(outfile, "%u\n", mid->num_registered_rpcs);
    GET_SELF_ADDR_STR(mid, name);
    HASH_JEN(name, strlen(name),
             hash); /*record own address in the breadcrumb */

    fprintf(outfile, "%lu,%s\n", hash, name);

    tmp_rpc = mid->registered_rpcs;
    while (tmp_rpc) {
        fprintf(outfile, "0x%.4lx,%s\n", tmp_rpc->rpc_breadcrumb_fragment,
                tmp_rpc->func_name);
        tmp_rpc = tmp_rpc->next;
    }

    HASH_ITER(hh, mid->diag_rpc, dd, tmp)
    {
        int      i;
        uint64_t tmp_breadcrumb;
        for (i = 0; i < 4; i++) {
            tmp_breadcrumb = dd->rpc_breadcrumb;
            tmp_breadcrumb >>= (i * 16);
            tmp_breadcrumb &= 0xffff;

            if (!tmp_breadcrumb) continue;

            if (i == 3)
                sprintf(&rpc_breadcrumb_str[i * 7], "0x%.4lx", tmp_breadcrumb);
            else
                sprintf(&rpc_breadcrumb_str[i * 7], "0x%.4lx ", tmp_breadcrumb);
        }
        __margo_print_profile_data(mid, outfile, rpc_breadcrumb_str,
                                   "RPC statistics", dd);
    }

    if (outfile != stdout) fclose(outfile);

    return;
}
