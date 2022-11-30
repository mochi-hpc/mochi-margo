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

static inline bool margo_abt_pool_validate_json(const json_object_t*, uint32_t);
static inline bool margo_abt_pool_init_from_json(const json_object_t*,
                                                 uint32_t,
                                                 margo_abt_pool_t*);
static inline json_object_t* margo_abt_pool_to_json(const margo_abt_pool_t*);
static inline void           margo_abt_pool_destroy(margo_abt_pool_t*);
static inline bool
margo_abt_pool_init_external(const char*, ABT_pool, margo_abt_pool_t*);

/* Struct to track scheduler information in a margo_abt_xstream. */
typedef struct margo_abt_sched {
    ABT_sched sched;
    char*     type;
    uint32_t* pools;
    size_t    num_pools;
} margo_abt_sched_t;

static inline bool margo_abt_sched_validate_json(const json_object_t*,
                                                 uint32_t,
                                                 const json_object_t*);
static inline bool margo_abt_sched_init_from_json(const json_object_t*,
                                                  margo_abt_sched_t*,
                                                  const margo_abt_pool_t*,
                                                  unsigned);
static inline bool margo_abt_sched_init_external(ABT_sched,
                                                 margo_abt_sched_t*,
                                                 const margo_abt_pool_t*,
                                                 unsigned);
static inline json_object_t* margo_abt_sched_to_json(const margo_abt_sched_t*,
                                                     int,
                                                     const margo_abt_pool_t*,
                                                     unsigned);
static inline void           margo_abt_sched_destroy(margo_abt_sched_t*);

/* Struct to track ES created by margo along with a flag indicating if
 * margo is responsible for explicitly free'ing the ES or not.
 */
typedef struct margo_abt_xstream {
    char*                  name;
    ABT_xstream            xstream;
    struct margo_abt_sched sched;
    bool margo_free_flag; /* flag if Margo is responsible for freeing */
} margo_abt_xstream_t;

static inline bool margo_abt_xstream_validate_json(const json_object_t*,
                                                   uint32_t,
                                                   const json_object_t*);
static inline bool margo_abt_xstream_init_from_json(const json_object_t*,
                                                    uint32_t,
                                                    margo_abt_xstream_t*,
                                                    margo_abt_pool_t*,
                                                    unsigned);
static inline bool margo_abt_xstream_init_external(const char*,
                                                   ABT_xstream,
                                                   margo_abt_pool_t*,
                                                   unsigned,
                                                   margo_abt_xstream_t*);
static inline json_object_t* margo_abt_xstream_to_json(
    const margo_abt_xstream_t*, int, const margo_abt_pool_t*, unsigned);
static inline void margo_abt_xstream_destroy(margo_abt_xstream_t*);

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

static inline bool           margo_abt_validate_json(const json_object_t*,
                                                     const margo_abt_user_args_t*);
static inline bool           margo_abt_init_from_json(const json_object_t*,
                                                      const margo_abt_user_args_t*,
                                                      margo_abt_t*);
static inline json_object_t* margo_abt_to_json(const margo_abt_t*, int);
static inline void           margo_abt_destroy(margo_abt_t*);
static inline int margo_abt_find_pool_by_name(const margo_abt_t*, const char*);
static inline int margo_abt_find_pool_by_handle(const margo_abt_t*, ABT_pool);
static inline int margo_abt_find_xstream_by_name(const margo_abt_t*,
                                                 const char*);
static inline int margo_abt_find_xstream_by_handle(const margo_abt_t*,
                                                   ABT_xstream);

/* ------------------ Implementations ---------------------- */

/* ------ margo_abt_pool_* --------- */

static inline bool margo_abt_pool_validate_json(const json_object_t* jpool,
                                                uint32_t             index)
{
    if (!jpool || !json_object_is_type(jpool, json_type_object)) return false;

#define HANDLE_CONFIG_ERROR return false

    /* default: "mpmc" for predef pools */
    ASSERT_CONFIG_HAS_OPTIONAL(jpool, access, string, pool);
    json_object_t* jaccess = json_object_object_get(jpool, "access");
    if (jaccess) {
        char fmt[128];
        sprintf(fmt, "argobots.pools[%d].access", index);
        CONFIG_IS_IN_ENUM_STRING(jaccess, fmt, "private", "spsc", "mpsc",
                                 "spmc", "mpmc");
    }

    /* default: "fifo_wait" */
    ASSERT_CONFIG_HAS_OPTIONAL(jpool, kind, string, pool);
    json_object_t* jkind = json_object_object_get(jpool, "kind");
    if (jkind) {
        char fmt[128];
        sprintf(fmt, "argobots.pools[%d].kind", index);
        CONFIG_IS_IN_ENUM_STRING(jkind, fmt, "fifo", "fifo_wait", "prio_wait",
                                 "external");
        if (strcmp(json_object_get_string(jkind), "external") == 0) {
            margo_error(0,
                        "Cannot instantiate configuration with pools marked "
                        "\"external\"");
            return false;
        }
    }
    // TODO: support dlopen-ed pool definitions

    /* default: generated */
    ASSERT_CONFIG_HAS_OPTIONAL(jpool, name, string, pool);
    json_object_t* jname = json_object_object_get(jpool, "name");
    if (jname) CONFIG_NAME_IS_VALID(jpool);

#undef HANDLE_CONFIG_ERROR

    return true;
}

