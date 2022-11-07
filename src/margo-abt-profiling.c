/*
 * (C) 2022 The University of Chicago
 *
 * See COPYRIGHT in top-level directory.
 */
#include <margo.h>
#include "margo-logging.h"
#include "margo-instance.h"
#include "margo-globals.h"

static FILE* margo_output_file_open(margo_instance_id mid,
                                    const char*       file,
                                    int               uniquify,
                                    const char*       extension,
                                    char**            resolved_file_name);
static void  margo_abt_profiling_dump_fp(margo_instance_id mid, FILE* outfile);

/* open a file pointer for profiling/state dumps */
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
        return stdout;
    }

    revised_file_name = malloc(strlen(file) + 256);
    if (!revised_file_name) {
        MARGO_ERROR(mid, "malloc() failure: %d", errno);
        return NULL;
    }

    /* construct revised file name with correct extension and (if desired)
     * substitutes unique information
     */
    if (uniquify) {
        char hostname[128] = {0};
        int  pid;

        gethostname(hostname, 128);
        pid = getpid();

        sprintf(revised_file_name, "%s.%s.%d.%s", file, hostname, pid,
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
            return NULL;
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

static void margo_abt_profiling_dump_fp(margo_instance_id mid, FILE* outfile)
{
    time_t ltime;
    time(&ltime);

    fprintf(outfile, "# Margo diagnostics (Argobots profile)\n");
    fprintf(outfile, "# Addr Hash and Address Name: %lu,%s\n",
            mid->self_addr_hash, mid->self_addr_str);
    char time_str[128] = {0};
    strftime(time_str, 128, "%c", localtime(&ltime));
    fprintf(outfile, "# %s\n", time_str);

    if (g_margo_abt_prof_started) {
        /* have to stop profiling briefly to print results */
        ABTX_prof_stop(g_margo_abt_prof_context);
        ABTX_prof_print(g_margo_abt_prof_context, outfile,
                        ABTX_PRINT_MODE_SUMMARY | ABTX_PRINT_MODE_FANCY);
        ABTX_prof_start(g_margo_abt_prof_context, g_margo_abt_prof_mode);
    } else {
        ABTX_prof_print(g_margo_abt_prof_context, outfile,
                        ABTX_PRINT_MODE_SUMMARY | ABTX_PRINT_MODE_FANCY);
    }
}

int margo_start_abt_profiling(margo_instance_id mid, bool detailed)
{
    (void)mid;
    if (g_margo_abt_prof_init) {
        if (!g_margo_abt_prof_started) {
            int mode
                = detailed ? ABTX_PROF_MODE_DETAILED : ABTX_PROF_MODE_BASIC;
            ABTX_prof_start(g_margo_abt_prof_context, mode);
            g_margo_abt_prof_started = 1;
            g_margo_abt_prof_mode    = mode;
        }
    }
    return 0;
}

int margo_stop_abt_profiling(margo_instance_id mid)
{
    (void)mid;
    if (g_margo_abt_prof_init) {
        if (g_margo_abt_prof_started) {
            ABTX_prof_stop(g_margo_abt_prof_context);
            g_margo_abt_prof_started = 0;
        }
    }
    return 0;
}

int margo_dump_abt_profiling(margo_instance_id mid,
                             const char*       file,
                             int               uniquify,
                             char**            resolved)
{
    FILE* outfile;

    if (!g_margo_abt_prof_init) return -1;

    /* abt profiling */
    outfile = margo_output_file_open(mid, file, uniquify, "abt.txt", resolved);
    if (!outfile) return -1;

    margo_abt_profiling_dump_fp(mid, outfile);

    if (outfile != stdout) fclose(outfile);

    return 0;
}

int margo_state_dump(margo_instance_id mid,
                     const char*       file,
                     int               uniquify,
                     char**            resolved)
{
    FILE*    outfile;
    time_t   ltime;
    int      i = 0;
    char*    encoded_json;
    ABT_bool qconfig;
    unsigned pending_operations;

    outfile
        = margo_output_file_open(mid, file, uniquify, "state.txt", resolved);
    if (!outfile) return -1;

    time(&ltime);

    fprintf(outfile, "# Margo state dump\n");
    fprintf(outfile, "# Mercury address: %s\n", mid->self_addr_str);
    char time_str[128] = {0};
    strftime(time_str, 128, "%c", localtime(&ltime));
    fprintf(outfile, "# %s\n", time_str);

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
    fprintf(outfile, "pending_operations: %d\n", pending_operations);

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
    margo_abt_profiling_dump_fp(mid, outfile);

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
    for (i = 0; i < (int)mid->num_abt_pools; i++) {
        /* Display stack trace of ULTs within that pool.  This will not
         * include any ULTs that are presently executing (including the
         * caller).
         */
        fprintf(outfile, "# Pool: %s\n", margo_get_pool_name(mid, i));
        ABT_info_print_thread_stacks_in_pool(outfile, mid->abt_pools[i].pool);
    }

    if (outfile != stdout) fclose(outfile);

    return 0;
}
