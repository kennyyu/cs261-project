#! /bin/sh
VOL="$1"
VOLSTRING=`echo "$VOL" | sed 's/\//_/g'`

# usage:
#   $1: name
#   $2: pidfile
#   $3: signal
killit() {
    local name="$1"
    local pidfile="$2"
    local signal="$3"
    local pid

    #echo "$0: $name: pidfile $pidfile"

    if [ -e "$pidfile" ]; then
	pid=`cat "$pidfile"`
	#echo "$0: $name: pid $pid"
	if kill -0 "$pid" >/dev/null 2>&1; then
	    #echo "$0: killing $name..."
	    kill "$signal" "$pid"
	    # wait a moment
	    echo | cat | cat >/dev/null
	    while kill -0 "$pid" >/dev/null 2>&1; do
		echo "$0: waiting for $name to exit"
		sleep 1
	    done
	    echo "$0: $name: stopped"
	else
	    echo "$0: $name: pid $pid: not running" 1>&2
	    return 0
	fi
	rm -f "$pidfile"
    fi
    return 0
}

killit "waldo" /var/run/waldo"$VOLSTRING" -USR1
killit "sage" /var/run/sage"$VOLSTRING".pid -TERM

$ROOTPREFIX/sbin/umount_lasagna_helper $1
result=$?
if [ "$result" -ne "0" ]; then
	echo "umount.lasagna: lasagna umount failed with $result"
	exit $result
fi
umount $1
result=$?
if [ "$result" -ne "0" ]; then
	echo "umount.lasagna: ext3 umount failed with $result"
	exit $result
fi

