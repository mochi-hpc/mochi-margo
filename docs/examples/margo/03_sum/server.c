#include <assert.h>
#include <stdio.h>
#include <margo.h>
#include "types.h"

typedef struct {
    int max_rpcs;
    int num_rpcs;
} server_data;

static void sum(hg_handle_t h);
DECLARE_MARGO_RPC_HANDLER(sum)

int main(int argc, char** argv)
{
    margo_instance_id mid = margo_init("tcp", MARGO_SERVER_MODE, 0, 0);
    assert(mid);

    server_data svr_data = {
        .max_rpcs = 4,
        .num_rpcs = 0
    };

    hg_addr_t my_address;
    margo_addr_self(mid, &my_address);
    char addr_str[128];
    size_t addr_str_size = 128;
    margo_addr_to_string(mid, addr_str, &addr_str_size, my_address);
    margo_addr_free(mid,my_address);

    margo_info(mid, "Server running at address %s", addr_str);

    hg_id_t rpc_id = MARGO_REGISTER(mid, "sum", sum_in_t, sum_out_t, sum);
    margo_register_data(mid, rpc_id, &svr_data, NULL);

    margo_wait_for_finalize(mid);

    return 0;
}

static void sum(hg_handle_t h)
{
    hg_return_t ret;

    sum_in_t in;
    sum_out_t out;

    margo_instance_id mid = margo_hg_handle_get_instance(h);
    margo_set_log_level(mid, MARGO_LOG_INFO);

    const struct hg_info* info = margo_get_info(h);
    server_data* svr_data = (server_data*)margo_registered_data(mid, info->id);

    ret = margo_get_input(h, &in);
    assert(ret == HG_SUCCESS);

    out.ret = in.x + in.y;
    margo_trace(mid, "Computed %d + %d = %d", in.x, in.y, out.ret);

    ret = margo_respond(h, &out);
    assert(ret == HG_SUCCESS);

    ret = margo_free_input(h, &in);
    assert(ret == HG_SUCCESS);

    ret = margo_destroy(h);
    assert(ret == HG_SUCCESS);

    svr_data->num_rpcs += 1;
    if(svr_data->num_rpcs == svr_data->max_rpcs) {
        margo_finalize(mid);
    }
}
DEFINE_MARGO_RPC_HANDLER(sum)
