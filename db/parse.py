import bsddb3 as bsddb
import sys
import struct

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
