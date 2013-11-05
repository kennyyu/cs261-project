TOP=..
include $(TOP)/mk/base.mk

PROG=osage
SRCS=main.cc parse.cc ptnode.cc ast.cc \
	baseopt.cc indexify.cc \
	output.cc eval.cc path.cc dbops.cc builtins.cc value.cc \
	operators.cc dump.cc utils.cc
CXXLINK=yes

INST=osage
INSTFLAGS=
INSTDIR=$(PREFIX)/bin

AGCL?=agcl

CFLAGS+=-I$(TOP)/include $(BDB_CFLAGS)
LIBS=$(TOP)/libwdb/libwdb.a $(BDB_LIBS)
LINKDEPS=$(TOP)/libwdb/libwdb.a

include $(TOP)/mk/prog.mk
include $(TOP)/mk/install.mk

parse.cc parse.h: parse.syn
	$(AGCL) parse.syn

agclean:
	rm -f parse.cc parse.h

.PHONY: agclean
