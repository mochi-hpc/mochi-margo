/*
 * (C) 2022 The University of Chicago
 *
 * See COPYRIGHT in top-level directory.
 */
#ifndef __MARGO_CONFIG_INTERNAL_H
#define __MARGO_CONFIG_INTERNAL_H
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

#define MARGO_OWNS_HG_CLASS   0x1
#define MARGO_OWNS_HG_CONTEXT 0x2

/* Mercury environment */
typedef struct margo_hg {
    struct hg_init_info hg_init_info;
    hg_class_t*         hg_class;
    hg_context_t*       hg_context;
    hg_addr_t           self_addr;
    char*               self_addr_str;
    /* bitwise OR of MARGO_OWNS_HG_CLASS and MARGO_OWNS_HG_CONTEXT */
    uint8_t hg_ownership;
} margo_hg_t;

/* Structure to group user-provided arguments
 * or already initialized Mercury objects */
typedef struct margo_hg_user_args {
    const char*          protocol;
    bool                 listening;
    struct hg_init_info* hg_init_info;
    hg_class_t*          hg_class;
    hg_context_t*        hg_context;
} margo_hg_user_args_t;

static inline bool margo_hg_validate_json(const struct json_object*,
                                          const margo_hg_user_args_t*);
static inline bool margo_hg_init_from_json(const struct json_object*,
                                           const margo_hg_user_args_t*,
                                           margo_hg_t*);
static inline struct json_object* margo_hg_to_json(const margo_hg_t*);
static inline void                margo_hg_destroy(margo_hg_t*);

/* ------------------ Implementations ---------------------- */

