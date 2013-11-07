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
    return {"STRING": s}

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
    tokens = {tokens[num] for num in nums}
    return {"TOKENS": tokens}

def parse_int(s, tokens):
    return {"INT": struct.unpack("i", s)}

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
    for k,v in provdb.iteritems():
        pnode, version = struct.unpack(PNODE_VERSION_FORMAT_STRING, k)
        v_prefix = v[:8]
        v_suffix = v[8:]
        flags, valuetype, code_or_attrlen, valuelen = struct.unpack(PROV_FORMAT_STRING, v_prefix)
        if not flags & PROVDB_PACKED:
            valuestring = PROV_VALUE_TYPES[valuetype]
            unpacked_string = "" + str(code_or_attrlen) + "s" + str(valuelen) + "s"
            attr, value = struct.unpack(unpacked_string, v_suffix)
            if pnode in nodes:
                val = TYPE_CONV[attr](value, tokens)
                nodes[pnode][version][attr] = (flags, val)
#                nodes[pnode][version][attr] = (flags, valuestring, valuelen, ':'.join(x.encode('hex') for x in value))
#                nodes[pnode][version][valuestring] = (flags, code_or_attrlen, valuelen, attr, ':'.join(x.encode('hex') for x in value))
        else:
            valuestring = PROV_PACKED_VALUE_TYPES[code_or_attrlen]
#            unpacked_string = "" + str(valuelen) + "s"
#            value = struct.unpack(unpacked_string, v_suffix)
            if pnode in nodes:
                val = TYPE_CONV[valuestring](v_suffix, tokens)
                nodes[pnode][version][valuestring] = (flags, val)
#                nodes[pnode][version][valuestring] = (flags, valuelen, value[0])
#                nodes[pnode][version][valuestring] = (flags, valuelen, value[0], ':'.join(x.encode('hex') for x in value))

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
    return pnode

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

if __name__ == "__main__":
    if len(sys.argv) < 2:
        print "usage: %s db-directory" % sys.argv[0]
        sys.exit(-1)

    dbdir = sys.argv[1]
    childdb = bsddb.btopen(dbdir + "/" + CHILD_DB)
    parentdb = bsddb.btopen(dbdir + "/" + PARENT_DB)
    provdb = bsddb.btopen(dbdir + "/" + PROV_DB)
    tnum2tokdb = bsddb.rnopen(dbdir + "/" + TNUM2TOK_DB)

    tokens = load_token_map(tnum2tokdb)

    digraph, nodes = build_graph(parentdb)
    parse_prov(provdb, tokens, nodes)
    for pnode in nodes:
        print pnode
        for version in nodes[pnode]:
            print colored("->", "white", "on_red"), version
            for key in nodes[pnode][version]:
                print colored("--->", "white", "on_red"), key, "->", nodes[pnode][version][key]

    #nx.draw_networkx(digraph, pos=nx.spring_layout(digraph, scale=5, iterations=1000))
    #plt.show()






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
