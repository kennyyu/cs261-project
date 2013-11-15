import graph
import histogram

if __name__ == "__main__":
    good = '../data/db_hello_v1'
    bad = '../data/db_hello_x'
    dg_good, nodes_good = graph.make_graph(good)
    dg_bad, nodes_bad = graph.make_graph(bad)

    print ">>>>>GOOD ON GOOD"
    good_kdes = histogram.make_kdes(dg_good)
    good_vals = histogram.kde_predict_all(good_kdes, dg_good, (2680,1))
    for (k,v) in sorted(good_vals.items()):
        print k,v

    print
    print ">>>>>BAD ON BAD"
    bad_kdes = histogram.make_kdes(dg_bad)
    bad_vals = histogram.kde_predict_all(bad_kdes, dg_bad, (2609,1))
    for (k,v) in sorted(bad_vals.items()):
        print k,v

    print
    print ">>>>>BAD ON GOOD"
    bad_on_good_vals = histogram.kde_predict_all(good_kdes, dg_bad, (2609,1))
    for (k,v) in sorted(bad_on_good_vals.items()):
        print k,v
