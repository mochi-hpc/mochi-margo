/*
 * (C) 2026 The University of Chicago
 *
 * See COPYRIGHT in top-level directory.
 */
#include "mochi-arena.h"

#include <abt.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/* Each block holds block_capacity objects right after this header. Blocks are
 * chained so that mochi_arena_destroy can free them all. */
struct mochi_arena_block {
    struct mochi_arena_block* next;
};

struct mochi_arena {
    size_t object_size;    /* >= sizeof(void*), rounded up for alignment */
    size_t block_capacity; /* number of objects per block, >= 1 */
    void*  free_list;      /* intrusive: a free object's first bytes hold the
                              pointer to the next free object */
    struct mochi_arena_block* blocks;
    ABT_mutex_memory          mtx;
};

/* Round x up to a multiple of a (a must be a power of two). */
static inline size_t round_up(size_t x, size_t a)
{
    return (x + (a - 1)) & ~(a - 1);
}

mochi_arena_t mochi_arena_create(size_t object_size, size_t initial_capacity)
{
    mochi_arena_t arena = (mochi_arena_t)calloc(1, sizeof(*arena));
    if (!arena) return MOCHI_ARENA_NULL;

    if (object_size < sizeof(void*)) object_size = sizeof(void*);
    /* keep objects 8-byte aligned (sufficient for the structs stored here, all
     * of which contain only doubles/pointers/integers) */
    arena->object_size    = round_up(object_size, 8);
    arena->block_capacity = initial_capacity < 1 ? 1 : initial_capacity;
    arena->free_list      = NULL;
    arena->blocks         = NULL;
    return arena;
}

/* Allocate a new block and thread its objects onto the free list.
 * Must be called with the arena locked. Returns 0 on success, -1 on OOM. */
static int mochi_arena_grow(mochi_arena_t arena)
{
    size_t header = round_up(sizeof(struct mochi_arena_block), 8);
    size_t total  = header + arena->block_capacity * arena->object_size;

    struct mochi_arena_block* block
        = (struct mochi_arena_block*)malloc(total);
    if (!block) return -1;

    block->next    = arena->blocks;
    arena->blocks  = block;

    char* base = (char*)block + header;
    for (size_t i = 0; i < arena->block_capacity; i++) {
        void* obj         = base + i * arena->object_size;
        *(void**)obj      = arena->free_list;
        arena->free_list  = obj;
    }
    return 0;
}

void* mochi_arena_get(mochi_arena_t arena)
{
    if (!arena) return NULL;
    void* obj = NULL;
    ABT_mutex_spinlock(ABT_MUTEX_MEMORY_GET_HANDLE(&arena->mtx));
    if (!arena->free_list && mochi_arena_grow(arena) != 0) {
        ABT_mutex_unlock(ABT_MUTEX_MEMORY_GET_HANDLE(&arena->mtx));
        return NULL;
    }
    obj              = arena->free_list;
    arena->free_list = *(void**)obj;
    memset(obj, 0, arena->object_size);
    ABT_mutex_unlock(ABT_MUTEX_MEMORY_GET_HANDLE(&arena->mtx));
    return obj;
}

void mochi_arena_release(mochi_arena_t arena, void* obj)
{
    if (!arena || !obj) return;
    ABT_mutex_spinlock(ABT_MUTEX_MEMORY_GET_HANDLE(&arena->mtx));
    *(void**)obj     = arena->free_list;
    arena->free_list = obj;
    ABT_mutex_unlock(ABT_MUTEX_MEMORY_GET_HANDLE(&arena->mtx));
}

void mochi_arena_destroy(mochi_arena_t arena)
{
    if (!arena) return;
    struct mochi_arena_block* block = arena->blocks;
    while (block) {
        struct mochi_arena_block* next = block->next;
        free(block);
        block = next;
    }
    free(arena);
}
