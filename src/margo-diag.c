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
    struct margo_instance *mid = (struct margo_instance *)foo;
    struct diag_data *stat, *tmp;

    /* double check that profile collection should run, else, close this ULT */
    if(!mid->profile_enabled) {
        ABT_thread_join(mid->sparkline_data_collection_tid);
        ABT_thread_free(&mid->sparkline_data_collection_tid);
    }

    int sleep_time_msec = json_integer_value(json_object_get(mid->json_cfg, "profile_sparkline_timeslice_msec"));

    while(!mid->hg_progress_shutdown_flag)
    {
        margo_thread_sleep(mid, sleep_time_msec);
        HASH_ITER(hh, mid->diag_rpc, stat, tmp)
        {

            if(mid->sparkline_index > 0 && mid->sparkline_index < 100) {
                stat->sparkline_time[mid->sparkline_index] = stat->stats.cumulative - stat->sparkline_time[mid->sparkline_index - 1];
                stat->sparkline_count[mid->sparkline_index] = stat->stats.count - stat->sparkline_count[mid->sparkline_index - 1];
            } else if(mid->sparkline_index == 0) {
                stat->sparkline_time[mid->sparkline_index] = stat->stats.cumulative;
                stat->sparkline_count[mid->sparkline_index] = stat->stats.count;
            } else {
                //Drop!
            }
        }
        mid->sparkline_index++;
        mid->previous_sparkline_data_collection_time = ABT_get_wtime();
    }

    return;
}

void __margo_print_diag_data(
        margo_instance_id mid,
        FILE *file,
        const char* name,
        const char *description,
        struct diag_data *data)
{
    double avg;

    if(data->stats.count != 0)
        avg = data->stats.cumulative/data->stats.count;
    else
        avg = 0;

    fprintf(file, "%s,%.9f,%.9f,%.9f,%.9f,%lu\n",
            name,
            avg,
            data->stats.cumulative,
            data->stats.min,
            data->stats.max,
            data->stats.count);

    return;
}

void __margo_print_profile_data(
        margo_instance_id mid,
        FILE *file,
        const char* name,
        const char *description,
        struct diag_data *data)
{
    double avg;
    int i;

    if(data->stats.count != 0)
        avg = data->stats.cumulative/data->stats.count;
    else
        avg = 0;

    /* first line is breadcrumb data */
    fprintf(file, "%s,%.9f,%lu,%lu,%d,%.9f,%.9f,%.9f,%lu,%lu,%lu,%lu,%lu,%lu,%lu\n",
            name,
            avg,
            data->key.rpc_breadcrumb,
            data->key.addr_hash,
            data->type,
            data->stats.cumulative,
            data->stats.min,
            data->stats.max,
            data->stats.count,
            data->stats.abt_pool_size_hwm,
            data->stats.abt_pool_size_lwm,
            data->stats.abt_pool_size_cumulative,
            data->stats.abt_pool_total_size_hwm,
            data->stats.abt_pool_total_size_lwm,
            data->stats.abt_pool_total_size_cumulative);

    /* second line is sparkline data for the given breadcrumb*/
    fprintf(file, "%s,%d;", name, data->type);
    for(i = 0; i < mid->sparkline_index; i++)
        fprintf(file, "%.9f,%.9f, %d;", data->sparkline_time[i], data->sparkline_count[i], i);
    fprintf(file,"\n");

    return;
}

/* copy out the entire list of breadcrumbs on this margo instance */
void margo_breadcrumb_snapshot(margo_instance_id mid, struct margo_breadcrumb_snapshot* snap)
{
  assert(mid->profile_enabled);
  struct diag_data *dd, *tmp;
  struct margo_breadcrumb *tmp_bc;

#if 0
  fprintf(stderr, "Taking a snapshot\n");
#endif

  snap->ptr = calloc(1, sizeof(struct margo_breadcrumb));
  tmp_bc = snap->ptr;

  HASH_ITER(hh, mid->diag_rpc, dd, tmp)
  {
#if 0
    fprintf(stderr, "Copying out RPC breadcrumb %d\n", dd->rpc_breadcrumb);
#endif
    tmp_bc->stats.min = dd->stats.min;
    tmp_bc->stats.max = dd->stats.max;
    tmp_bc->type = dd->type;
    tmp_bc->key = dd->key;
    tmp_bc->stats.count = dd->stats.count;
    tmp_bc->stats.cumulative = dd->stats.cumulative;

    tmp_bc->stats.abt_pool_total_size_hwm = dd->stats.abt_pool_total_size_hwm;
    tmp_bc->stats.abt_pool_total_size_lwm = dd->stats.abt_pool_total_size_lwm;
    tmp_bc->stats.abt_pool_total_size_cumulative = dd->stats.abt_pool_total_size_cumulative;
    tmp_bc->stats.abt_pool_size_hwm = dd->stats.abt_pool_size_hwm;
    tmp_bc->stats.abt_pool_size_lwm = dd->stats.abt_pool_size_lwm;
    tmp_bc->stats.abt_pool_size_cumulative = dd->stats.abt_pool_size_cumulative;

    tmp_bc->next = calloc(1, sizeof(struct margo_breadcrumb));
    tmp_bc = tmp_bc->next;
    tmp_bc->next = NULL;
  }

}

