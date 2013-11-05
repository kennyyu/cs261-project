TOP=..
include $(TOP)/mk/base.mk

LIBVERSION=0.20080506.0
SRCS=libpass-python.c
PROG=libpass-python.so
CXXLINK=no

CFLAGS+=-I../include -I../libpass -fPIC
LDFLAGS+=-shared -Wl,-soname,libpass-python.so.$(LIBVERSION) $(TOP)/libpass/libpass.o $(TOP)/libpass/dpapi.o

INST=libpass-python.so.$(LIBVERSION)
INSTFLAGS=-m644
INSTDIR=$(PREFIX)/lib

include $(TOP)/mk/prog.mk
include $(TOP)/mk/install.mk

all: libpass-python.so.$(LIBVERSION)
libpass-python.so.$(LIBVERSION): libpass-python.so
	rm -f $@
	ln libpass-python.so $@

clean: cleanit
cleanit:
	rm -f libpass-python.so.$(LIBVERSION)

.PHONY: all clean cleanit
