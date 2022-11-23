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
#include "margo-prio-pool.h"
#include "margo-logging.h"
#include "margo-macros.h"
#include "margo-abt-macros.h"

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
    struct margo_pool_info info;
    char*                  kind;
    optional_char*         access; /* Unknown for custom user pools */
    uint32_t num_rpc_ids;          /* Number of RPC ids that use this pool */
    bool     margo_free_flag; /* flag if Margo is responsible for freeing */
} margo_abt_pool_t;

static inline bool
margo_abt_pool_validate_json(const struct json_object* pool_json);
static inline bool margo_abt_pool_init_from_json(const struct json_object*,
                                                 uint32_t,
                                                 margo_abt_pool_t*);
static inline struct json_object*
                   margo_abt_pool_to_json(const margo_abt_pool_t*);
static inline void margo_abt_pool_destroy(margo_abt_pool_t*);

/* Struct to track scheduler information in a margo_abt_xstream. */
typedef struct margo_abt_sched {
    ABT_sched sched;
    char*     type;
    uint32_t* pools;
    size_t    num_pools;
} margo_abt_sched_t;

static inline bool margo_abt_sched_validate_json(const struct json_object*,
                                                 const struct json_object*);
static inline bool margo_abt_sched_init_from_json(const struct json_object*,
                                                  margo_abt_sched_t*,
                                                  const margo_abt_pool_t*,
                                                  unsigned);
static inline struct json_object*
                   margo_abt_sched_to_json(const margo_abt_sched_t*);
static inline void margo_abt_sched_destroy(margo_abt_sched_t*);

/* Struct to track ES created by margo along with a flag indicating if
 * margo is responsible for explicitly free'ing the ES or not.
 */
typedef struct margo_abt_xstream {
    struct margo_xstream_info info;
    struct margo_abt_sched    sched;
    bool margo_free_flag; /* flag if Margo is responsible for freeing */
} margo_abt_xstream_t;

static inline bool margo_abt_xstream_validate_json(const struct json_object*,
                                                   const struct json_object*);
static inline bool margo_abt_xstream_init_from_json(const struct json_object*,
                                                    uint32_t,
                                                    margo_abt_xstream_t*,
                                                    const margo_abt_pool_t*,
                                                    unsigned);
static inline struct json_object*
                   margo_abt_xstream_to_json(const margo_abt_xstream_t*);
static inline void margo_abt_xstream_destroy(margo_abt_xstream_t*);

/* Argobots environment */
typedef struct margo_abt {
    struct margo_abt_pool*    pools;
    struct margo_abt_xstream* xstreams;
    unsigned                  num_pools;
    unsigned                  num_xstreams;
} margo_abt_t;

static inline bool margo_abt_validate_json(const struct json_object*);
static inline bool margo_abt_init_from_json(const struct json_object*,
                                            margo_abt_t*);
static inline struct json_object* margo_abt_to_json(const margo_abt_t*);
static inline void                margo_abt_destroy(margo_abt_t*);
static inline margo_abt_pool_t* margo_abt_find_pool_by_name(const margo_abt_t*,
                                                            const char*);
static inline margo_abt_pool_t*
margo_abt_find_pool_by_handle(const margo_abt_t*, ABT_pool);
static inline margo_abt_xstream_t*
margo_abt_find_xstream_by_name(const margo_abt_t*, const char*);
static inline margo_abt_xstream_t*
margo_abt_find_xstream_by_handle(const margo_abt_t*, ABT_xstream);

/* ------------------ Implementations ---------------------- */

/* ------ margo_abt_pool_* --------- */

static inline bool margo_abt_pool_validate_json(const struct json_object* json)
{
    if (!json || !json_object_is_type(json, json_type_object)) return false;

    ASSERT_CONFIG_HAS_OPTIONAL(json, access, string,
                               pool); /* default: "mpmcs" for predef pools */
    ASSERT_CONFIG_HAS_OPTIONAL(json, kind, string,
                               pool); /* default: "fifo_wait" */
    ASSERT_CONFIG_HAS_OPTIONAL(json, name, string,
                               pool); /* default: generated */

    if (json_object_object_get(json, "name")) CONFIG_NAME_IS_VALID(json);

    return true;
}

