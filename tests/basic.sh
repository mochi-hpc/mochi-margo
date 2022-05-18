#!/bin/bash -x

if [ -z $srcdir ]; then
    echo srcdir variable not set.
    exit 1
fi
source $srcdir/tests/test-util.sh

# start 1 server with 2 second wait, 20s timeout
test_start_servers 1 2 20

sleep 1

#####################

# run client test, which will also shut down server when done
run_to 20 tests/margo-test-client $svr1
if [ $? -ne 0 ]; then
    wait
    exit 1
fi

#####################


wait

exit 0
