#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <margo.h>

int main(int argc, char** argv)
{
    if(argc != 2) {
        fprintf(stderr,"Usage: %s <server address>\n", argv[0]);
        exit(0);
    }

    hg_return_t ret;
    margo_instance_id mid = MARGO_INSTANCE_NULL;


    mid = margo_init("tcp",MARGO_CLIENT_MODE, 0, 0);
    assert(mid);

    hg_id_t hello_rpc_id = MARGO_REGISTER(mid, "hello", void, void, NULL);

    margo_registered_disable_response(mid, hello_rpc_id, HG_TRUE);

    hg_addr_t svr_addr;
    ret = margo_addr_lookup(mid, argv[1], &svr_addr);
    assert(ret == HG_SUCCESS);

    hg_handle_t handle;
    ret = margo_create(mid, svr_addr, hello_rpc_id, &handle);
    assert(ret == HG_SUCCESS);

    ret = margo_forward(handle, NULL);
    assert(ret == HG_SUCCESS);

    ret = margo_destroy(handle);
    assert(ret == HG_SUCCESS);

    ret = margo_addr_free(mid, svr_addr);
    assert(ret == HG_SUCCESS);

    margo_finalize(mid);

    return 0;
}
