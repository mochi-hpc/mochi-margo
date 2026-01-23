.. _MargoBulk:

Transferring data over RDMA
===========================

Margo inherits from Mercury the possibility to do RDMA
operations. In this tutorial, we will revisit our *sum*
example and have the client send a bunch of values to
the server by exposing a buffer of memory where these
values are located, and have the server pull from this
memory.

Input/Output with hg_bulk_t
---------------------------

Let's first take a look at the types.

.. container:: toggle

    .. container:: header

       .. container:: btn btn-info

          types.h (show/hide)

    .. literalinclude:: ../../examples/margo/04_bulk/types.h
       :language: cpp

The :code:`hg_bulk_t` opaque type represents a handle to
a region of memory in a process. In addition to this handle,
we add a field :code:`n` that will tell us how many values
are in the buffer.

Client exposing memory
----------------------

Starting with the client code for once.

.. container:: toggle

    .. container:: header

       .. container:: btn btn-info

          client. (show/hide)

    .. literalinclude:: ../../examples/margo/04_bulk/client.c
       :language: cpp

We allocate the :code:`values` buffer as an array of 10 integers
(this array is on the stack in this example. An array allocated
on the heap would work just the same).
:code:`margo_bulk_create` is used to create an :code:`hg_bulk_t`
handle representing the segment of memory exposed by the client.
Its first parameter is the :code:`margo_instance_id`. Then come
the number of segments to expose, a :code:`void**` array of
addresses pointing to each segment, a :code:`hg_size_t*` array
of sizes for each segment, and the mode used to expose the
memory region. :code:`HG_BULK_READ_ONLY` indicates that Margo
will only read (i.e., the server will only pull) from this segment.
:code:`HG_BULK_WRITE_ONLY` indicates that Margo will only write
to the segment and :code:`HG_BULK_READWRITE` indicates that both
operations may happen.

The bulk handle is freed after being used, using
:code:`margo_bulk_free`.

Server pulling from client
--------------------------

Let's now take a look at the server.

.. container:: toggle

    .. container:: header

       .. container:: btn btn-info

          server.c (show/hide)

    .. literalinclude:: ../../examples/margo/04_bulk/server.c
       :language: cpp

Within the RPC handler, after deserializing the RPC's input,
we allocate an array of appropriate size:

.. code-block:: cpp

   values = calloc(in.n, sizeof(*values));

We then expose it the same way as we did on the client side,
to get a local bulk handle, using :code:`margo_bulk_create`.
This time we specify that this handle will be only written.

:code:`margo_bulk_transfer` is used to do the transfer.
Here we pull (:code:`HG_BULK_PULL`) the data from the client's memory
to the server's local memory. We provide the client's address
(obtained from the `hg_info` structure of the RPC handle),
the offset in the client's memory region (here 0)
and on the local memory region (0 as well),
as well as the size in bytes.

Once the transfer is completed, we perform the sum and
return it to the client. We don't forget to use :code:`margo_bulk_free`
to free the bulk handle we created (the bulk handle in the :code:`in`
structure will be freed by :code:`margo_free_input`, which is why
it is so important that this function be called).

