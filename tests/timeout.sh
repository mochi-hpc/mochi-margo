#!/bin/bash -x

if [ -z $srcdir ]; then
    echo srcdir variable not set.
    exit 1
fi
source $srcdir/tests/test-util-hang.sh

TMPOUT=$(mktemp --tmpdir test-XXXXXX)

# start 1 server with 2 second wait, 8s timeout
test_start_servers 1 2 8

sleep 1

#####################

# run client test
run_to 10 examples/client-timeout $svr1 &> $TMPOUT 
if [ $? -ne 0 ]; then
    wait
    exit 1
fi

# check output; look for four "returned 9" to indicate HG_CANCELED in the four
# concurrent RPCs
LINECOUNT=$(grep "returned 9" $TMPOUT | wc -l) 
if [$LINECOUNT -ne 4]; then
    exit 1
fi

#####################

# note that this test leaves the server running; it will be killed after the
# 8s timeout from test_start_servers

wait

echo cleaning up $TMPBASE
rm -rf $TMPBASE

exit 0
