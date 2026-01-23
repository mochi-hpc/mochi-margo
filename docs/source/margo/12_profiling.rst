Profiling and monitoring
========================

Enabling monitoring
-------------------

Margo provides a monitoring system allowing users to capture statistics
on all its RPC- and RDMA-related operations, including serialization of
RPC argument, registration of memory for RDMA, etc.

To enable monitoring, simply set set :code:`MARGO_ENABLE_MONITORING`
environment variable before running your services and applications,
and you fill find a bunch of files named *margo.<hostname>.<pid>.stats.json*
in the current working directory when your services and applications
finalize Margo.

To understand and play around with the content of these files, simply
copy these files back to your laptop and follow the instructions in
the mochi-statistics-analysis Jupyter notebook
`here <https://github.com/mochi-hpc/mochi-performance-analysis>`_.

This notebook also explains how to configure the monitoring system.


Custom monitoring
-----------------

Margo enables users to replace its default monitoring system
with a custom one. To implement your own monitoring system,
please refer to the
`margo-monitoring.h <https://github.com/mochi-hpc/mochi-margo/blob/main/include/margo-monitoring.h>`_
header. This header provides a :code:`margo_monitor` structure containing
function pointers that you can implement yourself, and that are called
at different points of the execution of a Mochi program. Having played
around with the default monitoring system and understanding how it works
is highly recommended before jumping into implementing your own monitoring
system.
