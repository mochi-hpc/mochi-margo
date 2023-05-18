/**
 * @file margo-hg-shim.c
 *
 * (C) 2023 The University of Chicago
 *
 * See COPYRIGHT in top-level directory.
 */
#include <mercury.h>
#include "margo-serialization.h"
#include "margo-id.h"
#include "margo-hg-shim.h"

static inline hg_return_t margo_hg_shim_forward_proc(hg_proc_t proc, void* args)
{
    margo_forward_proc_args_t sargs = (margo_forward_proc_args_t)args;
    hg_return_t               hret  = HG_SUCCESS;

    hret = hg_proc_memcpy(proc, (void*)(&sargs->header), sizeof(sargs->header));
    if (hret != HG_SUCCESS) return hret;
    if (!(sargs && sargs->user_cb)) return HG_SUCCESS;
    return sargs->user_cb(proc, sargs->user_args);
}

static inline hg_return_t margo_hg_shim_respond_proc(hg_proc_t proc, void* args)
{
    margo_respond_proc_args_t sargs = (margo_respond_proc_args_t)args;
    hg_return_t               hret  = HG_SUCCESS;

    hret = hg_proc_memcpy(proc, (void*)(&sargs->header), sizeof(sargs->header));
    if (hret != HG_SUCCESS) return hret;
    if (!(sargs && sargs->user_cb)) return HG_SUCCESS;
    return sargs->user_cb(proc, sargs->user_args);
}

hg_id_t HG_Register_name_for_margo(hg_class_t* hg_class,
                                   const char* func_name,
                                   hg_rpc_cb_t rpc_cb)
{
    hg_id_t     id  = gen_id(func_name, MARGO_DEFAULT_PROVIDER_ID);
    hg_return_t ret = HG_Register(hg_class, id, margo_hg_shim_forward_proc,
                                  margo_hg_shim_respond_proc, rpc_cb);
    if (ret == HG_SUCCESS)
        return id;
    else
        return 0;
}

hg_return_t HG_Forward_to_margo(hg_handle_t  handle,
                                hg_cb_t      cb,
                                void*        args,
                                hg_proc_cb_t proc_in,
                                void*        in_struct)
{
    struct margo_forward_proc_args in_args = {0};
    in_args.user_args                      = in_struct;
    in_args.user_cb                        = proc_in;
    return HG_Forward(handle, cb, args, &in_args);
}

hg_return_t HG_Respond_to_margo(hg_handle_t  handle,
                                hg_cb_t      cb,
                                void*        args,
                                hg_proc_cb_t proc_out,
                                void*        out_struct)
{
    struct margo_respond_proc_args out_args = {0};
    out_args.user_args                      = out_struct;
    out_args.user_cb                        = proc_out;
    return HG_Respond(handle, cb, args, &out_args);
}

hg_return_t HG_Get_input_from_margo(hg_handle_t  handle,
                                    hg_proc_cb_t proc_in,
                                    void*        in_struct)
{
    struct margo_forward_proc_args in_args = {0};
    in_args.user_args                      = in_struct;
    in_args.user_cb                        = proc_in;
    return HG_Get_input(handle, &in_args);
}

hg_return_t HG_Free_input_from_margo(hg_handle_t  handle,
                                     hg_proc_cb_t proc_in,
                                     void*        in_struct)
{
    struct margo_forward_proc_args in_args = {0};
    in_args.user_args                      = in_struct;
    in_args.user_cb                        = proc_in;
    return HG_Free_input(handle, &in_args);
}

hg_return_t HG_Get_output_from_margo(hg_handle_t  handle,
                                     hg_proc_cb_t proc_out,
                                     void*        out_struct)
{
    struct margo_respond_proc_args out_args = {0};
    out_args.user_args                      = out_struct;
    out_args.user_cb                        = proc_out;
    return HG_Get_output(handle, &out_args);
}

hg_return_t HG_Free_output_from_margo(hg_handle_t  handle,
                                      hg_proc_cb_t proc_out,
                                      void*        out_struct)
{
    struct margo_respond_proc_args out_args = {0};
    out_args.user_args                      = out_struct;
    out_args.user_cb                        = proc_out;
    return HG_Free_output(handle, &out_args);
}
