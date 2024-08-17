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

#include <mercury.h>
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
 * @param [in] name Name of the pool.
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
 * @param [in] index Index of the pool to find.
 * @param [out] info Pointer to margo_pool_info struct to fill.
 *
 * @return HG_SUCCESS or other HG error code (HG_INVALID_ARG or HG_NOENTRY).
 */
hg_return_t margo_find_pool_by_index(margo_instance_id       mid,
                                     uint32_t                index,
                                     struct margo_pool_info* info);

/**
 * @brief Find information about a margo-managed pool (generic version).
 *
 * @param [in] mid Margo instance.
 * @param [in] arg index, name, or ABT_pool.
 * @param [out] info Pointer to margo_pool_info struct to fill.
 *
 * @return HG_SUCCESS or other HG error code (HG_INVALID_ARG or HG_NOENTRY).
 */
#define margo_find_pool(mid, args, info) \
    _Generic((args),                          \
        ABT_pool: margo_find_pool_by_handle,  \
        const char*: margo_find_pool_by_name, \
        char*: margo_find_pool_by_name, \
        default: margo_find_pool_by_index     \
    )(mid, args, info)

/**
 * @brief Creates a new Argobots pool according to the provided
 * JSON description (following the same format as the pool objects
 * in the Margo configuration) and fill the output margo_pool_info
 * structure.
 *
 * @param [in] mid Margo instance.
 * @param [in] json JSON-formatted description.
 * @param [out] info Resulting pool information.
 *
 * @return HG_SUCCESS or other HG error code (e.g. HG_INVALID_ARG).
 */
hg_return_t margo_add_pool_from_json(margo_instance_id       mid,
                                     const char*             json,
                                     struct margo_pool_info* info);

/**
 * @brief Adds an existing Argobots pool for Margo to use.
 *
 * Important: it is the user's responsibility to ensure that the ABT_pool
 * remains valid until the Margo instance is destroyed or until the
 * pool is removed from the Margo instance.
 *
 * @param [in] mid Margo instance.
 * @param [in] name Name to give the pool (auto-generated if NULL).
 * @param [in] pool Pool handle.
 * @param [in] take_ownership Give ownership to the Margo instance.
 * @param [out] info Resulting pool information.
 *
 * @return HG_SUCCESS or other HG error code.
 */
hg_return_t margo_add_pool_external(margo_instance_id       mid,
                                    const char*             name,
                                    ABT_pool                pool,
                                    ABT_bool                take_ownership,
                                    struct margo_pool_info* info);

/**
 * @brief Removes the pool at the specified index.
 * If the pool has been created by Margo (in margo_init or
 * via margo_add_pool_from_json) or if it has been added
 * via margo_add_pool_external with take_ownership = true,
 * this function will free the pool. Otherwise, this function
 * will simply remove it from the pools known to the margo instance.
 *
 * This function will fail if the pool is used by an xstream
 * (that margo knows about), or if the pool is not empty.
 *
 * @param mid Margo instance.
 * @param index Index of the pool.
 *
 * @return HG_SUCCESS or other error code.
 */
hg_return_t margo_remove_pool_by_index(margo_instance_id mid, uint32_t index);

/**
 * @brief Same as margo_remove_pool_by_index by using the
 * name of the pool to remove.
 *
 * @param mid Margo instance.
 * @param name Name of the pool to remove.
 *
 * @return HG_SUCCESS or other error code.
 */
hg_return_t margo_remove_pool_by_name(margo_instance_id mid, const char* name);

/**
 * @brief Same as margo_remove_pool_by_index by using the
 * handle of the pool to remove.
 *
 * @param mid Margo instance.
 * @param handle ABT_pool handle of the pool to remove.
 *
 * @return HG_SUCCESS or other error code.
 */
hg_return_t margo_remove_pool_by_handle(margo_instance_id mid, ABT_pool handle);

/**
 * @brief Remove a margo-managed pool (generic version).
 *
 * @param [in] mid Margo instance.
 * @param [in] arg index, name, or ABT_pool.
 *
 * @return HG_SUCCESS or other HG error code (HG_INVALID_ARG or HG_NOENTRY).
 */
#define margo_remove_pool(mid, args) \
    _Generic((args),                            \
        ABT_pool: margo_remove_pool_by_handle,  \
        const char*: margo_remove_pool_by_name, \
        char*: margo_remove_pool_by_name, \
        default: margo_remove_pool_by_index     \
    )(mid, args)

