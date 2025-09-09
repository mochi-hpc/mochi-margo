/**
 * @file mochi-plumber.c
 *
 * (C) The University of Chicago
 *
 * See COPYRIGHT in top-level directory.
 */

#include "margo-config-private.h"

#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sched.h>
#include <sys/stat.h>
#include <sys/file.h>
#include <sys/types.h>
#include <rdma/fabric.h>
#include <rdma/fi_errno.h>
#include <hwloc.h>

struct bucket {
    int    num_nics;
    char** nics;
};

static int select_nic(hwloc_topology_t* topology,
                      const char*       bucket_policy,
                      const char*       nic_policy,
                      int               nbuckets,
                      struct bucket*    buckets,
                      const char**      out_nic);
static int select_nic_roundrobin(int            bucket_idx,
                                 struct bucket* bucket,
                                 const char**   out_nic);
static int
select_nic_random(int bucket_idx, struct bucket* bucket, const char** out_nic);
static int  select_nic_bycore(hwloc_topology_t* topology,
                              int               bucket_idx,
                              struct bucket*    bucket,
                              const char**      out_nic);
static int  select_nic_byset(hwloc_topology_t* topology,
                             int               bucket_idx,
                             struct bucket*    bucket,
                             const char**      out_nic);
static int  count_packages(hwloc_topology_t* topology);
static int  setup_buckets(hwloc_topology_t* topology,
                          const char*       bucket_policy,
                          int*              nbuckets,
                          struct bucket**   buckets);
static void release_buckets(int nbuckets, struct bucket* buckets);

static char* canonicalize_addr_string(const char* in_address)
{
    char* found = NULL;
    char* canon = NULL;

    found = strstr(in_address, "://");
    if (found) return (strdup(in_address));

    /* assume that if there is no :// present in the address string, then
     * the string must just be an na identifier for Mercury.  Append a "://"
     * to a new string and return it.
     */
    canon = malloc(strlen(in_address) + 4);
    if (!canon) return (NULL);
    sprintf(canon, "%s://", in_address);
    return (canon);
}

/* NOTE: this function is not likely to fail unless it is given invalid
 * parameters.  Otherwise it is a "best effort" function that will at lease
 * simply pass along the original address if the hardware probe fails.
 */
int mochi_plumber_resolve_nic(const char* in_address,
                              const char* bucket_policy,
                              const char* nic_policy,
                              char**      out_address)
{

    int              nbuckets = 0;
    hwloc_topology_t topology;
    struct bucket*   buckets = NULL;
    int              ret;
    int              i;
    const char*      selected_nic;
    char*            canon_address;

    canon_address = canonicalize_addr_string(in_address);
    if (!canon_address) return (-1);

    /* skip resolution if either policy is set to passthrough */
    if (strcmp(nic_policy, "passthrough") == 0
        || strcmp(bucket_policy, "passthrough") == 0) {
        *out_address = canon_address;
        return (0);
    }

    /* for now we only manipulate CXI addresses */
    if (strncmp(canon_address, "cxi", strlen("cxi")) != 0
        && strncmp(canon_address, "ofi+cxi", strlen("ofi+cxi")) != 0) {
        /* don't know what this is; just pass it through */
        *out_address = canon_address;
        return (0);
    }

    /* check to make sure the input address is not specific already */
    if (canon_address[strlen(canon_address) - 1] != '/'
        || canon_address[strlen(canon_address) - 2] != '/') {
        /* the address is already resolved to some degree; don't touch it */
        *out_address = canon_address;
        return (0);
    }

    /* get topology */
    hwloc_topology_init(&topology);
    hwloc_topology_set_io_types_filter(topology,
                                       HWLOC_TYPE_FILTER_KEEP_IMPORTANT);
    hwloc_topology_load(topology);

    /* divide up NICs into buckets that we will later draw from */
    ret = setup_buckets(&topology, bucket_policy, &nbuckets, &buckets);
    if (ret < 0) {
        hwloc_topology_destroy(topology);
        *out_address = canon_address;
        return (0);
    }

    /* sanity check: every bucket must have at least one NIC */
    for (i = 0; i < nbuckets; i++) {
        if (buckets[i].num_nics < 1) {
            // fprintf(stderr, "Error: bucket %d has no NICs\n", i);

            /* If we hit this point, then the node configuration is such
             * that we shouldn't be attempting to select network cards with
             * the specified policy (some buckets have no network cards
             * assigned to them).  Silently pass through input address.
             *
             * TODO: should this be a warning?  The "all" bucket policy
             * would have been fine.  Does matter on any known systems as of
             * December 2024.
             */
            release_buckets(nbuckets, buckets);
            hwloc_topology_destroy(topology);
            *out_address = canon_address;
            return (0);
        }
    }

    ret = select_nic(&topology, bucket_policy, nic_policy, nbuckets, buckets,
                     &selected_nic);
    if (ret < 0) {
        fprintf(stderr, "Error: failed to select NIC.\n");
        release_buckets(nbuckets, buckets);
        hwloc_topology_destroy(topology);
        *out_address = canon_address;
        return (0);
    }

    /* generate new address with specific nic */
    *out_address = malloc(strlen(canon_address) + strlen(selected_nic) + 1);
    sprintf(*out_address, "%s%s", canon_address, selected_nic);

    release_buckets(nbuckets, buckets);
    hwloc_topology_destroy(topology);

    free(canon_address);
    return (0);
}

