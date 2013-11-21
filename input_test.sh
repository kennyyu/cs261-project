#!/bin/bash

if [ -z $1 ]
then
    python detect.py -h
    exit 1
fi

python detect.py ../data/db_input 5151 0 ../data/db_input_x 4969 0 $1