static inline bool margo_abt_pool_init_from_json(const json_object_t* jpool,
                                                 uint32_t             index,
                                                 margo_abt_pool_t*    p)
{
    ABT_pool_access access = ABT_POOL_ACCESS_MPMC;
    ABT_pool_kind   kind   = (ABT_pool_kind)(-1);

    p->margo_free_flag = ABT_TRUE;

    json_object_t* jname = json_object_object_get(jpool, "name");
    if (jname)
        p->name = strdup(json_object_get_string(jname));
    else {
        // TODO check that the generated name is not used
        int name_size = snprintf(NULL, 0, "__pool_%d__", index);
        p->name       = calloc(1, name_size + 1);
        snprintf((char*)p->name, name_size + 1, "__pool_%d__", index);
    }

    p->kind
        = strdup(json_object_object_get_string_or(jpool, "kind", "fifo_wait"));
    if (strcmp(p->kind, "fifo_wait") == 0)
        kind = ABT_POOL_FIFO_WAIT;
    else if (strcmp(p->kind, "fifo") == 0)
        kind = ABT_POOL_FIFO;
#if ABT_NUMVERSION >= 20000000
    else if (strcmp(p->kind, "randws") == 0)
        kind = ABT_POOL_RANDWS;
#endif

    json_object_t* jaccess = json_object_object_get(jpool, "access");
    if (jaccess) {
        p->access = strdup(json_object_get_string(jaccess));
        if (strcmp(p->access, "private") == 0)
            access = ABT_POOL_ACCESS_PRIV;
        else if (strcmp(p->access, "mpmc") == 0)
            access = ABT_POOL_ACCESS_MPMC;
        else if (strcmp(p->access, "spsc") == 0)
            access = ABT_POOL_ACCESS_SPSC;
        else if (strcmp(p->access, "mpsc") == 0)
            access = ABT_POOL_ACCESS_MPSC;
        else if (strcmp(p->access, "spmc") == 0)
            access = ABT_POOL_ACCESS_SPMC;
    }

    int ret;
    if (kind != (ABT_pool_kind)(-1))
        ret = ABT_pool_create_basic(kind, access, ABT_TRUE, &p->pool);
    else if (strcmp(p->kind, "prio_wait") == 0) {
        ABT_pool_def prio_pool_def;
        margo_create_prio_pool_def(&prio_pool_def);
        ret = ABT_pool_create(&prio_pool_def, ABT_POOL_CONFIG_NULL, &p->pool);
    } else {
        // custom pool definition, not supported for now
        margo_error(NULL,
                    "Invalid pool kind \"%s\" "
                    "(custom pool definitions not yet supported)",
                    p->kind);
        ret = ABT_ERR_INV_POOL_KIND;
    }

    if (ret != ABT_SUCCESS) {
        margo_abt_pool_destroy(p);
        return false;
    }
    p->margo_free_flag = true;
    return true;
}

static inline bool margo_abt_pool_init_external(const char*       name,
                                                ABT_pool          handle,
                                                margo_abt_pool_t* p)
{
    memset(p, 0, sizeof(*p));
    p->name = strdup(name);
    p->pool = handle;
    p->kind = strdup("external");
    return true;
}

static inline json_object_t* margo_abt_pool_to_json(const margo_abt_pool_t* p)
{
    json_object_t* jpool = json_object_new_object();
    int flags = JSON_C_OBJECT_ADD_KEY_IS_NEW | JSON_C_OBJECT_ADD_CONSTANT_KEY;
    json_object_object_add_ex(jpool, "kind", json_object_new_string(p->kind),
                              flags);
    json_object_object_add_ex(jpool, "name", json_object_new_string(p->name),
                              flags);
    if (p->access)
        json_object_object_add_ex(jpool, "access",
                                  json_object_new_string(p->access), flags);
    return jpool;
}

static inline void margo_abt_pool_destroy(margo_abt_pool_t* p)
{
    free(p->kind);
    free(p->access);
    free((char*)p->name);
    if (p->margo_free_flag && !p->used_by_primary && p->pool != ABT_POOL_NULL
        && p->pool != NULL) {
        ABT_pool_free(&(p->pool));
    }
    memset(p, 0, sizeof(*p));
}

/* ------ margo_abt_sched_* --------- */

static inline bool
margo_abt_sched_validate_json(const json_object_t* jsched,
                              uint32_t             es_index,
                              const json_object_t* javailable_pool_array)
{
    if (!jsched || !json_object_is_type(jsched, json_type_object)) return false;

#define HANDLE_CONFIG_ERROR return false

    ASSERT_CONFIG_HAS_OPTIONAL(jsched, type, string, scheduler);
    json_object_t* jtype = json_object_object_get(jsched, "type");
    if (jtype) {
        char field[128];
        sprintf(field, "argobots.xstreams[%d].scheduler.type", es_index);
        CONFIG_IS_IN_ENUM_STRING(jtype, field, "default", "basic", "prio",
                                 "randws", "basic_wait");
    }

    ASSERT_CONFIG_HAS_OPTIONAL(jsched, pools, array, scheduler);

    json_object_t* jsched_pools = json_object_object_get(jsched, "pools");
    size_t sched_pool_array_len = json_object_array_length(jsched_pools);
    int num_available_pools = json_object_array_length(javailable_pool_array);

    for (unsigned i = 0; i < sched_pool_array_len; i++) {
        json_object_t* jpool_ref = json_object_array_get_idx(jsched_pools, i);
        /* jpool_ref is an integer */
        if (json_object_is_type(jpool_ref, json_type_int64)) {
            int index = json_object_get_int(jpool_ref);
            if (index < 0 || index >= num_available_pools) {
                margo_error(
                    0, "Invalid pool index (%d) in scheduler configuration",
                    index);
                return false;
            }
            /* jpool_ref is a string */
        } else if (json_object_is_type(jpool_ref, json_type_string)) {
            const char* pool_name = json_object_get_string(jpool_ref);
            int         j         = 0;
            for (; j < num_available_pools; j++) {
                json_object_t* p
                    = json_object_array_get_idx(javailable_pool_array, j);
                json_object_t* p_name = json_object_object_get(p, "name");
                if (p_name
                    && strcmp(pool_name, json_object_get_string(p_name)) == 0)
                    break;
            }
            if (j == num_available_pools) {
                margo_error(0,
                            "Invalid reference to pool \"%s\" in scheduler "
                            "configuration",
                            pool_name);
                return false;
            }
            /* pool_ref is something else */
        } else {
            margo_error(0,
                        "Invalid pool type in scheduler configuration "
                        "(expected integer or string)");
            return false;
        }
    }
    return true;
#undef HANDLE_CONFIG_ERROR
}

static inline bool margo_abt_sched_init_from_json(const json_object_t* jsched,
                                                  margo_abt_sched_t*   s,
                                                  const margo_abt_pool_t* pools,
                                                  unsigned total_num_pools)
{
    s->type
        = strdup(json_object_object_get_string_or(jsched, "type", "default"));

    ABT_sched_predef sched_predef = ABT_SCHED_DEFAULT;
    if (s->type) {
        if (strcmp(s->type, "default") == 0)
            sched_predef = ABT_SCHED_DEFAULT;
        else if (strcmp(s->type, "basic") == 0)
            sched_predef = ABT_SCHED_BASIC;
        else if (strcmp(s->type, "prio") == 0)
            sched_predef = ABT_SCHED_PRIO;
        else if (strcmp(s->type, "randws") == 0)
            sched_predef = ABT_SCHED_RANDWS;
        else if (strcmp(s->type, "basic_wait") == 0)
            sched_predef = ABT_SCHED_BASIC_WAIT;
    }
    // TODO add support for dynamically loaded sched definitions

    json_object_t* jpools = json_object_object_get(jsched, "pools");
    if (jpools)
        json_object_get(jpools);
    else
        jpools = json_object_new_array_ext(0);

    size_t jpools_len   = json_object_array_length(jpools);
    s->pools            = calloc(jpools_len, sizeof(*(s->pools)));
    s->num_pools        = jpools_len;
    ABT_pool* abt_pools = malloc(jpools_len * sizeof(*abt_pools));
    for (unsigned i = 0; i < jpools_len; i++) {
        json_object_t* jpool    = json_object_array_get_idx(jpools, i);
        uint32_t       pool_idx = 0;
        if (json_object_is_type(jpool, json_type_int64)) {
            pool_idx = (uint32_t)json_object_get_int(jpool);
        } else {
            const char* pool_name = json_object_get_string(jpool);
            for (; pool_idx < total_num_pools; ++pool_idx) {
                if (!pools[pool_idx].name) continue;
                if (strcmp(pools[pool_idx].name, pool_name) == 0) break;
            }
        }
        s->pools[i]  = pool_idx;
        abt_pools[i] = pools[pool_idx].pool;
    }
    json_object_put(jpools);

    int ret = ABT_sched_create_basic(sched_predef, jpools_len, abt_pools,
                                     ABT_SCHED_CONFIG_NULL, &s->sched);
    free(abt_pools);

    if (ret != ABT_SUCCESS) {
        margo_abt_sched_destroy(s);
        return false;
    }

    return true;
}

