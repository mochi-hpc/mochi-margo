/*
 * (C) 2020 The University of Chicago
 *
 * See COPYRIGHT in top-level directory.
 */

#ifndef __MARGO_MACROS
#define __MARGO_MACROS

#include <ctype.h>
#include <stdbool.h>
#include <json-c/json_c_version.h>
#include <json-c/json.h>

static const int json_type_int64 = json_type_int;

// json-c version is (major << 16) | (minor << 8) | (patch)
// 3584 corresponds to version 0.14.0
#if JSON_C_VERSION_NUM < 3584
static inline struct json_object* json_object_new_uint64(uint64_t x)
{
    return json_object_new_int64((int64_t)x);
}
#endif

// 3840 corresponds to version 0.14.0
#if JSON_C_VERSION_NUM < 3840
static inline struct json_object* json_object_new_array_ext(int initial_size)
{
    (void)initial_size;
    return json_object_new_array();
}
#endif

inline static struct json_object* json_object_copy(struct json_object* in)
{
    struct json_object* out = NULL;
    if (json_object_deep_copy(in, &out, NULL) != 0) return NULL;
    return out;
}

inline static uint64_t json_object_object_get_uint64_or(
    const struct json_object* object, const char* key, uint64_t x)
{
    struct json_object* value = json_object_object_get(object, key);
    if (value && json_object_is_type(value, json_type_int64)) {
        return json_object_get_uint64(value);
    } else {
        return x;
    }
}

inline static int64_t json_object_object_get_int64_or(
    const struct json_object* object, const char* key, int64_t x)
{
    struct json_object* value = json_object_object_get(object, key);
    if (value && json_object_is_type(value, json_type_int64)) {
        return json_object_get_int64(value);
    } else {
        return x;
    }
}

inline static bool json_object_object_get_bool_or(
    const struct json_object* object, const char* key, bool x)
{
    struct json_object* value = json_object_object_get(object, key);
    if (value && json_object_is_type(value, json_type_boolean)) {
        return json_object_get_boolean(value);
    } else {
        return x;
    }
}

inline static const char* json_object_object_get_string_or(
    const struct json_object* object, const char* key, const char* fallback)
{
    struct json_object* value = json_object_object_get(object, key);
    if (value && json_object_is_type(value, json_type_string)) {
        return json_object_get_string(value);
    } else {
        return fallback;
    }
}

#define json_array_foreach(__array, __index, __element)                \
    for (__index = 0;                                                  \
         __index < json_object_array_length(__array)                   \
         && (__element = json_object_array_get_idx(__array, __index)); \
         __index++)

// Can be used in configurations to check if a JSON object has a particular
// field. If it does, the __out parameter is set to that field.
#define CONFIG_HAS(__config, __key, __out) \
    ((__out = json_object_object_get(__config, __key)) != NULL)

// Checks if a JSON object has a particular key and its value is of type object.
// If the field does not exist, creates it with an empty object.
// If the field exists but is not of type object, prints an error and return -1.
// After a call to this macro, __out is set to the ceated/found field.
#define CONFIG_HAS_OR_CREATE_OBJECT(__config, __key, __fullname, __out)       \
    do {                                                                      \
        __out = json_object_object_get(__config, __key);                      \
        if (__out && !json_object_is_type(__out, json_type_object)) {         \
            margo_error(0, "\"%s\" is in configuration but is not an object", \
                        __fullname);                                          \
            HANDLE_CONFIG_ERROR;                                              \
        }                                                                     \
        if (!__out) {                                                         \
            __out = json_object_new_object();                                 \
            json_object_object_add(__config, __key, __out);                   \
        }                                                                     \
    } while (0)

// Checks if a JSON object has a particular key and its value is of type array.
// If the field does not exist, creates it with an empty array.
// If the field exists but is not of type object, prints an error and return -1.
// After a call to this macro, __out is set to the ceated/found field.
#define CONFIG_HAS_OR_CREATE_ARRAY(__config, __key, __fullname, __out)       \
    do {                                                                     \
        __out = json_object_object_get(__config, __key);                     \
        if (__out && !json_object_is_type(__out, json_type_array)) {         \
            margo_error(0, "\"%s\" is in configuration but is not an array", \
                        __fullname);                                         \
            HANDLE_CONFIG_ERROR;                                             \
        }                                                                    \
        if (!__out) {                                                        \
            __out = json_object_new_array();                                 \
            json_object_object_add(__config, __key, __out);                  \
        }                                                                    \
    } while (0)

