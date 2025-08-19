/*
 * (C) 2022 The University of Chicago
 *
 * See COPYRIGHT in top-level directory.
 */
#ifndef __MARGO_HG_CONFIG_INTERNAL_H
#define __MARGO_HG_CONFIG_INTERNAL_H
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

typedef char
    optional_char; /* optional string (may be null after initialization) */

#define MARGO_OWNS_HG_CLASS   0x1
#define MARGO_OWNS_HG_CONTEXT 0x2

/* Mercury environment */
typedef struct margo_hg {
    struct hg_init_info hg_init_info;
    hg_class_t*         hg_class;
    hg_context_t*       hg_context;
    hg_addr_t           self_addr;
    char*               self_addr_str;
    char*               log_level;
    char*               log_subsys;
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

bool                __margo_hg_validate_json(const struct json_object*,
                                             const margo_hg_user_args_t*);
bool                __margo_hg_init_from_json(const struct json_object*,
                                              const margo_hg_user_args_t*,
                                              const char* plumber_bucket_policy,
                                              const char* plumber_nic_policy,
                                              margo_hg_t*);
struct json_object* __margo_hg_to_json(const margo_hg_t*);
void                __margo_hg_destroy(margo_hg_t*);

#endif
