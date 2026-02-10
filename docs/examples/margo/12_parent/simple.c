#include <assert.h>
#include <stdio.h>
#include <margo.h>

int main(int argc, char** argv)
{
    /* Create a parent Margo instance with default configuration. */
    margo_instance_id parent_mid
        = margo_init("na+sm", MARGO_SERVER_MODE, 0, 0);
    assert(parent_mid);

    /* Create a child that shares the parent's Argobots environment.
     * The child gets its own Mercury class and context, so it can
     * independently send and receive RPCs. By default (no JSON config),
     * the child uses the same progress and RPC pools as the parent. */
    struct margo_init_info child_args = MARGO_INIT_INFO_INITIALIZER;
    child_args.parent_mid = parent_mid;

    margo_instance_id child_mid
        = margo_init_ext("na+sm", MARGO_CLIENT_MODE, &child_args);
    assert(child_mid);

    /* Both instances now share the same Argobots pools and ES. */

    /* Always finalize the child before the parent. */
    margo_finalize(child_mid);
    margo_finalize(parent_mid);

    return 0;
}
