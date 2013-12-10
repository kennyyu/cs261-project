from collections import defaultdict
from random import shuffle
import bisect
import operator
import networkx as nx
import numpy as np
import math
import matplotlib.pyplot as plt
import json
import sys
import bsm
import bsm_list
import histogram

# functions to run on the graph (and aggregate results by node type)
FUNCTIONS = {
        "opsahl": histogram.centrality_opsahl,
        #"indegree": histogram.centrality_in_degree,
        #"ancestor": histogram.centrality_ancestor
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
    Also modifies the graph
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

                # add value to node
                node[f] = values[node_num]
    return metrics

# gaussian taken from http://stackoverflow.com/questions/14873203/plotting-of-1-dimensional-gaussian-distribution-function
def gaussian(x, mu, sig):
    return np.exp(-np.power(x - mu, 2.) / (2 * np.power(sig, 2.))) * 1./(np.sqrt(2 * math.pi)*sig)
def parzen_estimate(x, vals):
    std = np.std(vals)
    return 1./len(vals) * sum(gaussian(x, val, std) for val in vals)

def parzen_is_expected(x, vals, ll_ys, psi):
    # get log liklihood of x
    ll_x = math.log(parzen_estimate(x, vals))

    # count y in vals for ll(y) < ll(x)
    #count_y = sum(1 if math.log(parzen_estimate(y, vals)) < ll_x else 0 for y in vals)
    count_y = bisect.bisect_left(ll_ys, ll_x)

    # if this probability is greater than psi, accept
    pred = float(count_y) / len(vals)
    return pred > psi, pred


def sufficient(num_intrusions, num_normal):
    """
    Returns true if marking as intrusion
    TODO
    """
    return num_intrusions > num_normal

def get_candidate_set(g, w, t):
    """
    Returns set of node indicies that have edges that have timestamps in [t-w, t+w]
    """
    nodes = set()
    for u,v in g.edges():
        # lop off last 9
        if "time" in g[u][v]:
            timestamp = int(g[u][v]["time"]) / (10**9)
            if timestamp > t-w and timestamp < t+w:
                if g.node[u]["type"] == "process" and g.node[u]["cmd"] != "UNKNOWN":
                    nodes.add(u)
                if g.node[v]["type"] == "process" and g.node[v]["cmd"] != "UNKNOWN":
                    nodes.add(v)
    return nodes

def candidate_set_decisions(g, t, w, metric, psis):
    """
    Returns a mapping from psi to the number of nodes marked as an intrusion
    for psi in psis
    """
    candidate_set = get_candidate_set(g, t, w)
    num_nodes = 0
    parzen_probs = []
    for idx in candidate_set:
        name = g.node[idx]["cmd"]
        if name == "UNKNOWN":
            continue
        num_nodes += 1

        # get probability for this node
        v = g.node[idx][metric]
        vals = values[name][metric]
        ll_y = log_probs[name][metric]
        _,p = parzen_is_expected(v, vals, ll_y, 0.1)
        parzen_probs.append(p)

    # mapping from psi to (num_normal
    results = {}
    for psi in psis:
        num_normal = 0
        for p in parzen_probs:
            if p > psi:
                num_normal += 1
        results[psi] = {"normal": num_normal, "total": num_nodes, "intrusions": num_nodes - num_normal}

    return results


if __name__ == "__main__":
    if len(sys.argv) < 3:
        print "Usage: python analyze.py graph.txt bsm.list"
        sys.exit()

    datafile = sys.argv[1]
    bsmlistfile = sys.argv[2]
    print "datafile: ", datafile

    print "Loading graph..."
    g = bsm.load(datafile)

    print "Num nodes: {}, num edges: {}".format(len(g.nodes()), len(g.edges()))
    for n in g.nodes():
        inedges = g.in_edges(n)
        outedges = g.out_edges(n)
        node = g.node[n]
        num_edges = len(inedges) + len(outedges)
        if num_edges == 0:
            g.remove_node(n)

    print "Parsing bsm list..."
    bsm_rows = bsm_list.parse_bsm_list(bsmlistfile)
    u2r_rows = [row for row in bsm_rows if row["type"] == "u2r"]
    none_rows = [row for row in bsm_rows if row["type"] == "none"]
    shuffle(none_rows)
    none_rows = none_rows[:2*len(u2r_rows)]


    print "Found {} u2r, {} none".format(len(u2r_rows), len(none_rows))
    if len(none_rows) == 0:
        sys.exit()

    # vars to store results
    """
    true positive -> you are an intrusion and we flagged you
    false negative -> you are an intrusion and we did not flag
    false positive -> you are not an intrusion and we flagged you
    true negative -> you are not an intrusion and we did not flag
    keyed on psi
    """
    PSIS = [0.000001, 0.00001, 0.0001, 0.001, 0.01, 0.05, 0.1, 0.2, 0.3, 0.4, 0.5, 0.6, 0.7, 0.8, 0.9]
    true_positive = dict((k,0) for k in PSIS)
    false_negative = dict((k,0) for k in PSIS)
    false_positive = dict((k,0) for k in PSIS)
    true_negative = dict((k,0) for k in PSIS)


    # preprocess nodes, giving each a cmd of "UNKNOWN" if none provided
    for num in g.nodes():
        node = g.node[num]
        if node["type"] == "process":
            if not "cmd" in node:
                node["cmd"] = "UNKNOWN"

    # get values
    # name => function => list of values
    print "Computing centrality metrics..."
    values = get_values(g, FUNCTIONS)


    # compute the log probs in advance
    print "Computing log probs..."
    log_probs = {}
    for name in values:
        log_probs[name] = {}
        for f in values[name]:
            # generate list of L(y)
            results = {}
            ll_y = []
            for v in values[name][f]:
                # cache results
                if v in results:
                     p = results[v]
                else:
                    p = math.log(parzen_estimate(v, values[name][f]))
                    results[v] = p
                ll_y.append(p)
            # store the SORTED log probs (for quick lookup later)
            log_probs[name][f] = sorted(ll_y)

    W = 100

    print "Deciding u2r..."
    # iterate over u2r rows
    for row in u2r_rows:
        t = row["timestamp"]
        psi_to_count = candidate_set_decisions(g, t, W, "opsahl", PSIS)
        print psi_to_count
        for psi in psi_to_count:
            if sufficient(psi_to_count[psi]["intrusions"], psi_to_count[psi]["normal"]):
                true_positive[psi] += 1
            else:
                false_negative[psi] += 1

    print "Deciding none..."
    # iterate over normal rows
    for row in none_rows:
        t = row["timestamp"]
        psi_to_count = candidate_set_decisions(g, t, W, "opsahl", PSIS)
        print psi_to_count
        for psi in psi_to_count:
            if sufficient(psi_to_count[psi]["intrusions"], psi_to_count[psi]["normal"]):
                false_positive[psi] += 1
            else:
                true_negative[psi] += 1

    print "True positive:"
    sorted_x = sorted(true_positive.iteritems(), key=operator.itemgetter(0))
    for t in sorted_x:
        print "{}: {}".format(t[0], t[1])

    print "False positive:"
    sorted_x = sorted(false_positive.iteritems(), key=operator.itemgetter(0))
    for t in sorted_x:
        print "{}: {}".format(t[0], t[1])

    print "True negative:"
    sorted_x = sorted(true_negative.iteritems(), key=operator.itemgetter(0))
    for t in sorted_x:
        print "{}: {}".format(t[0], t[1])

    print "False negative:"
    sorted_x = sorted(false_negative.iteritems(), key=operator.itemgetter(0))
    for t in sorted_x:
        print "{}: {}".format(t[0], t[1])


"""
    psi = 0.1
    for v in vals:
        # cache results
        if v in results:
             p = results[v]
        else:
            _,p = parzen_is_expected(v, vals, ll_y, psi)
            results[v] = p
            #print "{} is {}expected (p = {})".format(v, "" if expected else "NOT ", p)
    for psi in [0.01, 0.05, 0.1, 0.2, 0.5]:
        num_expected = 0
        for val in vals:
            if results[val] > psi:
                num_expected += 1
        print "Psi: {}/{} expected ({}%)".format(psi, num_expected, len(vals), float(num_expected)/len(vals))
        """

