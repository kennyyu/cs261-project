LIBFILE=lib$(LIB).a

all: $(LIBFILE)

include $(TOP)/mk/compile.mk

$(LIBFILE): $(OBJS)
	$(AR) -cruv $(LIBFILE) $(OBJS)
	$(RANLIB) $(LIBFILE)

clean distclean:
	rm -f *~ *.o *.a

.PHONY: all clean distclean
