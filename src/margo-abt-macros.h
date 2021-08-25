/*
 * (C) 2021 The University of Chicago
 *
 * See COPYRIGHT in top-level directory.
 */
#ifndef __MARGO_ABT_MACROS_H
#define __MARGO_ABT_MACROS_H

#include <abt.h>

/* In recent versions of Argobots, some new types
 * have been provided to manipulate mutex, condition
 * variables, and eventuals on the stack rather than
 * allocating them on the heap. The following macros
 * allow to use these new types when available.
 *
 * Note: the margo_eventual_t type represents either
 * an ABT_eventual or an ABT_eventual_memory, but
 * cannot hold a value.
 */

#ifdef ABT_EVENTUAL_INITIALIZER

typedef ABT_eventual_memory margo_eventual_t;

static inline int __margo_eventual_create(margo_eventual_t* ev)
{
    memset(ev, 0, sizeof(*ev));
    return ABT_SUCCESS;
}

    #define MARGO_EVENTUAL_CREATE(__ev__) __margo_eventual_create(__ev__)

    #define MARGO_EVENTUAL_FREE(__ev__)

    #define MARGO_EVENTUAL_WAIT(__ev__) \
        ABT_eventual_wait(ABT_EVENTUAL_MEMORY_GET_HANDLE(&(__ev__)), NULL)

    #define MARGO_EVENTUAL_SET(__ev__) \
        ABT_eventual_set(ABT_EVENTUAL_MEMORY_GET_HANDLE(&(__ev__)), NULL, 0)

    #define MARGO_EVENTUAL_RESET(__ev__) \
        ABT_eventual_reset(ABT_EVENTUAL_MEMORY_GET_HANDLE(&(__ev__)))

    #define MARGO_EVENTUAL_TEST(__ev__, __flag__)                          \
        ABT_eventual_test(ABT_EVENTUAL_MEMORY_GET_HANDLE(&(__ev__)), NULL, \
                          (__flag__))

#else

typedef ABT_eventual margo_eventual_t;

    #define MARGO_EVENTUAL_CREATE(__ev__) ABT_eventual_create(0, (__ev__))

    #define MARGO_EVENTUAL_FREE(__ev__) ABT_eventual_free(__ev__)

    #define MARGO_EVENTUAL_WAIT(__ev__) ABT_eventual_wait((__ev__), NULL)

    #define MARGO_EVENTUAL_SET(__ev__) ABT_eventual_set((__ev__), NULL, 0)

    #define MARGO_EVENTUAL_RESET(__ev__) ABT_eventual_reset(__ev__)

    #define MARGO_EVENTUAL_TEST(__ev__, __flag__) \
        ABT_eventual_test((__ev__), NULL, (__flag__))

#endif

#endif
