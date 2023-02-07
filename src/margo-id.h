/*
 * (C) 2022 The University of Chicago
 *
 * See COPYRIGHT in top-level directory.
 */
#ifndef __MARGO_ID_H
#define __MARGO_ID_H

#include "lookup3.h"

static inline void demux_id(hg_id_t in, hg_id_t* base_id, uint16_t* provider_id)
{
    /* retrieve low bits for provider */
    *provider_id = 0;
    *provider_id += (in & (((1 << (__MARGO_PROVIDER_ID_SIZE * 8)) - 1)));

    /* clear low order bits */
    *base_id = (in >> (__MARGO_PROVIDER_ID_SIZE * 8))
            << (__MARGO_PROVIDER_ID_SIZE * 8);
    /* set them to 1s */
    *base_id |= MARGO_MAX_PROVIDER_ID;

    return;
}

static inline hg_id_t mux_id(hg_id_t base_id, uint16_t provider_id)
{
    hg_id_t id;

    id = (base_id >> (__MARGO_PROVIDER_ID_SIZE * 8))
      << (__MARGO_PROVIDER_ID_SIZE * 8);
    id |= provider_id;

    return id;
}

static inline hg_id_t gen_id(const char* func_name, uint16_t provider_id)
{
    hg_id_t id  = 0;
    uint32_t a32 = 0, b32 = 0;
    margo_bj_hashlittle2(func_name, strlen(func_name), &a32, &b32);
    b32          = b32 << (__MARGO_PROVIDER_ID_SIZE * 8);
    uint64_t a64 = a32;
    a64          = a64 << 32;
    id |= a64;
    id |= b32;
    id |= provider_id;
    return id;
}

#endif
