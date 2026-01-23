Initializing Margo
==================

Initializing a server
---------------------

The following code initializes Margo for use as a server, that is, a process that is meant to receive RPCs.
It then prints the address at which the server can be contacted.

.. literalinclude:: ../../examples/margo/01_init/server.c
   :language: cpp

:code:`margo_init` takes four arguments.
The first one is *protocol* (here TCP). It is also possible to provide the address and port number to use.
The second argument specifies that Margo is initialized as a server.
The third argument indicates whether an Argobots execution stream (ES) should be created to run the
Mercury progress loop. If this argument is set to 0, the progress loop is going to run in the
context of the main ES (this should be the standard scenario, unless you have a good reason for not
using the main ES, such as the main ES using MPI primitives that could block the progress loop). A value
of 1 will make Margo create an ES to run the Mercury progress loop.
The fourth argument is the number of ES to create and use for executing RPC handlers. A value of 0 will
make Margo execute RPCs in the ES that called :code:`margo_init`. A value of -1 will make Margo execute the
RPCs in the ES running the progress loop. A positive value will make Margo create new ESs to run the RPCs.

:code:`margo_addr_self` is then used to get the address of the server, which is then converted into a string
using :code:`margo_addr_to_string`. The address returned by :code:`margo_addr_self` should be freed using
:code:`margo_addr_free`.

:code:`margo_wait_for_finalize` blocks the server in the Mercury progress loop until another ES calls
:code:`margo_finalize`. In this example, nothing calls :code:`margo_finalize`, so you will need to kill the server
manually if you run it.

.. note::
   We use :code:`margo_info` to display information. This function is part of Margo's logging API,
   which includes six logging levels: :code:`margo_trace`, :code:`margo_debug`, :code:`margo_info`,
   :code:`margo_warning`, :code:`margo_error`, and :code:`margo_critical`. These functions take a margo
   instance as first argument, a string format as second argument, and optional parameters.
   Note that these functions will automatically add a :code:`\\n` at the end of the
   provided string. The logging level can be set using :code:`margo_set_log_level` (see margo-logging.h).


Initializing a client
---------------------

The following code initializes Margo for use as a client.

.. literalinclude:: ../../examples/margo/01_init/client.c
   :language: cpp

We call :code:`margo_init` with :code:`MARGO_CLIENT_MODE` to indicate that this is a client.
Just like servers, clients have to run a Mercury progress loop. This progress loop can be
executed in the context of the main ES (by providing 0 as a third argument) or in a separate
ES (by providing 1). In general, a value of 0 is sufficient. Putting the Mercury progress
loop of a client in a separate ES is useful if the client uses non-blocking RPCs,
or if the client is multithreaded.

:code:`margo_finalize` is used to finalize the :code:`margo_instance_id` object.


.. important::
   A margo instance initialized as a server (i.e. expected to receive RPCs) can also be used
   as a client (i.e. expected to send RPCs). There is no need to initialize multiple margo
   instances in the same process, and it is often a bad idea to do so as their respective
   progress loops will compete for network resources in an unpredictable manner.


Extended initialization
-----------------------

A :code:`margo_init_ext` function is provided that enables passing
configuration parameters as well as externally-initialized Argobots pools
and Mercury class/context. This function is explained in detail in
a latter section.