static inline bool
margo_abt_sched_init_external(ABT_sched               sched,
                              margo_abt_sched_t*      s,
                              const margo_abt_pool_t* known_pools,
                              unsigned                num_known_pools)
{
    s->type  = strdup("external");
    s->sched = sched;

    int num_pools;
    int ret = ABT_sched_get_num_pools(sched, &num_pools);
    if (ret != ABT_SUCCESS) return false;

    s->num_pools    = (size_t)num_pools;
    s->pools        = calloc(num_pools, sizeof(*s->pools));
    ABT_pool* pools = alloca(num_pools * sizeof(ABT_pool));
    ret             = ABT_sched_get_pools(sched, num_pools, 0, pools);
    if (ret != ABT_SUCCESS) goto error;

    for (int i = 0; i < num_pools; i++) {
        unsigned j;
        for (j = 0; j < num_known_pools; j++) {
            if (pools[i] == known_pools[j].pool) {
                s->pools[i] = j;
                break;
            }
        }
        if (j == num_known_pools) {
            margo_error(0,
                        "A pool associated with external ES is not registered");
            goto error;
        }
    }

    return true;

error:
    margo_abt_sched_destroy(s);
    return false;
}

static inline json_object_t*
margo_abt_sched_to_json(const margo_abt_sched_t* s,
                        int                      options,
                        const margo_abt_pool_t*  known_pools,
                        unsigned                 num_known_pools)
{
    (void)num_known_pools;
    json_object_t* json = json_object_new_object();
    int flags = JSON_C_OBJECT_ADD_KEY_IS_NEW | JSON_C_OBJECT_ADD_CONSTANT_KEY;
    json_object_object_add_ex(json, "type", json_object_new_string(s->type),
                              flags);
    json_object_t* jpools = json_object_new_array_ext(s->num_pools);
    for (uint32_t i = 0; i < s->num_pools; i++) {
        if ((options & MARGO_CONFIG_HIDE_EXTERNAL)
            && (strcmp(known_pools[s->pools[i]].kind, "external") == 0))
            continue; // skip external pools if requested
        if (options & MARGO_CONFIG_USE_NAMES) {
            json_object_array_add(
                jpools, json_object_new_string(known_pools[s->pools[i]].name));
        } else {
            json_object_array_add(jpools, json_object_new_uint64(s->pools[i]));
        }
    }
    json_object_object_add_ex(json, "pools", jpools, flags);
    return json;
}

static inline void margo_abt_sched_destroy(margo_abt_sched_t* s)
{
    free(s->type);
    free(s->pools);
    memset(s, 0, sizeof(*s));
}

/* ------ margo_abt_xstream_* --------- */

static inline bool
margo_abt_xstream_validate_json(const json_object_t* jxstream,
                                uint32_t             index,
                                const json_object_t* javailable_pools)
{
#define HANDLE_CONFIG_ERROR return false
    if (!jxstream || !json_object_is_type(jxstream, json_type_object))
        return false;

    ASSERT_CONFIG_HAS_OPTIONAL(jxstream, scheduler, object, xstreams);
    ASSERT_CONFIG_HAS_OPTIONAL(jxstream, name, string, xstreams);
    ASSERT_CONFIG_HAS_OPTIONAL(jxstream, cpubind, int, xstreams);
    ASSERT_CONFIG_HAS_OPTIONAL(jxstream, affinity, array, xstreams);

    json_object_t* jsched = json_object_object_get(jxstream, "scheduler");
    if (jsched
        && !margo_abt_sched_validate_json(jsched, index, javailable_pools))
        return false;

    json_object_t* jaffinity = json_object_object_get(jxstream, "affinity");
    if (jaffinity) {
        for (unsigned i = 0; i < json_object_array_length(jaffinity); i++) {
            json_object_t* value = json_object_array_get_idx(jaffinity, i);
            if (!json_object_is_type(value, json_type_int)) {
                margo_error(
                    0,
                    "Invalid type found in affinity array (expected integer)");
                return false;
            }
        }
    }

    json_object_t* jname = json_object_object_get(jxstream, "name");
    if (jname) {
        CONFIG_NAME_IS_VALID(jxstream);
        const char* name = json_object_get_string(jname);
        if (strcmp(name, "__primary__") == 0) {
            if (!jsched) {
                margo_error(
                    0, "__primary__ xstream requires a scheduler definition");
                return false;
            }
            json_object_t* jpools = json_object_object_get(jsched, "pools");
            if (!jpools || json_object_array_length(jpools) == 0) {
                margo_error(0,
                            "__primary__ xstream requires scheduler to have at "
                            "least one pool");
                return false;
            }
        }
    }
    return true;
#undef HANDLE_CONFIG_ERROR
}

