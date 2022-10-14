/*
 * (C) 2022 The University of Chicago
 *
 * See COPYRIGHT in top-level directory.
 */

#include <margo-config.h>

#ifdef HAVE_DL_ITERATE_PHDR
    #define _GNU_SOURCE
    #include <link.h>
#endif /* HAVE_DL_ITERATE_PHDR */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <assert.h>
#include <json-c/json.h>

#include "margo.h"
#include "margo-logging.h"

struct options {
    int   all_libraries_flag;
    char* target_addr;
};

/* ansi terminal color helpers */
#define ANSI_COLOR_RED     "\x1b[31m"
#define ANSI_COLOR_GREEN   "\x1b[32m"
#define ANSI_COLOR_YELLOW  "\x1b[33m"
#define ANSI_COLOR_BLUE    "\x1b[34m"
#define ANSI_COLOR_MAGENTA "\x1b[35m"
#define ANSI_COLOR_CYAN    "\x1b[36m"
#define ANSI_COLOR_RESET   "\x1b[0m"

#define json_array_foreach(__array, __index, __element)                \
    for (__index = 0;                                                  \
         __index < json_object_array_length(__array)                   \
         && (__element = json_object_array_get_idx(__array, __index)); \
         __index++)

/* TODO: in retrospect this could have been json, and in fact could be
 * ingested with an optional command line argument so that this utility
 * could be used to test protocols that were not known at compile time.
 * Maybe a feature for later.
 */
/* X macro to enumerate all known possible plugin combinations */
/* format: transport library, protocol, address format, description */
#define MARGO_KNOWN_HG_PLUGINS                                                 \
    X("ofi+tcp://", "ofi", "tcp", "libfabric tcp provider (TCP/IP)")           \
    X("ofi+verbs://", "ofi", "verbs",                                          \
      "libfabric Verbs provider (InfiniBand or RoCE)")                         \
    X("ofi+shm://", "ofi", "shm", "libfabric shm provider (shared memory)")    \
    X("ofi+sockets://", "ofi", "shm", "libfabric sockets provider (TCP/IP)")   \
    X("ofi+psm2://", "ofi", "psm2", "libfabric PSM2 provider (OmniPath")       \
    X("ofi+opx://", "ofi", "opx", "libfabric OPX provide (OmniPath)")          \
    X("ofi+gni://", "ofi", "gni", "libfabric GNI provider (Cray Aries)")       \
    X("ofi+cxi://", "ofi", "cxi",                                              \
      "libfabric CXI provider (HPE Cassini/Slingshot 11)")                     \
    X("psm+psm://", "psm", "psm", "integrated PSM plugin (OmniPath)")          \
    X("psm2+psm2://", "psm2", "psm2", "integrated PSM2 plugin (OmniPath)")     \
    X("na+sm://", "na", "sm", "integrated sm plugin (shared memory)")          \
    X("bmi+tcp://", "bmi", "tcp", "BMI tcp module (TCP/IP)")                   \
    X("ucx+tcp://", "ucx", "tcp", "UCX TCP/IP over SOCK_STREAM sockets")       \
    X("ucx+rc://", "ucx", "rc", "UCX RC (reliable connection)")                \
    X("ucx+ud://", "ucx", "ud", "UCX UD (unreliable datagram)")                \
    X("ucx+dc://", "ucx", "dc",                                                \
      "UCX DC (dynamic connection, only available on Mellanox adapters)")      \
    X("ucx+all://", "ucx", "<any>",                                            \
      "UCX default/automatic transport selection")                             \
    X("tcp://", "<any>", "tcp", "TCP/IP protocol, transport not specified")    \
    X("verbs://", "<any>", "verbs", "Verbs protocol, transport not specified") \
    X("sm://", "<any>", "sm",                                                  \
      "shared memory protocol, transport not specified")                       \
    X("psm2://", "<any>", "psm2", "PSM2 protocol, transport not specified")    \
    X(NULL, NULL, NULL, NULL)

#define X(addr, xport, proto, desc) addr,
static const char* const margo_addrs[] = {MARGO_KNOWN_HG_PLUGINS};
#undef X
#define X(addr, xport, proto, desc) xport,
static const char* const margo_xports[] = {MARGO_KNOWN_HG_PLUGINS};
#undef X
#define X(addr, xport, proto, desc) proto,
static const char* const margo_protos[] = {MARGO_KNOWN_HG_PLUGINS};
#undef X
#define X(addr, xport, proto, desc) desc,
static const char* const margo_descs[] = {MARGO_KNOWN_HG_PLUGINS};
#undef X

#ifdef HAVE_DL_ITERATE_PHDR
/* list of strings that are likely to appear in relevant communication
 * libraries
 */
