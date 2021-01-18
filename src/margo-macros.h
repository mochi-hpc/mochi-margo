/*
 * (C) 2020 The University of Chicago
 *
 * See COPYRIGHT in top-level directory.
 */

#ifndef __MARGO_MACROS
#define __MARGO_MACROS

static const int json_type_int64 = json_type_int;

inline static struct json_object* json_object_copy(struct json_object* in)
{
    struct json_object* out = NULL;
    if (json_object_deep_copy(in, &out, NULL) != 0) return NULL;
    return out;
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
            MARGO_ERROR(0, "\"%s\" is in configuration but is not an object", \
                        __fullname);                                          \
            return -1;                                                        \
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
            MARGO_ERROR(0, "\"%s\" is in configuration but is not an array", \
                        __fullname);                                         \
            return -1;                                                       \
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
            MARGO_ERROR(0,                                                   \
                        "\"%s\" in configuration but has an incorrect type " \
                        "(expected %s)",                                     \
                        __fullname, #__type);                                \
            return -1;                                                       \
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
            MARGO_ERROR(0, "\"%s\" not found in configuration", __fullname);   \
            return -1;                                                         \
        }                                                                      \
        if (!json_object_is_type(__out, json_type_##__type)) {                 \
            MARGO_ERROR(                                                       \
                0, "\"%s\" in configuration has incorrect type (expected %s)", \
                __fullname, #__type);                                          \
            return -1;                                                         \
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
                MARGO_WARNING(0, "Overriding field \"%s\" with value \"%s\"", \
                              __fullname, __value);                           \
            else if (strcmp(json_object_get_string(_tmp), __value) != 0)      \
                MARGO_WARNING(                                                \
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
                MARGO_WARNING(0, "Overriding field \"%s\" with value \"%s\"", \
                              __field_name, __value ? "true" : "false");      \
            else if (json_object_get_boolean(_tmp) != !!__value)              \
                MARGO_WARNING(                                                \
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
                MARGO_WARNING(0, "Overriding field \"%s\" with value %d",      \
                              __field_name, (int)__value);                     \
            else if (json_object_get_int64(_tmp) != __value)                   \
                MARGO_WARNING(0, "Overriding field \"%s\" (%d) with value %d", \
                              __field_name, json_object_get_int64(_tmp),       \
                              __value);                                        \
        }                                                                      \
        json_object_object_add(__config, __key,                                \
                               json_object_new_int64(__value));                \
    } while (0)

// If the specified field is not positive or null, prints an error and returns
// -1.
#define CONFIG_INTEGER_MUST_BE_POSITIVE(__config, __key, __fullname)          \
    do {                                                                      \
        int _tmp                                                              \
            = json_object_get_int64(json_object_object_get(__config, __key)); \
        if (_tmp < 0) {                                                       \
            MARGO_ERROR(0, "\"%s\" must not be negative", __fullname);        \
            return -1;                                                        \
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
            MARGO_ERROR(0,                                                     \
                        "Could not find element named \"%s\" in \"%s\" array", \
                        __name, __array_name);                                 \
            return -1;                                                         \
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
            MARGO_ERROR(0, "Invalid enum value for \"%s\" (\"%s\")",     \
                        __field_name, json_object_get_string(__config)); \
            return -1;                                                   \
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
                if (json_object_equal(_a_name, _b_name)) {               \
                    MARGO_ERROR(0,                                       \
                                "Found two elements with the same name " \
                                "(\"%s\") in \"%s\"",                    \
                                json_object_get_string(_a_name),         \
                                __container_name);                       \
                    return -1;                                           \
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
            MARGO_ERROR(0, "Empty \"name\" field");                          \
            return -1;                                                       \
        }                                                                    \
        if (isdigit(_name[0])) {                                             \
            MARGO_ERROR(0, "First character of a name cannot be a digit");   \
            return -1;                                                       \
        }                                                                    \
        for (unsigned _i = 0; _i < _len; _i++) {                             \
            if (!(isalnum(_name[_i]) || _name[_i] == '_')) {                 \
                MARGO_ERROR(0,                                               \
                            "Invalid character \"%c\" found in name \"%s\"", \
                            _name[_i], _name);                               \
                return -1;                                                   \
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

#endif /* __MARGO_MACROS */