static int select_nic(hwloc_topology_t* topology,
                      const char*       bucket_policy,
                      const char*       nic_policy,
                      int               nbuckets,
                      struct bucket*    buckets,
                      const char**      out_nic)
{
    int             bucket_idx = 0;
    int             ret;
    hwloc_cpuset_t  last_cpu;
    hwloc_nodeset_t last_numa;
    hwloc_obj_t     package;
    hwloc_obj_t     covering;

    /* figure out which bucket to draw from */
    if (nbuckets == 1)
        bucket_idx = 0;
    else {
        if (strcmp(bucket_policy, "numa") == 0) {
            last_cpu  = hwloc_bitmap_alloc();
            last_numa = hwloc_bitmap_alloc();
            assert(last_cpu && last_numa);

            /* select a bucket based on the numa domain that this process is
             * executing in
             */
            ret = hwloc_get_last_cpu_location(*topology, last_cpu,
                                              HWLOC_CPUBIND_THREAD);
            if (ret < 0) {
                hwloc_bitmap_free(last_cpu);
                hwloc_bitmap_free(last_numa);
                fprintf(stderr, "hwloc_get_last_cpu_location() failure.\n");
                return (-1);
            }
            hwloc_cpuset_to_nodeset(*topology, last_cpu, last_numa);
            bucket_idx = hwloc_bitmap_first(last_numa);
            assert(bucket_idx < nbuckets);

            hwloc_bitmap_free(last_cpu);
            hwloc_bitmap_free(last_numa);
        } else if (strcmp(bucket_policy, "package") == 0) {
            last_cpu = hwloc_bitmap_alloc();
            assert(last_cpu);

            /* select a bucket based on the package that this process is
             * executing in
             */
            ret = hwloc_get_last_cpu_location(*topology, last_cpu,
                                              HWLOC_CPUBIND_THREAD);
            if (ret < 0) {
                hwloc_bitmap_free(last_cpu);
                fprintf(stderr, "hwloc_get_last_cpu_location() failure.\n");
                return (-1);
            }
            covering = hwloc_get_obj_covering_cpuset(*topology, last_cpu);
            package  = hwloc_get_ancestor_obj_by_type(
                *topology, HWLOC_OBJ_PACKAGE, covering);

            bucket_idx = package->os_index;
            assert(bucket_idx < nbuckets);

            hwloc_bitmap_free(last_cpu);
        } else {
            fprintf(stderr, "Error: inconsistent bucket policy %s.\n",
                    bucket_policy);
            return (-1);
        }
    }

    /* select a NIC from within the chosen bucket */
    if (buckets[bucket_idx].num_nics == 1) {
        *out_nic = buckets[bucket_idx].nics[0];
        return (0);
    }

    if (strcmp(nic_policy, "roundrobin") == 0) {
        ret = select_nic_roundrobin(bucket_idx, &buckets[bucket_idx], out_nic);
    } else if (strcmp(nic_policy, "random") == 0) {
        ret = select_nic_random(bucket_idx, &buckets[bucket_idx], out_nic);
    } else if (strcmp(nic_policy, "bycore") == 0) {
        ret = select_nic_bycore(topology, bucket_idx, &buckets[bucket_idx],
                                out_nic);
    } else if (strcmp(nic_policy, "byset") == 0) {
        ret = select_nic_byset(topology, bucket_idx, &buckets[bucket_idx],
                               out_nic);
    } else {
        fprintf(stderr, "Error: unknown nic_policy \"%s\"\n", nic_policy);
        ret = -1;
    }

    return (ret);
}

