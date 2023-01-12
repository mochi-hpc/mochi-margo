/**
 * @file margo-util.h
 *
 * (C) 2020 The University of Chicago
 *
 * See COPYRIGHT in top-level directory.
 */
#ifndef __MARGO_UTIL_H
#define __MARGO_UTIL_H

#include <stdint.h>
#include <margo.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief This function sets the ABT_MEM_MAX_NUM_STACKS
 * environment variable for Argobots to use. It should be
 * called before Argobots is initialized (hence before
 * Margo is initialized).
 *
 * @param max_num_stacks
 *
 * @return 0 in case of success, -1 if Argobots is already initialized
 */
int margo_set_abt_mem_max_num_stacks(unsigned max_num_stacks);

/**
 * @brief This function sets the ABT_THREAD_STACKSIZE
 * environment variable for Argobots to use. It should be
 * called before Argobots is initialized (hence before
 * Margo is initialized).
 *
 * @param stacksize
 *
 * @return 0 in case of success, -1 if Argobots is already initialized
 */
int margo_set_abt_thread_stacksize(unsigned stacksize);

/**
 * @brief Utility function to get the number of calls to HG_Progress
 * that the margo instance did from the moment it was created.
 *
 * @param mid Margo instance.
 *
 * @return Number of calls to HG_Progress made.
 */
uint64_t margo_get_num_progress_calls(margo_instance_id mid);

/**
 * @brief Utility function to get the number of calls to HG_Trigger
 * that the margo instance did from the moment it was created.
 *
 * @param mid Margo instance.
 *
 * @return Number of calls to HG_Trigger made.
 */
uint64_t margo_get_num_trigger_calls(margo_instance_id mid);

#ifdef __cplusplus
}
#endif

#endif
