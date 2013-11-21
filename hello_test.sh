#!/bin/bash

if [ -z $1 ]
then
    python detect.py -h
    exit 1
fi

python detect.py ../data/db_hello 2680 0 ../data/db_hello_x 2609 0 $1
