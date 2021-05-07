/*
 * (C) 2020 The University of Chicago
 *
 * See COPYRIGHT in top-level directory.
 */
#ifndef __MARGO_DIAG_INTERNAL_H
#define __MARGO_DIAG_INTERNAL_H

#include <stdio.h>     /* defines printf for tests */
#include <time.h>      /* defines time_t for timings in the test */
#include <stdint.h>    /* defines uint32_t etc */
#include <sys/param.h> /* attempt to define endianness */
#ifdef linux
    #include <endian.h> /* attempt to define endianness */
#endif
#include "margo-instance.h"
#include "margo-globals.h"

void __margo_sparkline_thread_start(margo_instance_id mid);
void __margo_sparkline_thread_stop(margo_instance_id mid);

void __margo_print_diag_data(margo_instance_id mid,
                             FILE*             file,
                             const char*       name,
                             const char*       description,
                             struct diag_data* data);

void __margo_print_profile_data(margo_instance_id mid,
                                FILE*             file,
                                const char*       name,
                                const char*       description,
                                struct diag_data* data);

uint64_t __margo_breadcrumb_set(hg_id_t rpc_id);

void __margo_breadcrumb_measure(margo_instance_id     mid,
                                uint64_t              rpc_breadcrumb,
                                double                start,
                                margo_breadcrumb_type type,
                                uint16_t              provider_id,
                                uint64_t              hash,
                                hg_handle_t           h);

#define GET_SELF_ADDR_STR(__mid, __addr_str)                              \
    do {                                                                  \
        hg_addr_t __self_addr;                                            \
        hg_size_t __size;                                                 \
        __addr_str = NULL;                                                \
        if (margo_addr_self(__mid, &__self_addr) != HG_SUCCESS) break;    \
        if (margo_addr_to_string(__mid, NULL, &__size, __self_addr)       \
            != HG_SUCCESS) {                                              \
            margo_addr_free(__mid, __self_addr);                          \
            break;                                                        \
        }                                                                 \
        if ((__addr_str = malloc(__size)) == NULL) {                      \
            margo_addr_free(__mid, __self_addr);                          \
            break;                                                        \
        }                                                                 \
        if (margo_addr_to_string(__mid, __addr_str, &__size, __self_addr) \
            != HG_SUCCESS) {                                              \
            free(__addr_str);                                             \
            __addr_str = NULL;                                            \
            margo_addr_free(__mid, __self_addr);                          \
            break;                                                        \
        }                                                                 \
        margo_addr_free(__mid, __self_addr);                              \
    } while (0)

#define __DIAG_UPDATE(__data, __time)                                 \
    do {                                                              \
        __data.stats.count++;                                         \
        __data.stats.cumulative += (__time);                          \
        if ((__time) > __data.stats.max) __data.stats.max = (__time); \
        if (__data.stats.min == 0 || (__time) < __data.stats.min)     \
            __data.stats.min = (__time);                              \
    } while (0)

#endif
