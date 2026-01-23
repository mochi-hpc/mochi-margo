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
    margo_set_log_level(mid, MARGO_LOG_DEBUG);

    hg_id_t sum_rpc_id = MARGO_REGISTER(mid, "sum", sum_in_t, sum_out_t, NULL);

    hg_addr_t svr_addr;
    margo_addr_lookup(mid, argv[1], &svr_addr);

    int i;
    sum_in_t args;
    for(i=0; i<4; i++) {

        int32_t values[10] = { 1,4,2,5,6,3,5,3,2,5 };
        hg_size_t segment_sizes[1] = { 10*sizeof(int32_t) };
        void* segment_ptrs[1] = { (void*)values };

        hg_bulk_t local_bulk;
        margo_bulk_create(mid, 1, segment_ptrs, segment_sizes, HG_BULK_READ_ONLY, &local_bulk);

        args.n = 10;
        args.bulk = local_bulk;

        hg_handle_t h;
        margo_create(mid, svr_addr, sum_rpc_id, &h);
        margo_forward(h, &args);

        sum_out_t resp;
        margo_get_output(h, &resp);

        margo_debug(mid, "Got response: %d", resp.ret);

        margo_free_output(h,&resp);
        margo_destroy(h);

        margo_bulk_free(local_bulk);
    }

    margo_addr_free(mid, svr_addr);

    margo_finalize(mid);

    return 0;
}
