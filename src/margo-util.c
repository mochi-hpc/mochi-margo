/*
 * (C) 2020 The University of Chicago
 *
 * See COPYRIGHT in top-level directory.
 */
#include "margo-util.h"
#include <stdlib.h>
#include <abt.h>
#include "margo-instance.h"

int margo_confirm_abt_mem_max_num_stacks(unsigned max_num_stacks)
{
    const char* abt_mem_max_num_stacks_str = getenv("ABT_MEM_MAX_NUM_STACKS");
    if (!abt_mem_max_num_stacks_str
        || atoi(abt_mem_max_num_stacks_str) != max_num_stacks) {
        MARGO_ERROR(0,
                    "Argobots was initialized externally, but the "
                    "ABT_MEM_MAX_NUM_STACKS environment variable "
                    "is not set to the value of %u expected by Margo",
                    max_num_stacks);
        return -1;
    }

    return 0;
}

int margo_set_abt_mem_max_num_stacks(unsigned max_num_stacks)
{
    if (ABT_initialized() != ABT_ERR_UNINITIALIZED) return -1;
    char env_str[64];
    sprintf(env_str, "%d", max_num_stacks);
    setenv("ABT_MEM_MAX_NUM_STACKS", env_str, 1);
    return 0;
}

int margo_confirm_abt_thread_stacksize(unsigned stacksize)
{
    const char* abt_thread_stacksize_str = getenv("ABT_THREAD_STACKSIZE");
    if (!abt_thread_stacksize_str
        || atoi(abt_thread_stacksize_str) != stacksize) {
        MARGO_ERROR(0,
                    "Argobots was initialized externally, but the "
                    "ABT_THREAD_STACKSIZE environment variable is "
                    "not set to the value of %u expected by Margo",
                    stacksize);
        return -1;
    }

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