// Checks if a JSON object has a particular key and its value is of the
// specified type (not array or object or null). If the field does not exist,
// creates it with the provided value.. If the field exists but is not of type
// object, prints an error and return -1. After a call to this macro, __out is
// set to the ceated/found field.
#define CONFIG_HAS_OR_CREATE(__config, __type, __key, __value, __fullname,   \
                             __out)                                          \
    do {                                                                     \
        __out = json_object_object_get(__config, __key);                     \
        if (__out && !json_object_is_type(__out, json_type_##__type)) {      \
            margo_error(0,                                                   \
                        "\"%s\" in configuration but has an incorrect type " \
                        "(expected %s)",                                     \
                        __fullname, #__type);                                \
            HANDLE_CONFIG_ERROR;                                             \
        }                                                                    \
        if (!__out) {                                                        \
            __out = json_object_new_##__type(__value);                       \
            json_object_object_add(__config, __key, __out);                  \
        }                                                                    \
    } while (0)

// Checks if a JSON object contains a field of a specified type. If the field is
// not found or if the type is incorrect, prints an error and returns -1.
// After a call to this macro, __out is set to the created/found field.
#define CONFIG_MUST_HAVE(__config, __type, __key, __fullname, __out)           \
    do {                                                                       \
        __out = json_object_object_get(__config, __key);                       \
        if (!__out) {                                                          \
            margo_error(0, "\"%s\" not found in configuration", __fullname);   \
            HANDLE_CONFIG_ERROR;                                               \
        }                                                                      \
        if (!json_object_is_type(__out, json_type_##__type)) {                 \
            margo_error(                                                       \
                0, "\"%s\" in configuration has incorrect type (expected %s)", \
                __fullname, #__type);                                          \
            HANDLE_CONFIG_ERROR;                                               \
        }                                                                      \
    } while (0)

// Overrides a field with a string. If the field already existed and was
// different from the new value, and __warning is true, prints a warning.
#define CONFIG_OVERRIDE_STRING(__config, __key, __value, __fullname,          \
                               __warning)                                     \
    do {                                                                      \
        struct json_object* _tmp = json_object_object_get(__config, __key);   \
        if (_tmp && __warning) {                                              \
            if (!json_object_is_type(_tmp, json_type_string))                 \
                margo_warning(0, "Overriding field \"%s\" with value \"%s\"", \
                              __fullname, __value);                           \
            else if (strcmp(json_object_get_string(_tmp), __value) != 0)      \
                margo_warning(                                                \
                    0, "Overriding field \"%s\" (\"%s\") with value \"%s\"",  \
                    __fullname, json_object_get_string(_tmp), __value);       \
        }                                                                     \
        _tmp = json_object_new_string(__value);                               \
        json_object_object_add(__config, __key, _tmp);                        \
    } while (0)

// Overrides a field with a boolean. If the field already existed and was
// different from the new value, and __warning is true, prints a warning.
#define CONFIG_OVERRIDE_BOOL(__config, __key, __value, __field_name,          \
                             __warning)                                       \
    do {                                                                      \
        struct json_object* _tmp = json_object_object_get(__config, __key);   \
        if (_tmp && __warning) {                                              \
            if (!json_object_is_type(_tmp, json_type_boolean))                \
                margo_warning(0, "Overriding field \"%s\" with value \"%s\"", \
                              __field_name, __value ? "true" : "false");      \
            else if (json_object_get_boolean(_tmp) != !!__value)              \
                margo_warning(                                                \
                    0, "Overriding field \"%s\" (\"%s\") with value \"%s\"",  \
                    __field_name,                                             \
                    json_object_get_boolean(_tmp) ? "true" : "false",         \
                    __value ? "true" : "false");                              \
        }                                                                     \
        json_object_object_add(__config, __key,                               \
                               json_object_new_boolean(__value));             \
    } while (0)

// Overrides a field with an integer. If the field already existed and was
// different from the new value, and __warning is true, prints a warning.
#define CONFIG_OVERRIDE_INTEGER(__config, __key, __value, __field_name,        \
                                __warning)                                     \
    do {                                                                       \
        struct json_object* _tmp = json_object_object_get(__config, __key);    \
        if (_tmp && __warning) {                                               \
            if (!json_object_is_type(_tmp, json_type_int))                     \
                margo_warning(0, "Overriding field \"%s\" with value %d",      \
                              __field_name, (int)__value);                     \
            else if (json_object_get_int64(_tmp) != (int64_t)(__value))        \
                margo_warning(0, "Overriding field \"%s\" (%d) with value %d", \
                              __field_name, json_object_get_int64(_tmp),       \
                              __value);                                        \
        }                                                                      \
        json_object_object_add(__config, __key,                                \
                               json_object_new_int64(__value));                \
    } while (0)

// If the specified field is not positive or null, prints an error and returns
// false.
#define CONFIG_INTEGER_MUST_BE_POSITIVE(__config, __key, __fullname)          \
    do {                                                                      \
        int _tmp                                                              \
            = json_object_get_int64(json_object_object_get(__config, __key)); \
        if (_tmp < 0) {                                                       \
            margo_error(0, "\"%s\" must not be negative", __fullname);        \
            HANDLE_CONFIG_ERROR;                                              \
        }                                                                     \
    } while (0)

// This macro takes a JSON array, a string name, and finds the first
// object in the array that has the specified name field, When found,
// it sets __index_out to the index of that object, and __item_out to
// the object itself. If now found, __index_out is set to -1 and
// __item_out is set to NULL.
#define CONFIG_FIND_BY_NAME(__array, __name, __index_out, __item_out) \
    do {                                                              \
        __index_out = -1;                                             \
        __item_out  = NULL;                                           \
        unsigned            _i;                                       \
        struct json_object* _item;                                    \
        json_array_foreach(__array, _i, _item)                        \
        {                                                             \
            struct json_object* _name_json                            \
                = json_object_object_get(_item, "name");              \
            if (!_name_json) continue;                                \
            const char* _name = json_object_get_string(_name_json);   \
            if (_name && strcmp(_name, __name) == 0) {                \
                __index_out = _i;                                     \
                __item_out  = _item;                                  \
                break;                                                \
            }                                                         \
        }                                                             \
    } while (0)

// Loop through the __config array in search for an element with a "name"
// field matching the provided name. If not found, prints an error and returns
// -1. If found __out is set to the found object.
#define CONFIG_ARRAY_MUST_HAVE_ITEM_NAMED(__config, __name, __array_name,      \
                                          __out)                               \
    do {                                                                       \
        unsigned _i;                                                           \
        __out       = NULL;                                                    \
        bool _found = 0;                                                       \
        json_array_foreach(__config, _i, __out)                                \
        {                                                                      \
            struct json_object* _name_json                                     \
                = json_object_object_get(__out, "name");                       \
            if (!_name_json) continue;                                         \
            const char* _name = json_object_get_string(_name_json);            \
            if (_name && strcmp(_name, __name) == 0) {                         \
                _found = 1;                                                    \
                break;                                                         \
            }                                                                  \
        }                                                                      \
        if (!_found) {                                                         \
            margo_error(0,                                                     \
                        "Could not find element named \"%s\" in \"%s\" array", \
                        __name, __array_name);                                 \
            HANDLE_CONFIG_ERROR;                                               \
        }                                                                      \
    } while (0)

