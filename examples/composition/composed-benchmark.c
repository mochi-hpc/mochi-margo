/*
 * (C) 2015 The University of Chicago
 *
 * See COPYRIGHT in top-level directory.
 */

#include <stdio.h>
#include <assert.h>
#include <unistd.h>
#include <stdlib.h>
#include <abt.h>
#include <margo.h>

#include "composed-client-lib.h"

static hg_id_t   my_rpc_shutdown_id;
static void*     buffer;
static hg_size_t buffer_sz = 8 * 1024 * 1024;

int main(int argc, char** argv)
{
    int               i;
    int               ret;
    margo_instance_id mid;
    hg_return_t       hret;
    hg_addr_t         delegator_svr_addr = HG_ADDR_NULL;
    hg_addr_t         data_xfer_svr_addr = HG_ADDR_NULL;
    hg_handle_t       handle;
    char*             proto;
    char*             colon;
    int               iterations;
    double            start, end, t1, t2, avg, min = 0, max = 0;

    if (argc != 4) {
        fprintf(stderr,
                "Usage: ./client <delegator_svr_addr> <data_xfer_svr_addr> "
                "<iterations>\n");
        return (-1);
    }

    ret = sscanf(argv[3], "%d", &iterations);
    assert(ret == 1);

    /* initialize Mercury using the transport portion of the destination
     * address (i.e., the part before the first : character if present)
     */
    proto = strdup(argv[1]);
    assert(proto);
    colon = strchr(proto, ':');
    if (colon) *colon = '\0';

    /* TODO: this is a hack for now; I don't really want this to operate in
     * server mode, but it seems like it needs to for now for sub-service to be
     * able to get back to it
     */
    /* actually start margo */
    /***************************************/
    mid = margo_init(proto, MARGO_SERVER_MODE, 0, -1);
    free(proto);
    if (mid == MARGO_INSTANCE_NULL) {
        fprintf(stderr, "Error: margo_init()\n");
        return (-1);
    }
    margo_diag_start(mid);

    /* register core RPC */
    my_rpc_shutdown_id
        = MARGO_REGISTER(mid, "my_shutdown_rpc", void, void, NULL);
    /* register service APIs */
    data_xfer_register_client(mid);
    composed_register_client(mid);

    /* find addrs for servers */
    hret = margo_addr_lookup(mid, argv[2], &data_xfer_svr_addr);
    assert(hret == HG_SUCCESS);
    hret = margo_addr_lookup(mid, argv[1], &delegator_svr_addr);
    assert(hret == HG_SUCCESS);

    buffer = calloc(1, buffer_sz);
    assert(buffer);

    /* TODO: this is where benchmark timing would go, probably with iterations
     */
    /***************************************************************************/

    sleep(3);
    printf("# DBG: starting data_xfer_read() benchmark.\n");
    start = ABT_get_wtime();
    for (i = 0; i < iterations; i++) {
        t1 = ABT_get_wtime();
        data_xfer_read(mid, data_xfer_svr_addr, buffer, buffer_sz);
        t2 = ABT_get_wtime();
        if (min == 0 || t2 - t1 < min) min = t2 - t1;
        if (max == 0 || t2 - t1 > max) max = t2 - t1;
    }
    end = ABT_get_wtime();
    avg = (end - start) / ((double)iterations);
    printf("# DBG:    ... DONE.\n");
    printf("# <op> <min> <avg> <max>\n");
    printf("direct\t%f\t%f\t%f\n", min, avg, max);

    sleep(3);

    printf("# DBG: starting composed_read() benchmark.\n");
    start = ABT_get_wtime();
    for (i = 0; i < iterations; i++) {
        t1 = ABT_get_wtime();
        composed_read(mid, delegator_svr_addr, buffer, buffer_sz, argv[2]);
        t2 = ABT_get_wtime();
        if (min == 0 || t2 - t1 < min) min = t2 - t1;
        if (max == 0 || t2 - t1 > max) max = t2 - t1;
    }
    end = ABT_get_wtime();
    avg = (end - start) / ((double)iterations);
    printf("# DBG:    ... DONE.\n");
    printf("# <op> <min> <avg> <max>\n");
    printf("composed\t%f\t%f\t%f\n", min, avg, max);
    printf("# DBG:    ... DONE.\n");

    /***************************************************************************/

    /* send rpc(s) to shut down server(s) */
    sleep(3);
    printf("Shutting down delegator server.\n");
    hret = margo_create(mid, delegator_svr_addr, my_rpc_shutdown_id, &handle);
    assert(hret == HG_SUCCESS);
    hret = margo_forward(handle, NULL);
    assert(hret == HG_SUCCESS);
    margo_destroy(handle);
    if (strcmp(argv[1], argv[2])) {
        sleep(3);
        printf("Shutting down data_xfer server.\n");
        hret = margo_create(mid, data_xfer_svr_addr, my_rpc_shutdown_id,
                            &handle);
        assert(hret == HG_SUCCESS);
        hret = margo_forward(handle, NULL);
        assert(hret == HG_SUCCESS);
        margo_destroy(handle);
    }

    margo_addr_free(mid, delegator_svr_addr);
    margo_addr_free(mid, data_xfer_svr_addr);

    /* shut down everything */
    margo_diag_dump(mid, "-", 0);
    margo_finalize(mid);
    free(buffer);

    return (0);
}