/**
 * @brief Increment the reference count of a pool managed by Margo.
 * This reference count is used to prevent removal of pools that are in use.
 *
 * @param [in] mid Margo instance.
 * @param [in] handle ABT_pool handle.
 *
 * @return HG_SUCCESS or other HG error code (HG_INVALID_ARG or HG_NOENTRY).
 */
hg_return_t margo_pool_ref_incr_by_handle(margo_instance_id mid,
                                          ABT_pool          handle);

/**
 * @brief Increment the reference count of a pool managed by Margo.
 * This reference count is used to prevent removal of pools that are in use.
 *
 * @param [in] mid Margo instance.
 * @param [in] name Name of the pool.
 *
 * @return HG_SUCCESS or other HG error code (HG_INVALID_ARG or HG_NOENTRY).
 */
hg_return_t margo_pool_ref_incr_by_name(margo_instance_id mid,
                                        const char*       name);

/**
 * @brief Increment the reference count of a pool managed by Margo.
 * This reference count is used to prevent removal of pools that are in use.
 *
 * @param [in] mid Margo instance.
 * @param [in] index Index of the pool.
 *
 * @return HG_SUCCESS or other HG error code (HG_INVALID_ARG or HG_NOENTRY).
 */
hg_return_t margo_pool_ref_incr_by_index(margo_instance_id mid, uint32_t index);

/**
 * @brief Increment the reference count of a margo-managed pool (generic
 * version).
 *
 * @param [in] mid Margo instance.
 * @param [in] arg index, name, or ABT_pool.
 *
 * @return HG_SUCCESS or other HG error code (HG_INVALID_ARG or HG_NOENTRY).
 */
#define margo_pool_ref_incr(mid, args) \
    _Generic((args),                             \
        ABT_pool: margo_pool_ref_incr_by_handle,  \
        const char*: margo_pool_ref_incr_by_name, \
        char*: margo_pool_ref_incr_by_name, \
        default: margo_pool_ref_incr_by_index     \
    )(mid, args)

/**
 * @brief Decrement the reference count of a pool managed by Margo.
 * This reference count is used to prevent removal of pools that are in use.
 *
 * @param [in] mid Margo instance.
 * @param [in] handle ABT_pool handle.
 *
 * @return HG_SUCCESS or other HG error code (HG_INVALID_ARG or HG_NOENTRY).
 */
hg_return_t margo_pool_release_by_handle(margo_instance_id mid,
                                         ABT_pool          handle);

/**
 * @brief Decrement the reference count of a pool managed by Margo.
 * This reference count is used to prevent removal of pools that are in use.
 *
 * @param [in] mid Margo instance.
 * @param [in] name Name of the pool.
 *
 * @return HG_SUCCESS or other HG error code (HG_INVALID_ARG or HG_NOENTRY).
 */
hg_return_t margo_pool_release_by_name(margo_instance_id mid, const char* name);

/**
 * @brief Decrement the reference count of a pool managed by Margo.
 * This reference count is used to prevent removal of pools that are in use.
 *
 * @param [in] mid Margo instance.
 * @param [in] index Index of the pool.
 *
 * @return HG_SUCCESS or other HG error code (HG_INVALID_ARG or HG_NOENTRY).
 */
hg_return_t margo_pool_release_by_index(margo_instance_id mid, uint32_t index);

/**
 * @brief Decrement the reference count of a margo-managed pool (generic
 * version).
 *
 * @param [in] mid Margo instance.
 * @param [in] arg index, name, or ABT_pool.
 *
 * @return HG_SUCCESS or other HG error code (HG_INVALID_ARG or HG_NOENTRY).
 */
#define margo_pool_release(mid, args) \
    _Generic((args),                             \
        ABT_pool: margo_pool_release_by_handle,  \
        const char*: margo_pool_release_by_name, \
        char*: margo_pool_release_by_name, \
        default: margo_pool_release_by_index     \
    )(mid, args)

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
 * @param [in] name Name of the ES.
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
 * @param [in] index Index of the ES.
 * @param [out] info Pointer to margo_xstream_info struct to fill.
 *
 * @return HG_SUCCESS or other HG error code (HG_INVALID_ARG or HG_NOENTRY).
 */
hg_return_t margo_find_xstream_by_index(margo_instance_id          mid,
                                        uint32_t                   index,
                                        struct margo_xstream_info* info);

