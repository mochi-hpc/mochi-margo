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

#define DEPRECATED(msg) __attribute__((deprecated(msg)))

/**
 * Margo instance id. Entry point to the margo runtime.
 */
typedef struct margo_instance* margo_instance_id;

/* human-readable JSON output */
#define MARGO_CONFIG_PRETTY_JSON 0x1
/* don't show external pools/xstreams */
#define MARGO_CONFIG_HIDE_EXTERNAL 0x2
/* use names instead of indices */
#define MARGO_CONFIG_USE_NAMES 0x4

/**
 * @brief Retrieves complete configuration of margo instance, encoded as json.
 *
 * @param [in] mid Margo instance.
 *
 * @return Null-terminated string that must be free'd by caller.
 */
char* margo_get_config(margo_instance_id mid);

/**
 * @brief Retrieves complete configuration of margo instance, encoded as json.
 *
 * @param [in] mid Margo instance.
 * @param [in] options Bitwise OR of MARGO_CONFIG_* options.
 *
 * @return Null-terminated string that must be free'd by caller.
 */
char* margo_get_config_opt(margo_instance_id mid, int options);

/**
 * @brief Get the number of xstreams defined in the margo instance.
 *
 * @param mid Margo instance.
 *
 * @return The number of xstreams.
 */
size_t margo_get_num_xstreams(margo_instance_id mid);

/**
 * @brief Get the number of pools defined in the margo instance.
 *
 * @param mid Margo instance.
 *
 * @return The number of pools.
 */
size_t margo_get_num_pools(margo_instance_id mid);

/**
 * @brief Structure used to retrieve information about margo-managed pools.
 */
struct margo_pool_info {
    ABT_pool    pool;
    const char* name;
    uint32_t    index;
};

/**
 * @brief Find information about a margo-managed pool by
 * searching for the ABT_pool handle.
 *
 * @param [in] mid Margo instance.
 * @param [in] handle ABT_pool handle.
 * @param [out] info Pointer to margo_pool_info struct to fill.
 *
 * @return HG_SUCCESS or other HG error code (HG_INVALID_ARG or HG_NOENTRY).
 */
hg_return_t margo_find_pool_by_handle(margo_instance_id       mid,
                                      ABT_pool                handle,
                                      struct margo_pool_info* info);

/**
 * @brief Find information about a margo-managed pool by
 * searching for the name.
 *
 * @param [in] mid Margo instance.
 * @param [in] handle ABT_pool handle.
 * @param [out] info Pointer to margo_pool_info struct to fill.
 *
 * @return HG_SUCCESS or other HG error code (HG_INVALID_ARG or HG_NOENTRY).
 */
hg_return_t margo_find_pool_by_name(margo_instance_id       mid,
                                    const char*             name,
                                    struct margo_pool_info* info);

/**
 * @brief Find information about a margo-managed pool by
 * searching for the index.
 *
 * @param [in] mid Margo instance.
 * @param [in] name Name of the pool to find.
 * @param [out] info Pointer to margo_pool_info struct to fill.
 *
 * @return HG_SUCCESS or other HG error code (HG_INVALID_ARG or HG_NOENTRY).
 */
hg_return_t margo_find_pool_by_index(margo_instance_id       mid,
                                     uint32_t                index,
                                     struct margo_pool_info* info);

/**
 * @brief Structure used to retrieve information about margo-managed xstreams.
 */
struct margo_xstream_info {
    ABT_xstream xstream;
    const char* name;
    uint32_t    index;
};

/**
 * @brief Find information about a margo-managed xstream by
 * searching for the ABT_xstream handle.
 *
 * @param [in] mid Margo instance.
 * @param [in] handle ABT_xstream handle.
 * @param [out] info Pointer to margo_xstream_info struct to fill.
 *
 * @return HG_SUCCESS or other HG error code (HG_INVALID_ARG or HG_NOENTRY).
 */
hg_return_t margo_find_xstream_by_handle(margo_instance_id          mid,
                                         ABT_xstream                handle,
                                         struct margo_xstream_info* info);

/**
 * @brief Find information about a margo-managed xstream by
 * searching for the name.
 *
 * @param [in] mid Margo instance.
 * @param [in] handle ABT_xstream handle.
 * @param [out] info Pointer to margo_xstream_info struct to fill.
 *
 * @return HG_SUCCESS or other HG error code (HG_INVALID_ARG or HG_NOENTRY).
 */
hg_return_t margo_find_xstream_by_name(margo_instance_id          mid,
                                       const char*                name,
                                       struct margo_xstream_info* info);

/**
 * @brief Find information about a margo-managed xstream by
 * searching for the index.
 *
 * @param [in] mid Margo instance.
 * @param [in] name Name of the xstream to find.
 * @param [out] info Pointer to margo_xstream_info struct to fill.
 *
 * @return HG_SUCCESS or other HG error code (HG_INVALID_ARG or HG_NOENTRY).
 */
hg_return_t margo_find_xstream_by_index(margo_instance_id          mid,
                                        uint32_t                   index,
                                        struct margo_xstream_info* info);

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
                           ABT_pool*         pool)
    DEPRECATED("Use margo_find_pool_by_name instead");

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
                            ABT_pool*         pool)
    DEPRECATED("Use margo_find_pool_by_index instead");

/**
 * @brief Get the name of a pool at a given index.
 *
 * @param mid Margo instance.
 * @param index Index of the pool.
 *
 * @return The null-terminated name, or NULL if index is invalid.
 */
const char* margo_get_pool_name(margo_instance_id mid, unsigned index)
    DEPRECATED("Use margo_find_pool_by_index instead");

/**
 * @brief Get the index of a pool from a given name.
 *
 * @param mid Margo instance.
 * @param name Name of the pool.
 *
 * @return The index of the pool, or -1 if the name is invalid.
 */
int margo_get_pool_index(margo_instance_id mid, const char* name)
    DEPRECATED("Use margo_find_pool_by_name instead");

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
                              ABT_xstream*      es)
    DEPRECATED("Use margo_find_xstream_by_name instead");

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
                               ABT_xstream*      es)
    DEPRECATED("Use margo_find_xstream_by_index instead");

/**
 * @brief Get the name of a xstream at a given index.
 *
 * @param mid Margo instance.
 * @param index Index of the xstream.
 *
 * @return The null-terminated name, or NULL if index is invalid.
 */
const char* margo_get_xstream_name(margo_instance_id mid, unsigned index)
    DEPRECATED("Use margo_find_xstream_by_index instead");

/**
 * @brief Get the index of an xstream from a given name.
 *
 * @param mid Margo instance.
 * @param name Name of the xstream.
 *
 * @return the index of the xstream, or -1 if the name is invalid.
 */
int margo_get_xstream_index(margo_instance_id mid, const char* name)
    DEPRECATED("Use margo_find_xstream_by_name instead");

#ifdef __cplusplus
}
#endif

#endif /* __MARGO_CONFIG */
