TOP=..
include $(TOP)/mk/base.mk

SRCS=libpass-java.c
PROG=libpass-java.so
CXXLINK=no

CFLAGS+=-I../include -I../libpass -I$(JAVA_HOME)/include -I$(JAVA_HOME)/include/linux -fPIC
LDFLAGS+=-shared -Wl,-soname,libpass-java.so.1 $(TOP)/libpass/libpass.o $(TOP)/libpass/dpapi.o

INST=libpass-java.so
INSTFLAGS=-m644
INSTDIR=$(PREFIX)/lib

include $(TOP)/mk/prog.mk
include $(TOP)/mk/install.mk

org/harvard/pass/DPAPI.class : org/harvard/pass/DPAPI.java
	javac org/harvard/pass/DPAPI.java

org_harvard_pass_DPAPI.h : org/harvard/pass/DPAPI.class
	javah -jni org.harvard.pass.DPAPI

clean distclean:
	rm -rf org/harvard/pass/DPAPI.class org_harvard_pass_DPAPI.h *.so *.o *.a *~
