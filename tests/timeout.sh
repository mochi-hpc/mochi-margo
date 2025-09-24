#!/bin/bash -x

MKTEMP=${MKTEMP:-mktemp}

source $(dirname $0)/test-util.sh

TMPOUT=$($MKTEMP test-XXXXXX)

# start 1 server with 2 second wait, 8s timeout
test_start_servers 1 2 10

sleep 1

#####################

# run client test
run_to 20 ./margo-test-client-timeout $svr1 &> $TMPOUT
if [ $? -ne 0 ]; then
    wait
    rm -rf $TMPOUT
    exit 1
fi

# check output; look for four "returned 18" to indicate HG_TIMEOUT in the four
# concurrent RPCs
LINECOUNT=$(grep "returned HG_TIMEOUT" $TMPOUT | wc -l)
if [ $LINECOUNT -ne 4 ]; then
    rm -rf $TMPOUT
    exit 1
fi

#####################

# note that this test leaves the server running; it will be killed after the
# 8s timeout from test_start_servers

wait

rm -rf $TMPOUT
exit 0