static inline bool
margo_abt_xstream_init_from_json(const json_object_t* jxstream,
                                 uint32_t             index,
                                 margo_abt_xstream_t* x,
                                 margo_abt_pool_t*    pools,
                                 unsigned             num_pools)
{
    bool           result = true;
    json_object_t* jname  = json_object_object_get(jxstream, "name");
    if (jname)
        x->name = strdup(json_object_get_string(jname));
    else {
        int name_len = snprintf(NULL, 0, "__xstream_%d__", index);
        x->name      = calloc(1, name_len + 1);
        snprintf((char*)x->name, name_len + 1, "__xstream_%d__", index);
    }

    int            cpubind  = -1;
    json_object_t* jcpubind = json_object_object_get(jxstream, "cpubind");
    if (jcpubind) cpubind = json_object_get_int(jcpubind);

    int*           affinity     = NULL;
    int            affinity_len = 0;
    json_object_t* jaffinity    = json_object_object_get(jxstream, "affinity");
    if (jaffinity) {
        affinity_len = json_object_array_length(jaffinity);
        affinity     = malloc(sizeof(*affinity) * affinity_len);
        for (int i = 0; i < affinity_len; i++) {
            affinity[i]
                = json_object_get_int(json_object_array_get_idx(jaffinity, i));
        }
    }

    json_object_t* jsched = json_object_object_get(jxstream, "scheduler");
    if (jsched)
        json_object_get(jsched);
    else
        jsched = json_object_new_object();

    if (!margo_abt_sched_init_from_json(jsched, &(x->sched), pools, num_pools))
        goto error;

    int ret;
    if (strcmp(x->name, "__primary__") != 0) {
        /* not the primary ES, create a new ES */
        ret                = ABT_xstream_create(x->sched.sched, &(x->xstream));
        x->margo_free_flag = true;
        for (unsigned i = 0; i < x->sched.num_pools; i++)
            pools[x->sched.pools[i]].used_by_primary = true;
    } else {
        /* primary ES, change its scheduler */
        ret = ABT_xstream_self(&x->xstream);
        if (ret != ABT_SUCCESS) {
            margo_error(0,
                        "Failed to retrieve self xstream (ABT_xstream_self "
                        "returned %d)",
                        ret);
            goto error;
        }
        ABT_bool is_primary;
        ret = ABT_xstream_is_primary(x->xstream, &is_primary);
        if (ret != ABT_SUCCESS) {
            margo_error(0,
                        "Could not check if current ES is primary "
                        "(ABT_xstream_is_primary returned %d)",
                        ret);
            goto error;
        }
        if (!is_primary) {
            margo_error(
                0,
                "\"__primary__\" ES can only be defined from the primary ES");
            goto error;
        }
        ret = ABT_xstream_set_main_sched(x->xstream, x->sched.sched);
        if (ret != ABT_SUCCESS) {
            margo_error(0,
                        "Could not set the main scheduler of the primary ES");
            goto error;
        }
    }

    if (ret == ABT_SUCCESS) {
        if (affinity)
            ABT_xstream_set_affinity(x->xstream, affinity_len, affinity);
        if (cpubind >= 0) ABT_xstream_set_cpubind(x->xstream, cpubind);
    }

finish:
    json_object_put(jsched);
    free(affinity);
    return result;

error:
    result = false;
    margo_abt_xstream_destroy(x);
    goto finish;
}

static inline bool
margo_abt_xstream_init_external(const char*          name,
                                ABT_xstream          handle,
                                margo_abt_pool_t*    known_pools,
                                unsigned             num_known_pools,
                                margo_abt_xstream_t* x)
{
    x->name    = strdup(name);
    x->xstream = handle;

    ABT_sched sched;
    int       ret = ABT_xstream_get_main_sched(handle, &sched);
    if (ret != ABT_SUCCESS) {
        margo_error(0,
                    "Could not retrieve main scheduler from ES "
                    "(ABT_xstream_get_main_sched returned %d",
                    ret);
        goto error;
    }

    if (!margo_abt_sched_init_external(sched, &x->sched, known_pools,
                                       num_known_pools)) {
        goto error;
    }

    if (strcmp(name, "__primary__") == 0) {
        for (unsigned i = 0; i < x->sched.num_pools; i++) {
            known_pools[x->sched.pools[i]].used_by_primary = true;
        }
    }

    return true;

error:
    margo_abt_xstream_destroy(x);
    return false;
}

static inline json_object_t*
margo_abt_xstream_to_json(const margo_abt_xstream_t* x,
                          int                        options,
                          const margo_abt_pool_t*    known_pools,
                          unsigned                   num_known_pools)
{
    json_object_t* jxstream = json_object_new_object();
    json_object_t* jsched   = margo_abt_sched_to_json(
        &(x->sched), options, known_pools, num_known_pools);
    int flags = JSON_C_OBJECT_ADD_KEY_IS_NEW | JSON_C_OBJECT_ADD_CONSTANT_KEY;
    json_object_object_add_ex(jxstream, "scheduler", jsched, flags);

    json_object_object_add_ex(jxstream, "name", json_object_new_string(x->name),
                              flags);

    int cpuid;
    int ret = ABT_xstream_get_cpubind(x->xstream, &cpuid);
    if (ret == ABT_SUCCESS) {
        json_object_object_add_ex(jxstream, "cpubind",
                                  json_object_new_int(cpuid), flags);
    }

    int num_cpus;
    ret = ABT_xstream_get_affinity(x->xstream, 0, NULL, &num_cpus);
    if (ret == ABT_SUCCESS) {
        int* cpuids = malloc(num_cpus * sizeof(*cpuids));
        ret = ABT_xstream_get_affinity(x->xstream, num_cpus, cpuids, &num_cpus);
        if (ret == ABT_SUCCESS) {
            json_object_t* jcpuids = json_object_new_array_ext(num_cpus);
            for (int i = 0; i < num_cpus; i++) {
                json_object_array_add(jcpuids,
                                      json_object_new_int64(cpuids[i]));
            }
            json_object_object_add_ex(jxstream, "affinity", jcpuids, flags);
        }
        free(cpuids);
    }
    return jxstream;
}

static inline void margo_abt_xstream_destroy(margo_abt_xstream_t* x)
{
    if (x->margo_free_flag && x->xstream != ABT_XSTREAM_NULL
        && x->xstream != NULL) {
        ABT_xstream_join(x->xstream);
        ABT_xstream_free(&x->xstream);
    }
    margo_abt_sched_destroy(&(x->sched));
    free((char*)x->name);
    memset(x, 0, sizeof(*x));
}

/* ------ margo_abt_* --------- */

