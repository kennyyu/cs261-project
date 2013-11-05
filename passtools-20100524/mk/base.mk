-include $(TOP)/defs.mk

CFLAGS=$(DEBUG) -Wall -W -Wwrite-strings
CFLAGS_C=-Wmissing-prototypes
CFLAGS_CXX=
LDFLAGS=
LIBS=

OBJCOPY?=objcopy

ROOTPREFIX?=$(PREFIX)
MOUNTSBINDIR?=/sbin
