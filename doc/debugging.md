# Margo debugging

This file documents debugging capabilities that are built into the
margo library.  See the [top level README.md](../README.md) for general
information about margo.

## State dumps

``State dumps'' are point-in-time snapshots of runtime information from the
margo library and any Argobots execution streams that are under it's
control.  They may be useful to get insight from abnormal situations (RPC
hangs, for example) that are difficult to inspect with a debugger due to the
environment.

The following function can be used to generate state dumps from the margo
library at any time during execution:

```
/**
 * Appends Margo state information (including Argobots stack information) to
 * the specified file in text format.
 *
 * @param [in] mid Margo instance
 * @param [in] file output file ("-" for stdout)
 * @param [in] uniquify flag indicating if file name should have additional
 *   information added to it to make output from different processes unique
 * @param [out] resolved_file_name (ignored if NULL) pointer to char* that
 *   will be set to point to a string with the fully resolved path to the
 *   state file that was generated.  Must be freed by caller.
 * @returns void
 */
void margo_state_dump(margo_instance_id mid,
                      const char*       file,
                      int               uniquify,
                      char**            resolved_file_name);
```

This function could, for example, be triggered within an RPC in order for a
client-side administrative program to gather information about a remote
daemon.  An example of this style of usage can be found in
examples/my-rpc.c.  The function can be called within any program (client or
server) that has initialized margo.

The format of the output file is subject to change.  The information
may also be more or less detailed depending on the availability of
runtime margo options and compile time Argobots options that provide
more debugging information.

The state information is emitted in text format either to stdout or to a
text file according to the argument functions.  The file will be placed in a
subdirectory according to the MARGO_OUTPUT_DIR environment variable or the
output_dir JSON parameter.

The following Argobots compile time options can be used to add more detail
to a state dump:

* Argobots tool interface
  * Enabled at compile time via the `--enable-tool` configure option or `+tool`
    Spack variant.  This Argobots feature enables interfaces for libraries
    to extract performance information from Argobots.  Minor overhead is
    incurred by enabling this feature at compile time.  Additional overhead
    is then incurred if the feature is then activated at runtime by enabling
    Margo diagnostics mode (see below).
  * If this Argobots feature (and the Margo runtime diagnostics feature) are
    enabled, then a Margo state dump will include per-execution stream
    statistics about user-level thread response time.
* Argobots stack unwinding
  * Enabled at compile time via the `--enable-stack-unwind` configure option
    or `+stackunwind` Spack variant.  It adds minimal overhead but
    introduces an additional library dependency.
  * If Argobots stack unwinding is enabled, then the Margo state dump will
    include a human-readable backtrace of all ULTs that are currently
    awaiting execution (i.e., not including currently executing ULTs).  This
    information may be helpful for debugging RPCs that appear to have hung.

The following Margo runtime options will also enable additional information
to be added to the state file:

* Margo RPC profiling
  * This can be enabled at runtime via the `MARGO_ENABLE_PROFILING=1`
    environment variable, the `margo_profiling_start()` API function, or
    the `enable_profiling` JSON parameter.  No additional compilation
    options are needed.
  * This features gathers statistics about each combination of RPCs handled
    by the process, including the count, elapsed time, and dependency chain
    for each RPC type.
* Margo diagnostics
  * This can be enabled at runtime via the `MARGO_ENABLE_DIAGNOSTICS=1`
    environment variable, the `margo_diag_start()` API function, or
    the `enable_diagnostics` JSON parameter.  No additional compilation
    options are needed.
  * This feature gathers statistics about how much time is spent in various
    Mercury network progress functions.  If the Argobots tool interface is
    available (see above), it will also also collect Argobots performance
    statistics.

### Example state dump output

