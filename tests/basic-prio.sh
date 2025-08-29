#!/bin/bash -x

source $(dirname $0)/test-util.sh

# start 1 server with 2 second wait, 20s timeout, priority pool
test_start_servers 1 2 20 prio_wait

sleep 1

#####################

# run client test, which will also shut down server when done
run_to 20 ./margo-test-client $svr1 &> /dev/null
if [ $? -ne 0 ]; then
    wait
    exit 1
fi

#####################


wait

exit 0
