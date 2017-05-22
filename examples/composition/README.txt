The examples in this directory benchmark the overhead of relaying to RPCs to co-located
services in various configurations.

<NOTE: ignore performance results in these examples; they were taken from a debugging build>

The following example will compare the latency of issuing RPCs (with 8 MiB data transfers)
directly to a service vs the latency of the same RPC relayed through a delegator service.
The two daemon instances do not need to run on the same node, but you need to use another
transport like CCI, BMI, or OFI to relay between remote nodes.

==========================

$ ./composed-svc-daemon na+sm:// data-xfer
# accepting RPCs on address "na+sm://24151/0"

$ ./composed-svc-daemon na+sm:// delegator
# accepting RPCs on address "na+sm://24180/0"

$ ./composed-benchmark na+sm://24180/0 na+sm://24151/0 100
# DBG: starting data_xfer_read() benchmark.
# DBG:    ... DONE.
# <op> <min> <avg> <max>
direct  0.001103    0.001333    0.005824
# DBG: starting composed_read() benchmark.
# DBG:    ... DONE.
# <op> <min> <avg> <max>
composed    0.001103    0.001751    0.005824
# DBG:    ... DONE.
Shutting down delegator server.
Shutting down data_xfer server.

==========================

Alternatively, you can run the two services in the same executable, in which case the relay
will bypass the network layer of Mercury entirely.

<NOTE: if one daemon is used to run both services, then data-xfer must be listed first in
 the comma-separated list>

==========================

$ ./composed-svc-daemon na+sm:// data-xfer,delegator
# accepting RPCs on address "na+sm://24259/0"

$ ./composed-benchmark na+sm://24259/0 na+sm://24259/0 100
# DBG: starting data_xfer_read() benchmark.
# DBG:    ... DONE.
# <op> <min> <avg> <max>
direct  0.001215    0.001514    0.010304
# DBG: starting composed_read() benchmark.
# DBG:    ... DONE.
# <op> <min> <avg> <max>
composed    0.001215    0.001756    0.010304
# DBG:    ... DONE.
Shutting down delegator server.


