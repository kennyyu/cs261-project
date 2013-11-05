import sys

from ctypes import *
lp = CDLL("/mnt/lasagna/libpass-python.so")

lp.pydpapi_init()

def mkphony(pd):
    assert (type(pd) == type(0)) or (str(type(pd)).find("provenance.wrapper") >= 0)
    return lp.mkphony(pd)

def addxref(pd,key,xref_pd):
#    assert type(pd) == type(0)
#    assert type(xref_pd) == type(0)
    assert type(key) == type('')
#    assert retlen > 0
#    sys.stdout.write("addxref calling lp w/key %s\n" % key)
    retlen = lp.addxref(c_int(pd), key, c_int(xref_pd))
#    sys.stdout.write("addxref called lp ret %d\n" % retlen)
    return retlen

def addstr(pd, key, value):
#    assert type(pd) == type(0)    
    assert type(key) == type('')
    assert type(value) == type('')    
    retlen = lp.addstr(c_int(pd), key, value)
#    assert retlen > 0
    return retlen

    
