.. _margo_14_parent:

Child instances with parent_mid
===============================

In some situations, you may need multiple Margo instances within the same
process -- for example to use multiple protocols. Normally, each Margo
instance creates its own Argobots pools and execution streams (ES), which
can be wasteful when the instances could share these resources.

The :code:`parent_mid` field in :code:`struct margo_init_info` solves this
problem. By setting it to an existing Margo instance, the new (child)
instance will reuse the parent's Argobots environment -- pools, execution
streams, and schedulers -- instead of creating its own. Each instance still
gets its own Mercury class and context, so they can independently send
and receive RPCs.

Basic usage
-----------

The simplest way to create a child instance is to set the :code:`parent_mid`
field and leave everything else at its defaults:

.. container:: toggle

    .. container:: header

       .. container:: btn btn-info

          simple.c (show/hide)

    .. literalinclude:: ../../examples/margo/12_parent/simple.c
       :language: cpp

In this example, the child inherits the parent's :code:`__primary__` pool
for both its progress loop and its RPC handlers. Since no Argobots resources
are duplicated, the child is very lightweight.

Selecting pools by name
-----------------------

When the parent has multiple pools, the child can select which ones to use
for its progress loop and RPC handlers via the :code:`progress_pool` and
:code:`rpc_pool` fields in the JSON configuration:

.. container:: toggle

    .. container:: header

       .. container:: btn btn-info

          parent.c (show/hide)

    .. literalinclude:: ../../examples/margo/12_parent/parent.c
       :language: cpp

Here the parent defines two pools (:code:`__primary__` and :code:`my_pool`)
with their own execution streams. The child references :code:`my_pool` by
name in its JSON configuration, so its progress loop and RPC handlers run
on the ES associated with that pool.

Pool references in the child's configuration can be specified either as a
string (pool name) or as an integer (pool index in the parent's pool array).

.. important::
   The pool referenced by the child must have at least one execution stream
   associated with it in the parent's configuration, otherwise ULTs pushed
   into that pool will never be executed.

Configuration restrictions
--------------------------

A child instance borrows its Argobots environment from the parent. As a
consequence, the following fields are **not allowed** in the child's JSON
configuration and will cause initialization to fail:

- :code:`"argobots"` -- the child cannot define its own pools or xstreams.
- :code:`"use_progress_thread"` -- adding a progress thread would require
  creating a new ES, which is the parent's responsibility.
- :code:`"rpc_thread_count"` -- similarly, creating RPC threads would
  require new pools and xstreams.

The following fields are still accepted in the child's configuration:

- :code:`"mercury"` -- Mercury settings for the child's own HG class.
- :code:`"progress_pool"` -- pool name or index in the parent's pool array.
- :code:`"rpc_pool"` -- pool name or index in the parent's pool array.
- :code:`"progress_timeout_ub_msec"`, :code:`"progress_spindown_msec"`,
  :code:`"handle_cache_size"` -- per-instance tuning knobs.

Lifetime management
-------------------

The child holds an internal reference to the parent instance. This means
you should always follow these rules:

1. **Finalize the child before the parent.** Calling :code:`margo_finalize`
   on the parent while a child is still using its pools may lead to undefined
   behavior.
2. **The parent's Argobots resources stay alive** as long as any child (or the
   parent itself) is still running. The parent's reference count is
   incremented when the child is created and decremented when the child is
   finalized or released.
3. The reference-counting mechanism described in :ref:`margo_13_lifetime`
   applies to both parent and child instances.

.. note::
   A child instance may also serve as the parent for another child,
   creating a chain. All instances in the chain share the root parent's
   Argobots environment.

Warning about progress loop location
------------------------------------

Some multi-instance setups can lead to undesirable behaviors. For instance
if the two instances have their progress loop in the same pool, both progress
loops will end up busy-spinning because they cannot simply block their ES
while waiting for network activities.
