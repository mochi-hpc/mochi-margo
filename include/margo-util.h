/*
 * (C) 2020 The University of Chicago
 *
 * See COPYRIGHT in top-level directory.
 */
#ifndef __MARGO_UTIL_H
#define __MARGO_UTIL_H

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

#ifdef __cplusplus
}
#endif

#endif
