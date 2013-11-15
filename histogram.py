import networkx as nx

"""
type of node to look at ____ direction ____ edge type to look for
"""

def file_out_file(dg):
    return count_edge_types(dg, dir='out', nodetype='FILE', edgetype='INPUT', nodetype2='FILE')

def file_in_file(dg):
    return count_edge_types(dg, dir='in', nodetype='FILE', edgetype='INPUT', nodetype2='FILE')

def file_out_proc(dg):
    return count_edge_types(dg, dir='out', nodetype='FILE', edgetype='INPUT', nodetype2='PROC')

def file_in_proc(dg):
    return count_edge_types(dg, dir='in', nodetype='FILE', edgetype='INPUT', nodetype2='PROC')

def proc_out_file(dg):
    return count_edge_types(dg, dir='out', nodetype='PROC', edgetype='INPUT', nodetype2='FILE')

def proc_in_file(dg):
    return count_edge_types(dg, dir='in', nodetype='PROC', edgetype='INPUT', nodetype2='FILE')

def proc_out_proc(dg):
    return count_edge_types(dg, dir='out', nodetype='PROC', edgetype='FORKPARENT', nodetype2='PROC')

def proc_in_proc(dg):
    return count_edge_types(dg, dir='in', nodetype='PROC', edgetype='FORKPARENT', nodetype2='PROC')

def proc_in_version(dg):
    return count_edge_types(dg, dir='in', nodetype='PROC', edgetype='VERSION', nodetype2='PROC')

def proc_out_version(dg):
    return count_edge_types(dg, dir='out', nodetype='PROC', edgetype='VERSION', nodetype2='PROC')

def file_in_version(dg):
    return count_edge_types(dg, dir='in', nodetype='FILE', edgetype='VERSION', nodetype2='FILE')

def file_out_version(dg):
    return count_edge_types(dg, dir='out', nodetype='FILE', edgetype='VERSION', nodetype2='FILE')

def count_edge_types(dg, dir, nodetype, edgetype, nodetype2):
    """
    Returns dict: name -> [counts]
    """
    d = {}
    for node in dg.nodes():
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

