/*
 * (C) 2024 The University of Chicago
 *
 * See COPYRIGHT in top-level directory.
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <assert.h>
#include <string.h>
#include <sched.h>

#include <rdma/fabric.h>
#include <rdma/fi_errno.h>

#include <hwloc.h>

/* NOTE: this is normally only available as an internal interface.
 * Defining it here so that we can unit test it independently.
 */
int mochi_plumber_resolve_nic(const char* in_address,
                              const char* bucket_policy,
                              const char* nic_policy,
                              char**      out_address);

struct options {
    char prov_name[256];
};

struct nic {
    char         iface_name[256];
    unsigned int domain_id;
    unsigned int bus_id;
    unsigned int device_id;
    unsigned int function_id;
};

struct test_combo {
    const char* bucket_policy;
    const char* nic_policy;
};

struct test_combo test_combos[]
    = {{.bucket_policy = "all", .nic_policy = "roundrobin"},
       {.bucket_policy = "all", .nic_policy = "random"},
       {.bucket_policy = "all", .nic_policy = "bycore"},
       {.bucket_policy = "all", .nic_policy = "byset"},
       {.bucket_policy = "package", .nic_policy = "roundrobin"},
       {.bucket_policy = "package", .nic_policy = "random"},
       {.bucket_policy = "package", .nic_policy = "bycore"},
       {.bucket_policy = "package", .nic_policy = "byset"},
       {.bucket_policy = "numa", .nic_policy = "roundrobin"},
       {.bucket_policy = "numa", .nic_policy = "random"},
       {.bucket_policy = "numa", .nic_policy = "bycore"},
       {.bucket_policy = "numa", .nic_policy = "byset"},
       {.bucket_policy = "passthrough", .nic_policy = "passthrough"},
       {0}};

static int  parse_args(int argc, char** argv, struct options* opts);
static int  find_nics(struct options* opts, int* num_nics, struct nic** nics);
static void usage(void);
static int  count_packages(hwloc_topology_t* topology);
static int  find_cores(struct options* opts,
                       pid_t*          pid,
                       int*            num_cores,
                       int*            num_numa,
                       int*            num_packages,
                       int*            current_core,
                       int*            current_numa,
                       int*            current_package);

static int check_locality(struct options* opts,
                          int             num_cores,
                          int             num_numa,
                          int             num_packages,
                          int             num_nics,
                          struct nic*     nics);

int main(int argc, char** argv)
{
    struct options opts;
    struct nic*    nics = NULL;
    int            num_nics;
    int            num_cores;
    int            num_numa;
    int            num_packages;
    int            current_core;
    int            current_numa;
    int            current_package;
    pid_t          pid;
    int            ret;
    int            i;
    char           hostname[256] = {0};
    char*          out_addr      = NULL;

    ret = parse_args(argc, argv, &opts);
    if (ret < 0) {
        usage();
        exit(EXIT_FAILURE);
    }

    /* get an array of network interfaces with device ids */
    ret = find_nics(&opts, &num_nics, &nics);
    if (ret < 0) {
        fprintf(stderr, "Error: unable to find network cards.\n");
        return (-1);
    }

    /* get array of cpu ids */
    ret = find_cores(&opts, &pid, &num_cores, &num_numa, &num_packages,
                     &current_core, &current_numa, &current_package);
    if (ret < 0) {
        fprintf(stderr, "Error: unable to find CPUs.\n");
        return (-1);
    }

    gethostname(hostname, 255);
    printf("Host:\n");
    printf("\t%s\n", hostname);

    printf("\nCPU information:\n");
    printf(
        "\tPID %d running on core %d of %d and NUMA domain %d of %d and "
        "package %d of %d\n",
        (int)pid, current_core, num_cores, current_numa, num_numa,
        current_package, num_packages);

    printf("\n");
    printf("Network cards:\n");
    printf("\t#<name> <domain ID> <bus ID> <device ID> <function id>\n");
    for (i = 0; i < num_nics; i++) {
        printf("\t%s %u %u %u %u\n", nics[i].iface_name, nics[i].domain_id,
               nics[i].bus_id, nics[i].device_id, nics[i].function_id);
    }

    /* check locality of all permutations */
    ret = check_locality(&opts, num_cores, num_numa, num_packages, num_nics,
                         nics);
    if (ret < 0) {
        fprintf(stderr, "Error: check_locality() failure.\n");
        return (-1);
    }

    if (nics) free(nics);

    /* exercise programmatic fn for resolving addresses to specific NICs */
    printf("\nmochi_plumber_resolve_nic() test cases:\n");
    printf("\t#<bucket policy>\t<NIC policy>\t<in addr>\t<out addr>\n");

    i = 0;
    while (test_combos[i].bucket_policy) {
        ret = mochi_plumber_resolve_nic(opts.prov_name,
                                        test_combos[i].bucket_policy,
                                        test_combos[i].nic_policy, &out_addr);
        if (ret == 0) {
            printf("\t%10s\t%12s\t%s\t%s\n", test_combos[i].bucket_policy,
                   test_combos[i].nic_policy, opts.prov_name, out_addr);
            if (out_addr) free(out_addr);
            out_addr = NULL;
        } else {
            printf("\t%10s\t%12s\t%s\tN/A\n", test_combos[i].bucket_policy,
                   test_combos[i].nic_policy, opts.prov_name);
        }
        i++;
    }

    return (0);
}

