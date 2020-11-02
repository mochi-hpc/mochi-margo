/*
 * (C) 2015 The University of Chicago
 *
 * See COPYRIGHT in top-level directory.
 */

#include <stdio.h>     /* defines printf for tests */
#include <time.h>      /* defines time_t for timings in the test */
#include <stdint.h>    /* defines uint32_t etc */
#include <sys/param.h> /* attempt to define endianness */
#ifdef linux
    #include <endian.h> /* attempt to define endianness */
#endif

#ifndef __MARGO_DIAG
    #define __MARGO_DIAG

    #ifdef __cplusplus
extern "C" {
    #endif

/* used to identify a globally unique breadcrumb */
struct margo_global_breadcrumb_key {
    uint64_t rpc_breadcrumb; /* a.k.a RPC callpath */
    uint64_t addr_hash;      /* hash of server addr */
    uint16_t provider_id; /* provider_id within a server. NOT a globally unique
                             identifier */
};

enum margo_breadcrumb_type
{
    origin,
    target
};

typedef enum margo_breadcrumb_type margo_breadcrumb_type;

struct margo_breadcrumb_stats {
    /* stats for breadcrumb call times */
    double min;
    double max;
    double cumulative;

    /* stats for RPC handler pool sizes */
    /* Total pool size = Total number of runnable items + items waiting on a
     * lock */
    unsigned long abt_pool_total_size_lwm; /* low watermark */
    unsigned long abt_pool_total_size_hwm; /* high watermark */
    unsigned long abt_pool_total_size_cumulative;

    unsigned long abt_pool_size_lwm; /* low watermark */
    unsigned long abt_pool_size_hwm; /* high watermark */
    unsigned long abt_pool_size_cumulative;

    /* count of occurrences of breadcrumb */
    unsigned long count;
};

typedef struct margo_breadcrumb_stats margo_breadcrumb_stats;

/* structure to store breadcrumb snapshot, for consumption outside of margo.
   reflects the margo-internal structure used to hold diagnostic data */
struct margo_breadcrumb {
    margo_breadcrumb_stats stats;
    /* 0 is this is a origin-side breadcrumb, 1 if this is a target-side
     * breadcrumb */
    margo_breadcrumb_type type;

    struct margo_global_breadcrumb_key key;

    struct margo_breadcrumb* next;
};

/* snapshot contains linked list of breadcrumb data */
struct margo_breadcrumb_snapshot {
    struct margo_breadcrumb* ptr;
};

    #ifdef __cplusplus
}
    #endif

#endif /* __MARGO_DIAG */
