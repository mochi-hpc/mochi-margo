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

## Building

Example configuration:

    ../configure --prefix=/home/pcarns/working/install \
        PKG_CONFIG_PATH=/home/pcarns/working/install/lib/pkgconfig \
        CFLAGS="-g -Wall"

## To Do:

* Fix licensing 
* Convert dedicated thread that drives HG to an xstream

