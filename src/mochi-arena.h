/*
 * (C) 2026 The University of Chicago
 *
 * See COPYRIGHT in top-level directory.
 */
#ifndef MOCHI_ARENA_H
#define MOCHI_ARENA_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct mochi_arena* mochi_arena_t;
#define MOCHI_ARENA_NULL ((mochi_arena_t)NULL)

/**
 * @brief Create an arena that hands out fixed-size, zeroed objects of
 * object_size bytes. Memory is grabbed from the heap in blocks of
 * initial_capacity objects; when a block is exhausted another block of the
 * same size is allocated. Objects never move, so a pointer returned by
 * mochi_arena_get stays valid until it is released (and the underlying memory
 * stays valid until mochi_arena_destroy).
 *
 * object_size is clamped to at least sizeof(void*) (the free list is stored
 * in-place in unused objects) and rounded up for alignment. initial_capacity
 * is clamped to at least 1.
 *
 * get/release are thread-safe (guarded by an internal spinlock). create and
 * destroy are not meant to be called concurrently with get/release.
 *
 * @param object_size Size in bytes of each object.
 * @param initial_capacity Number of objects per block.
 *
 * @return a new arena, or MOCHI_ARENA_NULL on error.
 */
mochi_arena_t mochi_arena_create(size_t object_size, size_t initial_capacity);

/**
 * @brief Get a zeroed object from the arena. Returns NULL if memory could not
 * be allocated.
 */
void* mochi_arena_get(mochi_arena_t arena);

/**
 * @brief Return an object previously obtained from this arena. The object must
 * not be used after this call.
 */
void mochi_arena_release(mochi_arena_t arena, void* obj);

/**
 * @brief Destroy the arena and free all its blocks. NULL-safe. Any object still
 * checked out becomes invalid.
 */
void mochi_arena_destroy(mochi_arena_t arena);

#ifdef __cplusplus
}
#endif

#endif /* MOCHI_ARENA_H */