void margo_diag_dump(margo_instance_id mid, const char* file, int uniquify)
{
    FILE *outfile;
    time_t ltime;
    char revised_file_name[256] = {0};
    char * name;
    uint64_t hash;

    if (!mid->diag_enabled)
        return;

    if(uniquify)
    {
        char hostname[128] = {0};
        int pid;

        gethostname(hostname, 128);
        pid = getpid();

        sprintf(revised_file_name, "%s-%s-%d.diag", file, hostname, pid);
    }

    else
    {
        sprintf(revised_file_name, "%s.diag", file);
    }

    if(strcmp("-", file) == 0)
    {
        outfile = stdout;
    }
    else
    {
        outfile = fopen(revised_file_name, "a");
        if(!outfile)
        {
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
    HASH_JEN(name, strlen(name), hash); /*record own address in the breadcrumb */
    fprintf(outfile, "#Addr Hash and Address Name: %lu,%s\n", hash, name);
    fprintf(outfile, "# %s\n", ctime(&ltime));
    fprintf(outfile, "# Function Name, Average Time Per Call, Cumulative Time, Highwatermark, Lowwatermark, Call Count\n");

    __margo_print_diag_data(mid, outfile, "trigger_elapsed",
        "Time consumed by HG_Trigger()",
        &mid->diag_trigger_elapsed);
    __margo_print_diag_data(mid, outfile, "progress_elapsed_zero_timeout",
        "Time consumed by HG_Progress() when called with timeout==0",
        &mid->diag_progress_elapsed_zero_timeout);
    __margo_print_diag_data(mid, outfile, "progress_elapsed_nonzero_timeout",
        "Time consumed by HG_Progress() when called with timeout!=0",
        &mid->diag_progress_elapsed_nonzero_timeout);
    __margo_print_diag_data(mid, outfile, "bulk_create_elapsed",
        "Time consumed by HG_Bulk_create()",
        &mid->diag_bulk_create_elapsed);

    if(outfile != stdout)
        fclose(outfile);

    return;
}

void margo_profile_dump(margo_instance_id mid, const char* file, int uniquify)
{
    FILE *outfile;
    time_t ltime;
    char revised_file_name[256] = {0};
    struct diag_data *dd, *tmp;
    char rpc_breadcrumb_str[256] = {0};
    struct margo_registered_rpc *tmp_rpc;
    char * name;
    uint64_t hash;

    assert(mid->profile_enabled);

    if(uniquify)
    {
        char hostname[128] = {0};
        int pid;

        gethostname(hostname, 128);
        pid = getpid();

        sprintf(revised_file_name, "%s-%s-%d.csv", file, hostname, pid);
    }

    else
    {
        sprintf(revised_file_name, "%s.csv", file);
    }

    if(strcmp("-", file) == 0)
    {
        outfile = stdout;
    }
    else
    {
        outfile = fopen(revised_file_name, "a");
        if(!outfile)
        {
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
    HASH_JEN(name, strlen(name), hash); /*record own address in the breadcrumb */

    fprintf(outfile, "%lu,%s\n", hash, name);

    tmp_rpc = mid->registered_rpcs;
    while(tmp_rpc)
    {
        fprintf(outfile, "0x%.4lx,%s\n", tmp_rpc->rpc_breadcrumb_fragment, tmp_rpc->func_name);
        tmp_rpc = tmp_rpc->next;
    }

    HASH_ITER(hh, mid->diag_rpc, dd, tmp)
    {
        int i;
        uint64_t tmp_breadcrumb;
        for(i=0; i<4; i++)
        {
            tmp_breadcrumb = dd->rpc_breadcrumb;
            tmp_breadcrumb >>= (i*16);
            tmp_breadcrumb &= 0xffff;

            if(!tmp_breadcrumb) continue;

            if(i==3)
                sprintf(&rpc_breadcrumb_str[i*7], "0x%.4lx", tmp_breadcrumb);
            else
                sprintf(&rpc_breadcrumb_str[i*7], "0x%.4lx ", tmp_breadcrumb);
        }
        __margo_print_profile_data(mid, outfile, rpc_breadcrumb_str, "RPC statistics", dd);
    }

    if(outfile != stdout)
        fclose(outfile);

    return;
}
