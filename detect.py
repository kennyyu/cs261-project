import argparse
import graph
import histogram
import numpy as np
from collections import defaultdict

good = '../data/db_passtools_gcc'
pnode_good = 5183
version_good = 0

bad = '../data/db_passtools_x2'
pnode_bad = 5181
version_bad = 0

VERBOSE = False

def test1():
    node_good = (pnode_good, version_good)
    node_bad = (pnode_bad, version_bad)

    dg_good, nodes_good = graph.make_graph(good)

    good_kdes = histogram.make_kdes(dg_good)
    good_vals = histogram.kde_predict_all(good_kdes, dg_good, node_good)

    name = histogram.get_name(dg_good, node_good)

    # extract all nodes with same name:
    proc_nodes = []
    proc_vals = {}
    for node_num, t in nodes_good.iteritems():
        for v in t:
            test_node = (node_num, v)
            gnode_name = histogram.get_name(dg_good, test_node)
            if gnode_name == name:
                proc_nodes.append(test_node)
                vals = histogram.kde_predict_all(good_kdes, dg_good, test_node)
                proc_vals[test_node] = vals


    for (k,v) in sorted(good_kdes[name].items()):
        print k, v[1] if v else v
    print
    print

    print ">>>>>GOOD ON GOOD"
    for (k,v) in sorted(good_vals.items()):
        print k,v

    mins = {}
    min_nodes = {}
    print ">>>>>GOOD ON GOOD (on %d nodes)" % len(proc_nodes)
    for node, vals in proc_vals.items():
        for (k,v) in sorted(vals.items()):
            if not k in mins:
                mins[k] = v
                min_nodes[k] = node
            elif v < mins[k]:
                mins[k] = v
                min_nodes[k] = node
    for (k,v) in sorted(mins.items()):
        print k,v,min_nodes[k], histogram.get_vals(dg_good, min_nodes[k])
    print
    print ">>>>>BAD ON GOOD"
    dg_bad, nodes_bad = graph.make_graph(bad)
    bad_on_good_vals = histogram.kde_predict_all(good_kdes, dg_bad, node_bad)
    for (k,v) in sorted(bad_on_good_vals.items()):
        print k,v


    print
    print ">>>>>COMPARISON"
    diffs = {}
    for k in good_vals:
        if good_vals[k] is not None and bad_on_good_vals[k] is not None:
            diffs[k] = good_vals[k] - bad_on_good_vals[k]
        else:
            diffs[k] = None
    for (k,v) in sorted(diffs.items()):
        print k,v

def test_centrality(centrality_f, reverse=False):
    node_good = (pnode_good, version_good)
    node_bad = (pnode_bad, version_bad)

    dg_good, nodes_good = graph.make_graph(good)
    name = histogram.get_name(dg_good, node_good)

    if reverse:
        rank_good = centrality_f(dg_good.reverse(copy=True))
    else:
        rank_good = centrality_f(dg_good)
    counts_good = histogram.aggregate(dg_good, rank_good)
    kdes_good = histogram.counts_to_kdes(counts_good)

    # REMOVE
    print counts_good[name]
    print "mean", np.mean(counts_good[name])

    print
    print
    print ">>>>>GOOD ON GOOD"
    r_good = rank_good[node_good]
    pre_good = histogram.kde_predict(kdes_good[name], rank_good[node_good])
    print node_good, name, r_good, pre_good


    dg_bad, nodes_bad = graph.make_graph(bad)
    name = histogram.get_name(dg_bad, node_bad)

    if histogram.SHOULD_HARDCODE_GCC:
        name = "/usr/bin/gcc"

    print ">>>>>BAD ON GOOD"
    if reverse:
        rank_bad = centrality_f(dg_bad.reverse(copy=True))
    else:
        rank_bad = centrality_f(dg_bad)

    r_bad = rank_bad[node_bad]
    pre_bad = histogram.kde_predict(kdes_good[name], rank_bad[node_bad])
    print node_bad, name, r_bad, pre_bad

    print ">>>>>COMPARISON"
    print "diff (good - bad)", name, "rank diff", r_good - r_bad, "prediction diff", pre_good - pre_bad


def test_in_degree():
    test_centrality(histogram.centrality_in_degree)

def test_ancestor():
    test_centrality(histogram.centrality_ancestor)

def test_eigenvector():
    test_centrality(histogram.centrality_eigenvector)

def test_opsahl():
    test_centrality(histogram.centrality_opsahl)

def test_age():
    test_centrality(histogram.centrality_age)

def test_pagerank():
    test_centrality(histogram.pagerank)

if __name__ == "__main__":
    TESTS = {
        "simple": test1,
        "in_degree": test_in_degree,
        "ancestor": test_ancestor,
        "eigenvector": test_eigenvector,
        "opsahl": test_opsahl,
        "age": test_age,
        "pagerank": test_pagerank,
    }
    parser = argparse.ArgumentParser("Detector")
    parser.add_argument("db_good", type=str, help="db directory for good")
    parser.add_argument("pnode_good", type=int, help="pnode for good")
    parser.add_argument("version_good", type=int, help="version for good")

    parser.add_argument("db_bad", type=str, help="db directory for bad")
    parser.add_argument("pnode_bad", type=int, help="pnode for good")
    parser.add_argument("version_bad", type=int, help="version for bad")

    parser.add_argument("test", type=str, help="test to run",
                        choices=list(TESTS.keys()))
    parser.add_argument("--verbose", dest="verbose", action="store_true",
                        help="verbose")
    parser.add_argument("--hardcode", dest="hardcode", action="store_true",
                        help="hardcode /usr/bin/gcc")

    args = vars(parser.parse_args())
    good = args["db_good"]
    pnode_good = args["pnode_good"]
    version_good = args["version_good"]

    bad = args["db_bad"]
    pnode_bad = args["pnode_bad"]
    version_bad = args["version_bad"]

    print args

    VERBOSE = args["verbose"]
    histogram.SHOULD_HARDCODE_GCC = args["hardcode"]
    test = args["test"]
    TESTS[test]()

