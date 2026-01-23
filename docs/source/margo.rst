Margo (Core C runtime)
======================

Margo is a C library that helps develop distributed services
based on RPC and RDMA.

Margo provides Argobots-aware wrappers to Mercury functions.
It simplifies service development by expressing Mercury operations as
conventional blocking functions so that the caller does not need to manage
progress loops or callback functions.
Internally, Margo suspends callers after issuing a Mercury operation, and
automatically resumes them when the operation completes. This allows
other concurrent user-level threads to make progress while Mercury
operations are in flight without consuming operating system threads.
The goal of this design is to combine the performance advantages of
Mercury's native event-driven execution model with the progamming
simplicity of a multi-threaded execution model.

This section will walk you through a series of tutorials on how to use
Margo. We highly recommend to read all the tutorials before diving into
the implementation of any Margo-based service, in order to understand how
we envision designing such services. This will also help you greatly
in understanding other Margo-based services.

.. toctree::
   :maxdepth: 1

   margo/01_init.rst
   margo/02_hello.rst
   margo/03_sum.rst
   margo/04_bulk.rst
   margo/05_async.rst
   margo/06_provider.rst
   margo/07_pools.rst
   margo/08_proc.rst
   margo/09_config.rst
   margo/10_logging.rst
   margo/11_timers.rst
   margo/12_profiling.rst
   margo/13_lifetime.rst
   margo/c_api.rst
