Simple Hello World RPC with Margo
=================================

The previous tutorial explained how to initialize a server and a client.
This this tutorial, we will have the server register and RPC handler and
the client send an RPC request to the server.

Server-side RPC handler
-----------------------

We will change the code of our server as follows.

.. container:: toggle

    .. container:: header

       .. container:: btn btn-info

          server.c (show/hide)

    .. literalinclude:: ../../examples/margo/02_hello/server.c
       :language: cpp

What changes is the following declaration of an RPC handler.

.. code-block:: cpp

   static void hello_world(hg_handle_t h);
   DECLARE_MARGO_RPC_HANDLER(hello_world)

The first line declares the function that will be called upon
receiving a "hello" RPC request. This function must take a :code:`hg_handle_t`
object as argument and does not return anything.

The second line declares *hello_world* as a RPC handler. :code:`DECLARE_MARGO_RPC_HANDLER`
is a macro that generates the code necessary for the RPC handler to be placed
in an Argobots user-level thread (ULT).

The two lines that register the RPC handler in the Margo instance are the following.

.. code-block:: cpp

   hg_id_t rpc_id = MARGO_REGISTER(mid, "hello", void, void, hello_world);
   margo_registered_disable_response(mid, rpc_id, HG_TRUE);

:code:`MARGO_REGISTER` is a macro that registers the RPC handler.
Its first argument is the Margo instance. The second is the name of the RPC.
The third and fourth are the types of the RPC's input and output, respectively.
We will cover these in the next tutorial. For now, our hello_world RPC is not going
to receive any argument and not return any value. The last parameter is the function
we want to use as RPC handler.

The :code:`margo_registered_disable_response` is used to indicate that this RPC
handler does not send a response back to the client.

The rest of the program defines the :code:`hello_world` function.
From inside an RPC handler, we can access the Margo instance using
:code:`margo_hg_handle_get_instance`. This is the prefered method for better code
organization, rather than declaring the Margo instance as a global variable.

The RPC handler must call :code:`margo_destroy` on the :code:`hg_handle_t` argument
it is being passed, after we are done using it.

In this example, after receiving 4 requests, the RPC handler will call :code:`margo_finalize`,
which will make the main ES exit the call to :code:`margo_wait_for_finalize` and terminate.

After the definition of the RPC handler, :code:`DEFINE_MARGO_RPC_HANDLER` must be called for Margo to define additional wrapper functions.

.. note::
   We will see at the end of this tutorial how to avoid using global variables
   (here :code:`TOTAL_RPCS` and :code:`num_rpcs`).


Calling the RPC from clients
----------------------------

The following code is the corresponding client.

.. container:: toggle

    .. container:: header

       .. container:: btn btn-info

          client.c (show/hide)

    .. literalinclude:: ../../examples/margo/02_hello/client.c
       :language: cpp

This client takes the server's address as argument (copy the address printed
by the server when calling the client). This string representation of the server's
address must be resolved into a :code:`hg_addr_t` object. This is done by
:code:`margo_addr_lookup`.

Once resolved, the address can be used in a call to :code:`margo_create` to create
a :code:`hg_handle_t` object. The :code:`hg_handle_t` object represents an RPC request
ready to be sent to the server.

:code:`margo_forward` effectively sends the request to the server. We pass :code:`NULL`
as a second argument because the RPC does not take any input.

Because we have called :code:`margo_registered_disable_response`, Margo knows that the client
should not expect a response from the server, hence :code:`margo_forward` will return
immediately. We then destroy the handle using :code:`margo_destroy`,  free the :code:`hg_addr_t`
object using :code:`margo_addr_free`, and finalize Margo.

.. note::
   :code:`MARGO_REGISTER` in clients is being passed :code:`NULL` as last argument,
   since the actual RPC handler function is located in the server.

Attaching data to RPC handlers
------------------------------

Back to the server, we have used two global variables: :code:`TOTAL_RPCS` and :code:`num_rpcs`.
Any good developer knows that global variables are evil and every use of a global variable
kills a kitten somewhere. Fortunately, we can modify our program to get rid of global variables.

First we will declare a structure to encapsulate the server's data.

.. code-block:: cpp

   typedef struct {
       int max_rpcs;
       int num_rpcs;
   } server_data;

We can now initialize our server data as a local variable inside main, and attach it to our *hello*
RPC handler, as follows.

.. code-block:: cpp

   server_data svr_data = {
	   .max_rpcs = 4,
	   .num_rpcs = 0
   };
   ...
   hg_id_t rpc_id = MARGO_REGISTER(mid, "hello", void, void, hello_world);
   margo_registered_disable_response(mid, rpc_id, HG_TRUE);
   margo_register_data(mid, rpc_id, &svr_data, NULL);

:code:`margo_register_data` is the function to use to attach data to an RPC handler.
It takes a Margo instance, the id of the registered RPC, a pointer to the data to
register, and a pointer to a function to call to free that pointer. Since here our
data is on the stack, we pass :code:`NULL` as the last parameter.

.. important::
   You need to make sure that the data attached to an RPC handler will not
   disappear before Margo is finalized. A common mistake consists of attaching
   a pointer to a piece of data that is on the stack within a function that
   then returns. In our example above, because :code:`main` will block
   on :code:`margo_wait_for_finalize`, we know :code:`main` will return only
   after :code:`margo_finalize` has been called.

In the :code:`hello_world` RPC handler, we can now retrieve the attached data as
follows.

.. code-block:: cpp

   const struct hg_info* info = margo_get_info(h);
   server_data* svr_data = (server_data*)margo_registered_data(mid, info->id);

We can now replace the use of global variables by accessing the variables
inside :code:`svr_data` instead.

.. important::
   If you have initialized Margo with multiple ES to server RPCs (last argument
   of :code:`margo_init` strictly greater than 1), you will need to protect
   such attached data with a mutex or a read-write lock. For more information
   on such locking mechanisms, please refer to the Argobots tutorials.
