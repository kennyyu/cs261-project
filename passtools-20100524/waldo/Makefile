TOP=..
include $(TOP)/mk/base.mk

PROG=waldo
SRCS=waldo.c process.c recover.c log.c i2n.c md5.c ptr_array.c cleanpages.cc
CXXLINK=yes

INST=waldo
INSTFLAGS=
INSTDIR=$(PREFIX)/libexec/passtools

CFLAGS+=-I$(TOP)/include $(BDB_CFLAGS)
LIBS+=$(TOP)/libtwig/libtwig.a $(TOP)/libwdb/libwdb.a $(BDB_LIBS)
LINKDEPS=$(TOP)/libtwig/libtwig.a $(TOP)/libwdb/libwdb.a

include $(TOP)/mk/prog.mk
include $(TOP)/mk/install.mk
