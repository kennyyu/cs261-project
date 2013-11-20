import argparse
import graph
import histogram

good = '../data/db_hello_v1'
bad = '../data/db_hello_x'
VERBOSE = False

def test1():
    dg_good, nodes_good = graph.make_graph(good)

    print ">>>>>GOOD ON GOOD"
    good_kdes = histogram.make_kdes(dg_good)
    good_vals = histogram.kde_predict_all(good_kdes, dg_good, (2680,0))
    for (k,v) in sorted(good_vals.items()):
        print k,v

    print
    print ">>>>>BAD ON GOOD"
    dg_bad, nodes_bad = graph.make_graph(bad)
    bad_on_good_vals = histogram.kde_predict_all(good_kdes, dg_bad, (2609,0))
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

def test_centrality(centrality_f):
    dg_good, nodes_good = graph.make_graph(good)
    good_node = (2680, 0)
    name = histogram.get_name(dg_good, good_node)

    print ">>>>>GOOD ON GOOD"
    rank_good = centrality_f(dg_good)
    counts_good = histogram.aggregate(dg_good, rank_good)
    kdes_good = histogram.counts_to_kdes(counts_good)

    r_good = rank_good[good_node]
    print good_node, name, r_good, histogram.kde_predict(kdes_good[name], rank_good[good_node])


    dg_bad, nodes_bad = graph.make_graph(bad)
    bad_node = (2609,0)
    name = histogram.get_name(dg_bad, bad_node)

    print ">>>>>BAD ON GOOD"
    rank_bad = centrality_f(dg_bad)

    r_bad = rank_bad[bad_node]
    print bad_node, name, r_bad, histogram.kde_predict(kdes_good[name], rank_bad[bad_node])

def test_in_degree():
    test_centrality(histogram.centrality_in_degree)

def test_ancestor():
    test_centrality(histogram.centrality_ancestor)

def test_eigenvector():
    test_centrality(histogram.centrality_eigenvector)

def test_opsahl():
    test_centrality(histogram.centrality_opsahl)

if __name__ == "__main__":
    TESTS = {
        1: test1,
        2: test_in_degree,
        3: test_ancestor,
        4: test_eigenvector,
        5: test_opsahl,
    }
    parser = argparse.ArgumentParser("Detector")
    parser.add_argument("good", type=str, help="db directory for good")
    parser.add_argument("bad", type=str, help="db directory for bad")
    parser.add_argument("test", type=int, help="test to run")
    parser.add_argument("--verbose", dest="verbose", action="store_true",
                        help="verbose")
    args = vars(parser.parse_args())
    good = args["good"]
    bad = args["bad"]
    VERBOSE = args["verbose"]
    test = args["test"]
    TESTS[test]()

