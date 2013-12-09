import networkx as nx
import matplotlib.pyplot as plt
import json
import sys
import bsm

if len(sys.argv) < 2:
    print "Usage: python kenny.py json-file"
    sys.exit()

datafile = sys.argv[1]
print "datafile: ", datafile

g = bsm.load(datafile)

