/*
 * (C) 2020 The University of Chicago
 *
 * See COPYRIGHT in top-level directory.
 */


#include <abt.h>
#include <pthread.h>
#include <stdlib.h>

#include "margo-prio-pool.h"

/* Once a unit has been pushed this many times, it no longer receives a
 * priority boost, on the assumption that it is a long-running background ULT
 */
#define PUSH_COUNTER_PRIORITY_LIMIT 50

typedef struct unit_t {
    ABT_thread thread;
    ABT_task task;
    struct unit_t *p_prev;
    struct unit_t *p_next;
    int push_counter;
    ABT_bool is_in_pool;
} unit_t;

typedef struct queue_t {
    unit_t *p_tail;
    unit_t *p_head;
} queue_t;

static inline void queue_push(queue_t *p_queue, unit_t *p_unit)
{
    if (p_queue->p_head == NULL) {
        p_unit->p_next = p_unit;
        p_unit->p_prev = p_unit;
        p_queue->p_head = p_unit;
        p_queue->p_tail = p_unit;
    } else {
        unit_t *p_head = p_queue->p_head;
        unit_t *p_tail = p_queue->p_tail;
        p_tail->p_next = p_unit;
        p_head->p_prev = p_unit;
        p_unit->p_prev = p_tail;
        p_unit->p_next = p_head;
        p_queue->p_tail = p_unit;
    }
    p_unit->is_in_pool = ABT_TRUE;
}

static inline unit_t *queue_pop(queue_t *p_queue)
{
    if (p_queue->p_head == NULL) {
        return NULL;
    } else {
        unit_t *p_unit = p_queue->p_head;
        if(p_queue->p_head == p_queue->p_tail) {
            /* one item */
            p_queue->p_head = NULL;
            p_queue->p_tail = NULL;
        }
        else {
            p_unit->p_prev->p_next = p_unit->p_next;
            p_unit->p_next->p_prev = p_unit->p_prev;
            p_queue->p_head = p_unit->p_next;
        }
        p_unit->p_next = NULL;
        p_unit->p_prev = NULL;
        p_unit->is_in_pool = ABT_FALSE;
        return p_unit;
    }
}

typedef struct pool_t {
    queue_t high_prio_queue;
    queue_t low_prio_queue;
    int num;
    int cnt;
    pthread_mutex_t mutex;
    pthread_cond_t cond;
} pool_t;

static ABT_unit_type pool_unit_get_type(ABT_unit unit)
{
    unit_t *p_unit = (unit_t *)unit;
    if (p_unit->thread != ABT_THREAD_NULL) {
        return ABT_UNIT_TYPE_THREAD;
    } else {
        return ABT_UNIT_TYPE_TASK;
    }
}

static ABT_thread pool_unit_get_thread(ABT_unit unit)
{
    unit_t *p_unit = (unit_t *)unit;
    return p_unit->thread;
}

static ABT_task pool_unit_get_task(ABT_unit unit)
{
    unit_t *p_unit = (unit_t *)unit;
    return p_unit->task;
}

static ABT_bool pool_unit_is_in_pool(ABT_unit unit)
{
    unit_t *p_unit = (unit_t *)unit;
    return p_unit->is_in_pool;
}

static ABT_unit pool_unit_create_from_thread(ABT_thread thread)
{
    unit_t *p_unit = (unit_t *)malloc(sizeof(unit_t));
    p_unit->thread = thread;
    p_unit->task = ABT_TASK_NULL;
    p_unit->p_next = NULL;
    p_unit->p_prev = NULL;
    p_unit->push_counter = 0;
    p_unit->is_in_pool = ABT_FALSE;
    return (ABT_unit)p_unit;
}

static ABT_unit pool_unit_create_from_task(ABT_task task)
{
    unit_t *p_unit = (unit_t *)malloc(sizeof(unit_t));
    p_unit->thread = ABT_THREAD_NULL;
    p_unit->task = task;
    p_unit->p_next = NULL;
    p_unit->p_prev = NULL;
    p_unit->push_counter = 0;
    p_unit->is_in_pool = ABT_FALSE;
    return (ABT_unit)p_unit;
}

