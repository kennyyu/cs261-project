TOP=..
include $(TOP)/mk/base.mk

SRCS=dpapi.c libpasstest.c
LIB=passtest

CFLAGS+=-I../include

INST=libpasstest.a
INSTFLAGS=-m644
INSTDIR=$(PREFIX)/lib

include $(TOP)/mk/lib.mk
include $(TOP)/mk/install.mk
