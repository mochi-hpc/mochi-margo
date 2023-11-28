/*
 * (C) 2023 The University of Chicago
 *
 * See COPYRIGHT in top-level directory.
 */
#include "margo.h"
#include "margo-instance.h"
#include <mercury_proc_string.h>
#include <string.h>
#include <stdlib.h>

static void get_identity(hg_handle_t handle)
{
    const struct hg_info* info = margo_get_info(handle);
    margo_instance_id     mid  = margo_hg_handle_get_instance(handle);
    hg_string_t identity = (hg_string_t)margo_registered_data(mid, info->id);
    margo_respond(handle, &identity);
    margo_destroy(handle);
}
DEFINE_MARGO_RPC_HANDLER(get_identity)

hg_return_t margo_provider_register_identity(margo_instance_id mid,
                                             uint16_t          provider_id,
                                             const char*       identity)
{
    if (!identity) return HG_INVALID_ARG;
    hg_id_t id
        = MARGO_REGISTER_PROVIDER(mid, "__identity__", void, hg_string_t,
                                  get_identity, provider_id, ABT_POOL_NULL);
    if (!id) return HG_OTHER_ERROR;
    char* data = strdup(identity);
    margo_register_data(mid, id, data, free);
    return HG_SUCCESS;
}

hg_return_t margo_provider_deregister_identity(margo_instance_id mid,
                                               uint16_t          provider_id)
{
    hg_id_t     id;
    hg_bool_t   flag = HG_FALSE;
    hg_return_t hret;
    hret = margo_provider_registered_name(mid, "__identity__", provider_id, &id,
                                          &flag);
    if (hret != HG_SUCCESS) return hret;
    if (!flag) return HG_NOENTRY;
    margo_deregister(mid, id);
    return HG_SUCCESS;
}

const char* margo_provider_registered_identity(margo_instance_id mid,
                                               uint16_t          provider_id)
{
    hg_id_t     id;
    hg_bool_t   flag = HG_FALSE;
    hg_return_t hret;
    hret = margo_provider_registered_name(mid, "__identity__", provider_id, &id,
                                          &flag);
    if (hret != HG_SUCCESS) return NULL;
    if (!flag) return NULL;
    const char* identity = margo_registered_data(mid, id);
    return identity;
}

hg_return_t margo_provider_get_identity(margo_instance_id mid,
                                        hg_addr_t         address,
                                        uint16_t          provider_id,
                                        char*             buffer,
                                        size_t*           bufsize)
{
    if (!bufsize || !buffer) return HG_INVALID_ARG;

    hg_string_t out  = NULL;
    hg_handle_t h    = HG_HANDLE_NULL;
    hg_return_t hret = HG_SUCCESS;
    hg_id_t     id;
    hg_bool_t   flag = HG_FALSE;

    hret = margo_create(mid, address, mid->identity_rpc_id, &h);
    if (hret != HG_SUCCESS) return hret;

    hret = margo_provider_forward(provider_id, h, NULL);
    if (hret != HG_SUCCESS) goto finish;

    hret = margo_get_output(h, &out);
    if (hret != HG_SUCCESS) goto finish;

    if (out) {
        size_t len = strlen(out);
        if (*bufsize >= len + 1) {
            strcpy(buffer, out);
            *bufsize = len + 1;
        } else {
            if (*bufsize) buffer[0] = 0;
            *bufsize = len + 1;
            hret     = HG_NOMEM;
            goto finish;
        }
    } else {
        *bufsize = 0;
    }

    margo_free_output(h, &out);

finish:
    margo_destroy(h);
    return hret;
}
