#! /bin/sh
MKDIR=/bin/mkdir
RMDIR=/bin/rmdir
MOUNT=/bin/mount
UMOUNT=/bin/umount

tempdir=/mnt/__passtmp

FAILED=

fsck.ext3 "$@"
RESULT=$?
if [ $RESULT -ne 0 ]; then
    echo "$0: fsck.ext3 failed" 1>&2
    exit $RESULT
fi

if [ ! -d $tempdir ]; then
	$MKDIR $tempdir
fi

echo "$MOUNT -t ext3 $@ $tempdir"
$MOUNT -t ext3 "$@" $tempdir
if ! (
    cd $tempdir/.lasagna_stuff || exit 1
    $PREFIX/libexec/passtools/waldo -r || exit 1
); then
    FAILED=$?
fi
$UMOUNT $tempdir
$RMDIR $tempdir

if [ "x$FAILED" != x ]; then
    echo "$0: recovery failed." 1>&2
    exit $FAILED
fi

exit 0
