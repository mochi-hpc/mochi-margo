/**
 * @file margo.h
 *
 * (C) The University of Chicago
 *
 * See COPYRIGHT in top-level directory.
 */
#ifndef __MARGO_CONFIG
#define __MARGO_CONFIG

#ifdef __cplusplus
extern "C" {
#endif

#include <abt.h>

/**
 * Margo instance id. Entry point to the margo runtime.
 */
typedef struct margo_instance* margo_instance_id;

/**
 * @brief Retrieves complete configuration of margo instance, incoded as json.
 *
 * @param [in] mid Margo instance.
 *
 * @return Null-terminated string that must be free'd by caller.
 */
char* margo_get_config(margo_instance_id mid);

/**
 * @brief Get a pool from the configuration.
 *
 * @param [in] mid Margo instance.
 * @param [in] name Name of the pool.
 * @param [out] pool Argobots pool.
 *
 * @return 0 on success, -1 on failure
 */
int margo_get_pool_by_name(margo_instance_id mid,
                           const char*       name,
                           ABT_pool*         pool);

/**
 * @brief Get a pool from the configuration.
 *
 * @param [in] mid Margo instance.
 * @param [in] index Index of the pool.
 * @param [out] pool Argobots pool.
 *
 * @return 0 on success, -1 on failure.
 */
int margo_get_pool_by_index(margo_instance_id mid,
                            unsigned          index,
                            ABT_pool*         pool);

/**
 * @brief Get the name of a pool at a given index.
 *
 * @param mid Margo instance.
 * @param index Index of the pool.
 *
 * @return The null-terminated name, or NULL if index is invalid.
 */
const char* margo_get_pool_name(margo_instance_id mid, unsigned index);

/**
 * @brief Get the index of a pool from a given name.
 *
 * @param mid Margo instance.
 * @param name Name of the pool.
 *
 * @return The index of the pool, or -1 if the name is invalid.
 */
int margo_get_pool_index(margo_instance_id mid, const char* name);

/**
 * @brief Get the number of pools defined in the margo instance.
 *
 * @param mid Margo instance.
 *
 * @return The number of pools.
 */
size_t margo_get_num_pools(margo_instance_id mid);

/**
 * @brief Get an xstream from the configuration.
 *
 * @param [in] mid Margo instance.
 * @param [in] name Name of the ES.
 * @param [out] es ES.
 *
 * @return 0 on success, -1 on failure.
 */
int margo_get_xstream_by_name(margo_instance_id mid,
                              const char*       name,
                              ABT_xstream*      es);

/**
 * @brief Get an xstream from the configuration.
 *
 * @param [in] mid Margo instance.
 * @param [in] name Index of the ES.
 * @param [out] es ES.
 *
 * @return 0 on success, -1 on failure.
 */
int margo_get_xstream_by_index(margo_instance_id mid,
                               unsigned          index,
                               ABT_xstream*      es);

/**
 * @brief Get the name of a xstream at a given index.
 *
 * @param mid Margo instance.
 * @param index Index of the xstream.
 *
 * @return The null-terminated name, or NULL if index is invalid.
 */
const char* margo_get_xstream_name(margo_instance_id mid, unsigned index);

/**
 * @brief Get the index of an xstream from a given name.
 *
 * @param mid Margo instance.
 * @param name Name of the xstream.
 *
 * @return the index of the xstream, or -1 if the name is invalid.
 */
int margo_get_xstream_index(margo_instance_id mid, const char* name);

/**
 * @brief Get the number of xstreams defined in the margo instance.
 *
 * @param mid Margo instance.
 *
 * @return The number of xstreams.
 */
size_t margo_get_num_xstreams(margo_instance_id mid);

#ifdef __cplusplus
}
#endif

#endif /* __MARGO_CONFIG */
