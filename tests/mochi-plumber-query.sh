#!/bin/bash -x

if [ -z $srcdir ]; then
    echo srcdir variable not set.
    exit 1
fi
source $srcdir/tests/test-util.sh

# we don't expect to produce meaningful results with the tcp protocol, but
# things like cxi are platform-specific so we don't want them in a make
# check test
tests/mochi-plumber-query -p tcp
