import argparse
import bsddb3 as bsddb
import matplotlib.pyplot as plt
import networkx as nx
import sys
import struct
from termcolor import colored

"""
struct pnode_version {
   uint64_t pnode;
   uint32_t version;
};
"""
PNODE_VERSION_FORMAT_STRING = "QI"

"""
child db : parent -> child
parent db : child -> parent
"""
CHILD_DB = "child.db"
PARENT_DB = "parent.db"

"""
struct provdb_val {
   uint8_t  pdb_flags;
   uint8_t  pdb_valuetype;
   union {
      uint16_t pdb_attrcode;        /* if PROV_ATTRFLAG_PACKED */
      uint16_t pdb_attrlen;        /* if not */
   } __attribute__((__packed__));
   uint32_t pdb_valuelen;
   uint8_t  pdb_data[0];
} __attribute__((__packed__));
"""
PROV_FORMAT_STRING = "BBHI"

"""
prov db : (pnode, version) -> provdb_val
"""
PROV_DB = "prov.db"

PROVDB_TOKENIZED = 1
PROVDB_PACKED = 2
PROVDB_ANCESTRY = 4
PROVDB_MISMATCH = 8

PROV_VALUE_TYPES = [
    "NIL",
    "STRING",
    "MULTISTRING",
    "INT",
    "REAL",
    "TIMESTAMP",
    "INODE",
    "PNODE",
    "PNODEVERSION",
    "OBJECT",
    "OBJECTVERSION",
]

"""
tnum2tok : token number (int) -> string

Need to use rnopen
"""
TNUM2TOK_DB = "tnum2tok.db"

def parse_string(s, tokens):
    return s

def parse_timestamp(s, tokens):
    sec, nsec = struct.unpack("ii", s)
    return {"SEC": sec, "NSEC": nsec}

def parse_pnode_version(s, tokens):
    pnode, version = struct.unpack(PNODE_VERSION_FORMAT_STRING, s)
    return {"PNODE": pnode, "VERSION": version}

def parse_tokens(s, tokens):
    numtokens = len(s) / 4
    format = "I" * numtokens
    nums = struct.unpack(format, s)
    tokens = [tokens[num] for num in nums]
    return tokens

def parse_int(s, tokens):
    return struct.unpack("i", s)[0]

TYPE_CONV = {
    "TYPE": parse_string,
    "NAME": parse_string,
    "INODE": parse_int,
    "PATH": parse_string,
    "ARGV": parse_tokens,
    "ENV": parse_tokens,
    "FREEZETIME": parse_timestamp,
    "EXECTIME": parse_timestamp,
    "FORKPARENT": parse_pnode_version,
    "PID": parse_int,
    "CREAT": parse_string,
    "UNLINK": parse_string,
    "INPUT": parse_pnode_version,
}

PROV_PACKED_VALUE_TYPES = [
    "INVALID",
    "TYPE",
    "NAME",
    "INODE",
    "PATH",
    "ARGV",
    "ENV",
    "FREEZETIME",
    "INPUT",
]

def parse_prov(provdb, tokens, nodes):
    """
    Fills in nodes with the deserialized provenance data.
    """
    for k,v in provdb.iteritems():
        pnode, version = struct.unpack(PNODE_VERSION_FORMAT_STRING, k)
        v_prefix = v[:8]
        v_suffix = v[8:]
        flags, valuetype, code_or_attrlen, valuelen = struct.unpack(PROV_FORMAT_STRING, v_prefix)
        attr = None
        value_string = None
        if not flags & PROVDB_PACKED:
            attrlen = code_or_attrlen
            format_string = "" + str(attrlen) + "s" + str(valuelen) + "s"
            attr, value_string = struct.unpack(format_string, v_suffix)
        else:
            code = code_or_attrlen
            attr = PROV_PACKED_VALUE_TYPES[code]
            value_string = v_suffix
        if pnode in nodes:
            if attr == "INPUT":
                if attr not in nodes[pnode][version]:
                    nodes[pnode][version][attr] = []
                nodes[pnode][version][attr].append(TYPE_CONV[attr](value_string, tokens))
            else:
                nodes[pnode][version][attr] = TYPE_CONV[attr](value_string, tokens)
            if flags & PROVDB_ANCESTRY:
                nodes[pnode][version]["ANCESTRY"] = True

def load_token_map(tnum2tokdb):
    """
    builds a dictionary of token numbers -> tokens
    """
    tokens = {}
    for k,v in tnum2tokdb.iteritems():
        tokens[k] = v
    return tokens

def add_and_get_node(pnode, version, nodes):
    """
    schema.h provdb
    Some attributes are per pnode and some are per version. Per pnode attributes
    are attached to version 0 of the node.

    Adds an entry to nodes for the pnode if it doesn't already exist
    """
    if pnode not in nodes:
        nodes[pnode] = {}
        nodes[pnode][0] = {}
    nodes[pnode][version] = {}
    # TODO
    return (pnode, version)

