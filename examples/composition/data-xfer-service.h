/*
 * (C) 2015 The University of Chicago
 *
 * See COPYRIGHT in top-level directory.
 */

#ifndef __DATA_XFER_SERVICE
#define __DATA_XFER_SERVICE

#include <margo.h>

int  data_xfer_service_register(margo_instance_id mid,
                                ABT_pool          pool,
                                uint32_t          provider_id);
void data_xfer_service_deregister(margo_instance_id mid,
                                  ABT_pool          pool,
                                  uint32_t          provider_id);

#endif /* __DATA_XFER_SERVICE */
