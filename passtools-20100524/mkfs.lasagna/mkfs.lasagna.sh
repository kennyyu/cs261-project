#!/bin/sh
MKDIR=/bin/mkdir
RMDIR=/bin/rmdir
MOUNT=/bin/mount
UMOUNT=/bin/umount
DATE=/bin/date

tempdir=/mnt/__passtmp

for last in "$@"; do
	:
done
device="$last"

mkfs -t ext3 "$device"

if [ ! -d $tempdir ]
then
	$MKDIR $tempdir
fi

$MOUNT -t ext3 "$device" $tempdir
$MKDIR $tempdir/.lasagna_stuff
(cd $tempdir/.lasagna_stuff && $PREFIX/libexec/passtools/waldo -o) || exit 1
$DATE +%s > $tempdir/.lasagna_stuff/clean
$UMOUNT $tempdir
$RMDIR $tempdir
