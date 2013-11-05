TOP=..
include $(TOP)/mk/base.mk

PROG=wdb_s3dump
SRCS=wdb_s3dump.c
CXXLINK=no

INST=wdb_s3dump
INSTFLAGS=
INSTDIR=$(PREFIX)/sbin

CFLAGS+=-I$(TOP)/include $(BDB_CFLAGS)
LIBS+=$(TOP)/libtwig/libtwig.a $(TOP)/libwdb/libwdb.a $(BDB_LIBS)
LINKDEPS=$(TOP)/libtwig/libtwig.a $(TOP)/libwdb/libwdb.a

include $(TOP)/mk/prog.mk
include $(TOP)/mk/install.mk
