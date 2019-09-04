# Margo

Margo provides Argobots-aware wrappers to common Mercury library functions.
It simplifies service development by expressing Mercury operations as
conventional blocking functions so that the caller does not need to manage
progress loops or callback functions.

Internally, Margo suspends callers after issuing a Mercury operation, and
automatically resumes them when the operation completes.  This allows
other concurrent user-level threads to make progress while Mercury
operations are in flight without consuming operating system threads.
The goal of this design is to combine the performance advantages of
Mercury's native event-driven execution model with the progamming
simplicity of a multi-threaded execution model.

See the following for more details about Mercury and Argobots: 

* https://mercury-hpc.github.io/
* https://collab.mcs.anl.gov/display/ARGOBOTS/Argobots+Home

A companion library called abt-io provides similar wrappers for POSIX I/O
functions: https://xgitlab.cels.anl.gov/sds/abt-io

Note that Margo should be compatible with any Mercury transport (NA plugin).  The documentation assumes the use of the NA SM (shared memory) plugin that is built into Mercury for simplicity.  This plugin is only valid for communication between processes on a single node.  See [Using Margo with other Mercury NA plugins](##using-margo-with-other-mercury-na-plugins) for information on other configuration options.

##  Dependencies

* mercury  (git clone --recurse-submodules https://github.com/mercury-hpc/mercury.git)
* argobots (git clone https://github.com/pmodels/argobots.git)

### Recommended Mercury build options

* Mercury must be compiled with -DMERCURY_USE_BOOST_PP:BOOL=ON to enable the
  Boost preprocessor macros for encoding.
* Mercury should be compiled with -DMERCURY_USE_SELF_FORWARD:BOOL=ON in order to enable
  fast execution path for cases in which a Mercury service is linked into the same
  executable as the client

Example Mercury compilation:

```
mkdir build
cd build
cmake -DMERCURY_USE_SELF_FORWARD:BOOL=ON \
 -DBUILD_TESTING:BOOL=ON -DMERCURY_USE_BOOST_PP:BOOL=ON \
 -DCMAKE_INSTALL_PREFIX=/home/pcarns/working/install \
 -DBUILD_SHARED_LIBS:BOOL=ON -DCMAKE_BUILD_TYPE:STRING=Debug ../
```

## Building

Example configuration:

    ../configure --prefix=/home/pcarns/working/install \
        PKG_CONFIG_PATH=/home/pcarns/working/install/lib/pkgconfig \
        CFLAGS="-g -Wall"

## Running examples

The examples subdirectory contains:

* margo-example-client.c: an example client
* margo-example-server.c: an example server
* my-rpc.[ch]: an example RPC definition

The following example shows how to execute them.  Note that when the server starts it will display the address that the client can use to connect to it.


```
$ examples/margo-example-server na+sm://
# accepting RPCs on address "na+sm://13367/0"
Got RPC request with input_val: 0
Got RPC request with input_val: 1
Got RPC request with input_val: 2
Got RPC request with input_val: 3
Got RPC request to shutdown

$ examples/margo-example-client na+sm://13367/0
ULT [0] running.
ULT [1] running.
ULT [2] running.
ULT [3] running.
Got response ret: 0
ULT [0] done.
Got response ret: 0
ULT [1] done.
Got response ret: 0
ULT [2] done.
Got response ret: 0
ULT [3] done.
```

The client will issue 4 concurrent RPCs to the server and wait for them to
complete.

## Running tests

`make check`

## Using Margo with the other NA plugins

You can use either the CCI NA plugin or BMI NA plugin to use either the CCI or BMI library for remote communication.  See the [Mercury documentation](http://mercury-hpc.github.io/documentation/) for details and status.

### CCI

Add the -DNA_USE_CCI:BOOL=ON option to the Mercury configuration.

You must then use addresses appropriate for your transport at run time when
executing Margo examples.  Examples for server "listening" addresses:

* cci+tcp://3344 # for TCP/IP, listening on port 3344
* cci+verbs://3344 # for InfiniBand, listening on port 3344
* cci+sm://1/1 # for shared memory, listening on CCI SM address 1/1

Examples for clients to specify to attach to the above:

* cci+tcp://localhost:3344 # for TCP/IP, assuming localhost use
* cci+verbs://192.168.1.78:3344 # for InfiniBand, note that you *must* use IP
  address rather than hostname
* cci+sm:///tmp/cci/sm/`hostname`/1/1 # note that this is a full path to local
  connection information.  The last portion of the path should match the
  address specified above

### BMI

Add the -DNA_USE_BMI:BOOL=ON option to the Mercury configuration.  You may
also need to specify
-DBMI_INCLUDE_DIR:PATH=/home/pcarns/working/install/include and -DBMI_LIBRARY:FILEPATH=/home/pcarns/working/install/lib/libbmi.a (adjusting the paths as appropriate for your system).

We do not recommend using any BMI methods besides TCP.  It's usage is very similar to the CCI/TCP examples above, except that "bmi+" should be substituted for "cci+".

## Instrumentation

See the [Instrumentation documentation](doc/instrumentation.md) for
information on how to extract diagnostic instrumentation from Margo.

## Design details

![Margo architecture](doc/fig/margo-diagram.png)

Margo provides Argobots-aware wrappers to common Mercury library functions
like HG_Forward(), HG_Addr_lookup(), and HG_Bulk_transfer().  The wrappers
have the same arguments as their native Mercury counterparts except that no
callback function is specified.  Each function blocks until the operation 
is complete.  The above diagram illustrates a typical control flow.

Margo launches a long-running user-level thread internally to drive
progress on Mercury and execute Mercury callback functions (labeled
```__margo_progress()``` above).  This thread can be assigned to a
dedicated Argobots execution stream (i.e., an operating system thread)
to drive network progress with a dedicated core.  Otherwise it will be
automatically scheduled when the caller's execution stream is blocked
waiting for network events as shown in the above diagram.

Argobots eventual constructs are used to suspend and resume user-level
threads while Mercury operations are in flight.

Margo allows several different threading/multicore configurations:
* The progress loop can run on a dedicated operating system thread or not
* Multiple Margo instances (and thus progress loops) can be 
  executed on different operating system threads
* (for servers) a single Margo instance can launch RPC handlers
  on different operating system threads

## V0.2 API changes

The following list provides details on new changes to the Margo API starting in
version 0.2:

* `margo_init()` is much more simplified and initializes Mercury and Argobots on behalf of
  the user (with `margo_finalize()` finalizing them in that case)
    * the prototype is `margo_init(addr_string, MARGO_CLIENT_MODE | MARGO_SERVER_MODE, use_progress_thread, num_rpc_handler_threads)`
    * `margo_init_pool()` is still available as an advanced initialize routine, where the
      user must initialize Mercury/Argobots and pass in an `HG_Context` and `ABT_pools` for
      RPC handlers and the progress loop
* Margo now has its own RPC registration functions that should be used for registering
  RPCs (as Margo is now attaching internal state to RPCs)
    * `MARGO_REGISTER` is basically equivalent to `MERCURY_REGISTER`, except it takes a
      `margo_instance_id` rather than a Mercury class
    * `MARGO_REGISTER_MPLEX` is mostly the same as above, but allows a user to specify
      an `ABT_pool` to use for a given RPC type
* relatedly, Margo users should now use `margo_register_data()` (rather than `HG_Register_data()`)
  for associating user data with an RPC type
    * like Mercury, there is a corresponding `margo_registered_data()` call to retrieve the user pointer
* the following Mercury-like functions are now defined within Margo, although it is
  still safe to just use the Mercury calls directly (most are just `#define`s to the
  corresponding Mercury call, anyway):
    * `margo_registered_disable_response()`
    * `margo_addr_free()`
    * `margo_addr_self()`
    * `margo_addr_dup()`
    * `margo_addr_to_string()`
    * `margo_create()`
    * `margo_destroy()`
        * Note that `margo_create()`/`margo_destroy()` are enhancing `HG_Create()`/`HG_Destroy()` with a
          cache of reusable handles, so they may be preferable to the Mercury calls
    * `margo_bulk_create()`
    * `margo_bulk_free()`
    * `margo_bulk_deserialize()`
* `margo_hg_handle_get_instance()` and `margo_hg_info_get_instance()` calls have been added for
  retrieving a `margo_instance_id` given a received handle or the HG info structure associated
  with the handle