static void usage(void)
{
    fprintf(stderr, "Usage: ofi-dm-query -p <provider_name>\n");
    return;
}

static int parse_args(int argc, char** argv, struct options* opts)
{
    int opt;
    int ret;

    memset(opts, 0, sizeof(*opts));

    while ((opt = getopt(argc, argv, "p:")) != -1) {
        switch (opt) {
        case 'p':
            ret = sscanf(optarg, "%s", opts->prov_name);
            if (ret != 1) return (-1);
            break;
        default:
            return (-1);
        }
    }

    if (strlen(opts->prov_name) == 0) return (-1);

    return (0);
}

static int find_nics(struct options* opts, int* num_nics, struct nic** nics)
{
    struct fi_info* info;
    struct fi_info* hints;
    struct fi_info* cur;
    int             ret;
    int             i;

    /* ofi info */
    hints = fi_allocinfo();
    assert(hints);
    /* These are required as input if we want to filter the results; they
     * indicate functionality that the caller is prepared to provide.  This
     * is just a query, so we want wildcard options except that we must disable
     * deprecated memory registration modes.
     */
    hints->mode                 = ~0;
    hints->domain_attr->mode    = ~0;
    hints->domain_attr->mr_mode = ~3;
    /* Indicate that we only want results matching a particular provider
     * (e.g., cxi or verbs)
     */
    hints->fabric_attr->prov_name = strdup(opts->prov_name);
    /* Depending on the provider we may also need to set the protocol we
     * want; some providers advertise more than one.
     */
    if (strcmp(opts->prov_name, "cxi") == 0) {
        hints->ep_attr->protocol = FI_PROTO_CXI;
    }

    /* This call will populate info with a linked list that can be traversed
     * to inspect what's available.
     */
    ret = fi_getinfo(FI_VERSION(FI_MAJOR_VERSION, FI_MINOR_VERSION), NULL, NULL,
                     0, hints, &info);
    if (ret != 0) {
        fprintf(stderr, "fi_getinfo: %d (%s)\n", ret, fi_strerror(-ret));
        fi_freeinfo(hints);
        return (ret);
    }
    // print_short_info(info);

    /* loop through results and see how many report PCI bus information */
    *num_nics = 0;
    for (cur = info; cur; cur = cur->next) {
        if (cur->nic && cur->nic->bus_attr
            && cur->nic->bus_attr->bus_type == FI_BUS_PCI)
            (*num_nics)++;
    }

    *nics = calloc(*num_nics, sizeof(**nics));
    assert(*nics);

    /* loop through results again and populate array */
    i = 0;
    for (cur = info; cur; cur = cur->next) {
        if (cur->nic && cur->nic->bus_attr
            && cur->nic->bus_attr->bus_type == FI_BUS_PCI) {
            assert(i < *num_nics);
            struct fi_pci_attr pci = cur->nic->bus_attr->attr.pci;
            strcpy((*nics)[i].iface_name, cur->domain_attr->name);
            (*nics)[i].domain_id   = pci.domain_id;
            (*nics)[i].bus_id      = pci.bus_id;
            (*nics)[i].device_id   = pci.device_id;
            (*nics)[i].function_id = pci.function_id;
            i++;
        }
    }

    fi_freeinfo(info);
    fi_freeinfo(hints);

    return (0);
}

