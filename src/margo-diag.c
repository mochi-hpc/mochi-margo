/*
 * (C) 2020 The University of Chicago
 *
 * See COPYRIGHT in top-level directory.
 */
#include <margo.h>
#include "margo-logging.h"
#include "margo-diag-internal.h"
#include "margo-instance.h"

#define SPARKLINE_ARRAY_LEN 100

static FILE* margo_output_file_open(margo_instance_id mid,
                                    const char*       file,
                                    int               uniquify,
                                    const char*       extension,
                                    char**            resolved_file_name);
static void  margo_diag_dump_fp(margo_instance_id mid, FILE* outfile);
static void  margo_diag_dump_abt_fp(margo_instance_id mid, FILE* outfile);
static void  margo_profile_dump_fp(margo_instance_id mid, FILE* outfile);

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

            if (mid->sparkline_index > 0
                && mid->sparkline_index < SPARKLINE_ARRAY_LEN) {
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
    for (i = 0; (i < mid->sparkline_index && i < SPARKLINE_ARRAY_LEN); i++)
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
        memset(stat->sparkline_time, 0.0, SPARKLINE_ARRAY_LEN * sizeof(double));
        memset(stat->sparkline_count, 0.0,
               SPARKLINE_ARRAY_LEN * sizeof(double));

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

void margo_breadcrumb_snapshot_destroy(margo_instance_id                 mid,
                                       struct margo_breadcrumb_snapshot* snap)
{
    struct margo_breadcrumb* tmp_bc      = snap->ptr;
    struct margo_breadcrumb* tmp_bc_next = NULL;
    while (tmp_bc) {
        tmp_bc_next = tmp_bc->next;
        free(tmp_bc);
        tmp_bc = tmp_bc_next;
    }
}

/* copy out the entire list of breadcrumbs on this margo instance */
void margo_breadcrumb_snapshot(margo_instance_id                 mid,
                               struct margo_breadcrumb_snapshot* snap)
{
    struct diag_data *       dd, *tmp;
    struct margo_breadcrumb* tmp_bc;

    memset(snap, 0, sizeof(*snap));

    if (!mid->profile_enabled) return;

    snap->ptr = calloc(1, sizeof(struct margo_breadcrumb));
    tmp_bc    = snap->ptr;

    HASH_ITER(hh, mid->diag_rpc, dd, tmp)
    {
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

/* open a file pointer for diagnostic/profile/state dumps */
static FILE* margo_output_file_open(margo_instance_id mid,
                                    const char*       file,
                                    int               uniquify,
                                    const char*       extension,
                                    char**            resolved_file_name)
{
    FILE* outfile;
    char* revised_file_name  = NULL;
    char* absolute_file_name = NULL;

    /* return early if the caller just wants stdout */
    if (strcmp("-", file) == 0) {
        if (resolved_file_name) *resolved_file_name = strdup("<STDOUT>");
        return (stdout);
    }

    revised_file_name = malloc(strlen(file) + 256);
    if (!revised_file_name) {
        MARGO_ERROR(mid, "malloc() failure: %d\n", errno);
        return (NULL);
    }

    /* construct revised file name with correct extension and (if desired)
     * substitutes unique information
     */
    if (uniquify) {
        char hostname[128] = {0};
        int  pid;

        gethostname(hostname, 128);
        pid = getpid();

        sprintf(revised_file_name, "%s-%s-%d.%s", file, hostname, pid,
                extension);
    } else {
        sprintf(revised_file_name, "%s.%s", file, extension);
    }

    /* if directory is not specified then use output directory from margo
     * configuration
     */
    if (revised_file_name[0] == '/') {
        absolute_file_name = revised_file_name;
    } else {
        absolute_file_name
            = malloc(strlen(json_object_get_string(
                         json_object_object_get(mid->json_cfg, "output_dir")))
                     + strlen(revised_file_name) + 2);
        if (!absolute_file_name) {
            MARGO_ERROR(mid, "malloc() failure: %d\n", errno);
            free(revised_file_name);
            return (NULL);
        }
        sprintf(absolute_file_name, "%s/%s",
                json_object_get_string(
                    json_object_object_get(mid->json_cfg, "output_dir")),
                revised_file_name);
    }

    /* actually open file */
    outfile = fopen(absolute_file_name, "a");
    if (!outfile)
        MARGO_ERROR(mid, "fopen(%s) failure: %d\n", absolute_file_name, errno);

    if (resolved_file_name) {
        if (absolute_file_name != revised_file_name) {
            *resolved_file_name = absolute_file_name;
            free(revised_file_name);
        } else {
            *resolved_file_name = revised_file_name;
        }
    } else {
        if (absolute_file_name != revised_file_name) free(absolute_file_name);
        free(revised_file_name);
    }

    return (outfile);
}

static void margo_diag_dump_abt_fp(margo_instance_id mid, FILE* outfile)
{
    time_t   ltime;
    char*    name;
    uint64_t hash;

    if (!mid->diag_enabled) return;

    time(&ltime);

    fprintf(outfile, "# Margo diagnostics (Argobots profile)\n");
    GET_SELF_ADDR_STR(mid, name);
    HASH_JEN(name, strlen(name),
             hash); /*record own address in the breadcrumb */
    fprintf(outfile, "# Addr Hash and Address Name: %lu,%s\n", hash, name);
    free(name);
    fprintf(outfile, "# %s\n", ctime(&ltime));

    if (g_margo_abt_prof_started) {
        /* have to stop profiling briefly to print results */
        ABTX_prof_stop(g_margo_abt_prof_context);
        ABTX_prof_print(g_margo_abt_prof_context, outfile,
                        ABTX_PRINT_MODE_SUMMARY | ABTX_PRINT_MODE_FANCY);
        /* TODO: consider supporting PROF_MODE_DETAILED also? */
        ABTX_prof_start(g_margo_abt_prof_context, ABTX_PROF_MODE_BASIC);
    }

    return;
}

static void margo_diag_dump_fp(margo_instance_id mid, FILE* outfile)
{
    time_t   ltime;
    char*    name;
    uint64_t hash;

    if (!mid->diag_enabled) return;

    time(&ltime);

    fprintf(outfile, "# Margo diagnostics\n");
    GET_SELF_ADDR_STR(mid, name);
    HASH_JEN(name, strlen(name),
             hash); /*record own address in the breadcrumb */
    fprintf(outfile, "# Addr Hash and Address Name: %lu,%s\n", hash, name);
    free(name);
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

    return;
}

void margo_diag_dump(margo_instance_id mid, const char* file, int uniquify)
{
    FILE* outfile;

    if (!mid->diag_enabled) return;

    /* rpc diagnostics */
    outfile = margo_output_file_open(mid, file, uniquify, "diag", NULL);
    if (!outfile) return;

    margo_diag_dump_fp(mid, outfile);

    /* abt profiling */
    outfile = margo_output_file_open(mid, file, uniquify, "diag.abt", NULL);
    if (!outfile) return;

    margo_diag_dump_abt_fp(mid, outfile);

    if (outfile != stdout) fclose(outfile);

    return;
}

static void margo_profile_dump_fp(margo_instance_id mid, FILE* outfile)
{
    time_t                       ltime;
    struct diag_data *           dd, *tmp;
    char                         rpc_breadcrumb_str[256] = {0};
    struct margo_registered_rpc* tmp_rpc;
    char*                        name;
    uint64_t                     hash;

    if (!mid->profile_enabled) return;

    time(&ltime);

    fprintf(outfile, "%u\n", mid->num_registered_rpcs);
    GET_SELF_ADDR_STR(mid, name);
    HASH_JEN(name, strlen(name),
             hash); /*record own address in the breadcrumb */

    fprintf(outfile, "%lu,%s\n", hash, name);
    free(name);

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

    return;
}

void margo_profile_dump(margo_instance_id mid, const char* file, int uniquify)
{
    FILE* outfile;

    if (!mid->profile_enabled) return;

    outfile = margo_output_file_open(mid, file, uniquify, "csv", NULL);
    if (!outfile) return;

    margo_profile_dump_fp(mid, outfile);

    if (outfile != stdout) fclose(outfile);

    return;
}
void margo_state_dump(margo_instance_id mid,
                      const char*       file,
                      int               uniquify,
                      char**            resolved_file_name)
{
    FILE*    outfile;
    time_t   ltime;
    char*    name;
    int      i = 0;
    char*    encoded_json;
    ABT_bool qconfig;
    unsigned pending_operations;

    outfile = margo_output_file_open(mid, file, uniquify, "state",
                                     resolved_file_name);
    if (!outfile) return;

    time(&ltime);

    fprintf(outfile, "# Margo state dump\n");
    GET_SELF_ADDR_STR(mid, name);
    fprintf(outfile, "# Mercury address: %s\n", name);
    free(name);
    fprintf(outfile, "# %s\n", ctime(&ltime));

    fprintf(outfile,
            "\n# Margo configuration (JSON)\n"
            "# ==========================\n");
    encoded_json = margo_get_config(mid);
    fprintf(outfile, "%s\n", encoded_json);
    if (encoded_json) free(encoded_json);

    fprintf(outfile,
            "\n# Margo instance state\n"
            "# ==========================\n");
    ABT_mutex_lock(mid->pending_operations_mtx);
    pending_operations = mid->pending_operations;
    ABT_mutex_unlock(mid->pending_operations_mtx);
    fprintf(outfile, "mid->pending_operations: %d\n", pending_operations);
    fprintf(outfile, "mid->diag_enabled: %d\n", mid->diag_enabled);
    fprintf(outfile, "mid->profile_enabled: %d\n", mid->profile_enabled);

    fprintf(
        outfile,
        "\n# Margo diagnostics\n"
        "\n# NOTE: this is only available if mid->diag_enabled == 1 above.  "
        "You can\n"
        "#       turn this on by calling margo_diag_start() "
        "programatically, by setting\n"
        "#       the MARGO_ENABLE_DIAGNOSTICS=1 environment variable, or "
        "by setting\n"
        "#       the \"enable_diagnostics\" JSON configuration parameter.\n"
        "# ==========================\n");
    margo_diag_dump_fp(mid, outfile);

    fprintf(
        outfile,
        "\n# Margo RPC profiling\n"
        "\n# NOTE: this is only available if mid->profile_enabled == 1 above.  "
        "You can\n"
        "#       turn this on by calling margo_profile_start() "
        "programatically, by\n"
        "#       setting the MARGO_ENABLE_PROFILING=1 environment variable, or "
        "by setting\n"
        "#       the \"enable_profiling\" JSON configuration parameter.\n"
        "# ==========================\n");
    margo_profile_dump_fp(mid, outfile);

    fprintf(outfile,
            "\n# Argobots configuration (ABT_info_print_config())\n"
            "# ================================================\n");
    ABT_info_print_config(outfile);

    fprintf(outfile,
            "\n# Argobots execution streams (ABT_info_print_all_xstreams())\n"
            "# ================================================\n");
    ABT_info_print_all_xstreams(outfile);

    fprintf(outfile,
            "\n# Margo Argobots profiling summary\n"
            "\n# NOTE: this is only available if mid->diag_enabled == 1 above "
            "*and* Argobots\n"
            "# has been compiled with tool interface support.  You can turn on "
            "Margo\n"
            "# diagnostics at runtime by calling margo_diag_start() "
            "programatically, by\n"
            "# setting the MARGO_ENABLE_DIAGNOSTICS=1 environment variable, or "
            "by setting\n"
            "# the \"enable_diagnostics\" JSON configuration parameter. You "
            "can enable the\n"
            "# Argobots tool interface by compiling Argobots with the "
            "--enable-tool or the\n"
            "# +tool spack variant.\n"
            "# ==========================\n");
    margo_diag_dump_abt_fp(mid, outfile);

    fprintf(outfile,
            "\n# Argobots stack dump (ABT_info_print_thread_stacks_in_pool())\n"
            "#   *IMPORTANT NOTE*\n"
            "# This stack dump does *not* display information about currently "
            "executing\n"
            "# user-level threads.  The user-level threads shown here are "
            "awaiting\n"
            "# execution due to synchronization primitives or resource "
            "constraints.\n");

    ABT_info_query_config(ABT_INFO_QUERY_KIND_ENABLED_STACK_UNWIND, &qconfig);
    fprintf(outfile, "# Argobots stack unwinding: %s\n",
            (qconfig == ABT_TRUE) ? "ENABLED" : "DISABLED");
    if (qconfig != ABT_TRUE) {
        fprintf(outfile,
                "# *IMPORTANT NOTE*\n"
                "# You can make the following stack dump more human readable "
                "by compiling\n"
                "# Argobots with --enable-stack-unwind or the +stackunwind "
                "spack variant.\n");
    }
    fprintf(outfile, "# ================================================\n");

    /* for each pool that margo is aware of */
    for (i = 0; i < mid->num_abt_pools; i++) {
        /* Display stack trace of ULTs within that pool.  This will not
         * include any ULTs that are presently executing (including the
         * caller).
         */
        ABT_info_print_thread_stacks_in_pool(outfile, mid->abt_pools[i].pool);
    }

    if (outfile != stdout) fclose(outfile);

    return;
}

void __margo_sparkline_thread_stop(margo_instance_id mid)
{
    if (!mid->profile_enabled) return;

    MARGO_TRACE(mid,
                "Waiting for sparkline data collection thread to complete");
    ABT_thread_join(mid->sparkline_data_collection_tid);
    ABT_thread_free(&mid->sparkline_data_collection_tid);

    return;
}

void __margo_sparkline_thread_start(margo_instance_id mid)
{
    int ret;

    if (!mid->profile_enabled) return;

    MARGO_TRACE(mid, "Profiling is enabled, starting profiling thread");
    mid->previous_sparkline_data_collection_time = ABT_get_wtime();

    ret = ABT_thread_create(
        mid->progress_pool, __margo_sparkline_data_collection_fn, mid,
        ABT_THREAD_ATTR_NULL, &mid->sparkline_data_collection_tid);
    if (ret != ABT_SUCCESS) {
        MARGO_WARNING(
            0,
            "Failed to start sparkline data collection thread, "
            "continuing to profile without sparkline data collection");
    }

    return;
}
