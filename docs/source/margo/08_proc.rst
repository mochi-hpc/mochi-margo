Serializing complex data structures
===================================

Let's come back to serializing/deserializing data structures.
In previous tutorials, we have always used structures that
can be defined using Mercury's :code:`MERCURY_GEN_PROC`
macro. If the structure contains pointers, things get
more complicated.

Let's assume that we have a type :code:`int_list_t` that
represents a pointer to a linked list of integers.

.. code-block:: cpp

   typedef struct int_list {
      int32_t          value;
      struct int_list* next;
   } *int_list_t;

We will need to define a function
:code:`hg_return_t hg_proc_int_list_t(hg_proc_t proc, void *data)`.
More generally for any custom type :code:`X` that we want to send
or receive, and that hasn't been created using the Mercury macro,
we need a function of the form
:code:`hg_return_t hg_proc_X(hg_proc_t proc, void *data)`.

This function, in our case will be as follows.

.. container:: toggle

    .. container:: header

       .. container:: btn btn-info

          types.h (show/hide)

    .. literalinclude:: ../../examples/margo/08_proc/types.h
       :language: cpp

Any proc function must have three part, separated by a switch.
The :code:`HG_ENCODE` part is used when the :code:`proc` handle
is serializing an existing object into a buffer.
The :code:`HG_DECODE` part is used when the :code:`proc` handle
is creating an new object from the content of its buffer.
The :code:`HG_FREE` part is used when freeing the object, e.g.
when calling :code:`margo_free_input` or :code:`margo_free_output`.

Note that here the type we are processing is :code:`int_list_t`,
so the :code:`void* data` argument is actually a pointer to an :code:`int_list_t`,
which is itself a pointer to a structure.

We use the :code:`hg_proc_int32_t` and :code:`hg_proc_hg_size_t` functions
to serialize/deserialize :code:`int32_t` and :code:`hg_size_t` respectively.
Most basic datatypes have such a function defined in Mercury. To serialize/deserialize
raw memory, you can use :code:`hg_proc_raw(hg_proc_t proc, void* data, hg_size_t size)`,
which will copy *size* bytes of the content of the memory pointed by *data*.
