# margo
margo is a library that provides Argobots bindings to the Mercury RPC
implementation.  See the following for more details about each project:

* https://collab.mcs.anl.gov/display/ARGOBOTS/Argobots+Home
* https://mercury-hpc.github.io/

Note that Margo should be compatible with any Mercury transport (NA plugin),
but we assume the use of CCI in all examples here.

##  Dependencies

* cci (git clone https://github.com/CCI/cci.git)
* mercury  (git clone --recurse-submodules https://github.com/mercury-hpc/mercury.git)
* argobots (git clone https://github.com/pmodels/argobots.git)
* abt-snoozer (git clone https://xgitlab.cels.anl.gov/sds/abt-snoozer)
* libev (e.g libev-dev package on Ubuntu or Debian)

### Recommended Mercury build options

* -DNA_CCI_USE_POLL:BOOL=ON enables the "poll" feature of CCI that will allow
  the na_cci plugin to make progress without busy spinning
* -DMERCURY_USE_CHECKSUMS:BOOL=OFF disables automatic checksumming of all
  Mercury RPC messages.  This reduces latency by removing a layer of
  integrity checking on communication.

## Building

Example configuration:

    ../configure --prefix=/home/pcarns/working/install \
        PKG_CONFIG_PATH=/home/pcarns/working/install/lib/pkgconfig \
        CFLAGS="-g -Wall"

## Setting up a CCI environment

You must set the CCI_CONFIG environment variable to point to a valid CCI
configuration file.  You can use the following example and un-comment the
appropriate section for the transport that you wish to use.  Note that there
is no need to specify a port; Mercury will dictate a port for CCI to use if
needed.

```
[mercury]
# use this example for TCP
transport = tcp
interface = lo  # switch this to eth0 or an external hostname for non-localhost use

## use this example instead for shared memory
# transport = sm

## use this example instead for InfiniBand
# transport = verbs
# interface = ib0
```

You must then use addresses appropriate for your transport at run time when
executing Margo examples.  Examples for server "listening" addresses:

* tcp://3344 # for TCP/IP, listening on port 3344
* verbs://3344 # for InfiniBand, listening on port 3344
* sm://1/1 # for shared memory, listening on CCI SM address 1/1

Examples for clients to specify to attach to the above:

* tcp://localhost:3344 # for TCP/IP, assuming localhost use
* verbs://192.168.1.78:3344 # for InfiniBand, note that you *must* use IP
  address rather than hostname
* sm:///tmp/cci/sm/`hostname`/1/1 # note that this is a full path to local
  connection information.  The last portion of the path should match the
  address specified above

## Running examples

The examples subdirectory contains:

* client.c: an example client
* server.c: an example server
* my-rpc.[ch]: an example RPC definition

To run them using CCI/TCP, for example, you would do this:

```
examples/server tcp://3344
examples/client tcp://localhost:3344
```

The client will issue 4 concurrent RPCs to the server and wait for them to
complete.

## Running tests

`make check`

Notes:
* the test scripts assume the use of the TCP/IP protocol and localhost
* the tests/timeout.sh script is known to fail when using CCI right now,
  because we do not yet have an implementation of cancel for the address
  lookup step which may block in CCI
