/*
 * (C) 2024 The University of Chicago
 *
 * See COPYRIGHT in top-level directory.
 */
#ifndef __MARGO_ABT_KEY_H
#define __MARGO_ABT_KEY_H
#include <assert.h>
#include <stdbool.h>
#include <unistd.h>
#include <stdint.h>
#include <string.h>
#include <abt.h>
#include <mercury.h>

#define __MARGO_LINEAGE_COMMON                                    \
    static const char* magic  = "matthieu";                       \
    ABT_thread_attr attr      = ABT_THREAD_ATTR_NULL;             \
    ABT_thread      ult       = ABT_THREAD_NULL;                  \
    void*           stackaddr = NULL;                             \
    size_t          stacksize = 0;                                \
    int             ret;                                          \
    ret = ABT_thread_self(&ult);                                  \
    if(ret != ABT_SUCCESS) return ret;                            \
    ret = ABT_thread_get_attr(ult, &attr);                        \
    if(ret != ABT_SUCCESS) return ret;                            \
    ret = ABT_thread_attr_get_stack(attr, &stackaddr, &stacksize);\
    if(ret != ABT_SUCCESS) return ret;                            \
    if(!stackaddr || !stacksize) return ABT_ERR_KEY;              \
    char* stackend = (char*)stackaddr + stacksize

static inline int margo_lineage_set(hg_id_t current_rpc_id) {
    __MARGO_LINEAGE_COMMON;
    memcpy(stackend - 8 - sizeof(current_rpc_id), magic, 8);
    memcpy(stackend - 8, &current_rpc_id, sizeof(current_rpc_id));
    return ABT_SUCCESS;
}

static inline int margo_lineage_erase() {
    __MARGO_LINEAGE_COMMON;
    memset(stackend - 8 - sizeof(hg_id_t), 0, 8 + sizeof(hg_id_t));
    return ABT_SUCCESS;
}

static inline int margo_lineage_get(hg_id_t* current_rpc_id) {
    __MARGO_LINEAGE_COMMON;
    if(memcmp(stackend - 8 - sizeof(current_rpc_id), magic, 8) != 0)
        return ABT_ERR_KEY;
    memcpy(current_rpc_id, stackend - 8, sizeof(*current_rpc_id));
    return ABT_SUCCESS;
}

#undef __MARGO_LINEAGE_COMMON

#endif
