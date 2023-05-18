/**
 * @file margo-hg-shim.h
 *
 * (C) 2023 The University of Chicago
 *
 * See COPYRIGHT in top-level directory.
 */
#ifndef __MARGO_HG_SHIM_H
#define __MARGO_HG_SHIM_H

#include <mercury.h>
#include <mercury_proc.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * This file provides functions to enable a purely Mercury-based program
 * and Margo-based program to work together properly. Margo adds a header
 * to the input and output serialization path, hence if a program registers
 * an RPC using Mercury (e.g. with HG_Register_name) while another program
 * registers the same RPC using Margo (e.g. with margo_register), Margo will
 * not find the header when Mercury sends the RPC and Mercury will find a
 * header when it is not expecting one. Margo also computes RPC ids differently
 * than Mercury from an RPC name.
 *
 * To solve this problem, please replace Mercury functions in your Mercury-based
 * code as follows, and link your Mercury code against libmargo-hg-shim.so.
 *
 * - HG_Register_name => HG_Register_name_for_margo
 * - HG_Get_input     => HG_Get_input_from_margo
 * - HG_Free_input    => HG_Free_input_from_margo
 * - HG_Get_output    => HG_Get_output_from_margo
 * - HG_Free_output   => HG_Free_output_from_margo
 * - HG_Forward       => HG_Forward_to_margo
 * - HG_Respond       => HG_Respond_to_margo
 *
 * HG_Register does not have an equivalent and should not be used.
 * The rest of the Mercury functions can remain unchanged.
 *
 * Note that using the above functions will still make it possible for your
 * Mercury program to send RPC to / receive RPC from another Mercury program
 * as long as this program also uses the above functions.
 *
 * Note that contrary to Mercury, where serialization functions are provided
 * in calls to HG_Register and HG_Register_name, in these new variants, the
 * serialization function is provided when calling HG_Forward, HG_Respond,
 * HG_Get_input/output and HG_Free_input/output. Hence, make sure you provide
 * the correct proc function pointer to all these calls. Additionally, the
 * HG_Registered_proc_cb is no longer relevant since it will return proc
 * functions that are internal to Margo.
 *
 * Important: these shim functions do not support provider IDs. Margo codes
 * should register their RPC with a provider ID of MARGO_DEFAULT_PROVIDER_ID
 * (or simply use MARGO_REGISTER/margo_register_name instead of
 * MARGO_REGISTER_PROVIDER/margo_provider_register_name).
 */

hg_id_t HG_Register_name_for_margo(hg_class_t* hg_class,
                                   const char* func_name,
                                   hg_rpc_cb_t rpc_cb);

hg_return_t HG_Forward_to_margo(hg_handle_t  handle,
                                hg_cb_t      cb,
                                void*        args,
                                hg_proc_cb_t proc_in,
                                void*        in_struct);

hg_return_t HG_Respond_to_margo(hg_handle_t  handle,
                                hg_cb_t      cb,
                                void*        args,
                                hg_proc_cb_t proc_out,
                                void*        out_struct);

hg_return_t HG_Get_input_from_margo(hg_handle_t  handle,
                                    hg_proc_cb_t proc_in,
                                    void*        in_struct);

hg_return_t HG_Free_input_from_margo(hg_handle_t  handle,
                                     hg_proc_cb_t proc_in,
                                     void*        in_struct);

hg_return_t HG_Get_output_from_margo(hg_handle_t  handle,
                                     hg_proc_cb_t proc_out,
                                     void*        out_struct);

hg_return_t HG_Free_output_from_margo(hg_handle_t  handle,
                                      hg_proc_cb_t proc_out,
                                      void*        out_struct);

#ifdef __cplusplus
}
#endif

#endif
