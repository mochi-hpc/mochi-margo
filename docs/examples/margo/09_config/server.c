#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <margo.h>

int main(int argc, char** argv)
{
    struct margo_init_info args = {
        .json_config   = NULL, /* const char*          */
        .progress_pool = NULL, /* ABT_pool             */
        .rpc_pool      = NULL, /* ABT_pool             */
        .hg_class      = NULL, /* hg_class_t*          */
        .hg_context    = NULL, /* hg_context_t*        */
        .hg_init_info  = NULL  /* struct hg_init_info* */
    };

    margo_instance_id mid = margo_init_ext("tcp", MARGO_SERVER_MODE, &args);
    assert(mid);
    margo_set_log_level(mid, MARGO_LOG_INFO);

    char* config = margo_get_config(mid);
    margo_info(mid, "%s", config);
    free(config);

    margo_finalize(mid);

    return 0;
}
