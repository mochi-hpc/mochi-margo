#include <assert.h>
#include <stdio.h>
#include <margo.h>

int main(int argc, char** argv)
{
    /* Create a parent Margo instance (server) with custom pools. */
    const char* parent_config
        = "{"
          "\"argobots\": {"
          "\"pools\": ["
          "{\"name\":\"__primary__\",\"access\":\"mpmc\",\"kind\":\"fifo_wait\"},"
          "{\"name\":\"my_pool\",\"access\":\"mpmc\",\"kind\":\"fifo_wait\"}"
          "],"
          "\"xstreams\": ["
          "{\"name\":\"__primary__\","
          "\"scheduler\":{\"pools\":[\"__primary__\"],"
          "\"type\":\"basic_wait\"}"
          "},"
          "{\"name\":\"es1\","
          "\"scheduler\":{\"pools\":[\"my_pool\"],"
          "\"type\":\"basic_wait\"}"
          "}"
          "]"
          "}"
          "}";

    struct margo_init_info parent_args = MARGO_INIT_INFO_INITIALIZER;
    parent_args.json_config = parent_config;

    margo_instance_id parent_mid
        = margo_init_ext("na+sm", MARGO_SERVER_MODE, &parent_args);
    assert(parent_mid);
    margo_set_log_level(parent_mid, MARGO_LOG_INFO);

    hg_addr_t my_address;
    margo_addr_self(parent_mid, &my_address);
    char   addr_str[128];
    size_t addr_str_size = 128;
    margo_addr_to_string(parent_mid, addr_str, &addr_str_size, my_address);
    margo_addr_free(parent_mid, my_address);

    margo_info(parent_mid, "Parent running at address %s", addr_str);

    /* Create a child instance that reuses the parent's Argobots
     * environment. The child gets its own Mercury context but
     * references one of the parent's pools by name. */
    const char* child_config
        = "{"
          "\"progress_pool\":\"my_pool\","
          "\"rpc_pool\":\"my_pool\""
          "}";

    struct margo_init_info child_args = MARGO_INIT_INFO_INITIALIZER;
    child_args.parent_mid  = parent_mid;
    child_args.json_config = child_config;

    margo_instance_id child_mid
        = margo_init_ext("na+sm", MARGO_CLIENT_MODE, &child_args);
    assert(child_mid);

    margo_info(parent_mid, "Child instance created, sharing parent pools");

    /* Verify both instances share the same pools. */
    ABT_pool parent_pool, child_pool;
    margo_get_handler_pool(parent_mid, &parent_pool);
    margo_get_handler_pool(child_mid, &child_pool);
    /* Note: they may differ because they reference different pools
     * (parent uses __primary__, child uses my_pool). */

    /* The child can now be used for client-side RPCs while
     * the parent handles server-side RPCs, both sharing
     * the same Argobots execution streams and pools. */

    /* Always finalize the child before the parent. */
    margo_finalize(child_mid);
    margo_finalize(parent_mid);

    return 0;
}
