import networkx as nx
from sklearn.neighbors import KernelDensity
import numpy as np

"""
NAMING CONVENTION
start node type, direction, edge type, other node's type
"""


def get_name(dg, node):
    if "cmd" not in dg.node[node]:
        return "UNKNOWN"
    else:
        return dg.node[node]["cmd"]

def count_edge_types(dg, dir, nodetype, edgetype, nodetype2, nodes=None):
    """
    Returns dict: name -> [counts]
    """
    d = {}
    if not nodes:
        nodes = dg.nodes()
    for node in nodes:
        if "TYPE" not in dg.node[node]:
            continue
        if dg.node[node]["TYPE"] != nodetype:
            continue
        name = get_name(dg, node)
        count = 0
        if dir == 'out':
            for (_, other) in dg.out_edges(node):
                if "TYPE" not in dg.node[other]:
                    continue
                if dg.node[other]["TYPE"] != nodetype2:
                    continue
                if dg[node][other]["TYPE"] == edgetype:
                    count += 1
        else:
            for (other, _) in dg.in_edges(node):
                if "TYPE" not in dg.node[other]:
                    continue
                if dg.node[other]["TYPE"] != nodetype2:
                    continue
                if dg[other][node]["TYPE"] == edgetype:
                    count += 1
        if name not in d:
            d[name] = []
        d[name].append(count)
    return d

def kde_make(counts,bw=0.5):
    #TODO use tophat kernel?
    kde = KernelDensity(bandwidth=bw, kernel='gaussian')
    kde = kde.fit(np.vstack(counts))
    return kde

def kde_predict(kde, val):
    return np.exp(kde.score(val))

def get_vals(dg, node):
    """
    given a directed graph and a node, calculate the statistics on that node
    and return them in a dictionary
    """
    d = {}
    name = get_name(dg, node)
    for k in functions:
        result = functions[k](dg, nodes=[node])
        if name in result:
            d[k] = result[name][0]
        else:
            d[k] = 0
    return d

def make_kdes(dg, bw=0.5):
    """
    returns a dictionary

    name -> dictionary: function name -> kernel density object
    """
    d = {}
    # add all node names
    for node in dg.nodes():
        name = get_name(dg, node)
        if name not in d:
            d[name] = {}
            for k in functions:
                d[name][k] = None
    for k in functions:
        for (name, counts) in functions[k](dg).items():
            try:
                d[name][k] = kde_make(counts,bw), counts
            except:
                d[name][k] = None
    return d

def kde_predict_all(kdes, dg, node):
    """
    given kdes ( name -> dictionary: function name -> kde)
    node must be a node in dg

    kdes does not have to be generated from dg
    """
    dvals = get_vals(dg, node)
    name = get_name(dg, node)

    print name, dvals

    if SHOULD_HARDCODE_GCC:
        name = '/usr/bin/gcc'
    d = {}
    for fname in kdes[name]:
        if kdes[name][fname]:
            d[fname] = kde_predict(kdes[name][fname][0], dvals[fname])
        else:
            d[fname] = None
    return d

from networkx.algorithms.link_analysis.pagerank_alg import pagerank_numpy

def centrality_pagerank(dg):
    return pagerank_numpy(dg)

from networkx.algorithms.centrality import out_degree_centrality, in_degree_centrality

def centrality_in_degree(dg):
    """
    dmargo's paper has edges in reverse of the data flow
    hello has inputs bar.txt and has outputs foo.txt
    then we have edges:

    bar.txt <--- hello <----foo.txt

    whereas our graph has edges in the other direction
    hence we actually want out degree
    """
    return in_degree_centrality(dg)

from networkx.algorithms.dag import ancestors, descendants

def centrality_ancestor(dg):
    """
    dmargo has edges swapped? DUNNO in his paper, == total # of descendents

    in our graph == total # of ancestors
    """
    V = float(len(dg.nodes()))
    return dict((node, len(descendants(dg, node)) / V) for node in dg.nodes())

from scipy.sparse import lil_matrix
from scipy.sparse.linalg import eigs

def centrality_eigenvector(dg):
    V = len(dg.nodes())
    M = lil_matrix((V,V))

    # index -> (pnode, version)
    ix_to_node = dict(zip(range(V), dg.nodes()))
    node_to_ix = dict(zip(dg.nodes(), range(V)))

    for (u, v) in dg.edges():
        M[node_to_ix[u],node_to_ix[v]] = 1.
    for node in dg.nodes():
        if len(dg[node]) == 0:
            i = node_to_ix[node]
            for j in range(V):
                M[i,j] = 1. / V
    vals, vecs = eigs(M, k=1, which='LM')
    print vals, vecs
    rank = {}
    for i in range(V):
        rank[ix_to_node[i]] = vecs[i,0]
    return rank

from networkx.algorithms.shortest_paths.unweighted import all_pairs_shortest_path_length

def centrality_opsahl(dg):
    """
    TODO: reverse edges
    """
    ds = all_pairs_shortest_path_length(dg)
    rank = {}
    for x in dg.nodes():
        r = 0.
        for v in dg.nodes():
            if x == v:
                continue
            if x not in ds:
                continue
            if v not in ds[x]:
                continue
            r += 1. / ds[x][v]
        rank[x] = r
    return rank

def centrality_age(dg):
    rank = {}
    min_time = float("inf")
    max_time = float("-inf")
    for node in dg.nodes():
        if "FREEZETIME" in dg.node[node]:
            time = dg.node[node]["FREEZETIME"]
            time_float = float("%d.%d" % (time["SEC"], time["NSEC"]))
            rank[node] = time_float
            min_time = min(min_time, time_float)
            max_time = max(max_time, time_float)
        else:
            rank[node] = None
    span_time = max_time - min_time
    for node in dg.nodes():
        if rank[node]:
            rank[node] = (rank[node] - min_time) / span_time
        else:
            rank[node] = 0.
    return rank

def aggregate(dg, rank):
    """
    returns a dictionary mapping names -> counts
    """
    d = {}
    for node in dg.nodes():
        name = get_name(dg, node)
        if name not in d:
            d[name] = []
        d[name].append(rank[node])
    return d

def counts_to_kdes(aggs):
    """
    transforms counts into kernel density objects
    """
    kdes = {}
    for name in aggs:
        kdes[name] = kde_make(aggs[name])
    return kdes


"""
need functions graph -> centrality for each node

centrality for each node -> {name -> list of centralities}


"""

centrality = {
    'PAGERANK' : pagerank_numpy,
    'IN_DEGREE' : out_degree_centrality,
}