static void pool_unit_free(ABT_unit *p_unit)
{
    free(*p_unit);
    *p_unit = ABT_UNIT_NULL;
}

static int pool_init(ABT_pool pool, ABT_pool_config config)
{
    pool_t *p_pool = (pool_t *)malloc(sizeof(pool_t));
    p_pool->high_prio_queue.p_tail = NULL;
    p_pool->high_prio_queue.p_head = NULL;
    p_pool->low_prio_queue.p_tail = NULL;
    p_pool->low_prio_queue.p_head = NULL;
    p_pool->num = 0;
    p_pool->cnt = 0;
    pthread_mutex_init(&p_pool->mutex, NULL);
    pthread_cond_init(&p_pool->cond, NULL);
    ABT_pool_set_data(pool, (void *)p_pool);
    return ABT_SUCCESS;
}

static size_t pool_get_size(ABT_pool pool)
{
    pool_t *p_pool;
    ABT_pool_get_data(pool, (void **)&p_pool);
    return p_pool->num;
}

static void pool_push(ABT_pool pool, ABT_unit unit)
{
    pool_t *p_pool;
    ABT_pool_get_data(pool, (void **)&p_pool);
    unit_t *p_unit = (unit_t *)unit;
    int push_counter;
#if 0
    ABT_thread_state state;
    /* If it is a thread, look at state */
    if(p_unit->thread != ABT_THREAD_NULL) {
        if(ABT_thread_get_state(p_unit->thread, &state) == ABT_SUCCESS) {
            fprintf(stderr, "DBG: %p state on push: %d\n", p_unit->thread, state);
        }
    }
#endif

    /* save incoming value of push counter, then increment */
    push_counter = p_unit->push_counter;
    if(p_unit->push_counter < PUSH_COUNTER_PRIORITY_LIMIT)
        p_unit->push_counter++;

    // fprintf(stderr, "DBG: looking at push_counter %d\n", push_counter);
    pthread_mutex_lock(&p_pool->mutex);
    if (push_counter == 0 || push_counter >= PUSH_COUNTER_PRIORITY_LIMIT) {
        /* The first push or long-running ULT, so put it to the low-priority pool. */
        queue_push(&p_pool->low_prio_queue, p_unit);
    } else {
        /* high-priority pool, for ULTs that have been suspended more than
         * once but not excessively
         */
        queue_push(&p_pool->high_prio_queue, p_unit);
    }
    p_pool->num++;
    pthread_cond_signal(&p_pool->cond);
    pthread_mutex_unlock(&p_pool->mutex);
}

static ABT_unit pool_pop(ABT_pool pool)
{
    pool_t *p_pool;
    ABT_pool_get_data(pool, (void **)&p_pool);

    /* Sometimes it should pop from low_prio_queue to avoid a deadlock. */
    pthread_mutex_lock(&p_pool->mutex);
    unit_t *p_unit = NULL;
    do {
        if ((p_pool->cnt++ & 0xFF) != 0) {
            p_unit = queue_pop(&p_pool->high_prio_queue);
            if (p_unit) {
                // fprintf(stderr, "DBG: found high.\n");
                break;
            }
            p_unit = queue_pop(&p_pool->low_prio_queue);
            if (p_unit) {
                // fprintf(stderr, "DBG: found low.\n");
                break;
            }
        } else {
            p_unit = queue_pop(&p_pool->low_prio_queue);
            if (p_unit) {
                // fprintf(stderr, "DBG: found low.\n");
                break;
            }
            p_unit = queue_pop(&p_pool->high_prio_queue);
            if (p_unit) {
                // fprintf(stderr, "DBG: found high.\n");
                break;
            }
        }
    } while (0);
    if (p_unit)
        p_pool->num--;
    pthread_mutex_unlock(&p_pool->mutex);
    return p_unit ? (ABT_unit)p_unit : ABT_UNIT_NULL;
}

static inline void convert_double_sec_to_timespec(struct timespec *ts_out,
                                                  double seconds)
{
    ts_out->tv_sec = (time_t)seconds;
    ts_out->tv_nsec = (long)((seconds - ts_out->tv_sec) * 1000000000.0);
}

