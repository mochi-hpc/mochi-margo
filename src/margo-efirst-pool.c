/*
 * (C) 2021 The University of Chicago
 *
 * See COPYRIGHT in top-level directory.
 */

#include <abt.h>
#include <pthread.h>
#include <stdlib.h>

#include "margo-efirst-pool.h"

/* ABT_POOL_EFIRST_WAIT */

/* This is a custom Argobots pool, compatible with ABT_POOL_FIFO_WAIT, that
 * prioritize the earliest posted threads/tasks over latter ones, using a
 * min-heap structure as an implementation of a priority queue.
 */

#define IS_IN_POOL 0x1
#define IS_THREAD  0x2

typedef struct unit_t {
    union {
        ABT_thread thread;
        ABT_task   task;
    };
    uint64_t priority;
    char     flag; // uses IS_IN_POOL and IS_THREAD
} unit_t;

typedef unit_t* entry_t;

typedef struct queue_t {
    entry_t* entries;
    size_t   capacity;
    size_t   size;
} queue_t;

static inline queue_t* create_queue(size_t initial_capacity)
{
    queue_t* queue = (queue_t*)malloc(sizeof(queue_t));
    queue->entries = (entry_t*)malloc((initial_capacity + 1) * sizeof(entry_t));
    queue->capacity = initial_capacity;
    queue->size     = 0;
    return queue;
}

static inline void destroy_queue(queue_t* queue)
{
    if (queue == NULL) return;
    free(queue->entries);
    free(queue);
}

static inline void queue_push(queue_t* queue, unit_t* p_unit)
{
    if (queue->size >= queue->capacity) {
        size_t   new_capacity = queue->capacity * 2; // Double the capacity
        entry_t* new_entries  = (entry_t*)realloc(
            queue->entries, (new_capacity + 1) * sizeof(entry_t));
        queue->entries  = new_entries;
        queue->capacity = new_capacity;
    }

    entry_t new_entry = p_unit;
    size_t  index     = ++queue->size;

    while (index > 1
           && new_entry->priority < queue->entries[index / 2]->priority) {
        queue->entries[index] = queue->entries[index / 2];
        index /= 2;
    }

    queue->entries[index] = new_entry;
    p_unit->flag |= IS_IN_POOL;
}

static inline unit_t* queue_pop(queue_t* queue)
{
    if (queue->size == 0) { return NULL; }

    entry_t min_entry  = queue->entries[1];
    entry_t last_entry = queue->entries[queue->size--];

    size_t index = 1;
    size_t child;

    while (index * 2 <= queue->size) {
        child = index * 2;
        if (child != queue->size
            && queue->entries[child + 1]->priority
                   < queue->entries[child]->priority)
            child++;

        if (last_entry->priority > queue->entries[child]->priority)
            queue->entries[index] = queue->entries[child];
        else
            break;

        index = child;
    }

    queue->entries[index] = last_entry;
    min_entry->flag ^= IS_IN_POOL;

    return min_entry;
}

typedef struct pool_t {
    queue_t*        queue;
    uint64_t        num; // number of new units created so far
    pthread_mutex_t mutex;
    pthread_cond_t  cond;
} pool_t;

static ABT_unit_type pool_unit_get_type(ABT_unit unit)
{
    unit_t* p_unit = (unit_t*)unit;
    if (p_unit->flag & IS_THREAD) {
        return ABT_UNIT_TYPE_THREAD;
    } else {
        return ABT_UNIT_TYPE_TASK;
    }
}

static ABT_thread pool_unit_get_thread(ABT_unit unit)
{
    unit_t* p_unit = (unit_t*)unit;
    return p_unit->thread;
}

static ABT_task pool_unit_get_task(ABT_unit unit)
{
    unit_t* p_unit = (unit_t*)unit;
    return p_unit->task;
}

static ABT_bool pool_unit_is_in_pool(ABT_unit unit)
{
    unit_t* p_unit = (unit_t*)unit;
    return p_unit->flag & IS_IN_POOL;
}

static ABT_unit pool_unit_create_from_thread(ABT_thread thread)
{
    unit_t* p_unit = (unit_t*)calloc(1, sizeof(unit_t));
    p_unit->thread = thread;
    p_unit->flag   = IS_THREAD;
    return (ABT_unit)p_unit;
}

