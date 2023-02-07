/*
 * (C) 2020 The University of Chicago
 *
 * See COPYRIGHT in top-level directory.
 */
#include <stdio.h>
#include <margo.h>
#include <unistd.h>
#include <json-c/json.h>
#include <mercury_proc_string.h>
#include "munit/munit.h"
#include "munit/munit-goto.h"

struct call_info {
    uint64_t fn_start;
    uint64_t fn_end;
};

struct test_monitor_data {
    struct call_info call_count[MARGO_MONITOR_MAX];
};

#define X(__x__, __y__) \
    static void test_monitor_on_##__y__( \
        void* uargs, double timestamp,  margo_monitor_event_t event_type, \
        margo_monitor_##__y__##_args_t event_args) \
    { \
        (void)timestamp; \
        (void)event_args; \
        struct test_monitor_data* monitor_data = (struct test_monitor_data*)uargs; \
        if(event_type == MARGO_MONITOR_FN_START) { \
            monitor_data->call_count[MARGO_MONITOR_ON_##__x__].fn_start += 1; \
        } else { \
            monitor_data->call_count[MARGO_MONITOR_ON_##__x__].fn_end += 1; \
        } \
    }

MARGO_EXPAND_MONITOR_MACROS
#undef X

static void* test_monitor_initialize(margo_instance_id    mid,
                                      void*               uargs,
                                      struct json_object* config)
{
    (void)mid;
    (void)config;
    return uargs;
}

static void test_monitor_finalize(void* uargs)
{
    (void)uargs;
}

MERCURY_GEN_PROC(echo_in_t,
    ((hg_bool_t)(relay))\
    ((hg_string_t)(str))\
    ((hg_bulk_t)(blk)))

DECLARE_MARGO_RPC_HANDLER(echo_ult)
static void echo_ult(hg_handle_t handle)
{
    margo_instance_id mid = margo_hg_handle_get_instance(handle);
    munit_assert_not_null(mid);

    const struct hg_info* info = margo_get_info(handle);
    munit_assert_not_null(info);

    echo_in_t   input;
    hg_return_t hret = HG_SUCCESS;

    hret = margo_get_input(handle, &input);
    munit_assert_int(hret, ==, HG_SUCCESS);

    if(input.relay == HG_TRUE) {

        // resend the same RPC to myself with relay = FALSE
        // to get an entry with a parent callpath in profiling

        hg_handle_t relay_handle;
        hret = margo_create(mid, info->addr, info->id, &relay_handle);
        munit_assert_int(hret, ==, HG_SUCCESS);
        input.relay = HG_FALSE;

        uint16_t provider_id = info->id & ((1 << (__MARGO_PROVIDER_ID_SIZE * 8)) - 1);

        hret = margo_provider_forward(provider_id, relay_handle, &input);
        munit_assert_int(hret, ==, HG_SUCCESS);

        char* relay_out = NULL;
        hret = margo_get_output(relay_handle, &relay_out);
        munit_assert_int(hret, ==, HG_SUCCESS);

        hret = margo_free_output(relay_handle, &relay_out);
        munit_assert_int(hret, ==, HG_SUCCESS);

        hret = margo_destroy(relay_handle);
        munit_assert_int(hret, ==, HG_SUCCESS);
    }

    char      buffer[256];
    void*     ptrs[1]  = { (void*)buffer };
    hg_size_t sizes[1] = { 256 };
    hg_bulk_t bulk     = HG_BULK_NULL;
    hret = margo_bulk_create(mid, 1, ptrs, sizes, HG_BULK_WRITE_ONLY, &bulk);
    munit_assert_int(hret, ==, HG_SUCCESS);

    hret = margo_bulk_transfer(mid, HG_BULK_PULL, info->addr, input.blk, 0, bulk, 0, 256);
    munit_assert_int(hret, ==, HG_SUCCESS);

    hret = margo_bulk_free(bulk);
    munit_assert_int(hret, ==, HG_SUCCESS);

    hret = margo_respond(handle, &input.str);
    munit_assert_int(hret, ==, HG_SUCCESS);

    hret = margo_free_input(handle, &input);
    munit_assert_int(hret, ==, HG_SUCCESS);

    hret = margo_destroy(handle);
    munit_assert_int(hret, ==, HG_SUCCESS);
}
DEFINE_MARGO_RPC_HANDLER(echo_ult)

DECLARE_MARGO_RPC_HANDLER(custom_echo_ult)
static void custom_echo_ult(hg_handle_t handle)
{
    margo_instance_id mid = margo_hg_handle_get_instance(handle);
    munit_assert_not_null(mid);

    const struct hg_info* info = margo_get_info(handle);
    struct test_monitor_data* monitor_data =
        (struct test_monitor_data*)margo_registered_data(mid, info->id);
    munit_assert_not_null(mid);

    munit_assert_int(monitor_data->call_count[MARGO_MONITOR_ON_RPC_HANDLER].fn_start, ==, 1);
    munit_assert_int(monitor_data->call_count[MARGO_MONITOR_ON_RPC_HANDLER].fn_end, ==, 1);
    munit_assert_int(monitor_data->call_count[MARGO_MONITOR_ON_RPC_ULT].fn_start, ==, 1);
    munit_assert_int(monitor_data->call_count[MARGO_MONITOR_ON_RPC_ULT].fn_end, ==, 0);

    echo_in_t   input;
    hg_return_t hret = HG_SUCCESS;

    hret = margo_get_input(handle, &input);
    munit_assert_int(hret, ==, HG_SUCCESS);
    munit_assert_int(monitor_data->call_count[MARGO_MONITOR_ON_GET_INPUT].fn_start, ==, 1);
    munit_assert_int(monitor_data->call_count[MARGO_MONITOR_ON_GET_INPUT].fn_end, ==, 1);

    char      buffer[256];
    void*     ptrs[1]  = { (void*)buffer };
    hg_size_t sizes[1] = { 256 };
    hg_bulk_t bulk     = HG_BULK_NULL;
    hret = margo_bulk_create(mid, 1, ptrs, sizes, HG_BULK_WRITE_ONLY, &bulk);
    munit_assert_int(hret, ==, HG_SUCCESS);
    munit_assert_int(monitor_data->call_count[MARGO_MONITOR_ON_BULK_CREATE].fn_start, ==, 2);
    munit_assert_int(monitor_data->call_count[MARGO_MONITOR_ON_BULK_CREATE].fn_end, ==, 2);

    hret = margo_bulk_transfer(mid, HG_BULK_PULL, info->addr, input.blk, 0, bulk, 0, 256);
    munit_assert_int(hret, ==, HG_SUCCESS);
    munit_assert_int(monitor_data->call_count[MARGO_MONITOR_ON_BULK_TRANSFER].fn_start, ==, 1);
    munit_assert_int(monitor_data->call_count[MARGO_MONITOR_ON_BULK_TRANSFER].fn_end, ==, 1);
    /* there is a wait that also started in the caller */
    munit_assert_int(monitor_data->call_count[MARGO_MONITOR_ON_WAIT].fn_start, ==, 2);
    munit_assert_int(monitor_data->call_count[MARGO_MONITOR_ON_WAIT].fn_end, ==, 1);
    munit_assert_int(monitor_data->call_count[MARGO_MONITOR_ON_BULK_TRANSFER_CB].fn_start, ==, 1);
    munit_assert_int(monitor_data->call_count[MARGO_MONITOR_ON_BULK_TRANSFER_CB].fn_end, ==, 1);

    hret = margo_bulk_free(bulk);
    munit_assert_int(hret, ==, HG_SUCCESS);
    /* for now bulk_free is not called because there is no way to retrieve the
     * margo instance in margo_bulk_free */
    munit_assert_int(monitor_data->call_count[MARGO_MONITOR_ON_BULK_FREE].fn_start, ==, 0);
    munit_assert_int(monitor_data->call_count[MARGO_MONITOR_ON_BULK_FREE].fn_end, ==, 0);

    hret = margo_respond(handle, &input.str);
    munit_assert_int(hret, ==, HG_SUCCESS);
    munit_assert_int(monitor_data->call_count[MARGO_MONITOR_ON_RESPOND].fn_start, ==, 1);
    munit_assert_int(monitor_data->call_count[MARGO_MONITOR_ON_RESPOND].fn_end, ==, 1);
    munit_assert_int(monitor_data->call_count[MARGO_MONITOR_ON_SET_OUTPUT].fn_start, ==, 1);
    munit_assert_int(monitor_data->call_count[MARGO_MONITOR_ON_SET_OUTPUT].fn_end, ==, 1);
    munit_assert_int(monitor_data->call_count[MARGO_MONITOR_ON_WAIT].fn_start, ==, 3);
    munit_assert_int(monitor_data->call_count[MARGO_MONITOR_ON_WAIT].fn_end, ==, 2);
    munit_assert_int(monitor_data->call_count[MARGO_MONITOR_ON_RESPOND_CB].fn_start, ==, 1);
    munit_assert_int(monitor_data->call_count[MARGO_MONITOR_ON_RESPOND_CB].fn_end, ==, 1);

    hret = margo_free_input(handle, &input);
    munit_assert_int(hret, ==, HG_SUCCESS);
    munit_assert_int(monitor_data->call_count[MARGO_MONITOR_ON_FREE_INPUT].fn_start, ==, 1);
    munit_assert_int(monitor_data->call_count[MARGO_MONITOR_ON_FREE_INPUT].fn_end, ==, 1);

    hret = margo_destroy(handle);
    munit_assert_int(hret, ==, HG_SUCCESS);
    /* final destruction of the handle will actually happen
     * in the code generated by DEFINE_MARGO_RPC_HANDLER */
    munit_assert_int(monitor_data->call_count[MARGO_MONITOR_ON_DESTROY].fn_start, ==, 0);
    munit_assert_int(monitor_data->call_count[MARGO_MONITOR_ON_DESTROY].fn_end, ==, 0);
}
DEFINE_MARGO_RPC_HANDLER(custom_echo_ult)

static void* test_context_setup(const MunitParameter params[], void* user_data)
{
    (void)params;
    (void)user_data;
    return NULL;
}

static void test_context_tear_down(void* fixture)
{
    (void)fixture;
}

static MunitResult test_custom_monitoring(const MunitParameter params[],
                                          void*                data)
{
    (void)data;
    hg_return_t hret                      = HG_SUCCESS;
    struct test_monitor_data monitor_data = {0};

    struct margo_monitor custom_monitor = {
        .uargs      = (void*)&monitor_data,
        .initialize = test_monitor_initialize,
        .finalize   = test_monitor_finalize,
#define X(__x__, __y__) .on_##__y__ = test_monitor_on_##__y__,
       MARGO_EXPAND_MONITOR_MACROS
#undef X
    };

    const char* protocol = munit_parameters_get(params, "protocol");
    struct margo_init_info init_info = {
        .json_config   = NULL,
        .progress_pool = ABT_POOL_NULL,
        .rpc_pool      = ABT_POOL_NULL,
        .hg_class      = NULL,
        .hg_context    = NULL,
        .hg_init_info  = NULL,
        .logger        = NULL,
        .monitor       = &custom_monitor
    };
    margo_instance_id mid = margo_init_ext(protocol, MARGO_SERVER_MODE, &init_info);
    munit_assert_not_null(mid);

    hg_id_t echo_id = MARGO_REGISTER(mid, "custom_echo", echo_in_t, hg_string_t, custom_echo_ult);
    munit_assert_int(echo_id, !=, 0);
    /* note: because of the __shutdown__ RPC, the count will be at 2 */
    munit_assert_int(monitor_data.call_count[MARGO_MONITOR_ON_REGISTER].fn_start, ==, 2);
    munit_assert_int(monitor_data.call_count[MARGO_MONITOR_ON_REGISTER].fn_end, ==, 2);

    margo_thread_sleep(mid, 1);
    munit_assert_int(monitor_data.call_count[MARGO_MONITOR_ON_SLEEP].fn_start, ==, 1);
    munit_assert_int(monitor_data.call_count[MARGO_MONITOR_ON_SLEEP].fn_end, ==, 1);

    hret = margo_register_data(mid, echo_id, &monitor_data, NULL);
    munit_assert_int(hret, ==, HG_SUCCESS);

    char      buffer[256];
    void*     ptrs[1]  = { (void*)buffer };
    hg_size_t sizes[1] = { 256 };
    hg_bulk_t bulk     = HG_BULK_NULL;
    hret = margo_bulk_create(mid, 1, ptrs, sizes, HG_BULK_READ_ONLY, &bulk);
    munit_assert_int(hret, ==, HG_SUCCESS);
    munit_assert_int(monitor_data.call_count[MARGO_MONITOR_ON_BULK_CREATE].fn_start, ==, 1);
    munit_assert_int(monitor_data.call_count[MARGO_MONITOR_ON_BULK_CREATE].fn_end, ==, 1);

    const char* input  = "hello world";
    echo_in_t in = {
        .relay = HG_FALSE,
        .str = (char*)input,
        .blk = bulk
    };
    hg_addr_t addr     = HG_ADDR_NULL;
    hg_handle_t handle = HG_HANDLE_NULL;

    hret = margo_addr_self(mid, &addr);
    munit_assert_int(hret, ==, HG_SUCCESS);
    munit_assert_int(monitor_data.call_count[MARGO_MONITOR_ON_LOOKUP].fn_start, ==, 1);
    munit_assert_int(monitor_data.call_count[MARGO_MONITOR_ON_LOOKUP].fn_end, ==, 1);

    hret = margo_create(mid, addr, echo_id, &handle);
    munit_assert_int(hret, ==, HG_SUCCESS);
    munit_assert_int(monitor_data.call_count[MARGO_MONITOR_ON_CREATE].fn_start, ==, 1);
    munit_assert_int(monitor_data.call_count[MARGO_MONITOR_ON_CREATE].fn_end, ==, 1);

    hret = margo_forward(handle, &in);
    munit_assert_int(hret, ==, HG_SUCCESS);
    munit_assert_int(monitor_data.call_count[MARGO_MONITOR_ON_FORWARD].fn_start, ==, 1);
    munit_assert_int(monitor_data.call_count[MARGO_MONITOR_ON_FORWARD].fn_end, ==, 1);
    munit_assert_int(monitor_data.call_count[MARGO_MONITOR_ON_SET_INPUT].fn_start, ==, 1);
    munit_assert_int(monitor_data.call_count[MARGO_MONITOR_ON_SET_INPUT].fn_end, ==, 1);
    munit_assert_int(monitor_data.call_count[MARGO_MONITOR_ON_WAIT].fn_start, ==, 3);
    munit_assert_int(monitor_data.call_count[MARGO_MONITOR_ON_WAIT].fn_end, ==, 3);
    munit_assert_int(monitor_data.call_count[MARGO_MONITOR_ON_FORWARD_CB].fn_start, ==, 1);
    munit_assert_int(monitor_data.call_count[MARGO_MONITOR_ON_FORWARD_CB].fn_end, ==, 1);

    hret = margo_bulk_free(bulk);
    munit_assert_int(hret, ==, HG_SUCCESS);
    /* for now bulk_free is not called because there is no way to retrieve the
     * margo instance in margo_bulk_free */
    munit_assert_int(monitor_data.call_count[MARGO_MONITOR_ON_BULK_FREE].fn_start, ==, 0);
    munit_assert_int(monitor_data.call_count[MARGO_MONITOR_ON_BULK_FREE].fn_end, ==, 0);

    char* output = NULL;
    hret = margo_get_output(handle, &output);
    munit_assert_int(hret, ==, HG_SUCCESS);
    munit_assert_int(monitor_data.call_count[MARGO_MONITOR_ON_GET_OUTPUT].fn_start, ==, 1);
    munit_assert_int(monitor_data.call_count[MARGO_MONITOR_ON_GET_OUTPUT].fn_end, ==, 1);

    hret = margo_free_output(handle, &output);
    munit_assert_int(hret, ==, HG_SUCCESS);
    munit_assert_int(monitor_data.call_count[MARGO_MONITOR_ON_FREE_OUTPUT].fn_start, ==, 1);
    munit_assert_int(monitor_data.call_count[MARGO_MONITOR_ON_FREE_OUTPUT].fn_end, ==, 1);

    hret = margo_destroy(handle);
    munit_assert_int(hret, ==, HG_SUCCESS);
    /* note: because the server is on the same process, the same handle is passed to
     * the RPC ULT, so there is only 1 handle alive in this program */
    munit_assert_int(monitor_data.call_count[MARGO_MONITOR_ON_DESTROY].fn_start, ==, 1);
    munit_assert_int(monitor_data.call_count[MARGO_MONITOR_ON_DESTROY].fn_end, ==, 1);

    hret = margo_addr_free(mid, addr);
    munit_assert_int(hret, ==, HG_SUCCESS);

    hret = margo_deregister(mid, echo_id);
    munit_assert_int(hret, ==, HG_SUCCESS);
    munit_assert_int(monitor_data.call_count[MARGO_MONITOR_ON_DEREGISTER].fn_start, ==, 1);
    munit_assert_int(monitor_data.call_count[MARGO_MONITOR_ON_DEREGISTER].fn_end, ==, 1);

    hret = margo_monitor_call_user(mid, MARGO_MONITOR_FN_START, NULL);
    munit_assert_int(hret, ==, HG_SUCCESS);
    munit_assert_int(monitor_data.call_count[MARGO_MONITOR_ON_USER].fn_start, ==, 1);

    margo_finalize(mid);
    munit_assert_int(monitor_data.call_count[MARGO_MONITOR_ON_PREFINALIZE].fn_start, ==, 1);
    munit_assert_int(monitor_data.call_count[MARGO_MONITOR_ON_PREFINALIZE].fn_end, ==, 1);
    munit_assert_int(monitor_data.call_count[MARGO_MONITOR_ON_FINALIZE].fn_start, ==, 1);
    munit_assert_int(monitor_data.call_count[MARGO_MONITOR_ON_FINALIZE].fn_end, ==, 1);

    munit_assert_int(monitor_data.call_count[MARGO_MONITOR_ON_PROGRESS].fn_start, >, 0);
    munit_assert_int(monitor_data.call_count[MARGO_MONITOR_ON_PROGRESS].fn_end, >, 0);
    munit_assert_int(monitor_data.call_count[MARGO_MONITOR_ON_TRIGGER].fn_start, >, 0);
    munit_assert_int(monitor_data.call_count[MARGO_MONITOR_ON_TRIGGER].fn_end, >, 0);

    return MUNIT_OK;
}

static MunitResult test_default_monitoring_statistics(const MunitParameter params[],
                                                      void*                data)
{
    (void)data;
    hg_return_t hret                      = HG_SUCCESS;
    const char* protocol = munit_parameters_get(params, "protocol");
    uint16_t provider_id_param = atoi(munit_parameters_get(params, "provider_id"));
    hg_bool_t relay = strcmp(munit_parameters_get(params, "relay"), "true") == 0 ? HG_TRUE : HG_FALSE;
    const char* json_config =
        "{\"monitoring\":{"
            "\"config\":{"
                "\"filename_prefix\":\"test\","
                "\"statistics\":{\"precision\":9, \"disable\":false,\"pretty_json\":true},"
                "\"time_series\":{\"disable\":true}"
            "}"
        "}}";
    struct margo_init_info init_info = {
        .json_config   = json_config,
        .progress_pool = ABT_POOL_NULL,
        .rpc_pool      = ABT_POOL_NULL,
        .hg_class      = NULL,
        .hg_context    = NULL,
        .hg_init_info  = NULL,
        .logger        = NULL,
        .monitor       = margo_default_monitor
    };
    margo_instance_id mid = margo_init_ext(protocol, MARGO_SERVER_MODE, &init_info);
    munit_assert_not_null(mid);

    /* self_addr is used later to check the content of the JSON output */
    char self_addr_str[256];
    hg_size_t self_addr_size = 256;
    hg_addr_t self_addr = HG_ADDR_NULL;
    margo_addr_self(mid, &self_addr);
    margo_addr_to_string(mid, self_addr_str, &self_addr_size, self_addr);
    margo_addr_free(mid, self_addr);

    hg_id_t echo_id = MARGO_REGISTER_PROVIDER(
        mid, "echo", echo_in_t, hg_string_t, echo_ult,
        provider_id_param, ABT_POOL_NULL);
    munit_assert_int(echo_id, !=, 0);

    margo_thread_sleep(mid, 1);

    char      buffer[256];
    void*     ptrs[1]  = { (void*)buffer };
    hg_size_t sizes[1] = { 256 };
    hg_bulk_t bulk     = HG_BULK_NULL;
    hret = margo_bulk_create(mid, 1, ptrs, sizes, HG_BULK_READ_ONLY, &bulk);
    munit_assert_int(hret, ==, HG_SUCCESS);

    const char* input  = "hello world";
    echo_in_t in = {
        .relay = relay,
        .str = (char*)input,
        .blk = bulk
    };
    hg_addr_t addr     = HG_ADDR_NULL;
    hg_handle_t handle = HG_HANDLE_NULL;

    hret = margo_addr_self(mid, &addr);
    munit_assert_int(hret, ==, HG_SUCCESS);

    hret = margo_create(mid, addr, echo_id, &handle);
    munit_assert_int(hret, ==, HG_SUCCESS);

    hret = margo_provider_forward(provider_id_param, handle, &in);
    munit_assert_int(hret, ==, HG_SUCCESS);

    hret = margo_bulk_free(bulk);
    munit_assert_int(hret, ==, HG_SUCCESS);

    char* output = NULL;
    hret = margo_get_output(handle, &output);
    munit_assert_int(hret, ==, HG_SUCCESS);

    hret = margo_free_output(handle, &output);
    munit_assert_int(hret, ==, HG_SUCCESS);

    hret = margo_destroy(handle);
    munit_assert_int(hret, ==, HG_SUCCESS);

    hret = margo_addr_free(mid, addr);
    munit_assert_int(hret, ==, HG_SUCCESS);

    hret = margo_deregister(mid, echo_id);
    munit_assert_int(hret, ==, HG_SUCCESS);

    hret = margo_monitor_call_user(mid, MARGO_MONITOR_FN_START, NULL);
    munit_assert_int(hret, ==, HG_SUCCESS);

    margo_finalize(mid);

    char* filename = NULL;
    {
        char hostname[1024];
        hostname[1023] = '\0';
        gethostname(hostname, 1023);
        pid_t pid = getpid();
        size_t fullname_size = snprintf(NULL, 0, "test.%s.%d.stats.json", hostname, pid);
        filename = calloc(1, fullname_size+1);
        sprintf(filename, "test.%s.%d.stats.json", hostname, pid);
    }
    munit_assert_int(access(filename, F_OK), ==, 0);

    FILE* file = fopen(filename, "r");
    munit_assert_not_null(file);
    fseek(file, 0L, SEEK_END);
    long int file_size = ftell(file);
    fseek(file, 0L, SEEK_SET);
    munit_assert_int(file_size, >, 0);
    char* file_content = malloc(file_size);
    munit_assert_int(file_size, ==, fread(file_content, 1, file_size, file));
    fclose(file);

    struct json_object* json_content = NULL;
    struct json_tokener* tokener     = json_tokener_new();
    json_content = json_tokener_parse_ex(tokener, file_content, file_size);
    json_tokener_free(tokener);
    munit_assert_not_null(json_content);

    munit_assert(json_object_is_type(json_content, json_type_object));

#define ASSERT_JSON_HAS_KEY(parent, key_name, key_obj, type) \
    struct json_object* key_obj = json_object_object_get(parent, key_name); \
    munit_assert_not_null(key_obj); \
    munit_assert(json_object_is_type(key_obj, json_type_##type))

#define ASSERT_JSON_HAS(parent, key, type) ASSERT_JSON_HAS_KEY(parent, #key, key, type)

#define ASSERT_JSON_HAS_STATS(parent, key) \
    do { \
        ASSERT_JSON_HAS(parent, key, object); \
        ASSERT_JSON_HAS(key, num, int); \
        ASSERT_JSON_HAS(key, min, double); \
        ASSERT_JSON_HAS(key, max, double); \
        ASSERT_JSON_HAS(key, avg, double); \
        ASSERT_JSON_HAS(key, var, double); \
        ASSERT_JSON_HAS(key, sum, double); \
    } while(0)

#define ASSERT_JSON_HAS_DOUBLE_STATS(parent, key, secondary) \
    do { \
        ASSERT_JSON_HAS(parent, key, object); \
        ASSERT_JSON_HAS_STATS(key, duration); \
        ASSERT_JSON_HAS_STATS(key, secondary); \
    } while(0)

    {
        // check for the "progress_loop" section
        ASSERT_JSON_HAS(json_content, progress_loop, object);
        ASSERT_JSON_HAS_STATS(progress_loop, progress_with_timeout);
        ASSERT_JSON_HAS_STATS(progress_loop, progress_timeout_value_msec);
        ASSERT_JSON_HAS_STATS(progress_loop, progress_without_timeout);
        ASSERT_JSON_HAS_STATS(progress_loop, trigger);

        // check for the "rpcs" secions
        ASSERT_JSON_HAS(json_content, rpcs, object);

        char echo_key[256];
        char addr_key[512];
        sprintf(echo_key, "65535:65535:3747772018183438335:%d", provider_id_param);

        // must have an "65535:65535:3747772018183438335:provider_id" secion for the echo RPC
        ASSERT_JSON_HAS_KEY(rpcs, echo_key, echo, object);
        {
            // check RPC info
            ASSERT_JSON_HAS(echo, rpc_id, int);
            munit_assert_long(3747772018183438335, ==, json_object_get_int64(rpc_id));
            ASSERT_JSON_HAS(echo, parent_rpc_id, int);
            munit_assert_long(65535, ==, json_object_get_int64(parent_rpc_id));
            ASSERT_JSON_HAS(echo, provider_id, int);
            munit_assert_long(provider_id_param, ==, json_object_get_int64(provider_id));
            ASSERT_JSON_HAS(echo, parent_provider_id, int);
            munit_assert_long(65535, ==, json_object_get_int64(parent_provider_id));
            ASSERT_JSON_HAS(echo, name, string);
            munit_assert_string_equal(json_object_get_string(name), "echo");
            // RPC must have an "origin" section
            ASSERT_JSON_HAS(echo, origin, object);
            {
                // "origin" section must have an object corresponding to the address
                sprintf(addr_key, "sent to %s", self_addr_str);
                ASSERT_JSON_HAS_KEY(origin, addr_key, sent_to, object);
                ASSERT_JSON_HAS_DOUBLE_STATS(sent_to, iforward, relative_timestamp_from_create);
                ASSERT_JSON_HAS_DOUBLE_STATS(sent_to, forward_cb, relative_timestamp_from_iforward_start);
                ASSERT_JSON_HAS_DOUBLE_STATS(sent_to, iforward_wait, relative_timestamp_from_iforward_end);
                ASSERT_JSON_HAS_DOUBLE_STATS(sent_to, set_input, relative_timestamp_from_iforward_start);
                ASSERT_JSON_HAS_DOUBLE_STATS(sent_to, get_output, relative_timestamp_from_wait_end);
            }
            // RPC must have an "target" section
            ASSERT_JSON_HAS(echo, target, object);
            {
                // "target" section must have an object corresponding to the source address
                sprintf(addr_key, "received from %s", self_addr_str);
                ASSERT_JSON_HAS_KEY(target, addr_key, received_from, object);
                ASSERT_JSON_HAS(received_from, handler, object);
                ASSERT_JSON_HAS_STATS(handler, duration);
                ASSERT_JSON_HAS_DOUBLE_STATS(received_from, ult, relative_timestamp_from_handler_start);
                ASSERT_JSON_HAS_DOUBLE_STATS(received_from, irespond, relative_timestamp_from_ult_start);
                ASSERT_JSON_HAS_DOUBLE_STATS(received_from, respond_cb, relative_timestamp_from_irespond_start);
                ASSERT_JSON_HAS_DOUBLE_STATS(received_from, irespond_wait, relative_timestamp_from_irespond_end);
                ASSERT_JSON_HAS_DOUBLE_STATS(received_from, set_output, relative_timestamp_from_irespond_start);
                ASSERT_JSON_HAS_DOUBLE_STATS(received_from, get_input, relative_timestamp_from_ult_start);
                // "received from ..." section must have a "bulk" section
                ASSERT_JSON_HAS(received_from, bulk, object);
                // "bulk" section must have a "create" section
                ASSERT_JSON_HAS(bulk, create, object);
                ASSERT_JSON_HAS_STATS(create, duration);
                ASSERT_JSON_HAS_STATS(create, size);
                // "bulk" section must have a "pull from ..." section
                char pull_from_key[512];
                sprintf(pull_from_key, "pull from %s", self_addr_str);
                ASSERT_JSON_HAS_KEY(bulk, pull_from_key, pull_from, object);
                // "pull from ..." secion must have a "itransfer" section
                ASSERT_JSON_HAS(pull_from, itransfer, object);
                ASSERT_JSON_HAS_STATS(itransfer, duration);
                ASSERT_JSON_HAS_STATS(itransfer, size);
                ASSERT_JSON_HAS_DOUBLE_STATS(pull_from, transfer_cb, relative_timestamp_from_itransfer_start);
                ASSERT_JSON_HAS_DOUBLE_STATS(pull_from, itransfer_wait, relative_timestamp_from_itransfer_end);
            }
        }
        // must have an "65535:65535:65535:65535" secion with a bulk create
        ASSERT_JSON_HAS_KEY(rpcs, "65535:65535:65535:65535", root, object);
        {
            // check RPC info
            ASSERT_JSON_HAS(root, rpc_id, int);
            munit_assert_long(65535, ==, json_object_get_int64(rpc_id));
            ASSERT_JSON_HAS(root, parent_rpc_id, int);
            munit_assert_long(65535, ==, json_object_get_int64(parent_rpc_id));
            ASSERT_JSON_HAS(root, provider_id, int);
            munit_assert_long(65535, ==, json_object_get_int64(provider_id));
            ASSERT_JSON_HAS(root, parent_provider_id, int);
            munit_assert_long(65535, ==, json_object_get_int64(parent_provider_id));
            ASSERT_JSON_HAS(root, name, string);
            munit_assert_string_equal(json_object_get_string(name), "");
            // RPC must have a "target" section
            ASSERT_JSON_HAS(root, target, object);
            // "target" section must have a section indexed by source address
            ASSERT_JSON_HAS_KEY(target, "received from <unknown>", received_from, object);
            // "received from ..." section must have a "bulk" section
            ASSERT_JSON_HAS(received_from, bulk, object);
            // "bulk" secion must have a "create" section
            ASSERT_JSON_HAS(bulk, create, object);
            ASSERT_JSON_HAS_STATS(create, duration);
            ASSERT_JSON_HAS_STATS(create, size);
        }
        if(relay == HG_TRUE) {
            sprintf(echo_key, "3747772018183438335:%d:3747772018183438335:%d", provider_id_param, provider_id_param);
            // must have an "3747772018183438335:provider_id:3747772018183438335:provider_id" secion for the echo RPC
            ASSERT_JSON_HAS_KEY(rpcs, echo_key, echo, object);
            {
                // check RPC info
                ASSERT_JSON_HAS(echo, rpc_id, int);
                munit_assert_long(3747772018183438335, ==, json_object_get_int64(rpc_id));
                ASSERT_JSON_HAS(echo, parent_rpc_id, int);
                munit_assert_long(3747772018183438335, ==, json_object_get_int64(parent_rpc_id));
                ASSERT_JSON_HAS(echo, provider_id, int);
                munit_assert_long(provider_id_param, ==, json_object_get_int64(provider_id));
                ASSERT_JSON_HAS(echo, parent_provider_id, int);
                munit_assert_long(provider_id_param, ==, json_object_get_int64(parent_provider_id));
                ASSERT_JSON_HAS(echo, name, string);
                munit_assert_string_equal(json_object_get_string(name), "echo");
                // RPC must have an "origin" section
                ASSERT_JSON_HAS(echo, origin, object);
                {
                    // "origin" section must have a section index by destination address
                    sprintf(addr_key, "sent to %s", self_addr_str);
                    ASSERT_JSON_HAS_KEY(origin, addr_key, sent_to, object);
                    ASSERT_JSON_HAS_DOUBLE_STATS(sent_to, iforward, relative_timestamp_from_create);
                    ASSERT_JSON_HAS_DOUBLE_STATS(sent_to, forward_cb, relative_timestamp_from_iforward_start);
                    ASSERT_JSON_HAS_DOUBLE_STATS(sent_to, iforward_wait, relative_timestamp_from_iforward_end);
                    ASSERT_JSON_HAS_DOUBLE_STATS(sent_to, set_input, relative_timestamp_from_iforward_start);
                    ASSERT_JSON_HAS_DOUBLE_STATS(sent_to, get_output, relative_timestamp_from_wait_end);
                }
                // RPC must have an "target" section
                ASSERT_JSON_HAS(echo, target, object);
                {
                    // "target" section must have a section index by source address
                    sprintf(addr_key, "received from %s", self_addr_str);
                    ASSERT_JSON_HAS_KEY(target, addr_key, received_from, object);
                    ASSERT_JSON_HAS(received_from, handler, object);
                    ASSERT_JSON_HAS_STATS(handler, duration);
                    ASSERT_JSON_HAS_DOUBLE_STATS(received_from, ult, relative_timestamp_from_handler_start);
                    ASSERT_JSON_HAS_DOUBLE_STATS(received_from, irespond, relative_timestamp_from_ult_start);
                    ASSERT_JSON_HAS_DOUBLE_STATS(received_from, respond_cb, relative_timestamp_from_irespond_start);
                    ASSERT_JSON_HAS_DOUBLE_STATS(received_from, irespond_wait, relative_timestamp_from_irespond_end);
                    ASSERT_JSON_HAS_DOUBLE_STATS(received_from, set_output, relative_timestamp_from_irespond_start);
                    ASSERT_JSON_HAS_DOUBLE_STATS(received_from, get_input, relative_timestamp_from_ult_start);
                    // "received from ..." must have a "bulk" section
                    ASSERT_JSON_HAS(received_from, bulk, object);
                    // "bulk" secion must have a "create" section
                    ASSERT_JSON_HAS(bulk, create, object);
                    // "bulk" section must have a section index by source address
                    ASSERT_JSON_HAS_STATS(create, duration);
                    ASSERT_JSON_HAS_STATS(create, size);
                    // "bulk" section must have a "pull from ..." section
                    char pull_from_key[512];
                    sprintf(pull_from_key, "pull from %s", self_addr_str);
                    ASSERT_JSON_HAS_KEY(bulk, pull_from_key, pull_from, object);
                    // "pull from..." secion must have a "itransfer" secion
                    ASSERT_JSON_HAS(pull_from, itransfer, object);
                    ASSERT_JSON_HAS_STATS(itransfer, duration);
                    ASSERT_JSON_HAS_STATS(itransfer, size);
                    ASSERT_JSON_HAS_DOUBLE_STATS(pull_from, transfer_cb, relative_timestamp_from_itransfer_start);
                    ASSERT_JSON_HAS_DOUBLE_STATS(pull_from, itransfer_wait, relative_timestamp_from_itransfer_end);
                }
            }
        }
    }

    json_object_put(json_content);
    free(file_content);
    free(filename);
    return MUNIT_OK;
}

static MunitResult test_default_monitoring_time_series(const MunitParameter params[],
                                                       void*                data)
{
    (void)data;
    hg_return_t hret                      = HG_SUCCESS;
    const char* protocol = munit_parameters_get(params, "protocol");
    uint16_t provider_id_param = atoi(munit_parameters_get(params, "provider_id"));
    hg_bool_t relay = strcmp(munit_parameters_get(params, "relay"), "true") == 0 ? HG_TRUE : HG_FALSE;
    const char* json_config =
        "{\"monitoring\":{"
            "\"config\":{"
                "\"filename_prefix\":\"test\","
                "\"statistics\":{\"disable\":true},"
                "\"time_series\":{\"precision\":9,\"disable\":false,\"pretty_json\":true}"
            "}"
        "}}";
    struct margo_init_info init_info = {
        .json_config   = json_config,
        .progress_pool = ABT_POOL_NULL,
        .rpc_pool      = ABT_POOL_NULL,
        .hg_class      = NULL,
        .hg_context    = NULL,
        .hg_init_info  = NULL,
        .logger        = NULL,
        .monitor       = margo_default_monitor
    };
    margo_instance_id mid = margo_init_ext(protocol, MARGO_SERVER_MODE, &init_info);
    munit_assert_not_null(mid);

    /* self_addr is used later to check the content of the JSON output */
    char self_addr_str[256];
    hg_size_t self_addr_size = 256;
    hg_addr_t self_addr = HG_ADDR_NULL;
    margo_addr_self(mid, &self_addr);
    margo_addr_to_string(mid, self_addr_str, &self_addr_size, self_addr);
    margo_addr_free(mid, self_addr);

    hg_id_t echo_id = MARGO_REGISTER_PROVIDER(
        mid, "echo", echo_in_t, hg_string_t, echo_ult,
        provider_id_param, ABT_POOL_NULL);
    munit_assert_int(echo_id, !=, 0);

    margo_thread_sleep(mid, 1);

    char      buffer[256];
    void*     ptrs[1]  = { (void*)buffer };
    hg_size_t sizes[1] = { 256 };
    hg_bulk_t bulk     = HG_BULK_NULL;
    hret = margo_bulk_create(mid, 1, ptrs, sizes, HG_BULK_READ_ONLY, &bulk);
    munit_assert_int(hret, ==, HG_SUCCESS);

    const char* input  = "hello world";
    echo_in_t in = {
        .relay = relay,
        .str = (char*)input,
        .blk = bulk
    };
    hg_addr_t addr     = HG_ADDR_NULL;
    hg_handle_t handle = HG_HANDLE_NULL;

    hret = margo_addr_self(mid, &addr);
    munit_assert_int(hret, ==, HG_SUCCESS);

    hret = margo_create(mid, addr, echo_id, &handle);
    munit_assert_int(hret, ==, HG_SUCCESS);

    hret = margo_provider_forward(provider_id_param, handle, &in);
    munit_assert_int(hret, ==, HG_SUCCESS);

    hret = margo_bulk_free(bulk);
    munit_assert_int(hret, ==, HG_SUCCESS);

    char* output = NULL;
    hret = margo_get_output(handle, &output);
    munit_assert_int(hret, ==, HG_SUCCESS);

    hret = margo_free_output(handle, &output);
    munit_assert_int(hret, ==, HG_SUCCESS);

    hret = margo_destroy(handle);
    munit_assert_int(hret, ==, HG_SUCCESS);

    hret = margo_addr_free(mid, addr);
    munit_assert_int(hret, ==, HG_SUCCESS);

    hret = margo_deregister(mid, echo_id);
    munit_assert_int(hret, ==, HG_SUCCESS);

    hret = margo_monitor_call_user(mid, MARGO_MONITOR_FN_START, NULL);
    munit_assert_int(hret, ==, HG_SUCCESS);

    margo_finalize(mid);

    char* filename = NULL;
    {
        char hostname[1024];
        hostname[1023] = '\0';
        gethostname(hostname, 1023);
        pid_t pid = getpid();
        size_t fullname_size = snprintf(NULL, 0, "test.%s.%d.series.json", hostname, pid);
        filename = calloc(1, fullname_size+1);
        sprintf(filename, "test.%s.%d.series.json", hostname, pid);
    }
    munit_assert_int(access(filename, F_OK), ==, 0);

    FILE* file = fopen(filename, "r");
    munit_assert_not_null(file);
    fseek(file, 0L, SEEK_END);
    long int file_size = ftell(file);
    fseek(file, 0L, SEEK_SET);
    munit_assert_int(file_size, >, 0);
    char* file_content = malloc(file_size);
    munit_assert_int(file_size, ==, fread(file_content, 1, file_size, file));
    fclose(file);

    struct json_object* json_content = NULL;
    struct json_tokener* tokener     = json_tokener_new();
    json_content = json_tokener_parse_ex(tokener, file_content, file_size);
    json_tokener_free(tokener);
    munit_assert_not_null(json_content);

    munit_assert(json_object_is_type(json_content, json_type_object));

    {
        // check for the "rpcs" secions
        ASSERT_JSON_HAS(json_content, rpcs, object);
        // check for the echo:XXXX section
        char echo_key[256];
        sprintf(echo_key, "echo:%d", provider_id_param);
        ASSERT_JSON_HAS_KEY(rpcs, echo_key, echo, object);
        {
            // check for "timestamps" array
            ASSERT_JSON_HAS(echo, timestamps, array);
            // check for the "count" array
            ASSERT_JSON_HAS(echo, count, array);
            // check for the "bulk_size" array
            ASSERT_JSON_HAS(echo, bulk_size, array);
        }
        // check for the pool section
        ASSERT_JSON_HAS(json_content, pools, object);
        // check for the __primary__ section
        ASSERT_JSON_HAS(pools, __primary__, object);
        {
            // check for "timestamps" array
            ASSERT_JSON_HAS(__primary__, timestamps, array);
            // check for the "size" array
            ASSERT_JSON_HAS(__primary__, size, array);
            // check for the "total_size" array
            ASSERT_JSON_HAS(__primary__, total_size, array);
        }
    }

    json_object_put(json_content);
    free(file_content);
    free(filename);
    return MUNIT_OK;
}

static char* protocol_params[] = {"na+sm", NULL};
static char* provider_id_params[] = {"65535", "42", "0", NULL};
static char* relay_params[] = {"true", "false", NULL};

static MunitParameterEnum test_params_custom[]
    = {{"protocol", protocol_params},
       {NULL, NULL}};

static MunitParameterEnum test_params[]
    = {{"protocol", protocol_params},
       {"provider_id", provider_id_params},
       {"relay", relay_params},
       {NULL, NULL}};

static MunitTest test_suite_tests[] = {
    {(char*)"/monitoring/statistics", test_default_monitoring_statistics, test_context_setup,
     test_context_tear_down, MUNIT_TEST_OPTION_NONE, test_params},
    {(char*)"/monitoring/time_series", test_default_monitoring_time_series, test_context_setup,
     test_context_tear_down, MUNIT_TEST_OPTION_NONE, test_params},
    {(char*)"/monitoring/custom", test_custom_monitoring, test_context_setup,
     test_context_tear_down, MUNIT_TEST_OPTION_NONE, test_params_custom},
    {NULL, NULL, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL}};

static const MunitSuite test_suite
    = {(char*)"/margo", test_suite_tests, NULL, 1, MUNIT_SUITE_OPTION_NONE};

int main(int argc, char* argv[MUNIT_ARRAY_PARAM(argc + 1)])
{
    return munit_suite_main(&test_suite, NULL, argc, argv);
}
