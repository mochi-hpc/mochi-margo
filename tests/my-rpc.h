/*
 * (C) 2015 The University of Chicago
 * 
 * See COPYRIGHT in top-level directory.
 */

#ifndef __MY_RPC
#define __MY_RPC

#include <margo.h>

/* visible API for example RPC operation */

MARGO_GEN_PROC(my_rpc_hang_out_t, ((int32_t)(ret)))
MARGO_GEN_PROC(my_rpc_hang_in_t,
    ((int32_t)(input_val))\
    ((hg_bulk_t)(bulk_handle)))
DECLARE_MARGO_RPC_HANDLER(my_rpc_hang_ult)

MARGO_GEN_PROC(my_rpc_out_t, ((int32_t)(ret)))
MARGO_GEN_PROC(my_rpc_in_t,
    ((int32_t)(input_val))\
    ((hg_bulk_t)(bulk_handle)))
DECLARE_MARGO_RPC_HANDLER(my_rpc_ult)

DECLARE_MARGO_RPC_HANDLER(my_rpc_shutdown_ult)

#endif /* __MY_RPC */
