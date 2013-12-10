from collections import defaultdict
import networkx as nx
import numpy as np
import matplotlib.pyplot as plt
import json
import sys
import bsm
import histogram

# functions to run on the graph (and aggregate results by node type)
FUNCTIONS = {
        "opsahl": histogram.centrality_opsahl,
        "indegree": histogram.centrality_in_degree,
        "ancestor": histogram.centrality_ancestor
        }

def process_counts(g):
    # count nodes by type
    proc_counts = defaultdict(int)
    for num in g.nodes():
        node = g.node[num]
        if node["type"] == "process":
            if not "cmd" in node:
                proc_counts["UNKNOWN"] += 1
            else:
                proc_counts[node["cmd"]] += 1

    return proc_counts

def edge_counts(g):
    """
    Returns a dictionary of process name to a list of edge type counts
    """
    # XXX TODO
    return {}

def get_values(g, functions):
    """
    Returns a dictionary of names to functions to list of values
    """
    metrics = {}

    # go through each metric
    for f in functions:
        values = functions[f](g)

        # for each node, if a process node, add metrics to count
        for node_num in values:
            node = g.node[node_num]
            # we only care about process nodes
            if node["type"] == "process":
                cmd = node["cmd"]

                # if first time we've seen this cmd, initialize
                if cmd not in metrics:
                    metrics[cmd] = dict((k,[]) for k in functions)

                # add this particular value to the metrics dict
                metrics[cmd][f].append(values[node_num])
    return metrics

# gaussian taken from http://stackoverflow.com/questions/14873203/plotting-of-1-dimensional-gaussian-distribution-function
def gaussian(x, mu, sig):
    return np.exp(-np.power(x - mu, 2.) / 2 * np.power(sig, 2.)) * 1./(2*math.PI*sig)
def parzen_estimate(x, vals):
    std = np.std(vals)
    return 1./len(vals) * sum(gaussian(x-val, x, std) for val in vals)

def parzen_is_expected(x, vals, psi):
    # get log liklihood of x
    ll_x = math.log(parzen_estimate(x, vals))

    # count y in vals for ll(y) < ll(x)
    count_y = sum(1 if math.log(parzen_estimate(y, vals)) < ll_x else 0 for y in vals)

    # if this probability is greater than psi, accept
    return float(count_y) / len(vals) > psi



if __name__ == "__main__":
    if len(sys.argv) < 2:
        print "Usage: python analyze.py json-file"
        sys.exit()

    datafile = sys.argv[1]
    print "datafile: ", datafile

    g = bsm.load(datafile)

    # preprocess nodes, giving each a cmd of "UNKNOWN" if none provided
    for num in g.nodes():
        node = g.node[num]
        if node["type"] == "process":
            if not "cmd" in node:
                node["cmd"] = "UNKNOWN"

    # get values
    # name => function => list of values
    values = get_values(g, FUNCTIONS)

    # create KDEs
    kdes = {}
    for name in values:
        kdes[name] = {}

    # iterate over names
    for name in values:
        # iterate over functions
        for f in values[name]:
            # get variance of values, for bandwidth
            variance = np.var(values[name][f])
            bw = np.abs(variance) if variance != 0 else 0.5
            kdes[name][f] = histogram.kde_make(values[name][f], bw=bw)
    print kdes