/**
 * @brief Find information about a margo-managed ES (generic version).
 *
 * @param [in] mid Margo instance.
 * @param [in] arg index, name, or ABT_xstream.
 * @param [out] info Pointer to margo_xstream_info struct to fill.
 *
 * @return HG_SUCCESS or other HG error code (HG_INVALID_ARG or HG_NOENTRY).
 */
#define margo_find_xstream(mid, args, info) \
    _Generic((args),                               \
        ABT_xstream: margo_find_xstream_by_handle, \
        const char*: margo_find_xstream_by_name,   \
        char*: margo_find_xstream_by_name,   \
        default: margo_find_xstream_by_index       \
    )(mid, args, info)

/**
 * @brief Creates a new Argobots xstream according to the provided
 * JSON description (following the same format as the xstream objects
 * in the Margo configuration) and fill the output margo_xstream_info
 * structure.
 *
 * @param [in] mid Margo instance.
 * @param [in] json JSON-formatted description.
 * @param [out] info Resulting xstream information.
 *
 * @return HG_SUCCESS or other HG error code (e.g. HG_INVALID_ARG).
 */
hg_return_t margo_add_xstream_from_json(margo_instance_id          mid,
                                        const char*                json,
                                        struct margo_xstream_info* info);

/**
 * @brief Adds an existing Argobots xstream for Margo to use.
 *
 * Note: any pool associated with the ES that is not yet registered
 * with the Margo instance will be added to the instance as an external
 * pool.
 *
 * Important: it is the user's responsibility to ensure that the ABT_xstream
 * remains valid until the Margo instance is destroyed or until the
 * xstream is removed from the Margo instance.
 *
 * @param [in] mid Margo instance.
 * @param [in] name Name to give the xstream (auto-generated if NULL).
 * @param [in] xstream ES handle.
 * @param [in] take_ownership Give ownership to the Margo instance.
 * @param [out] info Resulting xstream information.
 *
 * @return HG_SUCCESS or other HG error code.
 */
hg_return_t margo_add_xstream_external(margo_instance_id mid,
                                       const char*       name,
                                       ABT_xstream       xstream,
                                       ABT_bool          take_ownership,
                                       struct margo_xstream_info* info);

/**
 * @brief Removes the xstream at the specified index.
 * If the xstream has been created by Margo (in margo_init or
 * via margo_add_xstream_from_json) or if it has been added
 * via margo_add_xstream_external with take_ownership = true,
 * this function will join the xstream and free it. Otherwise,
 * this function will simply remove it from the xstreams known
 * to the margo instance.
 *
 * Note: this function will not check whether the removal
 * will leave pools detached from any xstream. It is the caller's
 * responsibility to ensure that any work left in the pools
 * associated with the removed xstream will be picked up by
 * another xstream now or in the future.
 *
 * @param mid Margo instance.
 * @param index Index of the xstream.
 *
 * @return HG_SUCCESS or other error code.
 */
hg_return_t margo_remove_xstream_by_index(margo_instance_id mid,
                                          uint32_t          index);

/**
 * @brief Same as margo_remove_xstream_by_index by using the
 * name of the xstream to remove.
 *
 * @param mid Margo instance.
 * @param name Name of the xstream to remove.
 *
 * @return HG_SUCCESS or other error code.
 */
hg_return_t margo_remove_xstream_by_name(margo_instance_id mid,
                                         const char*       name);

/**
 * @brief Same as margo_remove_xstream_by_index by using the
 * handle of the xstream to remove.
 *
 * @param mid Margo instance.
 * @param handle ABT_xstream handle of the xstream to remove.
 *
 * @return HG_SUCCESS or other error code.
 */
hg_return_t margo_remove_xstream_by_handle(margo_instance_id mid,
                                           ABT_xstream       handle);

/**
 * @brief Remove a margo-managed ES (generic version).
 *
 * @param [in] mid Margo instance.
 * @param [in] arg index, name, or ABT_xstream.
 *
 * @return HG_SUCCESS or other HG error code (HG_INVALID_ARG or HG_NOENTRY).
 */
#define margo_remove_xstream(mid, args) \
    _Generic((args),                                 \
        ABT_xstream: margo_remove_xstream_by_handle, \
        const char*: margo_remove_xstream_by_name,   \
        char*: margo_remove_xstream_by_name,   \
        default: margo_remove_xstream_by_index       \
    )(mid, args)

/**
 * @brief Increment the reference count of a xstream managed by Margo.
 * This reference count is used to prevent removal of xstreams that are in use.
 *
 * @param [in] mid Margo instance.
 * @param [in] handle ABT_xstream handle.
 *
 * @return HG_SUCCESS or other HG error code (HG_INVALID_ARG or HG_NOENTRY).
 */
