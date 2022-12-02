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

#include <margo-config-private.h>
#include "margo.h"
#include "margo-globals.h"
#include "margo-prio-pool.h"
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
    optional_char* access;      /* Unknown for custom user pools */
    uint32_t       num_rpc_ids; /* Number of RPC ids that use this pool */
    bool margo_free_flag;       /* flag if Margo is responsible for freeing */
    bool used_by_primary;       /* flag indicating the this pool is used by the
                                   primary ES */
} margo_abt_pool_t;

bool           __margo_abt_pool_validate_json(const json_object_t*, uint32_t);
bool           __margo_abt_pool_init_from_json(const json_object_t*,
                                               margo_abt_pool_t*,
                                               const margo_abt_pool_t*,
                                               unsigned);
json_object_t* __margo_abt_pool_to_json(const margo_abt_pool_t*);
void           __margo_abt_pool_destroy(margo_abt_pool_t*);
bool           __margo_abt_pool_init_external(const char*,
                                              ABT_pool,
                                              margo_abt_pool_t*,
                                              const margo_abt_pool_t*,
                                              unsigned);
/* Struct to track scheduler information in a margo_abt_xstream. */
typedef struct margo_abt_sched {
    ABT_sched sched;
    char*     type;
    uint32_t* pools;
    size_t    num_pools;
} margo_abt_sched_t;

bool           __margo_abt_sched_validate_json(const json_object_t*,
                                               uint32_t,
                                               const json_object_t*);
bool           __margo_abt_sched_init_from_json(const json_object_t*,
                                                margo_abt_sched_t*,
                                                const margo_abt_pool_t*,
                                                unsigned);
bool           __margo_abt_sched_init_external(ABT_sched,
                                               margo_abt_sched_t*,
                                               const margo_abt_pool_t*,
                                               unsigned);
json_object_t* __margo_abt_sched_to_json(const margo_abt_sched_t*,
                                         int,
                                         const margo_abt_pool_t*,
                                         unsigned);
void           __margo_abt_sched_destroy(margo_abt_sched_t*);

/* Struct to track ES created by margo along with a flag indicating if
 * margo is responsible for explicitly free'ing the ES or not.
 */
typedef struct margo_abt_xstream {
    char*                  name;
    ABT_xstream            xstream;
    struct margo_abt_sched sched;
    bool margo_free_flag; /* flag if Margo is responsible for freeing */
} margo_abt_xstream_t;

bool           __margo_abt_xstream_validate_json(const json_object_t*,
                                                 uint32_t,
                                                 const json_object_t*);
bool           __margo_abt_xstream_init_from_json(const json_object_t*,
                                                  margo_abt_xstream_t*,
                                                  const margo_abt_xstream_t*,
                                                  unsigned,
                                                  const margo_abt_pool_t*,
                                                  unsigned);
bool           __margo_abt_xstream_init_external(const char*,
                                                 ABT_xstream,
                                                 margo_abt_xstream_t*,
                                                 const margo_abt_xstream_t*,
                                                 unsigned,
                                                 const margo_abt_pool_t*,
                                                 unsigned);
json_object_t* __margo_abt_xstream_to_json(const margo_abt_xstream_t*,
                                           int,
                                           const margo_abt_pool_t*,
                                           unsigned);
void           __margo_abt_xstream_destroy(margo_abt_xstream_t*);

/* Argobots environment */
typedef struct margo_abt {
    struct margo_abt_pool*    pools;
    struct margo_abt_xstream* xstreams;
    unsigned                  num_pools;
    unsigned                  num_xstreams;
    unsigned                  progress_pool_idx;
    unsigned                  rpc_pool_idx;
} margo_abt_t;

/* User-provided initialization information */
typedef struct margo_abt_user_args {
    ABT_pool       progress_pool;        /* user-provided ABT_pool */
    ABT_pool       rpc_pool;             /* user-provided ABT_pool */
    json_object_t* jprogress_pool;       /* "progres_pool" field */
    json_object_t* jrpc_pool;            /* "handler_pool" field */
    json_object_t* juse_progress_thread; /* "use_progress_thread" field */
    json_object_t* jrpc_thread_count;    /* "rpc_thread_count" field */
} margo_abt_user_args_t;

bool           __margo_abt_validate_json(const json_object_t*,
                                         const margo_abt_user_args_t*);
bool           __margo_abt_init_from_json(const json_object_t*,
                                          const margo_abt_user_args_t*,
                                          margo_abt_t*);
json_object_t* __margo_abt_to_json(const margo_abt_t*, int);
void           __margo_abt_destroy(margo_abt_t*);
int            __margo_abt_find_pool_by_name(const margo_abt_t*, const char*);
int            __margo_abt_find_pool_by_handle(const margo_abt_t*, ABT_pool);
int __margo_abt_find_xstream_by_name(const margo_abt_t*, const char*);
int __margo_abt_find_xstream_by_handle(const margo_abt_t*, ABT_xstream);

#endif
