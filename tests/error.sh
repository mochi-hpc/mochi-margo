#!/bin/bash -x

if [ -z $srcdir ]; then
    echo srcdir variable not set.
    exit 1
fi

if [ -z "$MKTEMP" ] ; then
    echo expected MKTEMP variable defined to its respective command
    exit 1
fi

source $srcdir/tests/test-util.sh

TMPOUT=$($MKTEMP --tmpdir test-XXXXXX)

# do not start server, and set dummy address
svr1="na+sm://4661/0"

sleep 1

#####################

# run client test
run_to 10 tests/margo-test-client-error $svr1 &> $TMPOUT 
if [ $? -ne 0 ]; then
    wait
    cat $TMPOUT
    exit 1
fi

# check output; look for four "returned 10" to indicate HG_NODEV in the four
# concurrent RPCs
LINECOUNT=$(grep "returned HG_NODEV" $TMPOUT | wc -l) 
if [ $LINECOUNT -ne 4 ]; then
    wait
    cat $TMPOUT
    exit 1
fi

#####################

rm -rf $TMPOUT
exit 0
