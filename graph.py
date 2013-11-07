import bsddb3 as bsddb
import matplotlib.pyplot as plt
import networkx as nx
import sys
import struct

"""
struct pnode_version {
   uint64_t pnode;
   uint32_t version;
};
"""
ANCESTRY_FORMAT_STRING = "QI"

"""
child db : parent -> child
parent db : child -> parent
"""
CHILD_DB = "child.db"
PARENT_DB = "parent.db"

def add_and_get_node(pnode, version, nodes):
    """
    schema.h provdb
    Some attributes are per pnode and some are per version. Per pnode attributes
    are attached to version 0 of the node.

    Adds an entry to nodes for the pnode if it doesn't already exist
    """
    tuple = pnode #(pnode, version)
    if tuple not in nodes:
        nodes[tuple] = {}
        nodes[tuple]["name"] = str(tuple)
    return nodes[tuple]["name"]

def build_graph(parentdb):
    """
    Returns a digraph and the nodes, keyed on pnode and values is a dictionary.
    """
    nodes = {}
    digraph = nx.DiGraph()
    for child, parent in parentdb.iteritems():
        p_pnode, p_version = struct.unpack(ANCESTRY_FORMAT_STRING, parent)
        c_pnode, c_version = struct.unpack(ANCESTRY_FORMAT_STRING, child)
        p_object = add_and_get_node(p_pnode, p_version, nodes)
        c_object = add_and_get_node(c_pnode, c_version, nodes)
        digraph.add_edge(p_object, c_object)
        #print "parent (%d,%d) -> child (%d,%d)" % (p_pnode, p_version, c_pnode, c_version)
    return digraph, nodes

if __name__ == "__main__":
    if len(sys.argv) < 2:
        print "usage: %s db-directory" % sys.argv[0]
        sys.exit(-1)

    dbdir = sys.argv[1]
    childdb = bsddb.btopen(dbdir + "/" + CHILD_DB)
    parentdb = bsddb.btopen(dbdir + "/" + PARENT_DB)

    digraph, nodes = build_graph(parentdb)
    nx.draw_networkx(digraph, pos=nx.spring_layout(digraph, scale=5, iterations=1000))
    plt.show()






"""
FORMAT_STRING = "bbHI"

if len(sys.argv) < 3:
    print "usage: %s file mode" % sys.argv[0]
    sys.exit()

dbfile = sys.argv[1]
mode = sys.argv[2]

PROVDB_TOKENIZED = 1
PROVDB_PACKED = 2
PROVDB_ANCESTRY = 4
PROVDB_MISMATCH = 8

db = bsddb.btopen(dbfile)
if mode == "s":
    for k,v in db.iteritems():
        v_prefix = v[:8]
        v_suffix = v[8:]
        flags, valuetype, code_or_attrlen, valuelen = struct.unpack(FORMAT_STRING, v_prefix)
        if not flags & PROVDB_PACKED:
            unpacked_string = "" + str(code_or_attrlen) + "s" + str(valuelen) + "s"
            attr, value = struct.unpack(unpacked_string, v_suffix)
            print k, flags, valuetype, code_or_attrlen, valuelen, attr, ':'.join(x.encode('hex') for x in value)
        else:
            unpacked_string = "" + str(valuelen) + "s"
            value = struct.unpack(unpacked_string, v_suffix)
            print k, flags, valuetype, code_or_attrlen, valuelen, value[0], ':'.join(x.encode('hex') for x in value)
#            print flags, valuetype, code_or_attrlen, valuelen, v_suffix[:code_or_attrlen], int(v_suffix[code_or_attrlen:])
#            attr = v[:code_or_attrlen]
#            value = v[code_or_attrlen:]
#            print k, flags, "[(type, len, value)", valuetype, valuelen, value, "] [(len, attr):", code_or_attrlen, attr, "]"
#        print k, struct.unpack(FORMAT_STRING, v_prefix), v_suffix
elif mode == "kx":
    print [k for k,_ in db.iteritems()]
elif mode == "vx":
    print [v for _,v in db.iteritems()]
db.close()
"""
