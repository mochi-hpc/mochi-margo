#!/bin/bash

set -x

out1=/tmp/sleep-standard-$$.out
out2=/tmp/sleep-abt-$$.out

bash -c "time -p tests/margo-test-sleep 1" >& $out1
if [ $? -ne 0 ]; then
    cat $out1
    exit 1
fi
standardrealtime=`grep real $out1 | cut -d ' ' -f 2 |cut -d '.' -f 1`

bash -c "time -p tests/margo-test-sleep 1 ABT" >& $out2
if [ $? -ne 0 ]; then
    cat $out2
    exit 1
fi
abtrealtime=`grep real $out2 | cut -d ' ' -f 2 |cut -d '.' -f 1`

echo standard:
cat $out1

echo abt:
cat $out2

# standard one should take at least 4 seconds to complete; there are 4
# serialized sleeps of 1 second each
if [ "$standardrealtime" -lt 4 ]; then
    exit 1
fi

# abt one should take at least one second, but no more than 2 (there are 4
# sleeps running concurrently)
if [ "$abtrealtime" -lt 1 ]; then
    exit 1
fi
if [ "$abtrealtime" -gt 2 ]; then
    exit 1
fi

rm $out1
rm $out2

exit 0
