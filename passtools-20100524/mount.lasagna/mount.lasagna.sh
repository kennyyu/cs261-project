#! /bin/sh
waldo_location=$PREFIX/libexec/passtools/waldo
sage_location=$PREFIX/bin/sage

if [ -d $1 ]; then
    # underlying fs is already mounted and we're being asked to mount
    # just the stack.
    echo "$0: assuming $1 is an already-mounted underlying volume" 1>&2
    echo "$0: and not starting daemons" 1>&2
    DEV="$1"
    pid=''
else
    DEV="$2"
    mount -t ext3 $3 $4 $1 $2
    result=$?
    if [ "$result" -ne "0" ]; then
	echo "mount.lasagna: ext3 mount failed with $result"
	exit $result
    fi

    cd $DEV/.lasagna_stuff
    if [ "`pwd`" != "$2/.lasagna_stuff" ]; then
	echo "mount.lasagna: could not cd into $DEV/.lasagna_stuff"
	exit -1
    fi

    $waldo_location &
    pid=$!
    filename=`echo waldo$DEV | sed 's/\//_/g'`
    fullpath=/var/run/$filename
    echo $pid > $fullpath

    $sage_location -p db -s &
    spid=$!
    filename=`echo sage$DEV | sed 's/\//_/g'`
    fullpath=/var/run/$filename.pid
    echo $spid > $fullpath
fi

if ! cat /proc/filesystems | grep -w lasagna >/dev/null 2>&1; then
    echo "mount.lasagna: loading lasagna module explicitly just in case" 1>&2
    modprobe lasagna || true
fi

OPTIONS=`echo "$4" | sed 's/noauto,//;s/,noauto//;s/^noauto$//'`
$ROOTPREFIX/sbin/mount_lasagna_helper $DEV $2 $3 "$OPTIONS"
result=$?
if [ "$result" -ne "0" ]; then
	echo "mount.lasagna: lasagna mount failed with $result"
	if [ "x$pid" != x ]; then
	    kill -USR1 $pid
	    kill $spid
	    cd /
	    sleep 1
	    umount $2
	fi
	exit $result
fi