static int select_nic_roundrobin(int            bucket_idx,
                                 struct bucket* bucket,
                                 const char**   out_nic)
{
    int  ret;
    char tokenpath[256] = {0};
    int  fd;
    int  nic_idx = -1;

    snprintf(tokenpath, 256, "/tmp/%s-mochi-plumber", getlogin());
    ret = mkdir(tokenpath, 0700);
    if (ret != 0 && errno != EEXIST) {
        perror("mkdir");
        fprintf(stderr, "Error: failed to create %s\n", tokenpath);
        return (-1);
    }

    snprintf(tokenpath, 256, "/tmp/%s-mochi-plumber/%d", getlogin(),
             bucket_idx);
    fd = open(tokenpath, O_RDWR | O_CREAT | O_SYNC, 0600);
    if (fd < 0) {
        perror("open");
        fprintf(stderr, "Error: failed to open %s\n", tokenpath);
    }

    /* exlusive lock file */
    flock(fd, LOCK_EX);

    /* read most recently used nic index */
    /* note: if value hasn't been set yet (pread returns 0), nic_idx was
     * initialized to -1
     */
    ret = pread(fd, &nic_idx, sizeof(nic_idx), 0);
    if (ret < 0) {
        perror("pread");
        fprintf(stderr, "Error: failed to read %s\n", tokenpath);
        flock(fd, LOCK_UN);
        return (-1);
    }
    /* select next nic */
    nic_idx = (nic_idx + 1) % (bucket->num_nics);
    /* write selection back to file */
    ret = pwrite(fd, &nic_idx, sizeof(nic_idx), 0);
    if (ret < 0) {
        perror("pwrite");
        fprintf(stderr, "Error: failed to write %s\n", tokenpath);
        flock(fd, LOCK_UN);
        return (-1);
    }
    flock(fd, LOCK_UN);

    *out_nic = bucket->nics[nic_idx];
    return (0);
}

static int
select_nic_random(int bucket_idx, struct bucket* bucket, const char** out_nic)
{
    int nic_idx = -1;

    /* we only need to worry about unique seeding within a single node, so
     * its sufficient to just use the pid
     */
    srand(getpid());
    nic_idx = rand() % bucket->num_nics;

    *out_nic = bucket->nics[nic_idx];
    return (0);
}

/* static mapping based on what specific core the process is presently
 * runnign on.
 */
static int select_nic_bycore(hwloc_topology_t* topology,
                             int               bucket_idx,
                             struct bucket*    bucket,
                             const char**      out_nic)
{
    int            nic_idx = -1;
    int            ret;
    hwloc_cpuset_t last_cpu;

    last_cpu = hwloc_bitmap_alloc();
    assert(last_cpu);

    ret = hwloc_get_last_cpu_location(*topology, last_cpu,
                                      HWLOC_CPUBIND_THREAD);
    if (ret < 0) {
        hwloc_bitmap_free(last_cpu);
        fprintf(stderr, "hwloc_get_last_cpu_location() failure.\n");
        return (-1);
    }
    nic_idx = hwloc_bitmap_first(last_cpu) % bucket->num_nics;
    hwloc_bitmap_free(last_cpu);

    *out_nic = bucket->nics[nic_idx];
    return (0);
}

