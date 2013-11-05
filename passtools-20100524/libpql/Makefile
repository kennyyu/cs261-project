TOP=..
include $(TOP)/mk/base.mk

LIB=pql
ABI=abi.txt
SRCS=	pqlcontext.c pqlquery.c \
	pttree.c ptlex.c ptparse.c \
	   resolvevars.c normalize.c unify.c movepaths.c bindnil.c \
	   dequantify.c \
	tcalc.c tuplify.c \
	   typeinf.c typecheck.c \
	   norenames.c baseopt.c stepjoins.c \
	eval.c \
	functions.c pqlvalue.c valuerep.c columns.c datatype.c \
	layout.c \
	array.c memdefs.c utils.c

AGCL?=agcl

CFLAGS+=-I$(TOP)/include

include $(TOP)/mk/abilib.mk
include $(TOP)/mk/noinstall.mk

ptparse.c ptpcb.h: ptparse.syn
	$(AGCL) ptparse.syn

agclean:
	rm -f ptparse.c ptpcb.h

.PHONY: agclean
