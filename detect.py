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


if __name__ == "__main__":
    TESTS = {1: test1}
    parser = argparse.ArgumentParser("Detector")
    parser.add_argument("good", type=str, help="db directory for good")
    parser.add_argument("bad", type=str, help="db directory for bad")
    parser.add_argument("test", type=int, help="test to run")
    parser.add_argument("--graph", dest="graph", action="store_true", help="graph")
    parser.add_argument("--verbose", dest="verbose", action="store_true",
                        help="verbose")
    args = vars(parser.parse_args())
    good = args["good"]
    bad = args["bad"]
    VERBOSE = args["verbose"]
    test = args["test"]
    TESTS[test]()