static inline bool margo_abt_pool_init_from_json(const struct json_object* json,
                                                 uint32_t          index,
                                                 margo_abt_pool_t* p)
{
    ABT_pool_access access = ABT_POOL_ACCESS_MPMC;
    ABT_pool_kind   kind   = (ABT_pool_kind)(-1);

    p->margo_free_flag = ABT_TRUE;
    p->info.index      = index;

    struct json_object* jname = json_object_object_get(json, "name");
    if (jname)
        p->info.name = strdup(json_object_get_string(jname));
    else {
        int name_size = snprintf(NULL, 0, "__pool_%d__", index);
        p->info.name  = calloc(1, name_size + 1);
        snprintf((char*)p->info.name, name_size + 1, "__pool_%d__", index);
    }

    struct json_object* jkind = json_object_object_get(json, "kind");
    if (jkind)
        p->kind = strdup(json_object_get_string(jkind));
    else
        p->kind = strdup("fifo_wait");

    if (strcmp(p->kind, "fifo_wait") == 0)
        kind = ABT_POOL_FIFO_WAIT;
    else if (strcmp(p->kind, "fifo") == 0)
        kind = ABT_POOL_FIFO;
#if ABT_NUMVERSION >= 20000000
    else if (strcmp(p->kind, "randws") == 0)
        kind = ABT_POOL_RANDWS;
#endif

    struct json_object* jaccess = json_object_object_get(json, "access");
    if (jaccess)
        p->access = strdup(json_object_get_string(jaccess));
    else if (kind != (ABT_pool_kind)(-1))
        p->access = strdup("mpmc");

    if (p->access) {
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
        ret = ABT_pool_create_basic(kind, access, ABT_TRUE, &p->info.pool);
    else if (strcmp(p->kind, "prio_wait") == 0) {
        ABT_pool_def prio_pool_def;
        margo_create_prio_pool_def(&prio_pool_def);
        ret = ABT_pool_create(&prio_pool_def, ABT_POOL_CONFIG_NULL,
                              &p->info.pool);
    } else {
        // custom pool definition, not supported for now
        margo_error(NULL, "Invalid pool kind \"%s\"", p->kind);
        ret = ABT_ERR_INV_POOL_KIND;
    }

    if (ret != ABT_SUCCESS) {
        margo_abt_pool_destroy(p);
        return false;
    }
    return true;
}

static inline struct json_object*
margo_abt_pool_to_json(const margo_abt_pool_t* p)
{
    struct json_object* json = json_object_new_object();
    int flags = JSON_C_OBJECT_ADD_KEY_IS_NEW | JSON_C_OBJECT_ADD_CONSTANT_KEY;
    json_object_object_add_ex(json, "kind", json_object_new_string(p->kind),
                              flags);
    json_object_object_add_ex(json, "name",
                              json_object_new_string(p->info.name), flags);
    if (p->access)
        json_object_object_add_ex(json, "access",
                                  json_object_new_string(p->access), flags);
    return json;
}

static inline void margo_abt_pool_destroy(margo_abt_pool_t* p)
{
    free(p->kind);
    free(p->access);
    free((char*)p->info.name);
    if (p->margo_free_flag && p->info.pool != ABT_POOL_NULL) {
        ABT_pool_free(&(p->info.pool));
    }
    memset(p, 0, sizeof(*p));
}

/* ------ margo_abt_sched_* --------- */

static inline bool
margo_abt_sched_validate_json(const struct json_object* s,
                              const struct json_object* available_pool_array)
{
    if (!s || !json_object_is_type(s, json_type_object)) return false;

    ASSERT_CONFIG_HAS_OPTIONAL(s, type, string, scheduler);
    ASSERT_CONFIG_HAS_OPTIONAL(s, pools, array, scheduler);

    struct json_object* sched_pool_array = json_object_object_get(s, "pools");
    size_t sched_pool_array_len = json_object_array_length(sched_pool_array);
    size_t num_available_pools = json_object_array_length(available_pool_array);

    for (unsigned i = 0; i < sched_pool_array_len; i++) {
        struct json_object* pool_ref
            = json_object_array_get_idx(sched_pool_array, i);
        /* pool_ref is an integer */
        if (json_object_is_type(pool_ref, json_type_int64)) {
            int index = json_object_get_int(pool_ref);
            if (index < 0 || index >= num_available_pools) {
                margo_error(
                    0, "Invalid pool index (%d) in scheduler configuration",
                    index);
                return false;
            }
            /* pool_ref is a string */
        } else if (json_object_is_type(pool_ref, json_type_string)) {
            const char* pool_name = json_object_get_string(pool_ref);
            unsigned    j         = 0;
            for (; j < num_available_pools; j++) {
                struct json_object* p
                    = json_object_array_get_idx(available_pool_array, j);
                struct json_object* p_name = json_object_object_get(p, "name");
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
}

static inline bool
margo_abt_sched_init_from_json(const struct json_object* json,
                               margo_abt_sched_t*        s,
                               const margo_abt_pool_t*   pools,
                               unsigned                  total_num_pools)
{
    struct json_object* jtype = json_object_object_get(json, "type");
    if (jtype)
        s->type = strdup(json_object_get_string(jtype));
    else
        s->type = strdup("default");

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

    struct json_object* jpool_array = json_object_object_get(json, "pools");
    if (jpool_array)
        json_object_get(jpool_array);
    else
        jpool_array = json_object_new_array_ext(0);

    size_t num_pools    = json_object_array_length(jpool_array);
    s->pools            = malloc(num_pools * sizeof(*(s->pools)));
    s->num_pools        = num_pools;
    ABT_pool* abt_pools = malloc(num_pools * sizeof(*abt_pools));
    for (unsigned i = 0; i < num_pools; i++) {
        struct json_object* jpool = json_object_array_get_idx(jpool_array, i);
        uint32_t            pool_idx = 0;
        if (json_object_is_type(jpool, json_type_int64)) {
            pool_idx = (uint32_t)json_object_get_int(jpool);
        } else {
            const char* pool_name = json_object_get_string(jpool);
            for (; pool_idx < total_num_pools; ++pool_idx) {
                if (!pools[pool_idx].info.name) continue;
                if (strcmp(pools[pool_idx].info.name, pool_name) == 0) break;
            }
        }
        s->pools[i]  = pool_idx;
        abt_pools[i] = pools[pool_idx].info.pool;
    }
    json_object_put(jpool_array);

    int ret = ABT_sched_create_basic(sched_predef, num_pools, abt_pools,
                                     ABT_SCHED_CONFIG_NULL, &s->sched);

    if (ret != ABT_SUCCESS) {
        margo_abt_sched_destroy(s);
        return false;
    }

    return true;
}

static inline struct json_object*
margo_abt_sched_to_json(const margo_abt_sched_t* s)
{
    struct json_object* json = json_object_new_object();
    int flags = JSON_C_OBJECT_ADD_KEY_IS_NEW | JSON_C_OBJECT_ADD_CONSTANT_KEY;
    json_object_object_add_ex(json, "type", json_object_new_string(s->type),
                              flags);
    struct json_object* jpools = json_object_new_array_ext(s->num_pools);
    for (uint32_t i = 0; i < s->num_pools; i++) {
        json_object_array_add(jpools, json_object_new_uint64(s->pools[i]));
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
margo_abt_xstream_validate_json(const struct json_object* x,
                                const struct json_object* pool_array)
{
    if (!x || !json_object_is_type(x, json_type_object)) return false;

    ASSERT_CONFIG_HAS_OPTIONAL(x, scheduler, object, xstream);
    ASSERT_CONFIG_HAS_OPTIONAL(x, name, string, xstream);
    ASSERT_CONFIG_HAS_OPTIONAL(x, cpubind, int, xstream);
    ASSERT_CONFIG_HAS_OPTIONAL(x, affiniry, array, xstream);

    struct json_object* jsched = json_object_object_get(x, "scheduler");
    if (jsched && !margo_abt_sched_validate_json(jsched, pool_array))
        return false;

    struct json_object* jaffinity = json_object_object_get(x, "affinity");
    for (unsigned i = 0; i < json_object_array_length(jaffinity); i++) {
        struct json_object* value = json_object_array_get_idx(jaffinity, i);
        if (!json_object_is_type(value, json_type_int)) {
            margo_error(
                0, "Invalid type found in affinity array (expected integer)");
            return false;
        }
    }

    struct json_object* jname = json_object_object_get(x, "name");
    if (jname) {
        CONFIG_NAME_IS_VALID(x);
        const char* name = json_object_get_string(jname);
        if (strcmp(name, "__primary__") == 0
            && ABT_initialized() == ABT_SUCCESS) {
            struct json_object* disable_safety_warning
                = json_object_object_get(x, "disable_safety_warning");
            if (!(disable_safety_warning
                  && json_object_is_type(disable_safety_warning,
                                         json_type_boolean)
                  && json_object_get_boolean(disable_safety_warning))) {
                margo_warning(0,
                              "Configuration specified __primary__ xstream"
                              " definition but Argobots is already initialized."
                              " Margo will update the primary xstream according"
                              " to configuration, which may be unsafe."
                              " This message can be silenced by adding"
                              " \"disable_safety_warning\":true to the"
                              " primary xstream's configuration.");
            }

            if (!jsched) {
                margo_error(
                    0, "__primary__ xstream requires a scheduler definition");
                return false;
            }

            struct json_object* jpools
                = json_object_object_get(jsched, "pools");
            if (!jpools || json_object_array_length(jpools) == 0) {
                margo_error(0,
                            "__primary__ xstream requires scheduler to have at "
                            "least one pool");
                return false;
            }
        }
    }

    return true;
}

static inline bool
margo_abt_xstream_init_from_json(const struct json_object* json,
                                 uint32_t                  index,
                                 margo_abt_xstream_t*      x,
                                 const margo_abt_pool_t*   pools,
                                 unsigned                  num_pools)
{
    bool                result = false;
    struct json_object* jname  = json_object_object_get(json, "name");
    if (jname)
        x->info.name = strdup(json_object_get_string(jname));
    else {
        int name_len = snprintf(NULL, 0, "__xstream_%d__", index);
        x->info.name = calloc(1, name_len + 1);
        snprintf((char*)x->info.name, name_len + 1, "__xstream_%d__", index);
    }

    int                 cpubind  = -1;
    struct json_object* jcpubind = json_object_object_get(json, "cpubind");
    if (jcpubind) cpubind = json_object_get_int(jcpubind);

    int*                affinity     = NULL;
    int                 affinity_len = 0;
    struct json_object* jaffinity    = json_object_object_get(json, "affinity");
    if (jaffinity) {
        affinity_len = json_object_array_length(jaffinity);
        affinity     = malloc(sizeof(*affinity) * affinity_len);
        for (unsigned i = 0; i < affinity_len; i++) {
            affinity[i]
                = json_object_get_int(json_object_array_get_idx(jaffinity, i));
        }
    }

    struct json_object* jsched = json_object_object_get(json, "scheduler");
    if (jsched)
        json_object_get(jsched);
    else
        jsched = json_object_new_object();

    if (!margo_abt_sched_init_from_json(jsched, &(x->sched), pools, num_pools))
        goto finish;

    int ret = ABT_xstream_create(x->sched.sched, &(x->info.xstream));

    if (ret == ABT_SUCCESS) {
        result = true;
        if (affinity)
            ABT_xstream_set_affinity(x->info.xstream, affinity_len, affinity);
        if (cpubind >= 0) ABT_xstream_set_cpubind(x->info.xstream, cpubind);
    }

finish:
    json_object_put(jsched);
    free(affinity);
    return result;
}

static inline struct json_object*
margo_abt_xstream_to_json(const margo_abt_xstream_t* x)
{
    struct json_object* json   = json_object_new_object();
    struct json_object* jsched = margo_abt_sched_to_json(&(x->sched));
    int flags = JSON_C_OBJECT_ADD_KEY_IS_NEW | JSON_C_OBJECT_ADD_CONSTANT_KEY;
    json_object_object_add_ex(json, "scheduler", jsched, flags);

    int cpuid;
    int ret = ABT_xstream_get_cpubind(x->info.xstream, &cpuid);
    if (ret == ABT_SUCCESS) {
        json_object_object_add_ex(json, "cpubind", json_object_new_int(cpuid),
                                  flags);
    }

    int num_cpus;
    ret = ABT_xstream_get_affinity(x->info.xstream, 0, NULL, &num_cpus);
    if (ret == ABT_SUCCESS) {
        int* cpuids = malloc(num_cpus * sizeof(*cpuids));
        ret = ABT_xstream_get_affinity(x->info.xstream, num_cpus, cpuids,
                                       &num_cpus);
        if (ret == ABT_SUCCESS) {
            struct json_object* jcpuids = json_object_new_array_ext(num_cpus);
            for (int i = 0; i < num_cpus; i++) {
                json_object_array_add(jcpuids,
                                      json_object_new_int64(cpuids[i]));
            }
            json_object_object_add_ex(json, "affinity", jcpuids, flags);
        }
        free(cpuids);
    }
    return json;
}

static inline void margo_abt_xstream_destroy(margo_abt_xstream_t* x)
{
    if (x->margo_free_flag && x->info.xstream != ABT_XSTREAM_NULL) {
        ABT_xstream_join(x->info.xstream);
        ABT_xstream_free(&x->info.xstream);
    }
    margo_abt_sched_destroy(&(x->sched));
    free((char*)x->info.name);
    memset(x, 0, sizeof(*x));
}

/* ------ margo_abt_* --------- */

static inline bool margo_abt_validate_json(const struct json_object* a)
{
    if (!a || !json_object_is_type(a, json_type_object)) return false;

    bool result = true;
    ASSERT_CONFIG_HAS_OPTIONAL(a, pools, array, argobots);
    ASSERT_CONFIG_HAS_OPTIONAL(a, xstreams, array, argobots);
    ASSERT_CONFIG_HAS_OPTIONAL(a, abt_mem_max_num_stacks, int, argobots);
    ASSERT_CONFIG_HAS_OPTIONAL(a, abt_thread_stacksize, int, argobots);

    struct json_object* jpools = json_object_object_get(a, "pools");
    if (jpools) {
        json_object_get(jpools);
        size_t num_pools = json_object_array_length(jpools);
        for (unsigned i = 0; i < num_pools; ++i) {
            struct json_object* jpool = json_object_array_get_idx(jpools, i);
            if (!margo_abt_pool_validate_json(jpool)) {
                result = false;
                goto finish;
            }
        }
        CONFIG_NAMES_MUST_BE_UNIQUE(jpools, "argobots.pools");
    } else {
        jpools = json_object_new_array_ext(0);
    }

    struct json_object* jxstreams = json_object_object_get(a, "xstreams");
    if (jxstreams) {
        size_t num_es = json_object_array_length(jxstreams);
        for (unsigned i = 0; i < num_es; ++i) {
            if (!margo_abt_xstream_validate_json(
                    json_object_array_get_idx(jxstreams, i), jpools)) {
                result = false;
                goto finish;
            }
            struct json_object* jxstream_name
                = json_object_object_get(jxstreams, "name");
        }
        CONFIG_NAMES_MUST_BE_UNIQUE(jxstreams, "argobots.xstreams");
    }

finish:
    json_object_put(jpools);
    return result;
}

static inline bool margo_abt_init_from_json(const struct json_object* json,
                                            margo_abt_t*              a)
{
    bool                result    = true;
    struct json_object* jpools    = json_object_object_get(json, "pools");
    struct json_object* jxstreams = json_object_object_get(json, "xstreams");

    if (!jpools) goto init_xstreams;

    a->num_pools = json_object_array_length(jpools);
    a->pools     = calloc(a->num_pools, sizeof(*(a->pools)));
    for (unsigned i = 0; i < a->num_pools; ++i) {
        struct json_object* jpool = json_object_array_get_idx(jpools, i);
        if (!margo_abt_pool_init_from_json(jpool, i, a->pools + i)) {
            result = false;
            goto error;
        }
    }

init_xstreams:
    if (!jxstreams) goto finish;

    a->num_xstreams = json_object_array_length(jxstreams);
    a->xstreams     = calloc(a->num_xstreams, sizeof(*(a->xstreams)));
    for (unsigned i = 0; i < a->num_xstreams; ++i) {
        struct json_object* jxstream = json_object_array_get_idx(jxstreams, i);
        if (!margo_abt_xstream_init_from_json(jxstream, i, a->xstreams + i,
                                              a->pools, a->num_pools)) {
            result = false;
            goto error;
        }
    }

finish:
    return result;

error:
    goto finish;
}

static inline struct json_object* margo_abt_to_json(const margo_abt_t* a)
{
    struct json_object* json      = json_object_new_object();
    struct json_object* jpools    = json_object_new_array_ext(a->num_pools);
    struct json_object* jxstreams = json_object_new_array_ext(a->num_xstreams);
    int flags = JSON_C_OBJECT_ADD_KEY_IS_NEW | JSON_C_OBJECT_ADD_CONSTANT_KEY;
    json_object_object_add_ex(json, "pools", jpools, flags);
    json_object_object_add_ex(json, "xstreams", jxstreams, flags);
    for (unsigned i = 0; i < a->num_xstreams; ++i) {
        struct json_object* jxstream
            = margo_abt_xstream_to_json(a->xstreams + i);
        json_object_array_add(jxstreams, jxstream);
    }
    for (unsigned i = 0; i < a->num_pools; ++i) {
        struct json_object* jpool = margo_abt_pool_to_json(a->pools + i);
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
    for (unsigned i = 0; i < a->num_pools; ++i) {
        margo_abt_pool_destroy(a->pools + i);
    }
    memset(a, 0, sizeof(*a));
}

static inline margo_abt_pool_t*
margo_abt_find_pool_by_name(const margo_abt_t* abt, const char* name)
{
    if (abt == NULL || name == NULL) return NULL;
    for (uint32_t i = 0; i < abt->num_pools; ++i) {
        if (abt->pools[i].info.name == NULL) continue;
        if (strcmp(abt->pools[i].info.name, name) == 0) return &(abt->pools[i]);
    }
    return NULL;
}

static inline margo_abt_pool_t*
margo_abt_find_pool_by_handle(const margo_abt_t* abt, ABT_pool pool)
{
    if (abt == NULL || pool == ABT_POOL_NULL) return NULL;
    for (uint32_t i = 0; i < abt->num_pools; ++i) {
        if (abt->pools[i].info.pool == pool) return &(abt->pools[i]);
    }
    return NULL;
}

static inline bool margo_abt_add_external_pool(const margo_abt_t*, ABT_pool) {}

static inline margo_abt_xstream_t*
margo_abt_find_xstream_by_name(const margo_abt_t* abt, const char* name)
{
    if (abt == NULL || name == NULL) return NULL;
    for (uint32_t i = 0; i < abt->num_xstreams; ++i) {
        if (abt->xstreams[i].info.name == NULL) continue;
        if (strcmp(abt->xstreams[i].info.name, name) == 0)
            return &(abt->xstreams[i]);
    }
    return NULL;
}

static inline margo_abt_xstream_t*
margo_abt_find_xstream_by_handle(const margo_abt_t* abt, ABT_xstream xstream)
{
    if (abt == NULL || xstream == ABT_XSTREAM_NULL) return NULL;
    for (uint32_t i = 0; i < abt->num_xstreams; ++i) {
        if (abt->xstreams[i].info.xstream == xstream)
            return &(abt->xstreams[i]);
    }
    return NULL;
}

static inline bool margo_abt_add_external_xstream(const margo_abt_t*, ABT_pool)
{}
#endif
