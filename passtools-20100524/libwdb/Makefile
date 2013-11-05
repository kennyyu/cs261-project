TOP=..
include $(TOP)/mk/base.mk

SRCS=wdb.c provdb.c
LIB=wdb

CFLAGS+=-I$(TOP)/include $(BDB_CFLAGS)
LIBS+=$(TOP)/libtwig/libtwig.a $(BDB_LIBS)
LINKDEPS=$(TOP)/libtwig/libtwig.a

include $(TOP)/mk/lib.mk
include $(TOP)/mk/noinstall.mk