static const char* comm_lib_strings[] = {
    "mercury.so", "margo.so", "libfabric.so", "ucx", "ucp", "uct", "ucs", "psm",
    "verbs",      "rdma",     "gni",          "cxi", "opx", "bmi", NULL};
#endif

static void parse_args(int argc, char** argv, struct options* opts);
static void usage(void);
static void set_verbose_logging(void);
static void emit_results(struct json_object* json_result_array, char* hostname);
#ifdef HAVE_DL_ITERATE_PHDR
static int dl_callback(struct dl_phdr_info* info, size_t size, void* data);
#endif /* HAVE_DL_ITERATE_PHDR */

int main(int argc, char** argv)
{
    margo_instance_id   mid;
    hg_return_t         hret;
    hg_addr_t           addr;
    char                addr_str[256];
    char                tmp_stderr_template[256];
    char                hostname[128];
    int                 tmp_stderr_fd;
    FILE*               tmp_stderr_stream = NULL;
    hg_size_t           addr_str_size     = 256;
    int                 i;
    int                 target_addr_match = 0;
    int                 ret;
    struct json_object* json_result_array = NULL;
    struct json_object* json_result       = NULL;
    char                json_template[256];
    int                 json_fd;
    FILE*               json_stream = NULL;
    struct options      opts;

    parse_args(argc, argv, &opts);

    if (opts.target_addr) {
        /* The user wants us to limit the query to one protocol */
        /* In this case we also go ahead and enable verbose logging, with
         * everything going to stderr.  This will be redirected for
         * capture/display.
         */
        set_verbose_logging();
    }

    /* Redirect stderr up front; in some cases this utility will turn on
     * extensive logging output that we don't necessarily want to display
     * inline with the concise probe results.
     */
    /* This should work reliably, but we treat it as "best effort" here and
     * continue even if it does not work for some reason
     */
    sprintf(tmp_stderr_template, "/tmp/margo-info-stderr-XXXXXX");
    tmp_stderr_fd = mkstemp(tmp_stderr_template);
    if (tmp_stderr_fd < 0) {
        fprintf(stderr,
                "# Warning: unable to open temporary file for log output.\n");
        perror("# mkstemp");
    } else {
        tmp_stderr_stream = freopen(tmp_stderr_template, "w+", stderr);
        /* We no longer need the file descriptor now that we have a stream */
        close(tmp_stderr_fd);
    }

    /* retrieve hostname. This is useful to validate if someone may have
     * executed the utility on the wrong host (e.g., a login node or
     * management node)
     */
    ret = gethostname(hostname, 128);
    if (ret < 0) sprintf(hostname, "UNKNOWN");

    /* create json array to hold all results */
    json_result_array = json_object_new_array();
    if (!json_result_array) {
        ret = -1;
        printf("Error: JSON.\n");
        goto cleanup;
    }

    /* loop through address permuations */
    for (i = 0; margo_addrs[i]; i++) {
        sprintf(addr_str, "N/A");
        addr_str_size = 256;

        /* skip iteration if we are looking for a specific addr and this
         * isn't it.
         */
        if (opts.target_addr && strcmp(opts.target_addr, margo_addrs[i]))
            continue;

        /* note if we found a match */
        if (opts.target_addr) target_addr_match = 1;

        /* create json object to hold this specific result */
        json_result = json_object_new_object();
        if (!json_result) {
            ret = -1;
            printf("Error: JSON.\n");
            goto cleanup;
        }

        /* index so that we can emit these in order if we want */
        json_object_object_add(json_result, "index", json_object_new_int(i));
        /* addr */
        json_object_object_add(json_result, "addr",
                               json_object_new_string(margo_addrs[i]));
        /* xport */
        json_object_object_add(json_result, "xport",
                               json_object_new_string(margo_xports[i]));
        /* proto */
        json_object_object_add(json_result, "proto",
                               json_object_new_string(margo_protos[i]));
        /* description */
        json_object_object_add(json_result, "desc",
                               json_object_new_string(margo_descs[i]));

        /* attempt to initialize */
        mid = margo_init(margo_addrs[i], MARGO_SERVER_MODE, 0, 0);
        if (mid) {
            json_object_object_add(json_result, "result",
                                   json_object_new_boolean(1));
            /* query local address */
            hret = margo_addr_self(mid, &addr);
            if (hret == HG_SUCCESS) {
                hret
                    = margo_addr_to_string(mid, addr_str, &addr_str_size, addr);
                margo_addr_free(mid, addr);
            }
            if (hret != HG_SUCCESS) sprintf(addr_str, "UNKNOWN");
            margo_finalize(mid);
            json_object_object_add(json_result, "example_runtime_addr",
                                   json_object_new_string(addr_str));
        } else {
            json_object_object_add(json_result, "result",
                                   json_object_new_boolean(0));
            json_object_object_add(json_result, "example_runtime_addr",
                                   json_object_new_string("N/A"));
        }

        ret = json_object_array_add(json_result_array, json_result);
        if (ret < 0) {
            printf("Error: JSON.\n");
            goto cleanup;
        }
    }

    /* The user asked us to query a particular address specifier, but we
     * couldn't find it.
     */
    if (opts.target_addr && !target_addr_match) {
        printf(
            "# \"%s\" not supported by margo-info.  Try one of the "
            "following or run\n# margo-info with no arguments to probe for "
            "supported address types:\n",
            opts.target_addr);
        for (i = 0; margo_addrs[i]; i++) printf("      %s\n", margo_addrs[i]);
        ret = -1;
        goto cleanup;
    }

    emit_results(json_result_array, hostname);

    printf("\n");
    printf(
        "####################################################################"
        "\n");
    printf("# Notes on interpretting margo-info output:\n");
    printf(
        "# - This utility queries software stack capability, not hardware "
        "availability.\n");
    printf(
        "# - UCX does not directly expose which underlying transport plugins "
        "are available.\n   The \"dc\" protocol type is only available for "
        "Mellanox InfiniBand adapters, however.\n   See \"ucx_info -d\" for "
        "more information about transportws available in the UCX library.\n");
    printf(
        "# - For more information about a particular address specifier, "
        "please\n");
    printf(
        "#   execute margo-info with that address specifier as its only "
        "argument\n");
    printf("#   and check the resulting log file for details.\n");
    printf(
        "#   (E.g., \"margo-info ofi+verbs://\" for Verbs-specific "
        "diagnostics)\n");
    printf("# \n");

    printf(
        "####################################################################"
        "\n");
    printf("# Suggested transport-level diagnostic tools:\n");
    printf("# - libfabric:\t`fi_info -t FI_EP_RDM`\n");
    printf("# - UCX:\t`ucx_info -d`\n");
    printf("# - verbs:\t`ibstat`\n");
    printf("# - TCP/IP:\t`ifconfig`\n");
    printf("# - CXI:\t`cxi_stat`\n");
    printf("# \n");

    printf(
        "####################################################################"
        "\n");
    printf("# Verbose margo-info information:\n");
    if (tmp_stderr_stream) {
        printf("# - debug log output:\n");
        printf("#   " ANSI_COLOR_MAGENTA "%s\n" ANSI_COLOR_RESET,
               tmp_stderr_template);
    }
    sprintf(json_template, "/tmp/margo-info-json-XXXXXX");
    json_fd = mkstemp(json_template);
    if (json_fd < 0) {
        fprintf(stderr,
                "# Warning: unable to open temporary file for JSON output.\n");
        perror("# mkstemp");
    } else {
        json_stream = freopen(json_template, "w+", stderr);
        /* We no longer need the file descriptor now that we have a stream */
        close(json_fd);
        fprintf(json_stream, "%s\n",
                json_object_to_json_string_ext(
                    json_result_array,
                    JSON_C_TO_STRING_PRETTY | JSON_C_TO_STRING_NOSLASHESCAPE));
        printf("# - results in JSON format:\n");
        printf("#   " ANSI_COLOR_MAGENTA "%s\n" ANSI_COLOR_RESET,
               json_template);
    }
    printf("# \n");

    printf(
        "####################################################################"
        "\n");

#ifdef HAVE_DL_ITERATE_PHDR
    printf("# List of dynamic libraries used by the margo-info utility:\n");

    dl_iterate_phdr(dl_callback, &opts);

    if (!opts.all_libraries_flag) {
        printf("# \n");
        printf(
            "# Note: the above list was filtered display only those libraries "
            "likely related\n");
        printf(
            "#       to communication. Run margo-info with -l to display all "
            "libraries.\n");
    }
    printf("# \n");
    printf(
        "####################################################################"
        "\n");
#endif /* HAVE_DL_ITERATE_PHDR */

cleanup:
    if (json_result_array) json_object_put(json_result_array);
    if (tmp_stderr_stream) fclose(tmp_stderr_stream);
    if (json_stream) fclose(json_stream);
    if (opts.target_addr) free(opts.target_addr);

    return (ret);
}