#define ASSERT_CONFIG_HAS_REQUIRED(__config__, __key__, __type__, __ctx__) \
    do {                                                                   \
        struct json_object* __key__                                        \
            = json_object_object_get(__config__, #__key__);                \
        if (!__key__) {                                                    \
            margo_error(0, "\"" #__key__ "\" not found in " #__ctx__       \
                           " configuration");                              \
            return false;                                                  \
        }                                                                  \
        if (!json_object_is_type(__key__, json_type_##__type__)) {         \
            margo_error(0, "Invalid type for \"" #__key__ " in " #__ctx__  \
                           " configuration (expected " #__type__ ")");     \
        }                                                                  \
    } while (0)

#define ASSERT_CONFIG_HAS_OPTIONAL(__config__, __key__, __type__, __ctx__)    \
    do {                                                                      \
        struct json_object* __key__                                           \
            = json_object_object_get(__config__, #__key__);                   \
        if (__key__ && !json_object_is_type(__key__, json_type_##__type__)) { \
            margo_error(0, "Invalid type for \"" #__key__ " in " #__ctx__     \
                           " configuration (expected " #__type__ ")");        \
        }                                                                     \
    } while (0)

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

    if (json_object_object_get(x, "name")) CONFIG_NAME_IS_VALID(x);

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

/* ------ margo_hg_* --------- */

static inline bool margo_hg_validate_json(const struct json_object*   json,
                                          const margo_hg_user_args_t* user_args)
{
    if (!json || !json_object_is_type(json, json_type_object)) return false;

    // if hg_class or hg_init_info provided,
    // configuration will be ignored
    if (user_args->hg_class || user_args->hg_init_info) {
#define WARNING_CONFIG_HAS(__json__, __name__)                                \
    do {                                                                      \
        if (json_object_object_get(__json__, #__name__)) {                    \
            margo_warning(0,                                                  \
                          "\"" #__name__                                      \
                          "\" ignored in mercury configuration"               \
                          " because hg_class or hg_init_info were provided"); \
        }                                                                     \
    } while (0)

        WARNING_CONFIG_HAS(json, request_post_incr);
        WARNING_CONFIG_HAS(json, request_post_init);
        WARNING_CONFIG_HAS(json, auto_sm);
        WARNING_CONFIG_HAS(json, no_bulk_eager);
        WARNING_CONFIG_HAS(json, no_loopback);
        WARNING_CONFIG_HAS(json, stats);
        WARNING_CONFIG_HAS(json, na_no_block);
        WARNING_CONFIG_HAS(json, na_no_retry);
        WARNING_CONFIG_HAS(json, max_contexts);
        WARNING_CONFIG_HAS(json, ip_subnet);
        WARNING_CONFIG_HAS(json, auth_key);
        WARNING_CONFIG_HAS(json, na_max_expected_size);
        WARNING_CONFIG_HAS(json, na_max_unexpected_size);
        WARNING_CONFIG_HAS(json, sm_info_string);
        WARNING_CONFIG_HAS(json, na_request_mem_device);
        WARNING_CONFIG_HAS(json, checksum_level);
        WARNING_CONFIG_HAS(json, na_addr_format);
    }

    ASSERT_CONFIG_HAS_OPTIONAL(json, address, string, mercury);
    ASSERT_CONFIG_HAS_OPTIONAL(json, version, string, mercury);
    ASSERT_CONFIG_HAS_OPTIONAL(json, listening, boolean, mercury);
    ASSERT_CONFIG_HAS_OPTIONAL(json, request_post_incr, int, mercury);
    ASSERT_CONFIG_HAS_OPTIONAL(json, request_post_init, int, mercury);
    ASSERT_CONFIG_HAS_OPTIONAL(json, auto_sm, boolean, mercury);
    ASSERT_CONFIG_HAS_OPTIONAL(json, no_bulk_eager, boolean, mercury);
    ASSERT_CONFIG_HAS_OPTIONAL(json, no_loopback, boolean, mercury);
    ASSERT_CONFIG_HAS_OPTIONAL(json, stats, boolean, mercury);
    ASSERT_CONFIG_HAS_OPTIONAL(json, na_no_block, boolean, mercury);
    ASSERT_CONFIG_HAS_OPTIONAL(json, na_no_retry, boolean, mercury);
    ASSERT_CONFIG_HAS_OPTIONAL(json, max_contexts, int, mercury);
    ASSERT_CONFIG_HAS_OPTIONAL(json, ip_subnet, string, mercury);
    ASSERT_CONFIG_HAS_OPTIONAL(json, auth_key, string, mercury);
    ASSERT_CONFIG_HAS_OPTIONAL(json, input_eager_size, int, mercury);
    ASSERT_CONFIG_HAS_OPTIONAL(json, output_eager_size, int, mercury);
#if (HG_VERSION_MAJOR > 2)       \
    || (HG_VERSION_MAJOR == 2    \
        && (HG_VERSION_MINOR > 1 \
            || (HG_VERSION_MINOR == 0 && HG_VERSION_PATH > 0)))
    // na_max_unexpected_size and na_max_expected_size available from
    // version 2.0.1
    ASSERT_CONFIG_HAS_OPTIONAL(json, na_max_unexpected_size, int, mercury);
    ASSERT_CONFIG_HAS_OPTIONAL(json, na_max_expected_size, int, mercury);
#endif
#if (HG_VERSION_MAJOR > 2) || (HG_VERSION_MAJOR == 2 && HG_VERSION_MINOR > 0)
    // sm_info_string available from version 2.1.0
    ASSERT_CONFIG_HAS_OPTIONAL(json, sm_info_string, string, mercury);
#endif
#if (HG_VERSION_MAJOR > 2) || (HG_VERSION_MAJOR == 2 && HG_VERSION_MINOR > 1)
    // na_request_mem_device available from version 2.2.0
    ASSERT_CONFIG_HAS_OPTIONAL(json, na_request_mem_device, boolean, mercury);
    // checksum_level available from version 2.2.0
    ASSERT_CONFIG_HAS_OPTIONAL(json, checksum_level, string, mercury);
    struct json_object* checksum_level
        = json_object_object_get(json, "checksum_level");
    CONFIG_IS_IN_ENUM_STRING(checksum_level, "checksum_level", "none",
                             "rpc_headers", "rpc_payload");
    // na_addr_format available from version 2.2.0
    ASSERT_CONFIG_HAS_OPTIONAL(json, na_addr_format, string, mercury);
    struct json_object* na_addr_format
        = json_object_object_get(json, "na_addr_format");
    CONFIG_IS_IN_ENUM_STRING(na_addr_format, "na_addr_format", "unspec", "ipv4",
                             "ipv4", "ipv6", "native");
#endif
    return true;
}

static inline bool margo_hg_init_from_json(const struct json_object*   json,
                                           const margo_hg_user_args_t* user,
                                           margo_hg_t*                 hg)
{
    if (user->hg_init_info && !user->hg_class) {
        // initialize hg_init_info from user-provided hg_init_info
        if (!user->hg_class) {
            memcpy(&(hg->hg_init_info), user->hg_init_info,
                   sizeof(*user->hg_init_info));
#if (HG_VERSION_MAJOR > 2) || (HG_VERSION_MAJOR == 2 && HG_VERSION_MINOR > 0)
            if (hg->hg_init_info.sm_info_string)
                hg->hg_init_info.sm_info_string
                    = strdup(hg->hg_init_info.sm_info_string);
#endif
            if (hg->hg_init_info.na_init_info.ip_subnet)
                hg->hg_init_info.na_init_info.ip_subnet
                    = strdup(hg->hg_init_info.na_init_info.ip_subnet);
            if (hg->hg_init_info.na_init_info.auth_key)
                hg->hg_init_info.na_init_info.auth_key
                    = strdup(hg->hg_init_info.na_init_info.auth_key);
        }
    } else {
        // initialize hg_init_info from JSON configuration
        hg->hg_init_info.request_post_init
            = json_object_object_get_uint64_or(json, "request_post_init", 256);
        hg->hg_init_info.request_post_incr
            = json_object_object_get_uint64_or(json, "request_post_incr", 256);
        hg->hg_init_info.auto_sm
            = json_object_object_get_bool_or(json, "auto_sm", false);
        hg->hg_init_info.no_bulk_eager
            = json_object_object_get_bool_or(json, "no_bulk_eager", false);
        hg->hg_init_info.no_loopback
            = json_object_object_get_bool_or(json, "no_loopback", false);
        hg->hg_init_info.stats
            = json_object_object_get_bool_or(json, "stats", false);
        hg->hg_init_info.na_init_info.progress_mode = 0;
        if (json_object_object_get_bool_or(json, "na_no_block", false))
            hg->hg_init_info.na_init_info.progress_mode |= NA_NO_BLOCK;
        if (json_object_object_get_bool_or(json, "na_no_retry", false))
            hg->hg_init_info.na_init_info.progress_mode |= NA_NO_RETRY;
        hg->hg_init_info.na_init_info.max_contexts
            = json_object_object_get_uint64_or(json, "max_contexts", 0);
        struct json_object* ip_subnet
            = json_object_object_get(json, "ip_subnet");
        if (ip_subnet)
            hg->hg_init_info.na_init_info.ip_subnet
                = strdup(json_object_get_string(ip_subnet));
        struct json_object* auth_key = json_object_object_get(json, "auth_key");
        if (auth_key)
            hg->hg_init_info.na_init_info.auth_key
                = strdup(json_object_get_string(auth_key));
#if (HG_VERSION_MAJOR > 2)       \
    || (HG_VERSION_MAJOR == 2    \
        && (HG_VERSION_MINOR > 1 \
            || (HG_VERSION_MINOR == 0 && HG_VERSION_PATH > 0)))
        // na_max_unexpected_size and na_max_expected_size available from
        // version 2.0.1
        hg->hg_init_info.na_init_info.max_unexpected_size
            = json_object_object_get_uint64_or(json, "na_max_unexpected_size",
                                               0);
        hg->hg_init_info.na_init_info.max_expected_size
            = json_object_object_get_uint64_or(json, "na_max_expected_size", 0);
#endif
#if (HG_VERSION_MAJOR > 2) || (HG_VERSION_MAJOR == 2 && HG_VERSION_MINOR > 0)
        // sm_info_string available from version 2.1.0
        struct json_object* sm_info_string
            = json_object_object_get(json, "sm_info_string");
        if (sm_info_string)
            hg->hg_init_info.sm_info_string
                = strdup(json_object_get_string(sm_info_string));
#endif
#if (HG_VERSION_MAJOR > 2) || (HG_VERSION_MAJOR == 2 && HG_VERSION_MINOR > 1)
        // na_request_mem_device available from version 2.2.0
        hg->hg_init_info.na_init_info.request_mem_device
            = json_object_object_get_bool_or(json, "na_request_mem_device",
                                             false);
        // checksum_level available from version 2.2.0
        struct json_object* checksum_level
            = json_object_object_get(json, "checksum_level");
        if (checksum_level) {
            const char* checksum_level_str
                = json_object_get_string(checksum_level);
            if (strcmp(checksum_level_str, "rpc_headers") == 0)
                hg->hg_init_info.checksum_level = HG_CHECKSUM_RPC_HEADERS;
            else if (strcmp(checksum_level_str, "rpc_payload") == 0)
                hg->hg_init_info.checksum_level = HG_CHECKSUM_RPC_PAYLOAD;
            else
                hg->hg_init_info.checksum_level = HG_CHECKSUM_NONE;
        }
        // na_addr_format available from version 2.2.0
        struct json_object* na_addr_format
            = json_object_object_get(json, "na_addr_format");
        if (na_addr_format) {
            const char* na_addr_format_str
                = json_object_get_string(na_addr_format);
            if (strcmp(na_addr_format_str, "ipv4") == 0)
                hg->hg_init_info.na_init_info.addr_format = NA_ADDR_IPV4;
            else if (strcmp(na_addr_format_str, "ipv6") == 0)
                hg->hg_init_info.na_init_info.addr_format = NA_ADDR_IPV6;
            else if (strcmp(na_addr_format_str, "native") == 0)
                hg->hg_init_info.na_init_info.addr_format = NA_ADDR_NATIVE;
            else
                hg->hg_init_info.na_init_info.addr_format = NA_ADDR_UNSPEC;
        }
#endif
    }

    if (user->hg_class && !user->hg_context) {
        if (user->hg_init_info) {
            margo_warning(0,
                          "Both custom hg_class and hg_init_info provided, the "
                          "latter will be ignored");
        }
        hg->hg_class = user->hg_class;
    } else {
        hg->hg_class
            = HG_Init_opt(user->protocol, user->listening, &(hg->hg_init_info));
        if (!hg->hg_class) {
            margo_error(0, "Could not initialize hg_class with protocol %s",
                        user->protocol);
            goto error;
        }
        hg->hg_ownership = MARGO_OWNS_HG_CLASS;
    }

    if (user->hg_context) {
        hg->hg_context = user->hg_context;
        hg->hg_class   = HG_Context_get_class(hg->hg_context);
        if (!hg->hg_class) {
            margo_error(0,
                        "Could not get hg_class from user-provided hg_context");
            goto error;
        }
        if (user->hg_class && (user->hg_class != hg->hg_class)) {
            margo_warning(0,
                          "Both custom hg_context and hg_class provided, the "
                          "latter will be ignored");
        }
    } else {
        hg->hg_context = HG_Context_create(hg->hg_class);
        if (!hg->hg_context) {
            margo_error(0, "Could not initialize hg_context");
            goto error;
        }
        hg->hg_ownership |= MARGO_OWNS_HG_CONTEXT;
    }

    hg_return_t hret = HG_Addr_self(hg->hg_class, &(hg->self_addr));
    if (hret != HG_SUCCESS) {
        margo_error(0, "Could not resolve self address");
        goto error;
    } else {
        hg_size_t buf_size = 0;
        hret = HG_Addr_to_string(hg->hg_class, NULL, &buf_size, hg->self_addr);
        if (hret != HG_SUCCESS) {
            margo_warning(0, "Could not convert self address to string");
            hg->self_addr_str = NULL;
        } else {
            hg->self_addr_str = calloc(1, buf_size);
            HG_Addr_to_string(hg->hg_class, hg->self_addr_str, &buf_size,
                              hg->self_addr);
        }
    }

    return true;

error:
    margo_hg_destroy(hg);
    return false;
}

static inline struct json_object* margo_hg_to_json(const margo_hg_t* hg)
{
    struct json_object* json = json_object_new_object();

    int flags = JSON_C_OBJECT_ADD_KEY_IS_NEW | JSON_C_OBJECT_ADD_CONSTANT_KEY;
    // version
    char         hg_version_string[64];
    unsigned int hg_major = 0, hg_minor = 0, hg_patch = 0;
    HG_Version_get(&hg_major, &hg_minor, &hg_patch);
    snprintf(hg_version_string, 64, "%u.%u.%u", hg_major, hg_minor, hg_patch);
    json_object_object_add_ex(json, "version",
                              json_object_new_string(hg_version_string), flags);

    // address
    if (hg->self_addr_str)
        json_object_object_add_ex(
            json, "address", json_object_new_string(hg->self_addr_str), flags);

    // listening
    hg_bool_t listening = HG_Class_is_listening(hg->hg_class);
    json_object_object_add_ex(json, "listening",
                              json_object_new_boolean(listening), flags);

    // input_eager_size
    hg_size_t input_eager_size = HG_Class_get_input_eager_size(hg->hg_class);
    json_object_object_add_ex(json, "input_eager_size",
                              json_object_new_uint64(input_eager_size), flags);

    // output_eager_size
    hg_size_t output_eager_size = HG_Class_get_output_eager_size(hg->hg_class);
    json_object_object_add_ex(json, "output_eager_size",
                              json_object_new_uint64(output_eager_size), flags);

    /* if margo doesn't own the hg_class, then hg_init_info
     * does not correspond to the way the hg_class was actually
     * initialized (need to wait for Mercury to allow us to
     * retrieve the hg_init_info from an hg_class). */
    if (!(hg->hg_ownership & MARGO_OWNS_HG_CLASS)) goto finish;

    // request_post_init
    json_object_object_add_ex(
        json, "request_post_init",
        json_object_new_uint64(hg->hg_init_info.request_post_init), flags);

    // request_post_incr
    json_object_object_add_ex(
        json, "request_post_incr",
        json_object_new_uint64(hg->hg_init_info.request_post_incr), flags);

    // auto_sm
    json_object_object_add_ex(json, "auto_sm",
                              json_object_new_boolean(hg->hg_init_info.auto_sm),
                              flags);

    // no_bulk_eager
    json_object_object_add_ex(
        json, "no_bulk_eager",
        json_object_new_boolean(hg->hg_init_info.no_bulk_eager), flags);

    // no_loopback
    json_object_object_add_ex(
        json, "no_loopback",
        json_object_new_boolean(hg->hg_init_info.no_loopback), flags);

    // stats
    json_object_object_add_ex(
        json, "stats", json_object_new_boolean(hg->hg_init_info.stats), flags);

    // na_no_block
    json_object_object_add_ex(
        json, "na_no_block",
        json_object_new_boolean(hg->hg_init_info.na_init_info.progress_mode
                                & NA_NO_BLOCK),
        flags);

    // na_no_retry
    json_object_object_add_ex(
        json, "na_no_retry",
        json_object_new_boolean(hg->hg_init_info.na_init_info.progress_mode
                                & NA_NO_RETRY),
        flags);

    // max_contexts
    json_object_object_add_ex(
        json, "max_contexts",
        json_object_new_uint64(hg->hg_init_info.na_init_info.max_contexts),
        flags);

    // ip_subnet
    if (hg->hg_init_info.na_init_info.ip_subnet)
        json_object_object_add_ex(
            json, "ip_subnet",
            json_object_new_string(hg->hg_init_info.na_init_info.ip_subnet),
            flags);

    // auth_key
    if (hg->hg_init_info.na_init_info.auth_key)
        json_object_object_add_ex(
            json, "auth_key",
            json_object_new_string(hg->hg_init_info.na_init_info.auth_key),
            flags);

#if (HG_VERSION_MAJOR > 2)       \
    || (HG_VERSION_MAJOR == 2    \
        && (HG_VERSION_MINOR > 1 \
            || (HG_VERSION_MINOR == 0 && HG_VERSION_PATH > 0)))
    // na_max_unexpected_size and na_max_expected_size available from
    // version 2.0.1
    json_object_object_add_ex(
        json, "na_max_unexpected_size",
        json_object_new_uint64(
            hg->hg_init_info.na_init_info.max_unexpected_size),
        flags);
    json_object_object_add_ex(
        json, "na_max_expected_size",
        json_object_new_uint64(hg->hg_init_info.na_init_info.max_expected_size),
        flags);
#endif
#if (HG_VERSION_MAJOR > 2) || (HG_VERSION_MAJOR == 2 && HG_VERSION_MINOR > 0)
    // sm_info_string available from version 2.1.0
    if (hg->hg_init_info.sm_info_string)
        json_object_object_add_ex(
            json, "sm_info_string",
            json_object_new_string(hg->hg_init_info.sm_info_string), flags);
#endif
#if (HG_VERSION_MAJOR > 2) || (HG_VERSION_MAJOR == 2 && HG_VERSION_MINOR > 1)
    // na_request_mem_device available from version 2.2.0
    json_object_object_add_ex(
        json, "na_request_mem_device",
        json_object_new_boolean(
            hg->hg_init_info.na_init_info.request_mem_device),
        flags);
    // checksum_level available from version 2.2.0
    const char* checksum_level_str[] = {"none", "rpc_headers", "rpc_payload"};
    json_object_object_add_ex(
        json, "checksum_level",
        json_object_new_string(
            checksum_level_str[hg->hg_init_info.checksum_level]),
        flags);
    // na_addr_format available from version 2.2.0
    const char* na_addr_format_str[] = {"unspec", "ipv4", "ipv6", "native"};
    json_object_object_add_ex(
        json, "na_addr_format",
        json_object_new_string(
            na_addr_format_str[hg->hg_init_info.na_init_info.addr_format]),
        flags);
#endif

finish:
    return json;
}

static inline void margo_hg_destroy(margo_hg_t* hg)
{
    free((char*)hg->hg_init_info.sm_info_string);
    free((char*)hg->hg_init_info.na_init_info.auth_key);
    free((char*)hg->hg_init_info.na_init_info.ip_subnet);

    free(hg->self_addr_str);

    if (hg->hg_class && hg->self_addr != HG_ADDR_NULL)
        HG_Addr_free(hg->hg_class, hg->self_addr);

    if (hg->hg_context && (hg->hg_ownership & MARGO_OWNS_HG_CONTEXT)) {
        HG_Context_destroy(hg->hg_context);
        hg->hg_context = NULL;
    }

    if (hg->hg_class && (hg->hg_ownership & MARGO_OWNS_HG_CLASS)) {
        HG_Finalize(hg->hg_class);
        hg->hg_class = NULL;
    }
    memset(hg, 0, sizeof(*hg));
}

#endif
