#include "types.h"
#include "alpha-client.h"
#include <stdlib.h>

struct alpha_client {
   margo_instance_id mid;
   hg_id_t           sum_id;
   uint64_t          num_prov_hdl;
};

struct alpha_provider_handle {
    alpha_client_t client;
    hg_addr_t      addr;
    uint16_t       provider_id;
    uint64_t       refcount;
};

int alpha_client_init(margo_instance_id mid, alpha_client_t* client)
{
    int ret = ALPHA_SUCCESS;

    alpha_client_t c = (alpha_client_t)calloc(1, sizeof(*c));
    if(!c) return ALPHA_FAILURE;

    c->mid = mid;

    hg_bool_t flag;
    hg_id_t id;
    margo_registered_name(mid, "alpha_sum", &id, &flag);

    if(flag == HG_TRUE) {
        margo_registered_name(mid, "alpha_sum", &c->sum_id, &flag);
    } else {
        c->sum_id = MARGO_REGISTER(mid, "alpha_sum", sum_in_t, sum_out_t, NULL);
    }

    *client = c;
    return ALPHA_SUCCESS;
}

int alpha_client_finalize(alpha_client_t client)
{
    if(client->num_prov_hdl != 0) {
        margo_warning(client->mid,
            "%d provider handles not released when alpha_client_finalize was called",
            client->num_prov_hdl);
    }
    free(client);
    return ALPHA_SUCCESS;
}

int alpha_provider_handle_create(
        alpha_client_t client,
        hg_addr_t addr,
        uint16_t provider_id,
        alpha_provider_handle_t* handle)
{
    if(client == ALPHA_CLIENT_NULL)
        return ALPHA_FAILURE;

    alpha_provider_handle_t ph =
        (alpha_provider_handle_t)calloc(1, sizeof(*ph));

    if(!ph) return ALPHA_FAILURE;

    hg_return_t ret = margo_addr_dup(client->mid, addr, &(ph->addr));
    if(ret != HG_SUCCESS) {
        free(ph);
        return ALPHA_FAILURE;
    }

    ph->client      = client;
    ph->provider_id = provider_id;
    ph->refcount    = 1;

    client->num_prov_hdl += 1;

    *handle = ph;
    return ALPHA_SUCCESS;
}

int alpha_provider_handle_ref_incr(
        alpha_provider_handle_t handle)
{
    if(handle == ALPHA_PROVIDER_HANDLE_NULL)
        return ALPHA_FAILURE;
    handle->refcount += 1;
    return ALPHA_SUCCESS;
}

int alpha_provider_handle_release(alpha_provider_handle_t handle)
{
    if(handle == ALPHA_PROVIDER_HANDLE_NULL)
        return ALPHA_FAILURE;
    handle->refcount -= 1;
    if(handle->refcount == 0) {
        margo_addr_free(handle->client->mid, handle->addr);
        handle->client->num_prov_hdl -= 1;
        free(handle);
    }
    return ALPHA_SUCCESS;
}

int alpha_compute_sum(
        alpha_provider_handle_t handle,
        int32_t x,
        int32_t y,
        int32_t* result)
{
    hg_handle_t   h;
    sum_in_t     in;
    sum_out_t   out;
    hg_return_t ret;

    in.x = x;
    in.y = y;

    ret = margo_create(handle->client->mid, handle->addr, handle->client->sum_id, &h);
    if(ret != HG_SUCCESS)
        return ALPHA_FAILURE;

    ret = margo_provider_forward(handle->provider_id, h, &in);
    if(ret != HG_SUCCESS) {
        margo_destroy(h);
        return ALPHA_FAILURE;
    }

    ret = margo_get_output(h, &out);
    if(ret != HG_SUCCESS) {
        margo_destroy(h);
        return ALPHA_FAILURE;
    }

    *result = out.ret;

    margo_free_output(h, &out);
    margo_destroy(h);
    return ALPHA_SUCCESS;
}
