add_executable (margo-example-client
    margo-example-client.c
    my-rpc.c
)

add_executable (margo-example-server
    margo-example-server.c
    my-rpc.c
)

add_executable (margo-example-server-multihome
    margo-example-server-multihome.c
    my-rpc.c
)

add_executable (margo-example-mp-client
    multiplex/margo-example-mp-client.c
    multiplex/svc1-client.c
    multiplex/svc2-client.c
)

add_executable (margo-example-mp-server
    multiplex/margo-example-mp-server.c
    multiplex/svc1-server.c
    multiplex/svc2-server.c
)

add_executable (composed-svc-daemon
    composition/composed-svc-daemon.c
    composition/data-xfer-service.c
    composition/delegator-service.c
)

add_executable (composed-benchmark
    composition/composed-benchmark.c
    composition/composed-client-lib.c
)

target_link_libraries (margo-example-client margo)
target_link_libraries (margo-example-server margo)
target_link_libraries (margo-example-server-multihome margo)
target_link_libraries (margo-example-mp-client margo)
target_link_libraries (margo-example-mp-server margo)
target_link_libraries (composed-svc-daemon margo)
target_link_libraries (composed-benchmark margo)
