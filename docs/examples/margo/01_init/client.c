#include <assert.h>
#include <stdio.h>
#include <margo.h>

int main(int argc, char** argv)
{
    margo_instance_id mid = margo_init("tcp",MARGO_CLIENT_MODE, 0, 0);
    assert(mid);

    margo_finalize(mid);

    return 0;
}