static inline bool
margo_abt_validate_json(const json_object_t*         a,
                        const margo_abt_user_args_t* user_args)
{
#define HANDLE_CONFIG_ERROR \
    result = false;         \
    goto finish

    if (!a || !json_object_is_type(a, json_type_object)) return false;

    json_object_t* jpools = NULL;
    json_object_t* ignore = NULL;
    bool           result = true;
    ASSERT_CONFIG_HAS_OPTIONAL(a, pools, array, argobots);
    ASSERT_CONFIG_HAS_OPTIONAL(a, xstreams, array, argobots);
    ASSERT_CONFIG_HAS_OPTIONAL(a, abt_mem_max_num_stacks, int, argobots);
    ASSERT_CONFIG_HAS_OPTIONAL(a, abt_thread_stacksize, int, argobots);

    /* validate abt_mem_max_num_stacks and abt_thread_stacksize */

    json_object_t* jabt_mem_max_num_stacks
        = json_object_object_get(a, "abt_mem_max_num_stacks");
    if (jabt_mem_max_num_stacks) {
        if (getenv("ABT_MEM_MAX_NUM_STACKS")) {
            long val = atol(getenv("ABT_MEM_MAX_NUM_STACKS"));
            if (val != json_object_get_int64(jabt_mem_max_num_stacks)) {
                margo_warning(
                    0,
                    "\"abt_mem_max_num_stacks\" will be ignored"
                    " because the ABT_MEM_MAX_NUM_STACKS environment variable"
                    " is defined");
            }
        } else if (ABT_initialized() == ABT_SUCCESS) {
            margo_warning(0,
                          "\"abt_mem_max_num_stacks\" will be ignored"
                          " because Argobots is already initialized");
        }
    }

    json_object_t* jabt_thread_stacksize
        = json_object_object_get(a, "abt_thread_stacksize");
    if (jabt_thread_stacksize) {
        if (getenv("ABT_THREAD_STACKSIZE")) {
            long val = atol(getenv("ABT_THREAD_STACKSIZE"));
            if (val != json_object_get_int64(jabt_thread_stacksize)) {
                margo_warning(
                    0,
                    "\"abt_thread_stacksize\" will be ignored"
                    " because the ABT_THREAD_STACKSIZE environment variable"
                    " is defined");
            }
        } else if (ABT_initialized() == ABT_SUCCESS) {
            margo_warning(0,
                          "\"abt_thread_stacksize\" will be ignored"
                          " because Argobots is already initialized");
        }
    }

    /* validate the user-provided fields */

    bool has_external_progress_pool
        = (user_args->progress_pool != ABT_POOL_NULL)
       && (user_args->progress_pool != NULL);
    bool has_external_rpc_pool = (user_args->rpc_pool != ABT_POOL_NULL)
                              && (user_args->rpc_pool != NULL);

    if (user_args->jprogress_pool
        && !(json_object_is_type(user_args->jprogress_pool, json_type_int64)
             || json_object_is_type(user_args->jprogress_pool,
                                    json_type_string))) {
        margo_error(0,
                    "\"progress_pool\" field must be an integer or a string");
        HANDLE_CONFIG_ERROR;
    }

    if (user_args->jrpc_pool
        && !(json_object_is_type(user_args->jrpc_pool, json_type_int64)
             || json_object_is_type(user_args->jrpc_pool, json_type_string))) {
        margo_error(0, "\"rpc_pool\" field must be an integer or a string");
        HANDLE_CONFIG_ERROR;
    }

    if (user_args->juse_progress_thread
        && !json_object_is_type(user_args->juse_progress_thread,
                                json_type_boolean)) {
        margo_error(0, "\"use_progress_thread\" field must be a boolean");
        HANDLE_CONFIG_ERROR;
    }

    if (user_args->jrpc_thread_count
        && !json_object_is_type(user_args->jrpc_thread_count,
                                json_type_int64)) {
        margo_error(0, "\"rpc_thread_count\" field must be an integer");
        HANDLE_CONFIG_ERROR;
    }

    if (user_args->juse_progress_thread) {
        if (has_external_progress_pool) {
            margo_warning(0,
                          "\"use_progress_thread\" will be ignored"
                          " because external progress pool was provided");
        } else if (user_args->jprogress_pool) {
            margo_warning(0,
                          "\"use_progress_thread\" will be ignored"
                          " because \"progress_pool\" field was specified");
        }
    }

    if (has_external_progress_pool && user_args->jprogress_pool) {
        margo_warning(0,
                      "\"progress_pool\" will be ignored because"
                      " external progress pool was provided");
    }

    if (user_args->jrpc_thread_count) {
        if (has_external_rpc_pool) {
            margo_warning(0,
                          "\"rpc_thread_count\" will be ignored"
                          " because external rpc pool was provided");
        } else if (user_args->jrpc_pool) {
            margo_warning(0,
                          "\"rpc_thread_count\" will be ignored"
                          " because \"rpc_pool\" field was specified");
        }
    }

    if (has_external_rpc_pool && user_args->jrpc_pool) {
        margo_warning(0,
                      "\"rpc_pool\" will be ignored because"
                      " external rpc pool was provided");
    }

    /* validate the "pools" list */

    jpools        = json_object_object_get(a, "pools");
    int num_pools = 0;
    if (jpools) {
        json_object_get(jpools);
        num_pools = json_object_array_length(jpools);
        for (int i = 0; i < num_pools; ++i) {
            json_object_t* jpool = json_object_array_get_idx(jpools, i);
            if (!margo_abt_pool_validate_json(jpool, i)) {
                HANDLE_CONFIG_ERROR;
            }
        }
        CONFIG_NAMES_MUST_BE_UNIQUE(jpools, "argobots.pools");
    } else {
        jpools = json_object_new_array_ext(0);
    }

    /* check that progres_pool is present, if provided */
    if (user_args->jprogress_pool) {
        if (json_object_is_type(user_args->jprogress_pool, json_type_int64)) {
            /* jprogress_pool is an integer */
            int progress_pool_idx
                = json_object_get_int(user_args->jprogress_pool);
            if (progress_pool_idx < 0 || progress_pool_idx >= num_pools) {
                margo_error(0, "Invalid \"progress_pool\" index (%d)",
                            progress_pool_idx);
                HANDLE_CONFIG_ERROR;
            }
        } else {
            /* jprogress_pool is a string */
            const char* progress_pool_name
                = json_object_get_string(user_args->jprogress_pool);
            CONFIG_ARRAY_MUST_HAVE_ITEM_NAMED(jpools, progress_pool_name,
                                              "argobots.pools", ignore);
        }
    }

    /* check that rpc_pool is present, if provided */
    if (user_args->jrpc_pool) {
        if (json_object_is_type(user_args->jrpc_pool, json_type_int64)) {
            /* jrpc_pool is an integer */
            int rpc_pool_idx = json_object_get_int(user_args->jrpc_pool);
            if (rpc_pool_idx < 0 || rpc_pool_idx >= num_pools) {
                margo_error(0, "Invalid \"rpc_pool\" index (%d)", rpc_pool_idx);
                HANDLE_CONFIG_ERROR;
            }
        } else {
            /* jrpc_pool is a string */
            const char* rpc_pool_name
                = json_object_get_string(user_args->jrpc_pool);
            CONFIG_ARRAY_MUST_HAVE_ITEM_NAMED(jpools, rpc_pool_name,
                                              "argobots.pools", ignore);
        }
    }

    /* validate the list of xstreams */

    json_object_t* jxstreams = json_object_object_get(a, "xstreams");
    if (jxstreams) {
        size_t num_es = json_object_array_length(jxstreams);
        for (unsigned i = 0; i < num_es; ++i) {
            json_object_t* jxstream = json_object_array_get_idx(jxstreams, i);
            if (!margo_abt_xstream_validate_json(jxstream, i, jpools)) {
                HANDLE_CONFIG_ERROR;
            }
        }
        CONFIG_NAMES_MUST_BE_UNIQUE(jxstreams, "argobots.xstreams");
    }

finish:
    if (jpools) json_object_put(jpools);
    return result;

#undef HANDLE_CONFIG_ERROR
}

