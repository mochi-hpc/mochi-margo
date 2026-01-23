#include <assert.h>
#include <stdio.h>
#include <margo.h>
#include <alpha-server.h>

static void finalize_xstream_cb(void* data);

int main(int argc, char** argv)
{
    margo_instance_id mid = margo_init("tcp", MARGO_SERVER_MODE, 0, 0);
    assert(mid);
    margo_set_log_level(mid, MARGO_LOG_INFO);

    hg_addr_t my_address;
    margo_addr_self(mid, &my_address);
    char addr_str[128];
    size_t addr_str_size = 128;
    margo_addr_to_string(mid, addr_str, &addr_str_size, my_address);
    margo_addr_free(mid,my_address);
    margo_info(mid, "Server running at address %s, with provider id 42", addr_str);

    ABT_pool pool;
    ABT_pool_create_basic(
            ABT_POOL_FIFO,
            ABT_POOL_ACCESS_SPSC,
            ABT_TRUE,
            &pool);

    ABT_xstream xstream;
    ABT_xstream_create_basic(
            ABT_SCHED_DEFAULT,
            1,
            &pool,
            ABT_SCHED_CONFIG_NULL,
            &xstream);

    alpha_provider_register(mid, 42, pool, ALPHA_PROVIDER_IGNORE);

    margo_push_finalize_callback(mid, finalize_xstream_cb, (void*)xstream);

    margo_wait_for_finalize(mid);

    return 0;
}

static void finalize_xstream_cb(void* data) {
    ABT_xstream xstream = (ABT_xstream)data;
    ABT_xstream_join(xstream);
    ABT_xstream_free(&xstream);
}
