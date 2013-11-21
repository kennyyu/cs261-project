#!/bin/bash

if [ -z $1 ]
then
    python detect.py -h
    exit 1
fi

GCC_PNODE=5183
PYTHON_PNODE=5181
GCC2_PNODE=5846

python detect.py ../data/db_passtools 5404 0 ../data/db_passtools_x2 $PYTHON_PNODE 1 $1 --hardcode