hg_return_t margo_xstream_ref_incr_by_handle(margo_instance_id mid,
                                             ABT_xstream       handle);

/**
 * @brief Increment the reference count of a xstream managed by Margo.
 * This reference count is used to prevent removal of xstreams that are in use.
 *
 * @param [in] mid Margo instance.
 * @param [in] name Name of the xstream.
 *
 * @return HG_SUCCESS or other HG error code (HG_INVALID_ARG or HG_NOENTRY).
 */
hg_return_t margo_xstream_ref_incr_by_name(margo_instance_id mid,
                                           const char*       name);

/**
 * @brief Increment the reference count of a xstream managed by Margo.
 * This reference count is used to prevent removal of xstreams that are in use.
 *
 * @param [in] mid Margo instance.
 * @param [in] index Index of the xstream.
 *
 * @return HG_SUCCESS or other HG error code (HG_INVALID_ARG or HG_NOENTRY).
 */
hg_return_t margo_xstream_ref_incr_by_index(margo_instance_id mid,
                                            uint32_t          index);

/**
 * @brief Increment the reference count of a margo-managed xstream (generic
 * version).
 *
 * @param [in] mid Margo instance.
 * @param [in] arg index, name, or ABT_xstream.
 *
 * @return HG_SUCCESS or other HG error code (HG_INVALID_ARG or HG_NOENTRY).
 */
#define margo_xstream_ref_incr(mid, args) \
    _Generic((args),                             \
        ABT_xstream: margo_xstream_ref_incr_by_handle,  \
        const char*: margo_xstream_ref_incr_by_name, \
        char*: margo_xstream_ref_incr_by_name, \
        default: margo_xstream_ref_incr_by_index     \
    )(mid, args)

/**
 * @brief Decrement the reference count of a xstream managed by Margo.
 * This reference count is used to prevent removal of xstreams that are in use.
 *
 * @param [in] mid Margo instance.
 * @param [in] handle ABT_xstream handle.
 *
 * @return HG_SUCCESS or other HG error code (HG_INVALID_ARG or HG_NOENTRY).
 */
hg_return_t margo_xstream_release_by_handle(margo_instance_id mid,
                                            ABT_xstream       handle);

/**
 * @brief Decrement the reference count of a xstream managed by Margo.
 * This reference count is used to prevent removal of xstreams that are in use.
 *
 * @param [in] mid Margo instance.
 * @param [in] name Name of the xstream.
 *
 * @return HG_SUCCESS or other HG error code (HG_INVALID_ARG or HG_NOENTRY).
 */
hg_return_t margo_xstream_release_by_name(margo_instance_id mid,
                                          const char*       name);

/**
 * @brief Decrement the reference count of a xstream managed by Margo.
 * This reference count is used to prevent removal of xstreams that are in use.
 *
 * @param [in] mid Margo instance.
 * @param [in] index Index of the xstream.
 *
 * @return HG_SUCCESS or other HG error code (HG_INVALID_ARG or HG_NOENTRY).
 */
hg_return_t margo_xstream_release_by_index(margo_instance_id mid,
                                           uint32_t          index);

/**
 * @brief Decrement the reference count of a margo-managed xstream (generic
 * version).
 *
 * @param [in] mid Margo instance.
 * @param [in] arg index, name, or ABT_xstream.
 *
 * @return HG_SUCCESS or other HG error code (HG_INVALID_ARG or HG_NOENTRY).
 */
#define margo_xstream_release(mid, args) \
    _Generic((args),                             \
        ABT_xstream: margo_xstream_release_by_handle,  \
        const char*: margo_xstream_release_by_name, \
        char*: margo_xstream_release_by_name, \
        default: margo_xstream_release_by_index     \
    )(mid, args)

/**
 * @brief This helper function transfers the ULT from one pool to another.
 * It can be used to move ULTs out of a pool that we wish to remove.
 *
 * Note: this function will not remove ULTs that are blocked.
 * The caller can check for any remaining blocked ULTs by calling
 * ABT_pool_get_total_size(origin_pool, &size).
 *
 * @param origin_pool Origin pool.
 * @param target_pool Target pool.
 *
 * @return HG_SUCCESS or other error code.
 */
hg_return_t margo_transfer_pool_content(ABT_pool origin_pool,
                                        ABT_pool target_pool);

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