static inline bool
margo_abt_init_from_json(const json_object_t*         jabt,
                         const margo_abt_user_args_t* user_args,
                         margo_abt_t*                 a)
{
    int            ret;
    bool           result         = true;
    bool           first_abt_init = false;
    json_object_t* jpools         = json_object_object_get(jabt, "pools");
    json_object_t* jxstreams      = json_object_object_get(jabt, "xstreams");

    ABT_pool primary_pool      = ABT_POOL_NULL;
    ABT_pool progress_pool     = ABT_POOL_NULL;
    int      primary_pool_idx  = -1;
    int      progress_pool_idx = -1;
    int      rpc_pool_idx      = -1;

    if (jpools)
        json_object_get(jpools);
    else
        jpools = json_object_new_array_ext(0);

    if (jxstreams)
        json_object_get(jxstreams);
    else
        jxstreams = json_object_new_array_ext(0);

    bool has_external_progress_pool
        = (user_args->progress_pool != ABT_POOL_NULL)
       && (user_args->progress_pool != NULL);
    bool has_external_rpc_pool = (user_args->rpc_pool != ABT_POOL_NULL)
                              && (user_args->rpc_pool != NULL);

    /* handle ABT initialization */
    if (ABT_initialized() == ABT_ERR_UNINITIALIZED) {
        if (!getenv("ABT_THREAD_STACKSIZE")) {
            int abt_thread_stacksize = json_object_object_get_uint64_or(
                jabt, "abt_thread_stacksize",
                MARGO_DEFAULT_ABT_THREAD_STACKSIZE);
            char abt_thread_stacksize_str[128];
            sprintf(abt_thread_stacksize_str, "%d", abt_thread_stacksize);
            setenv("ABT_THREAD_STACKSIZE", abt_thread_stacksize_str, 1);
        }
        if (!getenv("ABT_MEM_MAX_NUM_STACKS")) {
            int abt_mem_max_num_stacks = json_object_object_get_uint64_or(
                jabt, "abt_mem_max_num_stacks",
                MARGO_DEFAULT_ABT_MEM_MAX_NUM_STACKS);
            char abt_mem_max_num_stacks_str[128];
            sprintf(abt_mem_max_num_stacks_str, "%d", abt_mem_max_num_stacks);
            setenv("ABT_MEM_MAX_NUM_STACKS", abt_mem_max_num_stacks_str, 1);
        }
        ret = ABT_init(0, NULL);
        if (ret != ABT_SUCCESS) {
            margo_error(
                0, "Could not initialize Argobots (ABT_init returned %d)", ret);
            result = false;
            goto error;
        }
        g_margo_abt_init = 1;
        g_margo_num_instances++;
        first_abt_init = true;
    }

    /* Turn on profiling capability if a) it has not been done already (this
     * is global to Argobots) and b) the argobots tool interface is enabled.
     */
    if (!g_margo_abt_prof_init) {
        ABT_bool tool_enabled;
        ABT_info_query_config(ABT_INFO_QUERY_KIND_ENABLED_TOOL, &tool_enabled);
        if (tool_enabled == ABT_TRUE) {
            ABTX_prof_init(&g_margo_abt_prof_context);
            g_margo_abt_prof_init = 1;
        }
    }

    /* build pools that are specified in the JSON */

    /* note: we allocate 3 more spaces than needed because we
     * may have to add a __primary__ pool, and/or a __progress__
     * pool, and/or an __rpc__ pool.
     */
    a->num_pools = json_object_array_length(jpools);
    a->pools     = calloc(a->num_pools + 3, sizeof(*(a->pools)));
    for (unsigned i = 0; i < a->num_pools; ++i) {
        json_object_t* jpool = json_object_array_get_idx(jpools, i);
        if (!margo_abt_pool_init_from_json(jpool, i, a->pools + i)) {
            result = false;
            goto error;
        }
    }

    /* compute how many extra ES we will add:
     * - 1 __primary__ may be added unless defined (conservatively allocate for
     * it)
     * - 1 progress ES if "use_progress_thread" is used
     * - N rpc ES if "rpc_thread_count" is used
     */
    int num_extra_es = 1; /* primary */
    if (!user_args->jprogress_pool && !has_external_progress_pool
        && user_args->juse_progress_thread) {
        if (json_object_get_boolean(user_args->juse_progress_thread)) {
            num_extra_es += 1; /* progress ES */
        }
    }
    if (!user_args->jrpc_pool && !has_external_rpc_pool
        && user_args->jrpc_thread_count) {
        int rpc_thread_count
            = json_object_get_uint64(user_args->jrpc_thread_count);
        num_extra_es += rpc_thread_count > 0 ? rpc_thread_count : 0;
    }

    /* build xstreams that are specified in the JSON */
    a->num_xstreams = json_object_array_length(jxstreams);
    a->xstreams
        = calloc(a->num_xstreams + num_extra_es, sizeof(*(a->xstreams)));
    for (unsigned i = 0; i < a->num_xstreams; ++i) {
        json_object_t* jxstream = json_object_array_get_idx(jxstreams, i);
        if (!margo_abt_xstream_init_from_json(jxstream, i, a->xstreams + i,
                                              a->pools, a->num_pools)) {
            result = false;
            goto error;
        }
        if (strcmp("__primary__", a->xstreams[i].name) == 0) {
            primary_pool_idx = a->xstreams[i].sched.pools[0];
            primary_pool     = a->pools[primary_pool_idx].pool;
        }
    }

    /* initialize __primary__ ES if it's not defined in the JSON */
    if (primary_pool_idx == -1) { /* no __primary__ ES defined */
        if (first_abt_init) {
            /* add a __primary__ pool */
            json_object_t* jprimary_pool = json_object_new_object();
            json_object_object_add(jprimary_pool, "name",
                                   json_object_new_string("__primary__"));
            json_object_object_add(jprimary_pool, "access",
                                   json_object_new_string("mpmc"));
            primary_pool_idx = a->num_pools;
            result           = margo_abt_pool_init_from_json(
                jprimary_pool, primary_pool_idx, a->pools + primary_pool_idx);
            json_object_put(jprimary_pool);
            if (!result) goto error;
            primary_pool = a->pools[primary_pool_idx].pool;
            a->pools[primary_pool_idx].margo_free_flag
                = false; /* can't free the pool associated with primary ES */
            a->num_pools += 1;
            /* add a __primary__ ES */
            json_object_t* jprimary_xstream = json_object_new_object();
            json_object_object_add(jprimary_xstream, "name",
                                   json_object_new_string("__primary__"));
            json_object_t* jprimary_sched = json_object_new_object();
            json_object_object_add(jprimary_xstream, "scheduler",
                                   jprimary_sched);
            json_object_t* jprimary_xstream_pools
                = json_object_new_array_ext(1);
            json_object_object_add(jprimary_sched, "pools",
                                   jprimary_xstream_pools);
            json_object_array_add(jprimary_xstream_pools,
                                  json_object_new_int(primary_pool_idx));
            result = margo_abt_xstream_init_from_json(
                jprimary_xstream, a->num_xstreams,
                a->xstreams + a->num_xstreams, a->pools, a->num_pools);
            json_object_put(jprimary_xstream);
            if (!result) goto error;
            a->num_xstreams += 1;
        } else { /* ABT was initialized before margo */
            ABT_xstream self_es;
            ABT_xstream_self(&self_es);
            ABT_xstream_get_main_pools(self_es, 1, &primary_pool);
            primary_pool_idx = a->num_pools;
            /* add the ES' pool as external */
            result = margo_abt_pool_init_external("__primary__", primary_pool,
                                                  a->pools + a->num_pools);
            if (!result) goto error;
            a->num_pools += 1;
            /* add an external __primary__ ES */
            result = margo_abt_xstream_init_external(
                "__primary__", self_es, a->pools, a->num_pools,
                a->xstreams + a->num_xstreams);
            if (!result) goto error;
            a->num_xstreams += 1;
        }
    }

    /* handle progress pool */

    if (primary_pool != ABT_POOL_NULL
        && user_args->progress_pool == primary_pool) {
        /* external progress_pool specified and corresponds to the primary pool
         */
        progress_pool_idx = primary_pool_idx;

    } else if (user_args->progress_pool != ABT_POOL_NULL
               && user_args->progress_pool != NULL) {
        /* external progress pool specified, and it's not the primary, so add it
         * as external */
        // TODO check if a __progress__ pool is already defined
        result = margo_abt_pool_init_external(
            "__progress__", user_args->progress_pool, a->pools + a->num_pools);
        if (!result) goto error;
        progress_pool_idx = (int)a->num_pools;
        a->num_pools += 1;

    } else if (user_args->jprogress_pool) {
        /* progress_pool specified in JSON, find it by index or by name */
        if (json_object_is_type(user_args->jprogress_pool, json_type_int)) {
            progress_pool_idx = json_object_get_int(user_args->jprogress_pool);
        } else { /* it's a string */
            progress_pool_idx = margo_abt_find_pool_by_name(
                a, json_object_get_string(user_args->jprogress_pool));
        }

    } else if (user_args->juse_progress_thread
               && json_object_get_boolean(user_args->juse_progress_thread)) {
        /* use_progress_thread specified and true, add a __progress__ ES with a
         * __progress__ pool */
        // TODO check if a __progress__ pool is already defined
        json_object_t* jprogress_pool = json_object_new_object();
        json_object_object_add(jprogress_pool, "name",
                               json_object_new_string("__progress__"));
        json_object_object_add(jprogress_pool, "access",
                               json_object_new_string("mpmc"));
        progress_pool_idx = a->num_pools;
        result            = margo_abt_pool_init_from_json(
            jprogress_pool, progress_pool_idx, a->pools + progress_pool_idx);
        json_object_put(jprogress_pool);
        if (!result) goto error;
        a->num_pools += 1;
        /* add a __proress__ ES */
        json_object_t* jprogress_xstream = json_object_new_object();
        json_object_object_add(jprogress_xstream, "name",
                               json_object_new_string("__progress__"));
        json_object_t* jprogress_sched = json_object_new_object();
        json_object_object_add(jprogress_xstream, "scheduler", jprogress_sched);
        json_object_t* jprogress_xstream_pools = json_object_new_array_ext(1);
        json_object_object_add(jprogress_sched, "pools",
                               jprogress_xstream_pools);
        json_object_array_add(jprogress_xstream_pools,
                              json_object_new_int(progress_pool_idx));
        result = margo_abt_xstream_init_from_json(
            jprogress_xstream, a->num_xstreams, a->xstreams + a->num_xstreams,
            a->pools, a->num_pools);
        json_object_put(jprogress_xstream);
        if (!result) goto error;
        a->num_xstreams += 1;

    } else {
        /* user_progress_thread is false or not defined, fall back to using
         * primary pool */
        progress_pool_idx = primary_pool_idx;
    }

    /* handle rpc pool */

    if (primary_pool != ABT_POOL_NULL && user_args->rpc_pool == primary_pool) {
        /* external rpc_pool specified and corresponds to the primary pool */
        rpc_pool_idx = primary_pool_idx;

    } else if (progress_pool != ABT_POOL_NULL
               && user_args->rpc_pool == progress_pool) {
        /* external rpc_pool specified and corresponds to the primary pool */
        rpc_pool_idx = progress_pool_idx;

    } else if (user_args->rpc_pool != ABT_POOL_NULL
               && user_args->rpc_pool != NULL) {
        /* external RPC pool specified, add it as external */
        // TODO check if the __rpc__ pool is already defined
        result = margo_abt_pool_init_external("__rpc__", user_args->rpc_pool,
                                              a->pools + a->num_pools);
        if (!result) goto error;
        rpc_pool_idx = (int)a->num_pools;
        a->num_pools += 1;

    } else if (user_args->jrpc_pool) {
        /* RPC pool specified in JSON, find it by index or by name */
        if (json_object_is_type(user_args->jrpc_pool, json_type_int)) {
            rpc_pool_idx = json_object_get_int(user_args->jrpc_pool);
        } else { /* it's a string */
            rpc_pool_idx = margo_abt_find_pool_by_name(
                a, json_object_get_string(user_args->jrpc_pool));
        }

    } else if (user_args->jrpc_thread_count
               && json_object_get_int(user_args->jrpc_thread_count) < 0) {
        /* rpc_thread_count specified and < 0, RPC pool is progress pool */
        rpc_pool_idx = progress_pool_idx;

    } else if (user_args->jrpc_thread_count
               && json_object_get_int(user_args->jrpc_thread_count) > 0) {
        /* rpc_thread_count specified and > 0, and RPC pool and some RPC ES
         * should be created */
        // TODO check is an RPC pool is already defined
        json_object_t* jrpc_pool = json_object_new_object();
        json_object_object_add(jrpc_pool, "name",
                               json_object_new_string("__rpc__"));
        json_object_object_add(jrpc_pool, "access",
                               json_object_new_string("mpmc"));
        rpc_pool_idx = a->num_pools;
        result       = margo_abt_pool_init_from_json(jrpc_pool, rpc_pool_idx,
                                               a->pools + rpc_pool_idx);
        json_object_put(jrpc_pool);
        if (!result) goto error;
        a->num_pools += 1;
        /* add a __rpc_X__ ESs */
        int num_rpc_es = json_object_get_int(user_args->jrpc_thread_count);
        for (int i = 0; i < num_rpc_es; i++) {
            // TODO check if __rpc_X_ name is already in used
            json_object_t* jrpc_xstream = json_object_new_object();
            char           es_name[64];
            sprintf(es_name, "__rpc_%d__", i);
            json_object_object_add(jrpc_xstream, "name",
                                   json_object_new_string(es_name));
            json_object_t* jrpc_sched = json_object_new_object();
            json_object_object_add(jrpc_xstream, "scheduler", jrpc_sched);
            json_object_t* jrpc_xstream_pools = json_object_new_array_ext(1);
            json_object_object_add(jrpc_sched, "pools", jrpc_xstream_pools);
            json_object_array_add(jrpc_xstream_pools,
                                  json_object_new_int(rpc_pool_idx));
            result = margo_abt_xstream_init_from_json(
                jrpc_xstream, a->num_xstreams, a->xstreams + a->num_xstreams,
                a->pools, a->num_pools);
            json_object_put(jrpc_xstream);
            if (!result) goto error;
            a->num_xstreams += 1;
        }

    } else {
        /* rpc_thread_count not specified or == 0, RPC pool is primary pool */
        rpc_pool_idx = primary_pool_idx;
    }

    /* set index for progress pool and rpc pool */
    a->progress_pool_idx = progress_pool_idx;
    a->rpc_pool_idx      = rpc_pool_idx;

finish:
    json_object_put(jpools);
    json_object_put(jxstreams);
    return result;

error:
    margo_abt_destroy(a);
    goto finish;
}