static ABT_unit pool_pop_timedwait(ABT_pool pool, double abstime_secs)
{
    pool_t *p_pool;
    ABT_pool_get_data(pool, (void **)&p_pool);
    /* Sometimes it should pop from low_prio_queue to avoid a deadlock. */
    pthread_mutex_lock(&p_pool->mutex);
    unit_t *p_unit = NULL;
    if (p_pool->num == 0) {
        struct timespec ts;
        convert_double_sec_to_timespec(&ts, abstime_secs);
        pthread_cond_timedwait(&p_pool->cond, &p_pool->mutex, &ts);
    }
    do {
        if ((p_pool->cnt++ & 0xFF) != 0) {
            p_unit = queue_pop(&p_pool->high_prio_queue);
            if (p_unit)
                break;
            p_unit = queue_pop(&p_pool->low_prio_queue);
            if (p_unit)
                break;
        } else {
            p_unit = queue_pop(&p_pool->low_prio_queue);
            if (p_unit)
                break;
            p_unit = queue_pop(&p_pool->high_prio_queue);
            if (p_unit)
                break;
        }
    } while (0);
    if (p_unit)
        p_pool->num--;
    pthread_mutex_unlock(&p_pool->mutex);
    return p_unit ? (ABT_unit)p_unit : ABT_UNIT_NULL;
}

static int pool_remove(ABT_pool pool, ABT_unit unit)
{
    pool_t *p_pool;
    ABT_pool_get_data(pool, (void **)&p_pool);
    unit_t *p_unit = (unit_t *)unit;

    pthread_mutex_lock(&p_pool->mutex);
    if (p_unit->p_prev) {
        p_unit->p_prev->p_next = p_unit->p_next;
    } else {
        if (p_pool->high_prio_queue.p_head == p_unit) {
            p_pool->high_prio_queue.p_head = p_unit->p_next;
        } else if (p_pool->low_prio_queue.p_head == p_unit) {
            p_pool->low_prio_queue.p_head = p_unit->p_next;
        }
    }
    if (p_unit->p_next) {
        p_unit->p_next->p_prev = p_unit->p_prev;
    } else {
        if (p_pool->high_prio_queue.p_tail == p_unit) {
            p_pool->high_prio_queue.p_tail = p_unit->p_prev;
        } else if (p_pool->low_prio_queue.p_tail == p_unit) {
            p_pool->low_prio_queue.p_tail = p_unit->p_prev;
        }
    }
    p_pool->num--;
    pthread_mutex_unlock(&p_pool->mutex);

    return ABT_SUCCESS;
}

static int pool_free(ABT_pool pool)
{
    pool_t *p_pool;
    ABT_pool_get_data(pool, (void **)&p_pool);
    pthread_mutex_destroy(&p_pool->mutex);
    pthread_cond_destroy(&p_pool->cond);
    free(p_pool);

    return ABT_SUCCESS;
}

void margo_create_prio_pool_def(ABT_pool_def *p_def)
{
    p_def->access = ABT_POOL_ACCESS_MPMC;
    p_def->u_get_type = pool_unit_get_type;
    p_def->u_get_thread = pool_unit_get_thread;
    p_def->u_get_task = pool_unit_get_task;
    p_def->u_is_in_pool = pool_unit_is_in_pool;
    p_def->u_create_from_thread = pool_unit_create_from_thread;
    p_def->u_create_from_task = pool_unit_create_from_task;
    p_def->u_free = pool_unit_free;
    p_def->p_init = pool_init;
    p_def->p_get_size = pool_get_size;
    p_def->p_push = pool_push;
    p_def->p_pop = pool_pop;
    /* NOTE: not present in abt v1.0 */
    /* p_def->p_pop_wait = pool_pop_wait; */
    p_def->p_pop_timedwait = pool_pop_timedwait;
    p_def->p_remove = pool_remove;
    p_def->p_free = pool_free;
    p_def->p_print_all = NULL; /* Optional. */
}

