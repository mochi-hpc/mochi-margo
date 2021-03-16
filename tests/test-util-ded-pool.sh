#
# General test script utilities
#

if [ -z "$TIMEOUT" ] ; then
    echo expected TIMEOUT variable defined to its respective command
    exit 1
fi

function run_to ()
{
    maxtime=${1}s
    shift
    $TIMEOUT --signal=9 $maxtime "$@"
}

function test_start_servers ()
{
    nservers=${1:-4}
    startwait=${2:-15}
    maxtime=${3:-120}s
    poolkind=${4:-fifo_wait}
    pid=$$

    # start daemons
    for i in `seq 1 $nservers`
    do
        hostfile=`mktemp`
        $TIMEOUT --signal=9 ${maxtime} tests/margo-test-server na+sm:// -f $hostfile -p $poolkind &
        if [ $? -ne 0 ]; then
            # TODO: this doesn't actually work; can't check return code of
            # something executing in background.  We have to rely on the
            # return codes of the actual client side tests to tell if
            # everything started properly
            exit 1
        fi
    done

    # wait for servers to start
    sleep ${startwait}

    svr1=`cat $hostfile`
}
