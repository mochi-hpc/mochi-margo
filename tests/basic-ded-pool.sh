#!/bin/bash -x

# NOTE: this example uses a dedicated execution stream in Argobots to drive
# progress on the server

if [ -z $srcdir ]; then
    echo srcdir variable not set.
    exit 1
fi
source $srcdir/tests/test-util-ded-pool.sh

# start 1 server with 2 second wait, 20s timeout
test_start_servers 1 2 20

sleep 1

#####################

# run client test, which will also shut down server when done
run_to 10 tests/margo-test-client $svr1 &> /dev/null 
if [ $? -ne 0 ]; then
    wait
    exit 1
fi

#####################


wait

exit 0