static ABT_unit pool_unit_create_from_task(ABT_task task)
{
    unit_t* p_unit = (unit_t*)calloc(1, sizeof(unit_t));
    p_unit->task   = task;
    return (ABT_unit)p_unit;
}

static void pool_unit_free(ABT_unit* p_unit)
{
    free(*p_unit);
    *p_unit = ABT_UNIT_NULL;
}

static int pool_init(ABT_pool pool, ABT_pool_config config)
{
    (void)config;
    pool_t* p_pool = (pool_t*)calloc(1, sizeof(pool_t));
    p_pool->queue  = create_queue(32);
    pthread_mutex_init(&p_pool->mutex, NULL);
    pthread_cond_init(&p_pool->cond, NULL);
    ABT_pool_set_data(pool, (void*)p_pool);
    return ABT_SUCCESS;
}

static size_t pool_get_size(ABT_pool pool)
{
    pool_t* p_pool;
    ABT_pool_get_data(pool, (void**)&p_pool);
    return p_pool->queue->size;
}

static void pool_push(ABT_pool pool, ABT_unit unit)
{
    pool_t* p_pool;
    ABT_pool_get_data(pool, (void**)&p_pool);
    unit_t* p_unit = (unit_t*)unit;
    pthread_mutex_lock(&p_pool->mutex);
    if (p_unit->priority == 0) {
        p_pool->num += 1;
        p_unit->priority = p_pool->num;
    }
    queue_push(p_pool->queue, p_unit);
    pthread_cond_signal(&p_pool->cond);
    pthread_mutex_unlock(&p_pool->mutex);
}

static ABT_unit pool_pop(ABT_pool pool)
{
    pool_t* p_pool;
    ABT_pool_get_data(pool, (void**)&p_pool);
    pthread_mutex_lock(&p_pool->mutex);
    unit_t* p_unit = queue_pop(p_pool->queue);
    pthread_mutex_unlock(&p_pool->mutex);
    return p_unit ? (ABT_unit)p_unit : ABT_UNIT_NULL;
}

static inline void convert_double_sec_to_timespec(struct timespec* ts_out,
                                                  double           seconds)
{
    ts_out->tv_sec  = (time_t)seconds;
    ts_out->tv_nsec = (long)((seconds - ts_out->tv_sec) * 1000000000.0);
}

static ABT_unit pool_pop_timedwait(ABT_pool pool, double abstime_secs)
{
    pool_t* p_pool;
    ABT_pool_get_data(pool, (void**)&p_pool);
    pthread_mutex_lock(&p_pool->mutex);
    if (p_pool->queue->size == 0) {
        struct timespec ts;
        convert_double_sec_to_timespec(&ts, abstime_secs);
        pthread_cond_timedwait(&p_pool->cond, &p_pool->mutex, &ts);
    }
    unit_t* p_unit = queue_pop(p_pool->queue);
    pthread_mutex_unlock(&p_pool->mutex);
    return p_unit ? (ABT_unit)p_unit : ABT_UNIT_NULL;
}

static int pool_free(ABT_pool pool)
{
    pool_t* p_pool;
    ABT_pool_get_data(pool, (void**)&p_pool);
    destroy_queue(p_pool->queue);
    pthread_mutex_destroy(&p_pool->mutex);
    pthread_cond_destroy(&p_pool->cond);
    free(p_pool);

    return ABT_SUCCESS;
}

void margo_create_efirst_pool_def(ABT_pool_def* p_def)
{
    p_def->access               = ABT_POOL_ACCESS_MPMC;
    p_def->u_get_type           = pool_unit_get_type;
    p_def->u_get_thread         = pool_unit_get_thread;
    p_def->u_get_task           = pool_unit_get_task;
    p_def->u_is_in_pool         = pool_unit_is_in_pool;
    p_def->u_create_from_thread = pool_unit_create_from_thread;
    p_def->u_create_from_task   = pool_unit_create_from_task;
    p_def->u_free               = pool_unit_free;
    p_def->p_init               = pool_init;
    p_def->p_get_size           = pool_get_size;
    p_def->p_push               = pool_push;
    p_def->p_pop                = pool_pop;
    p_def->p_pop_timedwait      = pool_pop_timedwait;
    p_def->p_remove             = NULL; /* Optional */
    p_def->p_free               = pool_free;
    p_def->p_print_all          = NULL; /* Optional */
}
