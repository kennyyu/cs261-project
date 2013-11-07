import bsddb3 as bsddb
import sys
import struct

# For string compression
# mappings from strings <-> token numbers
tnum2tokdb = bsddb.rnopen("tnum2tok.db")
tok2tnumdb = bsddb.btopen("tok2tnum.db")

# mapping from token numbers -> pnode number
arg2pdb = bsddb.btopen("arg2p.db")
env2pdb = bsddb.btopen("env2p.db")

# inode number -> (pnode number, version number)
i2pdb = bsddb.btopen("i2p.db")

# pnode number -> inode number
p2idb = bsddb.btopen("p2i.db")

# To lookup a pnode by process/file name
# string -> pnode number
namedb = bsddb.btopen("name.db")

# (pnode number, version number) -> (pnode number, version number)
childdb = bsddb.btopen("child.db")
parentdb = bsddb.btopen("parent.db")

# (pnode number, version number) -> struct provb_val
# see passtools/include/schema.h
provdb = bsddb.btopen("prov.db")

"""
struct provdb_val {
   uint8_t  pdb_flags;
   uint8_t  pdb_valuetype;
   union {
      uint16_t pdb_attrcode;    /* if PROV_ATTRFLAG_PACKED */
      uint16_t pdb_attrlen;     /* if not */
   } __attribute__((__packed__));
   uint32_t pdb_valuelen;
   uint8_t  pdb_data[0];
} __attribute__((__packed__));
"""



if len(sys.argv) < 3:
    print "usage: %s file mode" % sys.argv[0]
    sys.exit()

dbfile = sys.argv[1]
mode = sys.argv[2]

db = bsddb.btopen(dbfile)
if mode == "s":
    for k,v in db.iteritems():
        print k,v
elif mode == "kx":
    print [k for k,_ in db.iteritems()]
elif mode == "vx":
    print [v for _,v in db.iteritems()]
db.close()
