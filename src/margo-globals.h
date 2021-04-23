/*
 * (C) 2020 The University of Chicago
 *
 * See COPYRIGHT in top-level directory.
 */
#ifndef __MARGO_GLOBALS_H
#define __MARGO_GLOBALS_H

// This file contains all the global variables that must be shared across
// compilation units. These globals must start with the g_margo_ prefix.

#include <stdbool.h>
#include <margo.h>
#include "abtx_prof.h"

// If margo is initializing ABT, we need to track how many instances of margo
// are being created, so that the last one can call ABT_finalize.
// If margo initializes ABT, g_margo_num_instances_mtx will be created, so
// in later calls and in margo_cleanup we can check for
// g_margo_num_instances_mtx != ABT_MUTEX_NULL
// to know if we should do something to cleanup ABT as well.
extern int       g_margo_num_instances;
extern ABT_mutex g_margo_num_instances_mtx;
extern bool      g_margo_abt_init;

// Track if an instance has enabled abt profiling.  We can only do this
// once, no matter how many margo instances are running.
extern bool              g_margo_abt_prof_init;
extern bool              g_margo_abt_prof_started;
extern ABTX_prof_context g_margo_abt_prof_context;

// Keys for Argobots thread-local storage to track RPC breadcrumbs
// across thread execution.
extern ABT_key g_margo_rpc_breadcrumb_key;
extern ABT_key g_margo_target_timing_key;

#endif