/******************************************************************************/

#if 0
#include <stdio.h>
#include <unistd.h>
#include <stdarg.h>

#define DEFAULT_NUM_XSTREAMS 1
#define NUM_THREADS 4

void thread_func(void *arg)
{
    int rank;
    ABT_xstream_self_rank(&rank);
    printf("[ES%2d] execute tid = %d (0th yield)\n", rank, (int)(intptr_t)arg);
    ABT_thread_yield();
    printf("[ES%2d] execute tid = %d (after 1st yield)\n", rank,
           (int)(intptr_t)arg);
    ABT_thread_yield();
    printf("[ES%2d] execute tid = %d (after 2nd yield)\n", rank,
           (int)(intptr_t)arg);
}

int main(int argc, char **argv)
{
    /* Read arguments. */
    int num_xstreams = DEFAULT_NUM_XSTREAMS;
    int use_custom_pool = 1;
    while (1) {
        int opt = getopt(argc, argv, "he:c:");
        if (opt == -1)
            break;
        switch (opt) {
            case 'e':
                num_xstreams = atoi(optarg);
                break;
            case 'c':
                use_custom_pool = atoi(optarg);
                break;
            case 'h':
            default:
                printf("Usage: ./a.out [-e NUM_XSTREAMS] [-c CUSTOM_POOL]\n");
                return -1;
        }
    }

    /* Allocate memory. */
    ABT_xstream *xstreams =
        (ABT_xstream *)malloc(sizeof(ABT_xstream) * num_xstreams);
    ABT_pool *pools = (ABT_pool *)malloc(sizeof(ABT_pool) * num_xstreams);
    ABT_sched *scheds = (ABT_sched *)malloc(sizeof(ABT_sched) * num_xstreams);

    /* Initialize Argobots. */
    ABT_init(argc, argv);

    /* Create pools. */
    ABT_pool_def pool_def;
    create_pool_def(&pool_def);
    for (int i = 0; i < num_xstreams; i++) {
        if (use_custom_pool) {
            ABT_pool_create(&pool_def, ABT_POOL_CONFIG_NULL, &pools[i]);
        } else {
            ABT_pool_create_basic(ABT_POOL_FIFO, ABT_POOL_ACCESS_MPMC, ABT_TRUE,
                                  &pools[i]);
        }
    }

    /* Create schedulers. */
    for (int i = 0; i < num_xstreams; i++) {
        ABT_sched_create_basic(ABT_SCHED_BASIC_WAIT, 1, &pools[i],
                               ABT_SCHED_CONFIG_NULL, &scheds[i]);
    }

    /* Create secondary execution streams. */
    for (int i = 0; i < num_xstreams; i++) {
        ABT_xstream_create(scheds[i], &xstreams[i]);
    }

    /* Push threads. */
    ABT_thread *threads =
        (ABT_thread *)malloc(sizeof(ABT_thread) * num_xstreams * NUM_THREADS);
    for (int i = 0; i < num_xstreams; i++) {
        for (int j = 0; j < NUM_THREADS; j++) {
            int tid = i * NUM_THREADS + j;
            ABT_thread_create(pools[i], thread_func, (void *)(intptr_t)(tid),
                              ABT_THREAD_ATTR_NULL, &threads[tid]);
        }
    }

    /* Join threads. */
    for (int i = 0; i < num_xstreams; i++) {
        for (int j = 0; j < NUM_THREADS; j++) {
            int tid = i * NUM_THREADS + j;
            ABT_thread_free(&threads[tid]);
        }
    }
    free(threads);

    /* Join secondary execution streams. */
    for (int i = 0; i < num_xstreams; i++) {
        ABT_xstream_join(xstreams[i]);
        ABT_xstream_free(&xstreams[i]);
    }

    if (use_custom_pool) {
        for (int i = 0; i < num_xstreams; i++)
            ABT_pool_free(&pools[i]);
    }

    /* Finalize Argobots. */
    ABT_finalize();

    /* Free allocated memory. */
    free(xstreams);
    free(pools);
    free(scheds);

    /* Check the results. */
    return 0;
}
#endif
