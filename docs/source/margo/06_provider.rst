Working in terms of providers
=============================

In this tutorial, we will learn about providers.
This is probably the most important tutorial on Margo since it also
describe the design patterns and methodology to use to develop a Margo-based service.

We will take again the *sum* example developped in earlier tutorials,
but this time we will give it a proper microservice interface.
The result will be composed of two libraries: one for clients, one for servers,
as well as their respective headers. We will call this microservice *Alpha*.

Terminology
-----------

Although C is not an object-oriented programming language, the best
way to understand providers is to think of them an object that can receive
RPCs. Rather than targetting a *server*, as we did in previous tutorials,
a client's RPC will target a specific *provider* in this server.

Multiple providers belonguing to the same service and living at the same address
will expose the same set of RPCs, but each provider will be distinguished from
others using its unique *provider id* (an :code:`uint16_t`).

Clients will now use *provider handles* rather than *addresses* to communicate
with a particular provider at a given address. A provider handle encapsulates
the address of the server as well as the *provider id* of the provider inside
this server.

Input and output structures
---------------------------

Our alpha service will use the same input and output structures
as in earlier tutorials. We put it here for completeness.

.. container:: toggle

    .. container:: header

       .. container:: btn btn-info

          types.h (show/hide)

    .. literalinclude:: ../../examples/margo/06_provider/types.h
       :language: cpp

Alpha client interface
----------------------

First let's start with a header that will be common to the server and
the client. This header will simply define the :code:`ALPHA_SUCCESS`
and :code:`ALPHA_FAILURE` return codes.

.. container:: toggle

    .. container:: header

       .. container:: btn btn-info

          alpha-common.h (show/hide)

    .. literalinclude:: ../../examples/margo/06_provider/alpha-common.h
       :language: cpp

Let's now declare our client interface.

.. container:: toggle

    .. container:: header

       .. container:: btn btn-info

          alpha-client.h (show/hide)

    .. literalinclude:: ../../examples/margo/06_provider/alpha-client.h
       :language: cpp

The client interface defines two opaque pointers to structures.

- The :code:`alpha_client_t` handle will be pointing to an object
  keeping track of registered RPC identifiers for our microservice.
  An object of this type will be created using :code:`alpha_client_init`
  and destroyed using :code:`alpha_client_finalize`.
- The :code:`alpha_provider_handle_t` handle will be pointing to
  a provider handle for a provider of the Alpha service.
  An object of this type will be created using :code:`alpha_provider_handle_create`
  function and destroyed using :code:`alpha_provider_handle_release`.
  This object will have an internal reference count. :code:`alpha_provider_handle_ref_incr`
  will be used to manually increment this reference count.

The :code:`alpha_compute_sum` function will be in charge of sending
a *sum* RPC to the provider designated by the provider handle.

Client implementation
---------------------

The following code shows the implementation of our client interface.

.. container:: toggle

    .. container:: header

       .. container:: btn btn-info

          alpha-client.c (show/hide)

    .. literalinclude:: ../../examples/margo/06_provider/alpha-client.c
       :language: cpp

When initializing the client, :code:`margo_registered_name` is used
to check whether the RPC has been defined already. If it has, we use
this function to retrieve its id. Otherwise, we use the usual :code:`MARGO_REGISTER`
macro.

Notice the use of :code:`margo_provider_forward` in :code:`alpha_compute_sum`,
which uses the provider id to send the RPC to a specific provider.

Alpha server interface
----------------------

Moving on to the server's side, the following code shows how to define
the server's interface.

.. container:: toggle

    .. container:: header

       .. container:: btn btn-info

          alpha-server.h (show/hide)

    .. literalinclude:: ../../examples/margo/06_provider/alpha-server.h
       :language: cpp

This interface contains the definition of an opaque pointer type,
:code:`alpha_provider_t`, which will be used to hide the implementation
of our Alpha provider.
Our interface contains the :code:`alpha_provider_register` function,
which creates an Alpha provider and registers its RPCs, and the
:code:`alpha_provider_destroy` function, which destroys it and deregisters
the corresponding RPCs. The former also allows users to pass
:code:`ALPHA_PROVIDER_IGNORE` as last argument, when we don't expect to do
anything with the provider after registration.

