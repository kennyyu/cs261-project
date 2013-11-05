TOP=..
include $(TOP)/mk/base.mk

PROG=convert2xml
SRCS=convert2xml.c
CXXLINK=no

INST=convert2xml
INSTFLAGS=
INSTDIR=$(PREFIX)/sbin

CFLAGS+=-I$(TOP)/include $(BDB_CFLAGS) -I../waldo
LIBS+=$(TOP)/libwdb/libwdb.a $(BDB_LIBS)
LINKDEPS=$(TOP)/libwdb/libwdb.a

include $(TOP)/mk/prog.mk
include $(TOP)/mk/install.mk