static inline json_object_t* margo_abt_to_json(const margo_abt_t* a,
                                               int                options)
{
    json_object_t* json      = json_object_new_object();
    json_object_t* jpools    = json_object_new_array_ext(a->num_pools);
    json_object_t* jxstreams = json_object_new_array_ext(a->num_xstreams);
    int flags = JSON_C_OBJECT_ADD_KEY_IS_NEW | JSON_C_OBJECT_ADD_CONSTANT_KEY;
    json_object_object_add_ex(json, "pools", jpools, flags);
    json_object_object_add_ex(json, "xstreams", jxstreams, flags);
    for (unsigned i = 0; i < a->num_xstreams; ++i) {
        if ((options & MARGO_CONFIG_HIDE_EXTERNAL)
            && (strcmp(a->xstreams[i].sched.type, "external") == 0))
            continue; // skip external xstreams if requested
        json_object_t* jxstream = margo_abt_xstream_to_json(
            a->xstreams + i, options, a->pools, a->num_pools);
        json_object_array_add(jxstreams, jxstream);
    }
    for (unsigned i = 0; i < a->num_pools; ++i) {
        if ((options & MARGO_CONFIG_HIDE_EXTERNAL)
            && (strcmp(a->pools[i].kind, "external") == 0))
            continue; // skip external pools if requested
        json_object_t* jpool = margo_abt_pool_to_json(a->pools + i);
        json_object_array_add(jpools, jpool);
    }
    char* abt_mem_max_num_stacks = getenv("ABT_MEM_MAX_NUM_STACKS");
    if (abt_mem_max_num_stacks) {
        json_object_object_add_ex(
            json, "abt_mem_max_num_stacks",
            json_object_new_int64(atol(abt_mem_max_num_stacks)), flags);
    }
    char* abt_thread_stacksize = getenv("ABT_THREAD_STACKSIZE");
    if (abt_thread_stacksize) {
        json_object_object_add_ex(
            json, "abt_thread_stacksize",
            json_object_new_int64(atol(abt_thread_stacksize)), flags);
    }
    return json;
}