// Checks if the provided JSON string is one of the provided string arguments.
// Prints an error and returns -1 if it does not match any.
#define CONFIG_IS_IN_ENUM_STRING(__config, __field_name, ...)            \
    do {                                                                 \
        unsigned    _i      = 0;                                         \
        const char* _vals[] = {__VA_ARGS__, NULL};                       \
        while (_vals[_i]                                                 \
               && strcmp(_vals[_i], json_object_get_string(__config)))   \
            _i++;                                                        \
        if (!_vals[_i]) {                                                \
            margo_error(0, "Invalid enum value for \"%s\" (\"%s\")",     \
                        __field_name, json_object_get_string(__config)); \
            HANDLE_CONFIG_ERROR;                                         \
        }                                                                \
    } while (0)

// Checks all the entries in the provided arrays and make sure their "name"
// fields are unique. If not, prints an error and returns -1.
#define CONFIG_NAMES_MUST_BE_UNIQUE(__array, __container_name)           \
    do {                                                                 \
        unsigned _len = json_object_array_length(__array);               \
        for (unsigned _i = 0; _i < _len; _i++) {                         \
            for (unsigned _j = 0; _j < _i; _j++) {                       \
                struct json_object* _a                                   \
                    = json_object_array_get_idx(__array, _i);            \
                struct json_object* _b                                   \
                    = json_object_array_get_idx(__array, _j);            \
                struct json_object* _a_name                              \
                    = json_object_object_get(_a, "name");                \
                struct json_object* _b_name                              \
                    = json_object_object_get(_b, "name");                \
                if (_a_name && _b_name                                   \
                    && json_object_equal(_a_name, _b_name)) {            \
                    margo_error(0,                                       \
                                "Found two elements with the same name " \
                                "(\"%s\") in \"%s\"",                    \
                                json_object_get_string(_a_name),         \
                                __container_name);                       \
                    HANDLE_CONFIG_ERROR;                                 \
                }                                                        \
            }                                                            \
        }                                                                \
    } while (0)

