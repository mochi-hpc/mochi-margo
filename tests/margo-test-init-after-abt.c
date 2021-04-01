/*
 * (C) 2015 The University of Chicago
 *
 * See COPYRIGHT in top-level directory.
 */

#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <unistd.h>
#include <margo.h>
#include <margo-logging.h>

int main(int argc, char **argv)
{
    margo_instance_id mid;
    struct margo_init_info args = { 0 };

    ABT_init(0, NULL);

    mid = margo_init_ext("na+sm", MARGO_CLIENT_MODE, &args);
    assert(mid);

    margo_finalize(mid);

    ABT_finalize();

    return 0;
}
