Margo instance lifetime
=======================

In the previous sections, we have used :code:`margo_init` (or one of its variant)
to initialize a Margo instance, and :code:`margo_finalize` to destroy it. When
doing so, we have to understand that the actual destruction of the Margo instance
may not happen in :code:`margo_finalize` itself: if :code:`margo_finalize` is
called from an RPC, or if another ULT is blocked on :code:`margo_wait_for_finalize`,
then the Margo instance will be destroyed respectively after the RPC, and in
:code:`margo_wait_for_finalize`.

This lifetime management may not be suitable when wrapping Margo code in a
higher-level language that uses destructors or garbage collection. Even in C, it may
be useful to ensure that the Margo instance remains valid after its finalization,
for instance to be able to cleanup Argobots or Mercury objects (e.g. :code:`ABT_mutex` or
:code:`hg_addr_t`) after the Mercury progress loop has been stopped.

To help with such lifetime management, Margo instances have an internal reference
count. This reference count is initialize at 1 when the instance is created. It is
decreased by 1 when the instance is finalized, and the instance is destroyed when
the reference count goes to 0.

This reference count can be increased and decreased by the user by calling
:code:`margo_instance_ref_incr` and :code:`margo_instance_release`, respectively.
If the program calls :code:`margo_finalize` on an instance with a reference count
greater than 1, the progress loop will be stopped, making the instance unable
to send and receive RPCs, but the instance itself won't be destroyed until
:code:`margo_instance_release` has been called as many times as needed to make
the reference count go to 0.