// Checks if the name of an object is valid. A valid name is a name that can be
// used as a C identifier.
#define CONFIG_NAME_IS_VALID(__obj)                                          \
    do {                                                                     \
        struct json_object* _name_json                                       \
            = json_object_object_get(__obj, "name");                         \
        const char* _name = json_object_get_string(_name_json);              \
        unsigned    _len  = strlen(_name);                                   \
        if (_len == 0) {                                                     \
            margo_error(0, "Empty \"name\" field");                          \
            HANDLE_CONFIG_ERROR;                                             \
        }                                                                    \
        if (isdigit(_name[0])) {                                             \
            margo_error(0, "First character of a name cannot be a digit");   \
            HANDLE_CONFIG_ERROR;                                             \
        }                                                                    \
        for (unsigned _i = 0; _i < _len; _i++) {                             \
            if (!(isalnum(_name[_i]) || _name[_i] == '_')) {                 \
                margo_error(0,                                               \
                            "Invalid character \"%c\" found in name \"%s\"", \
                            _name[_i], _name);                               \
                HANDLE_CONFIG_ERROR;                                         \
            }                                                                \
        }                                                                    \
    } while (0)

// Adds a new pool in the provided array.
#define CONFIG_ADD_NEW_POOL(__pools, __name, __kind, __access)              \
    do {                                                                    \
        struct json_object* _p = json_object_new_object();                  \
        json_object_object_add(_p, "name", json_object_new_string(__name)); \
        json_object_object_add(_p, "kind", json_object_new_string(__kind)); \
        json_object_object_add(_p, "access",                                \
                               json_object_new_string(__access));           \
        json_object_array_add(__pools, _p);                                 \
    } while (0)

// Adds a new xstream in the provided array. The variadic argument corresponds
// to pool indices to add to the xstream's scheduler.
#define CONFIG_ADD_NEW_XSTREAM(__xstreams, __name, __sched_predef, ...)     \
    do {                                                                    \
        struct json_object* _x = json_object_new_object();                  \
        json_object_object_add(_x, "name", json_object_new_string(__name)); \
        json_object_object_add(_x, "cpubind", json_object_new_int64(-1));   \
        json_object_object_add(_x, "affinity", json_object_new_array());    \
        int                 _pool_index[] = {__VA_ARGS__, -1};              \
        struct json_object* _s            = json_object_new_object();       \
        json_object_object_add(_s, "type",                                  \
                               json_object_new_string(__sched_predef));     \
        struct json_object* _s_pools = json_object_new_array();             \
        unsigned            _i       = 0;                                   \
        while (_pool_index[_i] != -1) {                                     \
            json_object_array_add(_s_pools,                                 \
                                  json_object_new_int64(_pool_index[_i]));  \
            _i++;                                                           \
        }                                                                   \
        json_object_object_add(_s, "pools", _s_pools);                      \
        json_object_object_add(_x, "scheduler", _s);                        \
        json_object_array_add(__xstreams, _x);                              \
    } while (0)

#define ASSERT_CONFIG_HAS_REQUIRED(__config__, __key__, __type__, __ctx__) \
    do {                                                                   \
        struct json_object* __key__                                        \
            = json_object_object_get(__config__, #__key__);                \
        if (!__key__) {                                                    \
            margo_error(0, "\"" #__key__ "\" not found in " #__ctx__       \
                           " configuration");                              \
            HANDLE_CONFIG_ERROR;                                           \
        }                                                                  \
        if (!json_object_is_type(__key__, json_type_##__type__)) {         \
            margo_error(0, "Invalid type for \"" #__key__ " in " #__ctx__  \
                           " configuration (expected " #__type__ ")");     \
        }                                                                  \
    } while (0)

#define ASSERT_CONFIG_HAS_OPTIONAL(__config__, __key__, __type__, __ctx__)    \
    do {                                                                      \
        struct json_object* __key__                                           \
            = json_object_object_get(__config__, #__key__);                   \
        if (__key__ && !json_object_is_type(__key__, json_type_##__type__)) { \
            margo_error(0, "Invalid type for \"" #__key__ " in " #__ctx__     \
                           " configuration (expected " #__type__ ")");        \
            HANDLE_CONFIG_ERROR;                                              \
        }                                                                     \
    } while (0)

#endif /* __MARGO_MACROS */
