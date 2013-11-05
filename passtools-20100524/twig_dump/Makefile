TOP=..
include $(TOP)/mk/base.mk

PROG=twig_dump
SRCS=twig_dump.c
CXXLINK=no

INST=twig_dump
INSTFLAGS=
INSTDIR=$(PREFIX)/sbin

CFLAGS+=-I$(TOP)/include
LIBS+=$(TOP)/libtwig/libtwig.a
LINKDEPS+=$(TOP)/libtwig/libtwig.a

include $(TOP)/mk/prog.mk
include $(TOP)/mk/install.mk
