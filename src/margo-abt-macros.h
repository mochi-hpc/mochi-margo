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

// Eventuals

#ifdef ABT_EVENTUAL_INITIALIZER

typedef ABT_eventual_memory margo_eventual_t;

static inline int __margo_eventual_create(margo_eventual_t* ev)
{
    static const ABT_eventual_memory ev_init = ABT_EVENTUAL_INITIALIZER;
    memcpy(ev, &ev_init, sizeof(ev_init));
    return ABT_SUCCESS;
}

    #define MARGO_EVENTUAL_GET_HANDLE(__ev__) \
        ABT_EVENTUAL_MEMORY_GET_HANDLE(&(__ev__))

    #define MARGO_EVENTUAL_CREATE(__ev__) __margo_eventual_create(__ev__)

    /* NOTE: we don't need to literally "reset" the eventual when freeing it.
     * The point of calling reset() in this path is that it will force
     * Argobots to acquire an internal lock in the eventual, which in turn
     * ensures that the set() caller is done before we destroy the eventual
     * and allow it to pass out of scope.  This makes the margo_eventual_t
     * safe to use on ephemeral function call stacks.  See discussion at
     * https://github.com/pmodels/argobots/issues/367 for details.
     */
    #define MARGO_EVENTUAL_FREE(__ev__) \
        ABT_eventual_reset(ABT_EVENTUAL_MEMORY_GET_HANDLE(__ev__))

#else // ABT_EVENTUAL_INITIALIZED not defined

typedef ABT_eventual margo_eventual_t;

    #define MARGO_EVENTUAL_GET_HANDLE(__ev__) (__ev__)

    #define MARGO_EVENTUAL_CREATE(__ev__) ABT_eventual_create(0, (__ev__))

    #define MARGO_EVENTUAL_FREE(__ev__) ABT_eventual_free(__ev__)

#endif

#define MARGO_EVENTUAL_WAIT(__ev__) \
    ABT_eventual_wait(MARGO_EVENTUAL_GET_HANDLE(__ev__), NULL)

#define MARGO_EVENTUAL_SET(__ev__) \
    ABT_eventual_set(MARGO_EVENTUAL_GET_HANDLE(__ev__), NULL, 0)

#define MARGO_EVENTUAL_RESET(__ev__) \
    ABT_eventual_reset(MARGO_EVENTUAL_GET_HANDLE(__ev__))

#define MARGO_EVENTUAL_TEST(__ev__, __flag__) \
    ABT_eventual_test(MARGO_EVENTUAL_GET_HANDLE(__ev__), NULL, (__flag__))

// Mutex

#ifdef ABT_MUTEX_INITIALIZER

typedef ABT_mutex_memory margo_mutex_t;

static inline int __margo_mutex_create(margo_mutex_t* mtx)
{
    static const ABT_mutex_memory mtx_init = ABT_MUTEX_INITIALIZER;
    memcpy(mtx, &mtx_init, sizeof(mtx_init));
    return ABT_SUCCESS;
}

    #define MARGO_MUTEX_GET_HANDLE(__mtx__) \
        ABT_MUTEX_MEMORY_GET_HANDLE(&(__mtx__))

    #define MARGO_MUTEX_CREATE(__mtx__) __margo_mutex_create(__mtx__)

    #define MARGO_MUTEX_FREE(__mtx__)

#else // ABT_MUTEX_INITIALIZER not defined

typedef ABT_mutex margo_mutex_t;

    #define MARGO_MUTEX_GET_HANDLE(__mtx__) (__mtx__)

    #define MARGO_MUTEX_CREATE(__mtx__) ABT_mutex_create(__mtx__)

    #define MARGO_MUTEX_FREE(__mtx__) ABT_mutex_free(__mtx__)

#endif

#define MARGO_MUTEX_LOCK(__mtx__) \
    ABT_mutex_lock(MARGO_MUTEX_GET_HANDLE(__mtx__))

#define MARGO_MUTEX_UNLOCK(__mtx__) \
    ABT_mutex_unlock(MARGO_MUTEX_GET_HANDLE(__mtx__))

#endif