#ifdef HAVE_DL_ITERATE_PHDR
static int dl_callback(struct dl_phdr_info* info, size_t size, void* data)
{
    struct options* opts = data;
    int             i;

    /* display all libraries; no filter */
    if (opts->all_libraries_flag && strlen(info->dlpi_name) > 1) {
        printf("# - %s\n", info->dlpi_name);
        return (0);
    }

    /* only display libraries that contain a substring we expect to see in a
     * communication library
     */
    for (i = 0; comm_lib_strings[i] != NULL; i++) {
        if (strstr(info->dlpi_name, comm_lib_strings[i])) {
            printf("# - %s\n", info->dlpi_name);
            return (0);
        }
    }

    return (0);
}
#endif /* HAVE_DL_ITERATE_PHDR */

static void usage(void)
{
    int i;

    fprintf(stderr, "Usage: margo-info [address specifier] [-l]\n");
    fprintf(stderr, "   Run with no arguments to query available protocols.\n");
    fprintf(stderr,
            "   Run one of the following arguments for more detail on a "
            "specific protocol:\n");
    for (i = 0; margo_addrs[i]; i++)
        fprintf(stderr, "      %s\n", margo_addrs[i]);
    fprintf(stderr, "   -l to list all runtime libraries.\n");
    return;
}

