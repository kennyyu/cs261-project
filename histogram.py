import networkx as nx
from scipy.stats.kde import gaussian_kde

"""
NAMING CONVENTION
start node type, direction, edge type, other node's type
"""

def file_out_file(dg, nodes=None):
    return count_edge_types(dg, nodes=nodes, dir='out', nodetype='FILE', edgetype='INPUT', nodetype2='FILE')

def file_in_file(dg, nodes=None):
    return count_edge_types(dg, nodes=nodes, dir='in', nodetype='FILE', edgetype='INPUT', nodetype2='FILE')

def file_out_proc(dg, nodes=None):
    return count_edge_types(dg, nodes=nodes, dir='out', nodetype='FILE', edgetype='INPUT', nodetype2='PROC')

def file_in_proc(dg, nodes=None):
    return count_edge_types(dg, nodes=nodes, dir='in', nodetype='FILE', edgetype='INPUT', nodetype2='PROC')

def proc_out_file(dg, nodes=None):
    return count_edge_types(dg, nodes=nodes, dir='out', nodetype='PROC', edgetype='INPUT', nodetype2='FILE')

def proc_in_file(dg, nodes=None):
    return count_edge_types(dg, nodes=nodes, dir='in', nodetype='PROC', edgetype='INPUT', nodetype2='FILE')

def proc_out_proc(dg, nodes=None):
    return count_edge_types(dg, nodes=nodes, dir='out', nodetype='PROC', edgetype='FORKPARENT', nodetype2='PROC')

def proc_in_proc(dg, nodes=None):
    return count_edge_types(dg, nodes=nodes, dir='in', nodetype='PROC', edgetype='FORKPARENT', nodetype2='PROC')

def proc_in_version(dg, nodes=None):
    return count_edge_types(dg, nodes=nodes, dir='in', nodetype='PROC', edgetype='VERSION', nodetype2='PROC')

def proc_out_version(dg, nodes=None):
    return count_edge_types(dg, nodes=nodes, dir='out', nodetype='PROC', edgetype='VERSION', nodetype2='PROC')

def file_in_version(dg, nodes=None):
    return count_edge_types(dg, nodes=nodes, dir='in', nodetype='FILE', edgetype='VERSION', nodetype2='FILE')

def file_out_version(dg, nodes=None):
    return count_edge_types(dg, nodes=nodes, dir='out', nodetype='FILE', edgetype='VERSION', nodetype2='FILE')

def count_edge_types(dg, dir, nodetype, edgetype, nodetype2, nodes=None):
    """
    Returns dict: name -> [counts]
    """
    d = {}
    if not nodes:
        nodes = dg.nodes()
    for node in nodes:
        if dg.node[node]["TYPE"] != nodetype:
            continue
        name = dg.node[node]["NAME"]
        count = 0
        if dir == 'out':
            for (_, other) in dg.out_edges(node):
                if dg.node[other]["TYPE"] != nodetype2:
                    continue
                if dg[node][other]["TYPE"] == edgetype:
                    count += 1
        else:
            for (other, _) in dg.in_edges(node):
                if dg.node[other]["TYPE"] != nodetype2:
                    continue
                if dg[other][node]["TYPE"] == edgetype:
                    count += 1
        if count > 0:
            if name not in d:
                d[name] = []
            d[name].append(count)
    return d

def kde_make(counts):
    return gaussian_kde(counts)

def kde_predict(kde, val):
    return kde.evaluate(val)

functions = {
    'file_out_file': file_out_file,
    'file_in_file': file_in_file,
    'file_out_proc': file_out_proc,
    'file_in_proc': file_in_proc,
    'file_out_version': file_out_version,
    'file_in_version': file_in_version,
    'proc_out_file': proc_out_file,
    'proc_in_file': proc_in_file,
    'proc_out_proc': proc_out_proc,
    'proc_in_proc': proc_in_proc,
    'proc_out_version': proc_out_version,
    'proc_in_version': proc_in_version,
}

def get_vals(dg, node):
    """
    given a directed graph and a node, calculate the statistics on that node
    and return them in a dictionary
    """
    d = {}
    name = dg.node[node]["NAME"]
    for k in functions:
        result = functions[k](dg, nodes=[node])
        if name in result:
            d[k] = result[name]
        else:
            d[k] = []
    return d

def make_kdes(dg):
    """
    returns a dictionary

    name -> dictionary: function name -> kernel density object
    """
    d = {}
    # add all node names
    for node in dg.nodes():
        name = dg.node[node]["NAME"]
        if name not in d:
            d[name] = {}
            for k in functions:
                d[name][k] = None
    for k in functions:
        for (name, counts) in functions[k](dg).items():
            try:
                d[name][k] = kde_make(counts)
            except:
                d[name][k] = None
    return d

def kde_predict_all(dg, kdes, node):
    """
    given kdes ( name -> dictionary: function name -> kde)
    a name
    """
    dvals = get_vals(dg, node)
    name = dg.node[node]["NAME"]
    d = {}
    for fname in kdes[name]:
        if kdes[name][fname]:
            d[fname] = kdes[name][fname].evaluate(dval[fname])
        else:
            d[fname] = None
    return d

