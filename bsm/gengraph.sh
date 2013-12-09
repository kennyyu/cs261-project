#!/bin/bash

# fail quickly
#set -e

if [ -z $2 ]
then
    echo "usage: $0 data.tar graph.txt"
    exit 1
fi

TAR=$1
GRAPHFILE=$2
OUTPUT="."
DIR=$(basename $TAR | rev | cut -c 5- | rev)
DIR=$OUTPUT/$DIR

ERROR="error.log"
rm -rf $ERROR

echo -e -n "untarring $TAR to $DIR..."
tar -C $OUTPUT -xvf $TAR > /dev/null 2>>$ERROR
echo "done"

echo -e -n "converting $DIR/pascal.psmonitor to $DIR/pascal.pself..."
gzip -d $DIR/pascal.psmonitor.gz
cat $DIR/pascal.psmonitor | sed s/"ps -awwxu"//g | sed s/"----------------------------------------"//g| sed '/^ *$/d' > $DIR/pascal.pself
echo "done"

echo -e -n "running praudit -r on bsm in $DIR/pascal.raw.bsm..."
gzip -d $DIR/pascal.bsm.gz
praudit -r $DIR/pascal.bsm > $DIR/pascal.raw.bsm
echo "done"

echo -e -n "building graph and storing in $GRAPHFILE..."
python bsm.py $DIR/pascal.raw.bsm $DIR/pascal.pself $GRAPHFILE
echo "done"
