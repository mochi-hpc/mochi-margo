Using Argobots pools with Margo RPCs
====================================

In the previous tutorial, we saw that the :code:`alpha_provider_register`
function is taking an :code:`ABT_pool` argument that is passed down to
:code:`MARGO_REGISTER_PROVIDER`.

Argobots pools are a good way to assign resources (typically cores) to
particular providers. In the following example, we rewrite the server code
in such a way that the Alpha provider gets its own execution stream.

.. container:: toggle

    .. container:: header

       .. container:: btn btn-info

          server.c (show/hide)

    .. literalinclude:: ../../examples/margo/07_pool/server.c
       :language: cpp

After initializing Margo (which initializes Argobots), we create an Argobots
pool and an execution stream that will execute work (ULTs and tasklets) from
this pool. We use :code:`ABT_POOL_ACCESS_MPSC` as access type to indicate
that there will be multiple producers of work units (in particular, the ES
running the Mercury progress loop) and a single consumer of work units
(the ES we are about to create).

:code:`ABT_xstream_create_basic` is then used to create the ES.
Because Margo is initializing and finalizing Argobots, we need a way to
destroy this ES *before* Margo finalizes Argobots. Hence with use
:code:`margo_push_finalize_callback` to add a callback that will be
called upon finalizing Margo. This callback joins the ES and destroys it.

We pass the newly created pool to the :code:`alpha_provider_register`
function, which will make the Alpha provider use this pool to execute
its RPC handlers.

For more elaborate assignments of resources to providers, please see
the Argobots section of this documentation.
