# Margo instrumentation

This file documents instrumentation capabilities that are built into the
margo library.  See the [top level README.md](../README.md) for general
information about margo.

Margo includes two forms of instrumentation.  The first measures time spent
executing key Mercury functions within the communication progress
loop.  The second measures time spent invoking remote procedure calls.

## Usage

Both can be enabled at run time by calling the `margo_diag_start()` any
time after `margo_init()` on the process that you wish to instrument.
Statistics from both can then be emitted at any time prior to
`margo_finalize()` by calling the `margo_diag_dump()` function.

The arguments to `margo_diag_dump()` are as follows:
* `mid`: the margo instance to retrieve instrumentation from
* `file`: name of the file to write the (text) data to.  If the "-" string
  is used, then data will be written to `STDOUT`.
* `uniquify`: flag indicating that the file name should be suffixed with
  additional characters to make it unique from other diagnostic files emited
  on the same node.

## Output format

Example output from `margo_diag_dump()` will look like this for a given
processes:

```
# Margo diagnostics
# Wed Jul 31 11:15:13 2019

# RPC breadcrumbs for RPCs that were registered on this process:
# 0x5f22	data_xfer_read
# 0xa1ef	delegator_read
# 0x5f22	data_xfer_read
# 0x9245	my_shutdown_rpc
# <stat>	<avg>	<min>	<max>	<count>
# Time consumed by HG_Trigger()
trigger_elapsed	0.000000036	0.000000238	0.000114679	3911094
# Time consumed by HG_Progress() when called with timeout==0
progress_elapsed_zero_timeout	0.000004716	0.000000238	0.016073227	3909480
# Time consumed by HG_Progress() when called with timeout!=0
progress_elapsed_nonzero_timeout	0.051754011	0.000023842	0.100308180	411
# Timeout values passed to HG_Progress()
progress_timeout_value	0.010511802	0.000000000	100.000000000	3909891
# RPC statistics
0x5f22 0xa1ef 0x0000 0x0000 	0.001448274	0.001207113	0.007883787	100
```

Key components of the output are:

* A table of RPC names registered on that processes.  Each has a 16 bit
  hexadecimal identifier and a string name.  There may be duplicates in the
  table if the same RPC is registered more than once on the process.
* A set of statistics for Mercury functions used to drive communication and
  completion project.  There are counters and elapsed time measurements for
  the `HG_Trigger()` function and the `HG_Progress()` function (when called with
  or without a timeout value, as Margo varies its pollin strategy).  There
  is also a category that records statistics about the actual timeout values
  used.
* A set of statistics for each RPC that was _issued_ by the process (in the
  "RPC statistics" category at the end.  Each RPC will be identified by a
  set of up to 4 hexidecmial identifiers.  The set of identifiers represents a
  stack that shows the heritage of up to 4 chained RPCS that lead to this
  measurement.  Each identifier will match a name in the table at the top.
  In the above example, only one RPC was issued by this
  process: a "data_xfer_read" RPC that was issed as a side effect of a
  "delegator_read" RPC.    

## Implementation

## Future directions and use cases