```
# Margo state dump
# Mercury address: na+sm://1005343/0
# Thu Apr 22 17:23:43 2021
# Margo configuration (JSON)
# ==========================
{
  "output_dir":"/tmp",
  "version":"0.9.2",
  "progress_timeout_ub_msec":100,
  "enable_profiling":true,
  "enable_diagnostics":true,
  "handle_cache_size":32,
  "profile_sparkline_timeslice_msec":1000,
  "mercury":{
    "version":"2.0.1",
    "request_post_incr":256,
    "request_post_init":256,
    "auto_sm":false,
    "no_bulk_eager":false,
    "no_loopback":false,
    "stats":false,
    "na_no_block":false,
    "na_no_retry":false,
    "max_contexts":1,
    "address":"na+sm://1005343/0",
    "listening":true
  },
  "argobots":{
    "abt_mem_max_num_stacks":8,
    "abt_thread_stacksize":2097152,
    "version":"1.1",
    "pools":[
      {
        "name":"__primary__",
        "kind":"fifo_wait",
        "access":"mpmc"
      }
    ],
    "xstreams":[
      {
        "name":"__primary__",
        "cpubind":-1,
        "affinity":[
        ],
        "scheduler":{
          "type":"basic_wait",
          "pools":[
            0
          ]
        }
      }
    ]
  },
  "progress_pool":0,
  "rpc_pool":0
}
# Margo instance state
# ==========================
mid->pending_operations: 1
mid->diag_enabled: 1
mid->profile_enabled: 1
# Margo diagnostics
# NOTE: this is only available if mid->diag_enabled == 1 above.  You can
#       turn this on by calling margo_diag_start() programatically, by setting
#       the MARGO_ENABLE_DIAGNOSTICS=1 environment variable, or by setting
#       the "enable_diagnostics" JSON configuration parameter.
# ==========================
# Margo diagnostics
# Addr Hash and Address Name: 18446744047964262386,na+sm://1005343/0
# Thu Apr 22 17:23:43 2021
# Function Name, Average Time Per Call, Cumulative Time, Highwatermark, Lowwatermark, Call Count
trigger_elapsed,0.000001382,0.000196218,0.000000000,0.000018358,142
progress_elapsed_zero_timeout,0.000000477,0.000000954,0.000000238,0.000000715,2
progress_elapsed_nonzero_timeout,0.098170816,13.154889345,0.000001907,0.100457430,134
bulk_create_elapsed,0.000000954,0.000001907,0.000000477,0.000001431,2
# Margo RPC profiling
# NOTE: this is only available if mid->profile_enabled == 1 above.  You can
#       turn this on by calling margo_profile_start() programatically, by
#       setting the MARGO_ENABLE_PROFILING=1 environment variable, or by setting
#       the "enable_profiling" JSON configuration parameter.
# ==========================
2
18446744047964262386,na+sm://1005343/0
0x9245,my_shutdown_rpc
0xe282,my_rpc
0xe282 ,0.000033259,57986,0,1,0.000066519,0.000009060,0.000057459,2,18446744073709551615,1,2,18446744073709551615,2,4
0xe282 ,1;
# Argobots configuration (ABT_info_print_config())
# ================================================
Argobots Configuration:
 - version: 1.1
 - # of cores: 8
 - cache line size: 64 B
 - huge page size: 2097152 B
 - max. # of ESs: 8
 - cur. # of ESs: 1
 - ES affinity: off
 - logging: off
 - debug output: off
 - print errno: off
 - valgrind support: yes
 - thread cancellation: enabled
 - task cancellation: enabled
 - thread migration: enabled
 - external thread: enabled
 - error check: enabled
 - tool interface: yes
 - wait policy: passive
 - context-switch: fcontext
 - key table entries: 4
 - default ULT stack size: 2048 KB
 - default scheduler stack size: 4096 KB
 - default scheduler event check frequency: 50
 - default scheduler sleep: off
 - default scheduler sleep duration : 100 [ns]
 - timer function: clock_gettime
Memory Pool:
 - page size for allocation: 2048 KB
 - stack page size: 8192 KB
 - max. # of stacks per ES: 8
 - max. # of descs per ES: 4096
 - large page allocation: mmap regular pages
# Argobots execution streams (ABT_info_print_all_xstreams())
# ================================================
# of created ESs: 1
== ES (0x6130000202c0) ==
rank         : 0
type         : PRIMARY
state        : RUNNING
root_ythread : 0x7fdc871fe800
root_pool    : 0x611000000e00
thread       : 0x7fdc87400000
main_sched   : 0x610000000340
ctx          :
    == XSTREAM CONTEXT (0x6130000202e8) ==
    state : UNKNOWN
# Margo Argobots profiling summary
# NOTE: this is only available if mid->diag_enabled == 1 above *and* Argobots
# has been compiled with tool interface support.  You can turn on Margo
# diagnostics at runtime by calling margo_diag_start() programatically, by
# setting the MARGO_ENABLE_DIAGNOSTICS=1 environment variable, or by setting
# the "enable_diagnostics" JSON configuration parameter. You can enable the
# Argobots tool interface by compiling Argobots with the --enable-tool or the
# +tool spack variant.
# ==========================
# Margo diagnostics (Argobots profile)
# Addr Hash and Address Name: 18446744047964262386,na+sm://1005343/0
# Thu Apr 22 17:23:43 2021
                                       Average   ES-0
Approx. ULT granularity [s]               6.58   6.58
Approx. ULT throughput [/s]              0.152  0.152
Approx. non-main scheduling ratio [%]    100.0  100.0
# of events of ULT/yield [/s]            0.380  0.380
# of events of ULT/suspend [/s]          0.228  0.228
                          Sum   ES-0  Ext
# of created ULTs           3      3    0
# of created ULTs [/s]  0.228  0.228    0
# Argobots stack dump (ABT_info_print_thread_stacks_in_pool())
#   *IMPORTANT NOTE*
# This stack dump does *not* display information about currently executing
# user-level threads.  The user-level threads shown here are awaiting
# execution due to synchronization primitives or resource constraints.
# Argobots stack unwinding: ENABLED
# ================================================
== pool (0x611000001080) ==
=== ULT (0x7fdc876000c1) ===
id        : 0
ctx       : 0x7fdc87600120
p_ctx    : 0x7fdc875ffef0
p_link   : (nil)
stack     : 0x7fdc874000c0
stacksize : 2097152
#0 0x7fdc8cd2ad80 in ythread_unwind_stack () <+16> (RSP = 0x7fdc875fff20)
#1 0x7fdc8cd280e5 in ABT_thread_yield () <+533> (RSP = 0x7fdc875fff30)
#2 0x7fdc8cd679d5 in __margo_hg_progress_fn () <+1213> (RSP = 0x7fdc875fff90)
#3 0x7fdc8cd2d0d1 in ABTD_ythread_func_wrapper () <+65> (RSP = 0x7fdc876000a0)
#4 0x7fdc8cd2d331 in make_fcontext () <+33> (RSP = 0x7fdc876000c0)
```

## Memory debugging

The same memory debugging tools that work with Argobots will also work with
Margo.  In particular, `valgrind` can be used at runtime if Argobots is
compiled with the `--enable-valgrind` configure argument or `+valgrind`
variant.  Address sanitizer also works natively with Argobots and Margo
without any additional Argobots compile-time options.

You may enable address sanitizer as follows before configuring Margo (or any
other component you wish to debug):

```
export CFLAGS="-fsanitize=address -fno-omit-frame-pointer -g -Wall" && export ASAN_OPTIONS="abort_on_error=1" && export LDFLAGS="-fsanitize=address"
```

Note that the example options given will cause the resulting executable to
be strict about memory leaks!

