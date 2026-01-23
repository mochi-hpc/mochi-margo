#include <assert.h>
#include <stdio.h>
#include <margo.h>

static const int TOTAL_RPCS = 4;
static int num_rpcs = 0;

static void my_trace(void* uargs, const char* str) {
    printf("[trace] %s\n", str);
}

static void my_debug(void* uargs, const char* str) {
    printf("[debug] %s\n", str);
}

static void my_info(void* uargs, const char* str) {
    printf("[info] %s\n", str);
}

static void my_warning(void* uargs, const char* str) {
    printf("[warning] %s\n", str);
}

static void my_error(void* uargs, const char* str) {
    printf("[error] %s\n", str);
}

static void my_critical(void* uargs, const char* str) {
    printf("[critical] %s\n", str);
}

static struct margo_logger custom_logger = {
    .uargs    = NULL,
    .trace    = my_trace,
    .debug    = my_debug,
    .info     = my_info,
    .warning  = my_warning,
    .error    = my_error,
    .critical = my_critical
};

static void hello_world(hg_handle_t h);
DECLARE_MARGO_RPC_HANDLER(hello_world)

int main(int argc, char** argv)
{
    margo_set_global_log_level(MARGO_LOG_INFO);

    margo_info(MARGO_INSTANCE_NULL,
               "This message uses the global logger");

    margo_instance_id mid = margo_init("tcp", MARGO_SERVER_MODE, 0, -1);
    assert(mid);
    margo_set_logger(mid, &custom_logger);
    margo_set_log_level(mid, MARGO_LOG_INFO);

    margo_info(mid,
               "This message uses an instance's logger");

    hg_addr_t my_address;
    margo_addr_self(mid, &my_address);
    char addr_str[128];
    size_t addr_str_size = 128;
    margo_addr_to_string(mid, addr_str, &addr_str_size, my_address);
    margo_addr_free(mid,my_address);

    margo_info(mid, "Server running at address %s", addr_str);

    hg_id_t rpc_id = MARGO_REGISTER(mid, "hello", void, void, hello_world);
    margo_registered_disable_response(mid, rpc_id, HG_TRUE);

    margo_wait_for_finalize(mid);

    return 0;
}

static void hello_world(hg_handle_t h)
{
    hg_return_t ret;

    margo_instance_id mid = margo_hg_handle_get_instance(h);

    margo_info(mid, "Hello World!");
    num_rpcs += 1;

    ret = margo_destroy(h);
    assert(ret == HG_SUCCESS);

    if(num_rpcs == TOTAL_RPCS) {
        margo_finalize(mid);
    }
}
DEFINE_MARGO_RPC_HANDLER(hello_world)