static void set_verbose_logging(void)
{

    /* verbose Margo logging */
    margo_set_global_log_level(MARGO_LOG_TRACE);
    /* verbose Mercury logging */
    HG_Set_log_level("debug");
    HG_Set_log_subsys("hg");

    /* NOTE: we use environment variables (where available) for any
     * transport library or lower debugging.  We don't know which of these
     * are linked in so we can't make programmatic calls.
     */
    /* verbose libfabric logging */
    setenv("FI_LOG_LEVEL", "debug", 1);
    /* more verbose PSM2 logging */
    setenv("PSM2_TRACEMASK", "0x101", 1);

    return;
}

static void emit_results(struct json_object* json_result_array, char* hostname)
{
    unsigned int        i      = 0;
    struct json_object* result = NULL;
    char*               color_str;
    char*               result_str;

    printf("\n");
    printf(
        "####################################################################"
        "\n");
    printf(
        "# Available Margo (Mercury) network transports on "
        "host " ANSI_COLOR_MAGENTA "%s\n" ANSI_COLOR_RESET,
        hostname);
    printf("# - " ANSI_COLOR_GREEN "GREEN " ANSI_COLOR_RESET
           "indicates that it can be initialized successfully.\n");
    printf("# - " ANSI_COLOR_RED "RED " ANSI_COLOR_RESET
           "indicates that it cannot.\n");
    printf(
        "####################################################################"
        "\n");
    printf(
        "\n# <address> <transport> <protocol> <results> <example runtime "
        "address>\n\n");

    /* iterate through the json array twice so that we can display all of
     * the working ones at the top of the list
     */

    json_array_foreach(json_result_array, i, result)
    {
        if (!json_object_get_boolean(json_object_object_get(result, "result")))
            continue;

        color_str  = ANSI_COLOR_GREEN;
        result_str = "YES";
        printf("### %s ###\n",
               json_object_get_string(json_object_object_get(result, "desc")));
        printf("%s%s\t%s\t%s\t%s\t%s\n" ANSI_COLOR_RESET, color_str,
               json_object_get_string(json_object_object_get(result, "addr")),
               json_object_get_string(json_object_object_get(result, "xport")),
               json_object_get_string(json_object_object_get(result, "proto")),
               result_str,
               json_object_get_string(
                   json_object_object_get(result, "example_runtime_addr")));
    }

    json_array_foreach(json_result_array, i, result)
    {
        if (json_object_get_boolean(json_object_object_get(result, "result")))
            continue;
        color_str  = ANSI_COLOR_RED;
        result_str = "NO";
        printf("### %s ###\n",
               json_object_get_string(json_object_object_get(result, "desc")));
        printf("%s%s\t%s\t%s\t%s\t%s\n" ANSI_COLOR_RESET, color_str,
               json_object_get_string(json_object_object_get(result, "addr")),
               json_object_get_string(json_object_object_get(result, "xport")),
               json_object_get_string(json_object_object_get(result, "proto")),
               result_str,
               json_object_get_string(
                   json_object_object_get(result, "example_runtime_addr")));
    }

    return;
}

static void parse_args(int argc, char** argv, struct options* opts)
{
    int opt;

    memset(opts, 0, sizeof(*opts));

    while ((opt = getopt(argc, argv, "l")) != -1) {
        switch (opt) {
        case 'l':
            opts->all_libraries_flag = 1;
            break;
        default:
            usage();
            exit(EXIT_FAILURE);
        }
    }

    if ((argc - optind) > 1) {
        usage();
        exit(EXIT_FAILURE);
    }

    /* one optional argument with no flags: address to query */
    if ((argc - optind) == 1) { opts->target_addr = strdup(argv[optind]); }

    return;
}