static inline void margo_abt_destroy(margo_abt_t* a)
{
    for (unsigned i = 0; i < a->num_xstreams; ++i) {
        margo_abt_xstream_destroy(a->xstreams + i);
    }
    free(a->xstreams);
    for (unsigned i = 0; i < a->num_pools; ++i) {
        margo_abt_pool_destroy(a->pools + i);
    }
    free(a->pools);
    memset(a, 0, sizeof(*a));
    if (--g_margo_num_instances == 0 && g_margo_abt_init) {
        /* shut down global abt profiling if needed */
        if (g_margo_abt_prof_init) {
            if (g_margo_abt_prof_started) {
                ABTX_prof_stop(g_margo_abt_prof_context);
                g_margo_abt_prof_started = 0;
            }
            ABTX_prof_finalize(g_margo_abt_prof_context);
            g_margo_abt_prof_init = 0;
        }
        ABT_finalize();
        g_margo_abt_init = false;
    }
}

static inline int margo_abt_find_pool_by_name(const margo_abt_t* abt,
                                              const char*        name)
{
    if (abt == NULL || name == NULL) return -1;
    for (uint32_t i = 0; i < abt->num_pools; ++i) {
        if (abt->pools[i].name == NULL) continue;
        if (strcmp(abt->pools[i].name, name) == 0) return i;
    }
    return -1;
}

static inline int margo_abt_find_pool_by_handle(const margo_abt_t* abt,
                                                ABT_pool           pool)
{
    if (abt == NULL || pool == ABT_POOL_NULL) return -1;
    for (uint32_t i = 0; i < abt->num_pools; ++i) {
        if (abt->pools[i].pool == pool) return i;
    }
    return -1;
}

static inline int margo_abt_find_xstream_by_name(const margo_abt_t* abt,
                                                 const char*        name)
{
    if (abt == NULL || name == NULL) return -1;
    for (uint32_t i = 0; i < abt->num_xstreams; ++i) {
        if (abt->xstreams[i].name == NULL) continue;
        if (strcmp(abt->xstreams[i].name, name) == 0) return i;
    }
    return -1;
}

static inline int margo_abt_find_xstream_by_handle(const margo_abt_t* abt,
                                                   ABT_xstream        xstream)
{
    if (abt == NULL || xstream == ABT_XSTREAM_NULL) return -1;
    for (uint32_t i = 0; i < abt->num_xstreams; ++i) {
        if (abt->xstreams[i].xstream == xstream) return i;
    }
    return -1;
}
#endif
