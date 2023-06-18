/*
 * (C) 2022 The University of Chicago
 *
 * See COPYRIGHT in top-level directory.
 */
#ifndef __MARGO_ID_H
#define __MARGO_ID_H

#include <mercury.h>

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
    hg_id_t  id;
    unsigned hashval;

    HASH_JEN(func_name, strlen(func_name), hashval);
    // Imporant: until Mercury 2.3.0, there was a bug causing Mercury to
    // cast hg_id_t into a 32-bit value somewhere, causing half of its bytes
    // to get discarded. The two ways of generating the ID hereafter account
    // for this.
#if (HG_VERSION_MAJOR > 2) || (HG_VERSION_MAJOR == 2 && HG_VERSION_MINOR >= 3)
    // version for Mercury 2.3.0 and above
    id = hashval;
    id = id << 32;
    id |= (hg_id_t)provider_id;
#else
    // old version
    id = hashval << (__MARGO_PROVIDER_ID_SIZE * 8);
    id |= provider_id;
#endif

    return id;
}

#endif
