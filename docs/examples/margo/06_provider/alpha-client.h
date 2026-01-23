#ifndef __ALPHA_CLIENT_H
#define __ALPHA_CLIENT_H

#include <margo.h>
#include <alpha-common.h>

#if defined(__cplusplus)
extern "C" {
#endif

typedef struct alpha_client* alpha_client_t;
#define ALPHA_CLIENT_NULL ((alpha_client_t)NULL)

typedef struct alpha_provider_handle *alpha_provider_handle_t;
#define ALPHA_PROVIDER_HANDLE_NULL ((alpha_provider_handle_t)NULL)

/**
 * @brief Creates a ALPHA client.
 *
 * @param[in] mid Margo instance
 * @param[out] client ALPHA client
 *
 * @return ALPHA_SUCCESS or error code defined in alpha-common.h
 */
int alpha_client_init(margo_instance_id mid, alpha_client_t* client);

/**
 * @brief Finalizes a ALPHA client.
 *
 * @param[in] client ALPHA client to finalize
 *
 * @return ALPHA_SUCCESS or error code defined in alpha-common.h
 */
int alpha_client_finalize(alpha_client_t client);

/**
 * @brief Creates a ALPHA provider handle.
 *
 * @param[in] client ALPHA client responsible for the provider handle
 * @param[in] addr Mercury address of the provider
 * @param[in] provider_id id of the provider
 * @param[in] handle provider handle
 *
 * @return ALPHA_SUCCESS or error code defined in alpha-common.h
 */
int alpha_provider_handle_create(
        alpha_client_t client,
        hg_addr_t addr,
        uint16_t provider_id,
        alpha_provider_handle_t* handle);

/**
 * @brief Increments the reference counter of a provider handle.
 *
 * @param handle provider handle
 *
 * @return ALPHA_SUCCESS or error code defined in alpha-common.h
 */
int alpha_provider_handle_ref_incr(
        alpha_provider_handle_t handle);

/**
 * @brief Releases the provider handle. This will decrement the
 * reference counter, and free the provider handle if the reference
 * counter reaches 0.
 *
 * @param[in] handle provider handle to release.
 *
 * @return ALPHA_SUCCESS or error code defined in alpha-common.h
 */
int alpha_provider_handle_release(alpha_provider_handle_t handle);

/**
 * @brief Makes the target ALPHA provider compute the sum of the
 * two numbers and return the result.
 *
 * @param[in] handle provide handle.
 * @param[in] x first number.
 * @param[in] y second number.
 * @param[out] result resulting value.
 *
 * @return ALPHA_SUCCESS or error code defined in alpha-common.h
 */
int alpha_compute_sum(
        alpha_provider_handle_t handle,
        int32_t x,
        int32_t y,
        int32_t* result);

#endif