def build_graph(parentdb):
    """
    Returns a digraph and the nodes, keyed on pnode and values is a dictionary.
    """
    nodes = {}
    digraph = nx.DiGraph()
    for child, parent in parentdb.iteritems():
        p_pnode, p_version = struct.unpack(PNODE_VERSION_FORMAT_STRING, parent)
        c_pnode, c_version = struct.unpack(PNODE_VERSION_FORMAT_STRING, child)
        p_object = add_and_get_node(p_pnode, p_version, nodes)
        c_object = add_and_get_node(c_pnode, c_version, nodes)
        digraph.add_edge(p_object, c_object)
        #print "parent (%d,%d) -> child (%d,%d)" % (p_pnode, p_version, c_pnode, c_version)
    return digraph, nodes

COLOR_MAP = {
    "PROC": 'r',
    "FILE": 'g',
    "NP_FILE": 'b',
    "PIPE": 'y',
    "DIR": 'o',
}

def draw_graph(digraph, nodes):
    """
    Draws the digraph, coloring the nodes based on the type of provenance record.
    """
    dgnodes = digraph.nodes()
    values = [COLOR_MAP[digraph.node[n]["TYPE"]] for n in dgnodes]
    nx.draw_networkx(digraph, pos=nx.spring_layout(digraph, scale=5, iterations=1000),
                     node_color=values)
    from networkx.readwrite import json_graph
    data = json_graph.node_link_data(digraph)
    import json
    s = json.dumps(data)
    print s

    plt.show()

def graph_join(digraph, nodes):
    """
    Puts all the information from nodes into digraph
    """
    for pnode in nodes:
        # add node 0 information to all nodes
        version0 = nodes[pnode][0]
        for version in nodes[pnode]:
            # add VERSION edges
            digraph.add_node((pnode,version))
            prev_version = version - 1
            if prev_version >= 0:
                digraph.add_edge((pnode,prev_version),(pnode,version),TYPE="VERSION")
            for (k,v) in version0.items():
                if k == "ANCESTRY":
                    continue
                if k == "INPUT":
                    continue
                if k == "FORKPARENT":
                    continue
                digraph.node[(pnode,version)][k] = v
            copy = nodes[pnode][version].copy()
            if "ANCESTRY" in nodes[pnode][version]:
                del copy["ANCESTRY"]
                if "INPUT" in nodes[pnode][version]:
                    del copy["INPUT"]
                    for input in nodes[pnode][version]["INPUT"]:
                        pnode_other = input["PNODE"]
                        version_other = input["VERSION"]
                        assert(digraph.has_edge((pnode_other,version_other),(pnode,version)))
                        digraph.add_edge((pnode_other,version_other),
                                         (pnode,version),
                                         TYPE="INPUT")
                elif "FORKPARENT" in nodes[pnode][version]:
                    del copy["FORKPARENT"]
                    pnode_other = nodes[pnode][version]["FORKPARENT"]["PNODE"]
                    version_other = nodes[pnode][version]["FORKPARENT"]["VERSION"]
                    assert(digraph.has_edge((pnode_other,version_other),(pnode,version)))
                    digraph.add_edge((pnode_other,version_other),
                                     (pnode,version),
                                     TYPE="FORKPARENT")
            # do this if ancestry exists or not
            for (k,v) in copy.items():
                digraph.node[(pnode,version)][k] = v

def make_graph(dbdir):
    childdb = bsddb.btopen(dbdir + "/" + CHILD_DB)
    parentdb = bsddb.btopen(dbdir + "/" + PARENT_DB)
    provdb = bsddb.btopen(dbdir + "/" + PROV_DB)
    tnum2tokdb = bsddb.rnopen(dbdir + "/" + TNUM2TOK_DB)

    tokens = load_token_map(tnum2tokdb)
    digraph, nodes = build_graph(parentdb)
    parse_prov(provdb, tokens, nodes)
    graph_join(digraph, nodes)
    return digraph, nodes

def print_graph(digraph, nodes):
    for pnode in nodes:
        print pnode
        for version in nodes[pnode]:
            print colored("->", "white", "on_red"), version
            for key in nodes[pnode][version]:
                print colored("--->", "white", "on_red"), key, "->", nodes[pnode][version][key]

if __name__ == "__main__":
    parser = argparse.ArgumentParser("DAG builder from provenance data")
    parser.add_argument("dbdir", type=str, help="db directory")
    parser.add_argument("--graph", dest="graph", action="store_true",
                        help="should generate graph")
    parser.add_argument("--verbose", dest="verbose", action="store_true",
                        help="verbose")
    args = vars(parser.parse_args())

    dbdir = args["dbdir"]
    digraph, nodes = make_graph(dbdir)

    verbose = args["verbose"]
    if verbose:
        print_graph(digraph, nodes)

    should_graph = args["graph"]
    if should_graph:
        draw_graph(digraph, nodes)

