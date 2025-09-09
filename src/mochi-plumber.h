/**
 * @file mochi-plumber.h
 *
 * (C) The University of Chicago
 *
 * See COPYRIGHT in top-level directory.
 */

#ifndef __MOCHI_PLUMBER
#define __MOCHI_PLUMBER

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Resolve the general network address (e.g., cxi://) to a
 * specific network card (e.g., cxi://cxi0).
 *
 * @param [in] in_address input address string
 * @param [in] bucket_policy policy for bucket selection
 * @param [in] nic_policy policy for nic selection within bucket
 * @param [out] out_address output address string (to be freed by caller)
 */
int mochi_plumber_resolve_nic(const char* in_address,
                              const char* bucket_policy,
                              const char* nic_policy,
                              char**      out_address);

#ifdef __cplusplus
}
#endif

#endif /* __MOCHI_PLUMBER */
