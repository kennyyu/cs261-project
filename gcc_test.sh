#!/bin/bash

if [ -z $1 ]
then
    python detect.py -h
    exit 1
fi

python detect.py ../data/db_gcc 4913 0 ../data/db_gcc_x 7881 0 $1