static int find_cores(struct options* opts,
                      pid_t*          pid,
                      int*            num_cores,
                      int*            num_numa,
                      int*            num_packages,
                      int*            current_core,
                      int*            current_numa,
                      int*            current_package)
{
    hwloc_topology_t     topology;
    hwloc_obj_t          package;
    hwloc_obj_t          covering;
    hwloc_cpuset_t       last_cpu;
    hwloc_nodeset_t      last_numa;
    hwloc_const_bitmap_t cset_all;
    hwloc_const_bitmap_t nset_all;
    int                  ret;

    /* local process info */
    *pid = getpid();

    /* get topology */
    hwloc_topology_init(&topology);
    hwloc_topology_load(topology);

    /* query all PUs */
    cset_all      = hwloc_topology_get_complete_cpuset(topology);
    nset_all      = hwloc_topology_get_complete_nodeset(topology);
    *num_cores    = hwloc_bitmap_weight(cset_all);
    *num_numa     = hwloc_bitmap_weight(nset_all);
    *num_packages = count_packages(&topology);

    /* query where this process is running */
    last_cpu = hwloc_bitmap_alloc();
    if (!last_cpu) {
        fprintf(stderr, "hwloc_bitmap_alloc() failure.\n");
        return (-1);
    }
    last_numa = hwloc_bitmap_alloc();
    if (!last_numa) {
        fprintf(stderr, "hwloc_bitmap_alloc() failure.\n");
        return (-1);
    }
    ret = hwloc_get_last_cpu_location(topology, last_cpu, HWLOC_CPUBIND_THREAD);
    if (ret < 0) {
        fprintf(stderr, "hwloc_get_last_cpu_location() failure.\n");
        return (-1);
    }
    /* extract the index from the bitmap */
    *current_core = hwloc_bitmap_first(last_cpu);
    hwloc_cpuset_to_nodeset(topology, last_cpu, last_numa);
    *current_numa = hwloc_bitmap_first(last_numa);
    covering      = hwloc_get_obj_covering_cpuset(topology, last_cpu);
    package
        = hwloc_get_ancestor_obj_by_type(topology, HWLOC_OBJ_PACKAGE, covering);
    assert(package->type == HWLOC_OBJ_PACKAGE);
    *current_package = package->os_index;

    hwloc_bitmap_free(last_cpu);
    hwloc_bitmap_free(last_numa);
    hwloc_topology_destroy(topology);

    return (0);
}

static int check_locality(struct options* opts,
                          int             num_cores,
                          int             num_numa,
                          int             num_packages,
                          int             num_nics,
                          struct nic*     nics)
{
    int              i;
    int              j;
    hwloc_cpuset_t   cpu;
    hwloc_nodeset_t  numa;
    hwloc_obj_t      non_io_ancestor;
    hwloc_obj_t      package_ancestor;
    hwloc_obj_t      pci_dev;
    hwloc_topology_t topology;
    int              ret;

    hwloc_topology_init(&topology);
    ret = hwloc_topology_set_io_types_filter(topology,
                                             HWLOC_TYPE_FILTER_KEEP_IMPORTANT);
    assert(ret == 0);
    hwloc_topology_load(topology);

