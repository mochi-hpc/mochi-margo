.. _margo_09_config:

Using Margo's JSON configuration
================================

In addition to :code:`margo_init`, Margo provides a second
initialization function: :code:`margo_init_ext`. This function
takes the address of the process (or the protocol to use),
the mode (:code:`MARGO_CLIENT_MODE` or :code:`MARGO_SERVER_MODE`),
as well as a pointer to a :code:`struct margo_init_info` instance.
This instance can be used to provide any of the following.

- :code:`json_config`: a JSON-formatted, null-terminated string;
- :code:`progress_pool`: an existing Argobots pool in which to run
  the Mercury progress loop;
- :code:`rpc_pool`: an existing Argobots pool in which to run RPC
  handlers by default;
- :code:`hg_class`: an existing Mercury class;
- :code:`hg_context`: an existing Mercury context;
- :code:`hg_init_info`: an :code:`hg_init_info` structure to pass
  to Mercury when initializing its class.
- :code:`parent_mid`: an existing Margo instance whose Argobots
  environment (pools and execution streams) the new instance will
  share. See :ref:`margo_14_parent` for details.

The bellow code examplifies the use of the :code:`margo_init_ext`
function.

.. container:: header

    .. literalinclude:: ../../examples/margo/09_config/server.c
       :language: cpp

This code also shows the use of the :code:`margo_get_config` function,
which returns a JSON string representing the *exact* internal
configuration of the Margo instance. When called from this program,
for example, we get the following configuration.

