import bsddb
import sys

if len(sys.argv) < 3:
    print "usage: %s file mode" % sys.argv[0]
    sys.exit()

dbfile = sys.argv[1]
mode = sys.argv[2]

db = bsddb.rnopen(dbfile)
if mode == "s":
    for k,v in db.iteritems():
        print k,v
elif mode == "kx":
    print [k for k,_ in db.iteritems()]
elif mode == "vx":
    print [v for _,v in db.iteritems()]
db.close()