    cpu  = hwloc_bitmap_alloc();
    numa = hwloc_bitmap_alloc();

    printf("\nCore locality map:\n");
    printf("\t#<name> <core mask...>\n");

    /* loop through each nic and find its first non-io ancestor */
    for (i = 0; i < num_nics; i++) {
        pci_dev = hwloc_get_pcidev_by_busid(topology, nics[i].domain_id,
                                            nics[i].bus_id, nics[i].device_id,
                                            nics[i].function_id);
        if (pci_dev)
            non_io_ancestor = hwloc_get_non_io_ancestor_obj(topology, pci_dev);
        else {
            fprintf(stderr, "Error: could not find pci_dev in topology.\n");
            return (-1);
        }

        /* loop through every possible core id (assume they go from 0 to
         * num_cores-1) and check if it reports its locality to each nic.
         */
        printf("\t%s ", nics[i].iface_name);
        for (j = 0; j < num_cores; j++) {
            /* construct a cpu bitmap for core N */
            hwloc_bitmap_only(cpu, j);

            /* see if it is in the cpuset */
            if (non_io_ancestor
                && hwloc_bitmap_isincluded(cpu, non_io_ancestor->cpuset))
                printf("1");
            else
                printf("0");
        }
        printf("\n");
    }

    printf("\nNUMA locality map:\n");
    printf("\t#<name> <NUMA mask...>\n");

    /* loop through each nic and find its first non-io ancestor */
    for (i = 0; i < num_nics; i++) {
        pci_dev = hwloc_get_pcidev_by_busid(topology, nics[i].domain_id,
                                            nics[i].bus_id, nics[i].device_id,
                                            nics[i].function_id);
        if (pci_dev)
            non_io_ancestor = hwloc_get_non_io_ancestor_obj(topology, pci_dev);
        else {
            fprintf(stderr, "Error: could not find pci_dev in topology.\n");
            return (-1);
        }

        /* loop through every possible numa id and check if it reports its
         * locality to each nic.
         */
        printf("\t%s ", nics[i].iface_name);
        for (j = 0; j < num_numa; j++) {
            /* construct a cpu bitmap for NUMA N */
            hwloc_bitmap_only(numa, j);

            /* see if it is in the cpuset */
            if (non_io_ancestor
                && hwloc_bitmap_isincluded(numa, non_io_ancestor->nodeset))
                printf("1");
            else
                printf("0");
        }
        printf("\n");
    }

    printf("\nPackage locality map:\n");
    printf("\t#<name> <Package mask...>\n");

    /* loop through each nic and find its first package ancestor */
    for (i = 0; i < num_nics; i++) {
        pci_dev = hwloc_get_pcidev_by_busid(topology, nics[i].domain_id,
                                            nics[i].bus_id, nics[i].device_id,
                                            nics[i].function_id);
        if (pci_dev)
            package_ancestor = hwloc_get_ancestor_obj_by_type(
                topology, HWLOC_OBJ_PACKAGE, pci_dev);
        else {
            fprintf(stderr, "Error: could not find pci_dev in topology.\n");
            return (-1);
        }

        /* loop through every possible package id note if it is the ancestor
         * to this device.
         */
        printf("\t%s ", nics[i].iface_name);
        for (j = 0; j < num_packages; j++) {
            if (j == package_ancestor->os_index)
                printf("1");
            else
                printf("0");
        }
        printf("\n");
    }

    hwloc_bitmap_free(cpu);
    hwloc_bitmap_free(numa);
    hwloc_topology_destroy(topology);

    return (0);
}

static int count_packages(hwloc_topology_t* topology)
{
    hwloc_obj_t obj = NULL;
    hwloc_obj_t root;
    int         package_count = 0;

    root = hwloc_get_root_obj(*topology);
    do {
        obj = hwloc_get_next_child(*topology, root, obj);
        if (obj && obj->type == HWLOC_OBJ_PACKAGE) { package_count++; }
    } while (obj);

    return (package_count);
}
