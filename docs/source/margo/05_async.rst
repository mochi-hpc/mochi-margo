Non-blocking RPC
================

We may sometimes want to send an RPC and carry on some work,
checking later whether the RPC has completed. This is done
using :code:`margo_iforward` and some other functions that
will be described in this tutorial.

Input and output structures
---------------------------

We will take again the example of a *sum* RPC and make
the RPC non-blocking.
The header bellow is a reminder of what the input and output
structures look like.

.. container:: toggle

    .. container:: header

       .. container:: btn btn-info

          types.h (show/hide)

    .. literalinclude:: ../../examples/margo/05_async/types.h
       :language: cpp

Non-blocking forward on client
------------------------------

The following code examplifies the use of non-blocking
RPC on clients.

.. container:: toggle

    .. container:: header

       .. container:: btn btn-info

          client.c (show/hide)

    .. literalinclude:: ../../examples/margo/05_async/client.c
       :language: cpp

Instead of using :code:`margo_forward`, we use :code:`margo_iforward`.
This function returns immediately after having sent the RPC to the server.
It also takes an extra argument of type :code:`margo_request*`.
The client will use this request object to check the status of the RPC.

We then use :code:`margo_wait` on the request to block until we have
received a response from the server. Alternatively, :code:`margo_test`
can be be used to check whether the server has sent a response, without
blocking if it hasn't.

.. note::
   It is safe to delete or modify the RPC's input right after the call to
   :code:`margo_iforward`. :code:`margo_iforward` indeed returns *after*
   having serialized this input into its send buffer.

Non-blocking response on server
-------------------------------

Although generally less useful than non-blocking forwards,
non-blocking responses are also available on servers.
The :code:`margo_irespond` function can be used for this purpose.
It returns as soon as the response has been posted to the Mercury queue.

.. container:: toggle

    .. container:: header

       .. container:: btn btn-info

          server.c (show/hide)

    .. literalinclude:: ../../examples/margo/05_async/server.c
       :language: cpp

.. note::
   :code:`margo_respond` (the blocking version) returns when the
   response has been sent, but does not guarantees that the client
   has received it. Its behavior is not very different
   from :code:`margo_irespond`, which returns as soon as the
   response has been scheduled for sending. Hence it is unlikely
   that you ever need :code:`margo_irespond`.

Timeout
-------

Just like there is a :code:`margo_forward_timed`, there is a
:code:`margo_iforward_timed`, which takes an additional parameter
(before the request pointer) indicating a timeout in millisecond.
This timeout applies from the time of the call to :code:`margo_iforward_timed`.
Should the server not respond within this time limit, the called to
:code:`margo_wait` on the resulting request will return :code:`HG_TIMEOUT`.
