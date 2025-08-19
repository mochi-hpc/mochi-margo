/*
 * (C) 2022 The University of Chicago
 *
 * See COPYRIGHT in top-level directory.
 */

#include "margo-config-private.h"
#ifdef HAVE_MOCHI_PLUMBER
    #include <mochi-plumber.h>
#endif
#include "margo-hg-config.h"

bool __margo_hg_validate_json(const struct json_object*   json,
                              const margo_hg_user_args_t* user_args)
{
#define HANDLE_CONFIG_ERROR return false

    if (!json) return true;
    if (!json_object_is_type(json, json_type_object)) {
        margo_error(0, "\"mercury\" field in configuration must be an object");
        return false;
    }

    // if hg_class or hg_init_info provided,
    // configuration will be ignored
    if (user_args->hg_class || user_args->hg_init_info) {
#define WARNING_CONFIG_HAS(__json__, __name__)                                \
    do {                                                                      \
        if (json_object_object_get(__json__, #__name__)) {                    \
            margo_warning(0,                                                  \
                          "\"" #__name__                                      \
                          "\" ignored in mercury configuration"               \
                          " because hg_class or hg_init_info were provided"); \
        }                                                                     \
    } while (0)

        WARNING_CONFIG_HAS(json, request_post_incr);
        WARNING_CONFIG_HAS(json, request_post_init);
        WARNING_CONFIG_HAS(json, auto_sm);
        WARNING_CONFIG_HAS(json, no_bulk_eager);
        WARNING_CONFIG_HAS(json, no_loopback);
        WARNING_CONFIG_HAS(json, stats);
        WARNING_CONFIG_HAS(json, na_no_block);
        WARNING_CONFIG_HAS(json, na_no_retry);
        WARNING_CONFIG_HAS(json, max_contexts);
        WARNING_CONFIG_HAS(json, ip_subnet);
        WARNING_CONFIG_HAS(json, auth_key);
        WARNING_CONFIG_HAS(json, na_max_expected_size);
        WARNING_CONFIG_HAS(json, na_max_unexpected_size);
        WARNING_CONFIG_HAS(json, sm_info_string);
        WARNING_CONFIG_HAS(json, na_request_mem_device);
        WARNING_CONFIG_HAS(json, checksum_level);
        WARNING_CONFIG_HAS(json, na_addr_format);
    }

    ASSERT_CONFIG_HAS_OPTIONAL(json, "address", string, "mercury");
    ASSERT_CONFIG_HAS_OPTIONAL(json, "version", string, "mercury");
    ASSERT_CONFIG_HAS_OPTIONAL(json, "listening", boolean, "mercury");
    ASSERT_CONFIG_HAS_OPTIONAL(json, "request_post_incr", int, "mercury");
    ASSERT_CONFIG_HAS_OPTIONAL(json, "request_post_init", int, "mercury");
    ASSERT_CONFIG_HAS_OPTIONAL(json, "auto_sm", boolean, "mercury");
    ASSERT_CONFIG_HAS_OPTIONAL(json, "no_bulk_eager", boolean, "mercury");
    ASSERT_CONFIG_HAS_OPTIONAL(json, "no_loopback", boolean, "mercury");
    ASSERT_CONFIG_HAS_OPTIONAL(json, "stats", boolean, "mercury");
    ASSERT_CONFIG_HAS_OPTIONAL(json, "na_no_block", boolean, "mercury");
    ASSERT_CONFIG_HAS_OPTIONAL(json, "na_no_retry", boolean, "mercury");
    ASSERT_CONFIG_HAS_OPTIONAL(json, "max_contexts", int, "mercury");
    ASSERT_CONFIG_HAS_OPTIONAL(json, "ip_subnet", string, "mercury");
    ASSERT_CONFIG_HAS_OPTIONAL(json, "auth_key", string, "mercury");
    ASSERT_CONFIG_HAS_OPTIONAL(json, "input_eager_size", int, "mercury");
    ASSERT_CONFIG_HAS_OPTIONAL(json, "output_eager_size", int, "mercury");
    ASSERT_CONFIG_HAS_OPTIONAL(json, "log_level", string, "mercury");
    ASSERT_CONFIG_HAS_OPTIONAL(json, "log_subsys", string, "mercury");

#if (HG_VERSION_MAJOR > 2)       \
    || (HG_VERSION_MAJOR == 2    \
        && (HG_VERSION_MINOR > 1 \
            || (HG_VERSION_MINOR == 0 && HG_VERSION_PATH > 0)))
    // na_max_unexpected_size and na_max_expected_size available from
    // version 2.0.1
    ASSERT_CONFIG_HAS_OPTIONAL(json, "na_max_unexpected_size", int, "mercury");
    ASSERT_CONFIG_HAS_OPTIONAL(json, "na_max_expected_size", int, "mercury");
#endif
#if (HG_VERSION_MAJOR > 2) || (HG_VERSION_MAJOR == 2 && HG_VERSION_MINOR > 0)
    // sm_info_string available from version 2.1.0
    ASSERT_CONFIG_HAS_OPTIONAL(json, "sm_info_string", string, "mercury");
#endif
#if (HG_VERSION_MAJOR > 2) || (HG_VERSION_MAJOR == 2 && HG_VERSION_MINOR > 1)
    // na_request_mem_device available from version 2.2.0
    ASSERT_CONFIG_HAS_OPTIONAL(json, "na_request_mem_device", boolean,
                               "mercury");
    // checksum_level available from version 2.2.0
    ASSERT_CONFIG_HAS_OPTIONAL(json, "checksum_level", string, "mercury");
    struct json_object* checksum_level
        = json_object_object_get(json, "checksum_level");
    if (checksum_level)
        CONFIG_IS_IN_ENUM_STRING(checksum_level, "checksum_level", "none",
                                 "rpc_headers", "rpc_payload");
    // na_addr_format available from version 2.2.0
    ASSERT_CONFIG_HAS_OPTIONAL(json, "na_addr_format", string, "mercury");
    struct json_object* na_addr_format
        = json_object_object_get(json, "na_addr_format");
    if (na_addr_format) {
        CONFIG_IS_IN_ENUM_STRING(na_addr_format, "na_addr_format", "unspec",
                                 "ipv4", "ipv4", "ipv6", "native");
    }
#endif
    return true;
#undef HANDLE_CONFIG_ERROR
}

bool __margo_hg_init_from_json(const struct json_object*   json,
                               const margo_hg_user_args_t* user,
                               const char* plumber_bucket_policy,
                               const char* plumber_nic_policy,
                               margo_hg_t* hg)
{
    if (user->hg_init_info && !user->hg_class) {
        // initialize hg_init_info from user-provided hg_init_info
        if (!user->hg_class) {
            memcpy(&(hg->hg_init_info), user->hg_init_info,
                   sizeof(*user->hg_init_info));
#if (HG_VERSION_MAJOR > 2) || (HG_VERSION_MAJOR == 2 && HG_VERSION_MINOR > 0)
            if (hg->hg_init_info.sm_info_string)
                hg->hg_init_info.sm_info_string
                    = strdup(hg->hg_init_info.sm_info_string);
#endif
            if (hg->hg_init_info.na_init_info.ip_subnet)
                hg->hg_init_info.na_init_info.ip_subnet
                    = strdup(hg->hg_init_info.na_init_info.ip_subnet);
            if (hg->hg_init_info.na_init_info.auth_key)
                hg->hg_init_info.na_init_info.auth_key
                    = strdup(hg->hg_init_info.na_init_info.auth_key);
        }
    } else {
        // initialize hg_init_info from JSON configuration
        hg->hg_init_info.request_post_init
            = json_object_object_get_uint64_or(json, "request_post_init", 256);
        hg->hg_init_info.request_post_incr
            = json_object_object_get_uint64_or(json, "request_post_incr", 256);
        hg->hg_init_info.auto_sm
            = json_object_object_get_bool_or(json, "auto_sm", false);
        hg->hg_init_info.no_bulk_eager
            = json_object_object_get_bool_or(json, "no_bulk_eager", false);
        hg->hg_init_info.no_loopback
            = json_object_object_get_bool_or(json, "no_loopback", false);
        hg->hg_init_info.stats
            = json_object_object_get_bool_or(json, "stats", false);
        hg->hg_init_info.na_init_info.progress_mode = 0;
        if (json_object_object_get_bool_or(json, "na_no_block", false))
            hg->hg_init_info.na_init_info.progress_mode |= NA_NO_BLOCK;
        if (json_object_object_get_bool_or(json, "na_no_retry", false))
            hg->hg_init_info.na_init_info.progress_mode |= NA_NO_RETRY;
        hg->hg_init_info.na_init_info.max_contexts
            = json_object_object_get_uint64_or(json, "max_contexts", 0);
        struct json_object* ip_subnet
            = json_object_object_get(json, "ip_subnet");
        if (ip_subnet)
            hg->hg_init_info.na_init_info.ip_subnet
                = strdup(json_object_get_string(ip_subnet));
        struct json_object* auth_key = json_object_object_get(json, "auth_key");
        if (auth_key)
            hg->hg_init_info.na_init_info.auth_key
                = strdup(json_object_get_string(auth_key));
        struct json_object* log_level
            = json_object_object_get(json, "log_level");
        if (log_level)
            hg->log_level = strdup(json_object_get_string(log_level));
        struct json_object* log_subsys
            = json_object_object_get(json, "log_subsys");
        if (log_subsys)
            hg->log_subsys = strdup(json_object_get_string(log_subsys));
#if (HG_VERSION_MAJOR > 2)       \
    || (HG_VERSION_MAJOR == 2    \
        && (HG_VERSION_MINOR > 1 \
            || (HG_VERSION_MINOR == 0 && HG_VERSION_PATH > 0)))
        // na_max_unexpected_size and na_max_expected_size available from
        // version 2.0.1
        hg->hg_init_info.na_init_info.max_unexpected_size
            = json_object_object_get_uint64_or(json, "na_max_unexpected_size",
                                               0);
        hg->hg_init_info.na_init_info.max_expected_size
            = json_object_object_get_uint64_or(json, "na_max_expected_size", 0);
#endif
#if (HG_VERSION_MAJOR > 2) || (HG_VERSION_MAJOR == 2 && HG_VERSION_MINOR > 0)
        // sm_info_string available from version 2.1.0
        struct json_object* sm_info_string
            = json_object_object_get(json, "sm_info_string");
        if (sm_info_string)
            hg->hg_init_info.sm_info_string
                = strdup(json_object_get_string(sm_info_string));
#endif
#if (HG_VERSION_MAJOR > 2) || (HG_VERSION_MAJOR == 2 && HG_VERSION_MINOR > 1)
        // na_request_mem_device available from version 2.2.0
        hg->hg_init_info.na_init_info.request_mem_device
            = json_object_object_get_bool_or(json, "na_request_mem_device",
                                             false);
        // checksum_level available from version 2.2.0
        struct json_object* checksum_level
            = json_object_object_get(json, "checksum_level");
        if (checksum_level) {
            const char* checksum_level_str
                = json_object_get_string(checksum_level);
            if (strcmp(checksum_level_str, "rpc_headers") == 0)
                hg->hg_init_info.checksum_level = HG_CHECKSUM_RPC_HEADERS;
            else if (strcmp(checksum_level_str, "rpc_payload") == 0)
                hg->hg_init_info.checksum_level = HG_CHECKSUM_RPC_PAYLOAD;
            else
                hg->hg_init_info.checksum_level = HG_CHECKSUM_NONE;
        }
        // na_addr_format available from version 2.2.0
        struct json_object* na_addr_format
            = json_object_object_get(json, "na_addr_format");
        if (na_addr_format) {
            const char* na_addr_format_str
                = json_object_get_string(na_addr_format);
            if (strcmp(na_addr_format_str, "ipv4") == 0)
                hg->hg_init_info.na_init_info.addr_format = NA_ADDR_IPV4;
            else if (strcmp(na_addr_format_str, "ipv6") == 0)
                hg->hg_init_info.na_init_info.addr_format = NA_ADDR_IPV6;
            else if (strcmp(na_addr_format_str, "native") == 0)
                hg->hg_init_info.na_init_info.addr_format = NA_ADDR_NATIVE;
            else
                hg->hg_init_info.na_init_info.addr_format = NA_ADDR_UNSPEC;
        }
#endif
    }

    if (user->hg_class && !user->hg_context) {
        if (user->hg_init_info) {
            margo_warning(0,
                          "Both custom hg_class and hg_init_info provided, the "
                          "latter will be ignored");
        }
        hg->hg_class = user->hg_class;
    } else {
        char* resolved_addr = NULL;
#ifdef HAVE_MOCHI_PLUMBER
        /* mochi-plumber is enabled.  Make a best effort to use it to
         * resolve the input address into a more specific NIC assignment.
         * Pass address through unmodified if it does not produce a result.
         */
        mochi_plumber_resolve_nic(user->protocol, plumber_bucket_policy,
                                  plumber_nic_policy, &resolved_addr);

#endif
        if (!resolved_addr)
            resolved_addr = (char*)user->protocol;
        else
            margo_debug(
                0,
                "mochi-plumber resolved %s to %s for Mercury initialization.",
                user->protocol, resolved_addr);
        hg->hg_class
            = HG_Init_opt(resolved_addr, user->listening, &(hg->hg_init_info));
        if (resolved_addr != user->protocol) free(resolved_addr);
        if (!hg->hg_class) {
            margo_error(0, "Could not initialize hg_class with protocol %s",
                        user->protocol);
            goto error;
        }
        hg->hg_ownership = MARGO_OWNS_HG_CLASS;
    }

    if (user->hg_context) {
        hg->hg_context = user->hg_context;
        hg->hg_class   = HG_Context_get_class(hg->hg_context);
        if (!hg->hg_class) {
            margo_error(0,
                        "Could not get hg_class from user-provided hg_context");
            goto error;
        }
        if (user->hg_class && (user->hg_class != hg->hg_class)) {
            margo_warning(0,
                          "Both custom hg_context and hg_class provided, the "
                          "latter will be ignored");
        }
    } else {
        hg->hg_context = HG_Context_create(hg->hg_class);
        if (!hg->hg_context) {
            margo_error(0, "Could not initialize hg_context");
            goto error;
        }
        hg->hg_ownership |= MARGO_OWNS_HG_CONTEXT;
    }

    hg_return_t hret = HG_Addr_self(hg->hg_class, &(hg->self_addr));
    if (hret != HG_SUCCESS) {
        margo_error(0, "Could not resolve self address");
        goto error;
    } else {
        hg_size_t buf_size = 0;
        hret = HG_Addr_to_string(hg->hg_class, NULL, &buf_size, hg->self_addr);
        if (hret != HG_SUCCESS && HG_Class_is_listening(hg->hg_class)) {
            margo_error(0, "Could not convert self address to string");
            hg->self_addr_str = NULL;
        } else {
            hg->self_addr_str = calloc(1, buf_size);
            HG_Addr_to_string(hg->hg_class, hg->self_addr_str, &buf_size,
                              hg->self_addr);
        }
    }

    /* Set HG log defaults. */
    /* Note that this is global and will affect any Mercury classes */
    if (!hg->log_level) hg->log_level = strdup("warning");
    HG_Set_log_level(hg->log_level);
    if (!hg->log_subsys) hg->log_subsys = strdup("hg,na");
    HG_Set_log_subsys(hg->log_subsys);

    return true;

error:
    __margo_hg_destroy(hg);
    return false;
}

struct json_object* __margo_hg_to_json(const margo_hg_t* hg)
{
    struct json_object* json = json_object_new_object();

    int flags = JSON_C_OBJECT_ADD_KEY_IS_NEW | JSON_C_OBJECT_ADD_CONSTANT_KEY;
    // version
    char         hg_version_string[64];
    unsigned int hg_major = 0, hg_minor = 0, hg_patch = 0;
    HG_Version_get(&hg_major, &hg_minor, &hg_patch);
    snprintf(hg_version_string, 64, "%u.%u.%u", hg_major, hg_minor, hg_patch);
    json_object_object_add_ex(json, "version",
                              json_object_new_string(hg_version_string), flags);

    // address
    if (hg->self_addr_str)
        json_object_object_add_ex(
            json, "address", json_object_new_string(hg->self_addr_str), flags);

    // listening
    hg_bool_t listening = HG_Class_is_listening(hg->hg_class);
    json_object_object_add_ex(json, "listening",
                              json_object_new_boolean(listening), flags);

    // input_eager_size
    hg_size_t input_eager_size = HG_Class_get_input_eager_size(hg->hg_class);
    json_object_object_add_ex(json, "input_eager_size",
                              json_object_new_uint64(input_eager_size), flags);

    // output_eager_size
    hg_size_t output_eager_size = HG_Class_get_output_eager_size(hg->hg_class);
    json_object_object_add_ex(json, "output_eager_size",
                              json_object_new_uint64(output_eager_size), flags);

    /* if margo doesn't own the hg_class, then hg_init_info
     * does not correspond to the way the hg_class was actually
     * initialized (need to wait for Mercury to allow us to
     * retrieve the hg_init_info from an hg_class). */
    if (!(hg->hg_ownership & MARGO_OWNS_HG_CLASS)) goto finish;

    // request_post_init
    json_object_object_add_ex(
        json, "request_post_init",
        json_object_new_uint64(hg->hg_init_info.request_post_init), flags);

    // request_post_incr
    json_object_object_add_ex(
        json, "request_post_incr",
        json_object_new_uint64(hg->hg_init_info.request_post_incr), flags);

    // auto_sm
    json_object_object_add_ex(json, "auto_sm",
                              json_object_new_boolean(hg->hg_init_info.auto_sm),
                              flags);

    // no_bulk_eager
    json_object_object_add_ex(
        json, "no_bulk_eager",
        json_object_new_boolean(hg->hg_init_info.no_bulk_eager), flags);

    // no_loopback
    json_object_object_add_ex(
        json, "no_loopback",
        json_object_new_boolean(hg->hg_init_info.no_loopback), flags);

    // stats
    json_object_object_add_ex(
        json, "stats", json_object_new_boolean(hg->hg_init_info.stats), flags);

    // na_no_block
    json_object_object_add_ex(
        json, "na_no_block",
        json_object_new_boolean(hg->hg_init_info.na_init_info.progress_mode
                                & NA_NO_BLOCK),
        flags);

    // na_no_retry
    json_object_object_add_ex(
        json, "na_no_retry",
        json_object_new_boolean(hg->hg_init_info.na_init_info.progress_mode
                                & NA_NO_RETRY),
        flags);

    // max_contexts
    json_object_object_add_ex(
        json, "max_contexts",
        json_object_new_uint64(hg->hg_init_info.na_init_info.max_contexts),
        flags);

    // ip_subnet
    if (hg->hg_init_info.na_init_info.ip_subnet)
        json_object_object_add_ex(
            json, "ip_subnet",
            json_object_new_string(hg->hg_init_info.na_init_info.ip_subnet),
            flags);

    // auth_key
    if (hg->hg_init_info.na_init_info.auth_key)
        json_object_object_add_ex(
            json, "auth_key",
            json_object_new_string(hg->hg_init_info.na_init_info.auth_key),
            flags);

    // log_level
    if (hg->log_level)
        json_object_object_add_ex(json, "log_level",
                                  json_object_new_string(hg->log_level), flags);

    // log_subsys
    if (hg->log_subsys)
        json_object_object_add_ex(
            json, "log_subsys", json_object_new_string(hg->log_subsys), flags);

#if (HG_VERSION_MAJOR > 2)       \
    || (HG_VERSION_MAJOR == 2    \
        && (HG_VERSION_MINOR > 1 \
            || (HG_VERSION_MINOR == 0 && HG_VERSION_PATH > 0)))
    // na_max_unexpected_size and na_max_expected_size available from
    // version 2.0.1
    json_object_object_add_ex(
        json, "na_max_unexpected_size",
        json_object_new_uint64(
            hg->hg_init_info.na_init_info.max_unexpected_size),
        flags);
    json_object_object_add_ex(
        json, "na_max_expected_size",
        json_object_new_uint64(hg->hg_init_info.na_init_info.max_expected_size),
        flags);
#endif
#if (HG_VERSION_MAJOR > 2) || (HG_VERSION_MAJOR == 2 && HG_VERSION_MINOR > 0)
    // sm_info_string available from version 2.1.0
    if (hg->hg_init_info.sm_info_string)
        json_object_object_add_ex(
            json, "sm_info_string",
            json_object_new_string(hg->hg_init_info.sm_info_string), flags);
#endif
#if (HG_VERSION_MAJOR > 2) || (HG_VERSION_MAJOR == 2 && HG_VERSION_MINOR > 1)
    // na_request_mem_device available from version 2.2.0
    json_object_object_add_ex(
        json, "na_request_mem_device",
        json_object_new_boolean(
            hg->hg_init_info.na_init_info.request_mem_device),
        flags);
    // checksum_level available from version 2.2.0
    const char* checksum_level_str[] = {"none", "rpc_headers", "rpc_payload"};
    json_object_object_add_ex(
        json, "checksum_level",
        json_object_new_string(
            checksum_level_str[hg->hg_init_info.checksum_level]),
        flags);
    // na_addr_format available from version 2.2.0
    const char* na_addr_format_str[] = {"unspec", "ipv4", "ipv6", "native"};
    json_object_object_add_ex(
        json, "na_addr_format",
        json_object_new_string(
            na_addr_format_str[hg->hg_init_info.na_init_info.addr_format]),
        flags);
#endif

finish:
    return json;
}

void __margo_hg_destroy(margo_hg_t* hg)
{
    free((char*)hg->hg_init_info.sm_info_string);
    hg->hg_init_info.sm_info_string = NULL;
    free((char*)hg->hg_init_info.na_init_info.auth_key);
    hg->hg_init_info.na_init_info.auth_key = NULL;
    free((char*)hg->hg_init_info.na_init_info.ip_subnet);
    hg->hg_init_info.na_init_info.ip_subnet = NULL;

    free(hg->self_addr_str);
    hg->self_addr_str = NULL;
    free(hg->log_level);
    hg->log_level = NULL;
    free(hg->log_subsys);
    hg->log_subsys = NULL;

    if (hg->hg_class && hg->self_addr != HG_ADDR_NULL)
        HG_Addr_free(hg->hg_class, hg->self_addr);

    if (hg->hg_context && (hg->hg_ownership & MARGO_OWNS_HG_CONTEXT)) {
        HG_Context_destroy(hg->hg_context);
        hg->hg_context = NULL;
    }

    if (hg->hg_class && (hg->hg_ownership & MARGO_OWNS_HG_CLASS)) {
        HG_Finalize(hg->hg_class);
        hg->hg_class = NULL;
    }

    memset(hg, 0, sizeof(*hg));
}