/* static mapping based on the set of cores the process is allowed to run on */
static int select_nic_byset(hwloc_topology_t* topology,
                            int               bucket_idx,
                            struct bucket*    bucket,
                            const char**      out_nic)
{
    int            nic_idx = -1;
    int            ret;
    hwloc_cpuset_t cpuset;

    cpuset = hwloc_bitmap_alloc();
    assert(cpuset);

    ret = hwloc_get_cpubind(*topology, cpuset, HWLOC_CPUBIND_PROCESS);
    if (ret < 0) {
        hwloc_bitmap_free(cpuset);
        fprintf(stderr, "hwloc_get_cpuset_location() failure.\n");
        return (-1);
    }
    nic_idx = hwloc_bitmap_first(cpuset) % bucket->num_nics;
    hwloc_bitmap_free(cpuset);

    *out_nic = bucket->nics[nic_idx];
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

static int setup_buckets(hwloc_topology_t* topology,
                         const char*       bucket_policy,
                         int*              nbuckets,
                         struct bucket**   buckets)
{
    hwloc_const_bitmap_t nset_all;
    struct fi_info*      info;
    struct fi_info*      hints;
    struct fi_info*      cur;
    int                  ret;
    hwloc_obj_t          pci_dev;
    int                  bucket_idx = 0;
    hwloc_obj_t          non_io_ancestor;
    hwloc_obj_t          package_ancestor;
    int                  i;

    /* figure out how many buckets there will be */
    if (strcmp(bucket_policy, "all") == 0) {
        /* just one big bucket */
        *nbuckets = 1;
    } else if (strcmp(bucket_policy, "numa") == 0) {
        /* we need to query number of numa domains and make a bucket for
         * each
         */
        nset_all  = hwloc_topology_get_complete_nodeset(*topology);
        *nbuckets = hwloc_bitmap_weight(nset_all);
    } else if (strcmp(bucket_policy, "package") == 0) {
        /* query number of packages and make a bucket for each */
        *nbuckets = count_packages(topology);
    } else {
        fprintf(stderr,
                "mochi_plumber_resolve_nic: unknown bucket policy \"%s\"\n",
                bucket_policy);
        return (-1);
    }

    *buckets = calloc(*nbuckets, sizeof(**buckets));
    if (!*buckets) { return (-1); }

    /* query libfabric for interfaces */
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
    /* only supporting cxi for now */
    hints->fabric_attr->prov_name = strdup("cxi");
    hints->ep_attr->protocol      = FI_PROTO_CXI;
    ret = fi_getinfo(FI_VERSION(FI_MAJOR_VERSION, FI_MINOR_VERSION), NULL, NULL,
                     0, hints, &info);
    if (ret != 0) {
        fi_freeinfo(hints);
        free(*buckets);
        return (ret);
    }
    fi_freeinfo(hints);

    /* iterate through interfaces and assign to buckets */
    for (cur = info; cur; cur = cur->next) {
        if (cur->nic && cur->nic->bus_attr
            && cur->nic->bus_attr->bus_type == FI_BUS_PCI) {

            /* look for this device in hwloc topology */
            struct fi_pci_attr pci = cur->nic->bus_attr->attr.pci;
            pci_dev = hwloc_get_pcidev_by_busid(*topology, pci.domain_id,
                                                pci.bus_id, pci.device_id,
                                                pci.function_id);
            if (!pci_dev) {
                fprintf(stderr, "Error: can't find %s in hwloc topology.\n",
                        cur->domain_attr->name);
                fi_freeinfo(info);
                for (i = 0; i < *nbuckets; i++) {
                    if ((*buckets)[i].nics) free((*buckets)[i].nics);
                }
                free(*buckets);
                return (-1);
            }
            if (*nbuckets == 1) {
                /* add to the global bucket */
                bucket_idx = 0;
            } else if (strcmp(bucket_policy, "numa") == 0) {
                /* figure out what numa domain this maps to and put it in
                 * that bucket
                 */
                non_io_ancestor
                    = hwloc_get_non_io_ancestor_obj(*topology, pci_dev);
                bucket_idx = hwloc_bitmap_first(non_io_ancestor->nodeset);
            } else if (strcmp(bucket_policy, "package") == 0) {
                /* figure out what package this maps to and put it in that
                 * bucket
                 */
                package_ancestor = hwloc_get_ancestor_obj_by_type(
                    *topology, HWLOC_OBJ_PACKAGE, pci_dev);
                bucket_idx = package_ancestor->os_index;
            }

            (*buckets)[bucket_idx].num_nics++;
            (*buckets)[bucket_idx].nics
                = realloc((*buckets)[bucket_idx].nics,
                          (*buckets)[bucket_idx].num_nics
                              * sizeof(*(*buckets)[bucket_idx].nics));
            assert((*buckets)[bucket_idx].nics);
            (*buckets)[bucket_idx].nics[(*buckets)[bucket_idx].num_nics - 1]
                = strdup(cur->domain_attr->name);
            assert((*buckets)[bucket_idx]
                       .nics[(*buckets)[bucket_idx].num_nics - 1]);
        }
    }
    fi_freeinfo(info);

    return (0);
}

static void release_buckets(int nbuckets, struct bucket* buckets)
{
    int i;

    for (i = 0; i < nbuckets; i++) {
        if (buckets[i].nics) free(buckets[i].nics);
    }
    free(buckets);

    return;
}
