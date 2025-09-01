/*
 * (C) 2022 The University of Chicago
 *
 * See COPYRIGHT in top-level directory.
 */
#ifndef __MARGO_ABT_CONFIG_INTERNAL_H
#define __MARGO_ABT_CONFIG_INTERNAL_H
#include <assert.h>
#include <stdbool.h>
#include <unistd.h>
#include <abt.h>
#include <stdlib.h>
#include <json-c/json.h>

#include "margo.h"
#include "margo-globals.h"
#include "margo-prio-pool.h"
#include "margo-efirst-pool.h"
#include "margo-logging.h"
#include "margo-macros.h"
#include "margo-abt-macros.h"

/* default values for key ABT parameters if not specified */
#define MARGO_DEFAULT_ABT_MEM_MAX_NUM_STACKS 8
#define MARGO_DEFAULT_ABT_THREAD_STACKSIZE   2097152

typedef struct json_object json_object_t;

typedef char
    optional_char; /* optional string (may be null after initialization) */
/* after initialization from JSON, char* are assumed to point to a valid string
 */

typedef struct margo_abt margo_abt_t;

/* For all the structures bellow, the following functions are provided:
 * (1) _validate_json: validate that the provided JSON object can be used
 *   to initialize the given object type;
 * (2) _init_from_json: initializes the given structure using the provided
 *   JSON object. The JSON object must have been validated beforehand. The
 *   function will return true if initialization was successful, otherwise
 *   it returns false and reset the structure to 0.
 * (3) _to_json: returns a JSON object for the provided structure. The
 *   structure must have been initialized.
 * (4) _destroy: frees all the resources allocated by the structures. The
 *   function is valid on a zero-ed structure or on a partially-initialized
 *   structure.
 */

/* Struct to track pools created by margo along with a flag indicating if
 * margo is responsible for explicitly free'ing the pool or not.
 */
typedef struct margo_abt_pool {
    char*          name;
    ABT_pool       pool;
    char*          kind;
    optional_char* access; /* Unknown for custom user pools */
    _Atomic(uint32_t)
        refcount; /* Number of RPC ids and external uses of this pool */
    _Atomic(uint32_t) num_xstreams; /* Number of xstreams that use this pool */
    bool margo_free_flag; /* flag if Margo is responsible for freeing */
    bool used_by_primary; /* flag indicating the this pool is used by the
                             primary ES */
} margo_abt_pool_t;

bool __margo_abt_pool_validate_json(const json_object_t* config,
                                    const margo_abt_t*   abt);

bool __margo_abt_pool_init_from_json(const json_object_t* config,
                                     const margo_abt_t*   abt,
                                     margo_abt_pool_t*    pool);

json_object_t* __margo_abt_pool_to_json(const margo_abt_pool_t* pool);

void __margo_abt_pool_destroy(margo_abt_pool_t* pool, const margo_abt_t* abt);

bool __margo_abt_pool_init_external(const char*        name,
                                    ABT_pool           handle,
                                    const margo_abt_t* abt,
                                    margo_abt_pool_t*  pool);

/* Struct to track scheduler information in a margo_abt_xstream. */
typedef struct margo_abt_sched {
    char* type;
} margo_abt_sched_t;

bool __margo_abt_sched_validate_json(const json_object_t* sched,
                                     const margo_abt_t*   abt,
                                     const json_object_t* pools_array);

bool __margo_abt_sched_init_from_json(const json_object_t* config,
                                      const margo_abt_t*   abt,
                                      margo_abt_sched_t*   sched,
                                      ABT_sched*           abt_sched);

bool __margo_abt_sched_init_external(ABT_sched          handle,
                                     const margo_abt_t* abt,
                                     margo_abt_sched_t* sched);

json_object_t* __margo_abt_sched_to_json(const margo_abt_sched_t* sched,
                                         ABT_sched                abt_sched,
                                         const margo_abt_t*       abt,
                                         int                      options);

void __margo_abt_sched_destroy(margo_abt_sched_t* sched);

/* Struct to track ES created by margo along with a flag indicating if
 * margo is responsible for explicitly free'ing the ES or not.
 */
typedef struct margo_abt_xstream {
    char*       name;
    ABT_xstream xstream;
    _Atomic(uint32_t) refcount; /* Number of external use this xstream */
    struct margo_abt_sched sched;
    bool margo_free_flag; /* flag if Margo is responsible for freeing */
} margo_abt_xstream_t;

bool __margo_abt_xstream_validate_json(const json_object_t* config,
                                       const margo_abt_t*   abt,
                                       const json_object_t* pools_array);

bool __margo_abt_xstream_init_from_json(const json_object_t* config,
                                        const margo_abt_t*   abt,
                                        margo_abt_xstream_t*);

bool __margo_abt_xstream_init_external(const char*          name,
                                       ABT_xstream          handle,
                                       const margo_abt_t*   abt,
                                       margo_abt_xstream_t* xstream);

json_object_t* __margo_abt_xstream_to_json(const margo_abt_xstream_t* xstream,
                                           const margo_abt_t*         abt,
                                           int                        options);

void __margo_abt_xstream_destroy(margo_abt_xstream_t* xstream,
                                 const margo_abt_t*   abt);

/* Argobots environment */
typedef struct margo_abt {
    /* array of pools */
    struct margo_abt_pool* pools;
    unsigned               pools_len;
    unsigned               pools_cap;
    /* array of xstreams */
    struct margo_abt_xstream* xstreams;
    unsigned                  xstreams_len;
    unsigned                  xstreams_cap;
    /* mutex protecting access to the above */
    ABT_mutex_memory mtx;
    /* margo instance owning this margo_abt */
    margo_instance_id mid;
    /* default directory for ABT profiling */
    char* profiling_dir;
} margo_abt_t;

// Note: none of the functions bellow lock/unlock their access
// to the margo_abt_t structure. It is up to the public functions
// in margo-config.c to do so.
void __margo_abt_lock(const margo_abt_t* abt);
void __margo_abt_unlock(const margo_abt_t* abt);

bool __margo_abt_validate_json(const json_object_t* config);

bool __margo_abt_init_from_json(const json_object_t* config, margo_abt_t*);

json_object_t* __margo_abt_to_json(const margo_abt_t* abt, int options);

void __margo_abt_destroy(margo_abt_t* abt);

int  __margo_abt_find_pool_by_name(const margo_abt_t* abt, const char* name);
int  __margo_abt_find_pool_by_handle(const margo_abt_t* abt, ABT_pool pool);
int  __margo_abt_find_xstream_by_name(const margo_abt_t* abt, const char* name);
int  __margo_abt_find_xstream_by_handle(const margo_abt_t* abt,
                                        ABT_xstream        xstream);
bool __margo_abt_add_pool_from_json(margo_abt_t*         abt,
                                    const json_object_t* config);
bool __margo_abt_add_xstream_from_json(margo_abt_t*         abt,
                                       const json_object_t* config);
bool __margo_abt_add_external_pool(margo_abt_t* abt,
                                   const char*  name,
                                   ABT_pool     pool);
bool __margo_abt_add_external_xstream(margo_abt_t* abt,
                                      const char*  name,
                                      ABT_xstream  xstream);
hg_return_t __margo_abt_remove_pool(margo_abt_t* abt, uint32_t index);
hg_return_t __margo_abt_remove_xstream(margo_abt_t* abt, uint32_t index);
#endif
