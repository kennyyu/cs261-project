TOP=..
include $(TOP)/mk/base.mk

SRCS=dpapi.c libpass.c
LIB=pass

CFLAGS+=-I../include -fPIC

INST=libpass.a
INSTFLAGS=-m644
INSTDIR=$(PREFIX)/lib

include $(TOP)/mk/lib.mk
include $(TOP)/mk/install.mk
