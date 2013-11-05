TOP=..
include $(TOP)/mk/base.mk

PROG=sage
SRCS=			main.cc 			\
			interact.cc			\
	user.cc 			server.cc	\
			query.cc			\
	local.cc			remote.cc	\
	backend.cc					\
			result.cc			\
			socketpath.cc			\
			utils.cc
CXXLINK=yes

INST=sage
INSTFLAGS=
INSTDIR=$(PREFIX)/bin

CFLAGS+=-I$(TOP)/include

LIBS+=$(TOP)/libpql/libpql.a
LINKDEPS+=$(TOP)/libpql/libpql.a

LIBS+=$(TOP)/libpqlutil/libpqlutil.a
LINKDEPS+=$(TOP)/libpqlutil/libpqlutil.a

LIBS+=$(TOP)/libwdb/libwdb.a
LINKDEPS+=$(TOP)/libwdb/libwdb.a

CFLAGS+=$(BDB_CFLAGS)
LIBS+=$(BDB_LIBS)

include $(TOP)/mk/prog.mk
include $(TOP)/mk/install.mk
