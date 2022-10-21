/**
 * (C) 2022 The University of Chicago
 *
 * See COPYRIGHT in top-level directory.
 */
#ifndef __MARGO_MONITORING_INTERNAL_H
#define __MARGO_MONITORING_INTERNAL_H

#include "margo-monitoring.h"

#define __MARGO_MONITOR(__mid__, __mevent__, __fun__, __args__)               \
    do {                                                                      \
        if ((__mid__) == MARGO_INSTANCE_NULL) break;                          \
        struct margo_monitor* __monitor__ = &(__mid__)->monitor;              \
        if (__monitor__->on_##__fun__) {                                      \
            double __timestamp__ = ABT_get_wtime();                           \
            __monitor__->on_##__fun__(__monitor__->uargs, __timestamp__,      \
                                      MARGO_MONITOR_##__mevent__, &__args__); \
        }                                                                     \
    } while (0)

#endif
