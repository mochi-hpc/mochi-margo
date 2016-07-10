# margo
margo is a library that provides Argobots bindings to the Mercury RPC
implementation.  See the following for more details about each project:

* https://collab.mcs.anl.gov/display/ARGOBOTS/Argobots+Home
* https://mercury-hpc.github.io/

##  Dependencies

* mercury  (git clone --recurse-submodules https://github.com/mercury-hpc/mercury.git)
* argobots (git://git.mcs.anl.gov/argo/argobots.git)
* abt-snoozer (https://xgitlab.cels.anl.gov/sds/abt-snoozer)
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

## Running examples

See README file in the examples subdirectory.
