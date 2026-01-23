Sending arguments, returning values
===================================

In the previous tutorial we saw how to send a simple RPC.
This RPC was not taking any argument and the server was not sending
any response to the client. In this tutorial we will see how to add
arguments and return values to an RPC. We will build an RPC handler
that receives two integers and returns their sum to the client.

Input and output structures
---------------------------

First, we need to define structures encapsulating the input and output data.
This is done using Mercury macros as shown in the following code, which we
will place in a *types.h* private header file.

.. container:: toggle

    .. container:: header

       .. container:: btn btn-info

          types.h (show/hide)

    .. literalinclude:: ../../examples/margo/03_sum/types.h
       :language: cpp

We include :code:`<mercury.h>` and :code:`<mercury_macros.h>`. The latter
contains the necessary macros. We then use :code:`MERCURY_GEN_PROC`
to declare our structures. This macro expends not only to the definition of
the :code:`sum_in_t` and :code:`sum_out_t` structures, but also to that
of serialization functions that Mercury (hence Margo) can use to serialize
these structures into a buffer, and deserialize them from a buffer.

.. note::
   Once these types are defined using the macro, you can use them as
   members of other types.

.. note::
   The :code:`<mercury_proc_string.h>` may also be included. It provides
   the :code:`hg_string_t` and :code:`hg_const_string_t` types, which
   are typedefs of :code:`char*` and :code:`const char*` respectively
   and must be used to represent null-terminated strings.

.. important::
   The structures defined with the :code:`MERCURY_GEN_PROC` cannot
   contain pointers (apart from the :code:`hg_string_t` and :code:`hg_const_string_t`
   types). We will see in a future tutorial how to define serialization
   functions for more complex structures.

Sum server code
---------------

The following shows the server code.

.. container:: toggle

    .. container:: header

       .. container:: btn btn-info

          server.c (show/hide)

    .. literalinclude:: ../../examples/margo/03_sum/server.c
       :language: cpp


This code is very similar to our earlier code (you will notice that we have attached
data to the RPC to avoid using global variables, as advised at the end of the previous
tutorial).

Now :code:`MARGO_REGISTER` takes the types of the arguments being sent and received,
rather than :code:`void`. Notice that we are also not calling
:code:`margo_registered_disable_response` anymore since
this time the server will send a response to the client.

Two structures :code:`in` and :code:`out` are declared at the beginning of the RPC
handler. :code:`in` will be used to deserialize the arguments sent by the client,
while :code:`out` will be used to send a response.

:code:`margo_get_input` is used to deserialize the content of the RPC's data into the
variable :code:`in`. The :code:`out` variable is then modified with :code:`out.ret = in.x + in.y;`
before being sent back to the client using :code:`margo_respond`.

.. important::
   An input deserialized using :code:`margo_get_input` should be freed using
   :code:`margo_free_input`, even if the structure is on the stack and does not
   contain pointers.

Client code
-----------

Let's now take a look at the client's code.

.. container:: toggle

    .. container:: header

       .. container:: btn btn-info

          client.c (show/hide)

    .. literalinclude:: ../../examples/margo/03_sum/client.c
       :language: cpp

Again, :code:`MARGO_REGISTER` takes the types of the arguments being sent and received.
We initialize the :code:`sum_in_t args;` and :code:`sum_out_t resp;` to hold respectively
the arguments of the RPC (what will become the :code:`in` variable on the server side)
and the return value (:code:`out` on the server side).

:code:`margo_forward` now takes a pointer to the input argument as second parameter,
and :code:`margo_get_output` is used to deserialized the value returned by the server
into the :code:`resp` variable.

.. important::
   Just like we called :code:`margo_free_input` on the server because the input
   had been obtained using :code:`margo_get_input`, we must call :code:`margo_free_output`
   on the client side because the output has been obtained using :code:`margo_get_output`.

Timeout
-------

It can sometimes be important for the client to be able to timeout if an operation
takes too long. This can be done using :code:`margo_forward_timed`, which takes an
extra parameter: a timeout (:code:`double`) value in milliseconds. If the server has
not responded to the RPC after this timeout expires, :code:`margo_forward_timed`
will return :code:`HG_TIMEOUT` and the RPC will be cancelled.

.. important::
   The fact that a call has timed out does not mean that the server hasn't
   received the RPC or hasn't processed it. It simply means that, should the server send a
   reponse back, this response will be ignored by the client. Worse: the server will
   not be aware that the client has cancelled the operation. It is up to the developer to
   make sure that such a behavior is consistent with the semantics of her protocol.
