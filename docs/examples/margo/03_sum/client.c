#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <margo.h>
#include "types.h"

int main(int argc, char** argv)
{
    if(argc != 2) {
        fprintf(stderr,"Usage: %s <server address>\n", argv[0]);
        exit(0);
    }

    margo_instance_id mid = margo_init("tcp", MARGO_CLIENT_MODE, 0, 0);
    margo_set_log_level(mid, MARGO_LOG_INFO);

    hg_id_t sum_rpc_id = MARGO_REGISTER(mid, "sum", sum_in_t, sum_out_t, NULL);

    hg_addr_t svr_addr;
    margo_addr_lookup(mid, argv[1], &svr_addr);

    int i;
    sum_in_t args;
    for(i=0; i<4; i++) {
        args.x = 42+i*2;
        args.y = 42+i*2+1;

        hg_handle_t h;
        margo_create(mid, svr_addr, sum_rpc_id, &h);
        margo_forward(h, &args);

        sum_out_t resp;
        margo_get_output(h, &resp);

        margo_info(mid, "Got response: %d+%d = %d\n", args.x, args.y, resp.ret);

        margo_free_output(h,&resp);
        margo_destroy(h);
    }

    margo_addr_free(mid, svr_addr);

    margo_finalize(mid);

    return 0;
}
