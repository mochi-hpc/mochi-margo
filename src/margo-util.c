/*
 * (C) 2020 The University of Chicago
 *
 * See COPYRIGHT in top-level directory.
 */
#include "margo-util.h"
#include <stdlib.h>
#include <abt.h>

int margo_set_abt_mem_max_num_stacks(unsigned max_num_stacks)
{
    if (ABT_initialized() != ABT_ERR_UNINITIALIZED) return -1;
    char env_str[64];
    sprintf(env_str, "%d", max_num_stacks);
    setenv("ABT_MEM_MAX_NUM_STACKS", env_str, 1);
    return 0;
}

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
int margo_set_abt_thread_stacksize(unsigned stacksize)
{
    if (ABT_initialized() != ABT_ERR_UNINITIALIZED) return -1;
    char env_str[64];
    sprintf(env_str, "%d", stacksize);
    setenv("ABT_THREAD_STACKSIZE", env_str, 1);
    return 0;
}