This interface would also be the place where to put other functions that
configure or modify the Alpha provider once created.

.. note::
   The :code:`alpha_provider_register` function also takes an Argobots pool
   as argument. We will discuss this in a following tutorial.

Server implementation
---------------------

The following code shows the implementation of the interface we just defined.

.. container:: toggle

    .. container:: header

       .. container:: btn btn-info

          alpha-server.c (show/hide)

    .. literalinclude:: ../../examples/margo/06_provider/alpha-server.c
       :language: cpp

We start by defining the :code:`alpha_provider` structure. It may contain
the RPC ids as well as any data you may need as context for your RPCs.

The :code:`alpha_provider_register` function starts by checking that the
Margo instance is in server mode by using :code:`margo_is_listening`.
It then checks that there isn't already an alpha provider with the same id. It does
so by using :code:`margo_provider_registered_name` to check whether the *sum*
RPC has already been registered with the same provider id.

We then use :code:`MARGO_REGISTER_PROVIDER` instead of :code:`MARGO_REGISTER`.
This macro takes a provider id and an Argobots pool in addition to the parameters
of :code:`MARGO_REGISTER`.

Finally, we call :code:`margo_provider_push_finalize_callback` to setup
a callback that Margo should call when calling :code:`margo_finalize`.
This callback will deregister the RPCs and free the provider.

The :code:`alpha_provider_destroy` function is pretty simple but important
to understand. In most cases the user will create a provider and leave it running
until something calls :code:`margo_finalize`, at which point the provider's
finalization callback will be called. If the user wants to destroy the provider
before Margo is finalized, it is important to tell Margo not to call the provider's
finalization callback when :code:`margo_finalize`. Hence, we use :code:`margo_provider_pop_finalize_callback`.
This function takes a Margo instance, and an owner for the callback (here the provider).
If the provider registered multiple callbacks using :code:`margo_provider_push_finalize_callback`,
:code:`margo_provider_pop_finalize_callback` will pop the last one pushed, and should therefore
be called as many time as needed to pop all the finalization callbacks corresponding to the provider.

.. warning::
   Finalization callbacks are called after the Mercury progress loop is terminated.
   Hence, you cannot send RPCs from them. If you need a finalization callback to be
   called before the progress loop is terminated, use :code:`margo_push_prefinalize_callback`
   or :code:`margo_provider_push_prefinalize_callback`.

Using the Alpha client
----------------------

The previous codes can be compiled into two libraries, *libalpha-client.{a,so}*
and *libalpha-server.{a,so}*. The former will be used by client codes to use
the Alpha microservice as follows.

.. container:: toggle

    .. container:: header

       .. container:: btn btn-info

          client.c (show/hide)

    .. literalinclude:: ../../examples/margo/06_provider/client.c
       :language: cpp

Notice how simple such an interface is for end users.

Using the Alpha server
----------------------

A server can be written that spins up an Alpha providervas follows.

.. container:: toggle

    .. container:: header

       .. container:: btn btn-info

          server.c (show/hide)

    .. literalinclude:: ../../examples/margo/06_provider/server.c
       :language: cpp

A typical Mochi service will consist of a composition of
multiple providers spin up in the same program.

.. tip::
   To avoid conflicts with other microservices, it is recommended
   to prefix the name of the RPCs with the name of the service,
   as we did here with "alpha_sum".

.. note::
   Providers declaring RPCs with distinct names (i.e. providers from
   distinct microservices) can have the same provider ids. The provider id
   is here to distinguish providers of the same type within a given server.

Timeout
-------

The :code:`margo_provider_forward_timed` and :code:`margo_provider_iforward_timed`
can be used when sending RPCs (in a blocking or non-blocking manner) to specify
a timeout in milliseconds after which the call (or result of :code:`margo_wait`)
will be :code:`HG_TIMEOUT`.
