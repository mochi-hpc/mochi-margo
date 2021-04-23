/*
 * (C) 2020 The University of Chicago
 *
 * See COPYRIGHT in top-level directory.
 */
#include "margo-globals.h"
#include "margo-logging.h"

int               g_margo_num_instances      = 0;
ABT_mutex         g_margo_num_instances_mtx  = ABT_MUTEX_NULL;
bool              g_margo_abt_init           = 0;
bool              g_margo_abt_prof_init      = 0;
bool              g_margo_abt_prof_started   = 0;
ABT_key           g_margo_rpc_breadcrumb_key = ABT_KEY_NULL;
ABT_key           g_margo_target_timing_key  = ABT_KEY_NULL;
margo_log_level   g_margo_log_level          = MARGO_LOG_ERROR;
ABTX_prof_context g_margo_abt_prof_context;