.. code-block:: json

   {
     "progress_timeout_ub_msec":100,
     "progress_spindown_msec":10,
     "enable_profiling":false,
     "enable_diagnostics":false,
     "handle_cache_size":32,
     "profile_sparkline_timeslice_msec":1000,
     "plumber":{
       "bucket_policy":"package",
       "nic_policy":"roundrobin"
     },
     "mercury":{
       "version":"2.0.0",
       "request_post_incr":256,
       "request_post_init":256,
       "auto_sm":false,
       "no_bulk_eager":false,
       "no_loopback":false,
       "stats":false,
       "na_no_block":false,
       "na_no_retry":false,
       "max_contexts":1,
       "address":"ofi+tcp;ofi_rxm://10.0.2.15:42925",
       "listening":true
       "auth_key":"0:0"
     },
     "argobots":{
       "abt_mem_max_num_stacks":8,
       "abt_thread_stacksize":2097152,
       "version":"1.0",
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


Such a configuration string can be passed as the :code:`json_config`
field of the :code:`margo_init_info` structure to reinitialize the
margo instance in the exact same manner. It can also be modified to
tune Margo's internal configuration.

.. important::
   This configuration is also very useful to provide to Mochi
   developers whenever you encounter performance issues with Margo
   or Thallium.

Let's examine this configuration in more details.

.. important::
   None of the configuration fields is mandatory. You may provide a configuration
   including just the fields you want to change from their default values
   (shown above).

- :code:`progress_spindown_msec` is the "spindown" time, or the number of
  milliseconds that Margo will continue to actively poll for new events once
  all pending events are completed.  This improves latency and power
  consumption for sequential workloads by preventing Margo from entering
  idle mode too quickly.
- :code:`progress_timeout_ub_msec` is the number of milliseconds
  that will be passed to Mercury's progress function as maximum timeout;
- :code:`enable_profiling` enables internal profiling (extensive statistics
  about each RPC);
- :code:`enable_diagnostics` enables diagnostics collection (simple statistics);
- :code:`handle_cache_size` is the size of an internal cache that lets Margo
  reuse RPC handles instead of allocating new ones;
- :code:`profiling_sparkline_timeslice_msec` is the granularity of data collection
  for sparklines (when profiling is enabled);
- The :code:`plumber` section (if present) governs how Margo will select
  network cards if none is specified by the user.  Right now it only
  influences systems using Slingshot (CXI) networks with more than one
  network card per node.

  - :code:`bucket_policy` defines how network cards are differentiated into
    buckets with different properties.  Options are "package", which groups
    network cards by locality to the CPU package (a.k.a. socket).  Other
    options include "numa", which groups cards by NUMA domain, "all" which
    puts all available network cards into a single bucket, and
    "passthrough", which instructs Margo to pass network addresses
    unmodified to the network stack.
  - :code:`nic_policy` defines how cards are selected from the buckets
    defined above.  "roundrobin" cycles evenly among all network cards in a
    given bucket. "random" selects network cards randomly.  "bycore" selects
    network cards based on a static mapping of what core the process is
    executing on to a network card, while "byset" is similar to "bycore" but
    considers the cpuset that the process is elegible to run on rather than
    the one core that it is currently running on.
- The :code:`mercury` section provides Mercury parameters:

  - :code:`version` will be filled by Margo and does not need to be provided;
  - :code:`request_post_init` is the number of requests for unexpected messages that
    Mercury will initially post;
  - :code:`request_post_incr` is the increment to the above number of requests, when
    Mercury runs out of posted requests at any given time;
  - :code:`auto_sm` makes Mercury automatically use shared memory when the sender
    and receiver processes are on the same node;
  - :code:`no_bulk_eager` prevents Mercury from sending bulk data in RPC if this
    data is small enough;
  - :code:`no_loopback` prevents a Mercury process from sending RPCs to itself;
  - :code:`stats` enables internal statistics collection;
  - :code:`na_no_block` makes Mercury use busy-spinning instead of blocking on
    file descriptors;
  - :code:`na_no_retry` prevents Mercury from retrying operations;
  - :code:`max_contexts` is the maximum number of Mercury contexts that can be created;
  - :code:`address` is completed by Margo to provide the process address;
  - :code:`listening` indicates whether the process is listening (server) or not (client);
  - :code:`auth_key` specifies a particular network authorization key for
    supported transports (such as Slingshot/CXI). The format is
    `service:vni:index`, where service is the network service to use, VNI is
    the VNI number to use, and index (optional) is the index of the
    system-provided SLINGSHOT environment variables to use.  See your system
    documentation for more information.  Usually this does not need to be
    explicitly specified.

- The :code:`argobots` section configures the Argobots run time:

  - :code:`abt_mem_max_num_stacks` is the maximum number of pre-allocated stacks;
  - :code:`abt_thread_stacksize` provides the default ULT stack size;
  - :code:`version` is complete by Margo to indicate the version of Argobots in use;
  - :code:`pools` is an array of pool objects. Each pool object has a name (which should
    be a valid C identifier), a kind (*fifo* or *fifo_wait*), and an access type
    (*private*, *mpmc*, *spmc*, *spsc*, or *spmc*, indicating multiple or single producers,
    and multiple or single consumers);
  - :code:`xstreams` is an array of execution streams. Each xstream has a name, a binding to
    a particular CPU (or -1 for any CPU), and affinity to some CPUs, and a scheduler.
    The scheduler has a type (*default*, *basic*, *basic_wait*, *prio*, or *randws*) and
    an array of pools (referenced either by index or by name) that the scheduler is taking work from.
    Note that one of the xstream must be named "__primary__". If no __primary__ xstream is found
    by Margo, it will automatically be added, along with a __primary__ pool.
- :code:`progress_pool` is the pool to use for Mercury to run the progress loop. It can be
  referenced by name or by index. -1 is provided to indicate that the pool is externally
  provided via the progress_pool field in the margo_init_info structure.
- :code:`rpc_pool` is the pool to use for running RPC handlers by default. It can be
  referenced by name or by index. -1 is provided to indicate that the pool is externally
  provided via the rpc_pool field in the margo_init_info structure.


The margo JSON configuration system provides a simple mechanism to
initialize and configure a bunch of things, including Mercury and Argobots.
Don't hesitate to use it instead of hard-coding these initialization steps,
it can greatly help when testing various parameters later on.

.. note::
   This configuration format is also used by Bedrock to standardize
   Margo's initialization and configuration.
