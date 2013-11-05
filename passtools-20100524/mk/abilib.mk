# like lib.mk, but creates a library with a fixed ABI
# (so internal symbols don't leak out)

LIBFILE=lib$(LIB).a
LIBOBJFILE=lib$(LIB).o

all: $(LIBFILE)

include $(TOP)/mk/compile.mk

$(LIBFILE): $(OBJS)
	$(LD) -r -o $(LIBOBJFILE) $(OBJS)
	$(OBJCOPY) --keep-global-symbols=$(ABI) $(LIBOBJFILE)
	$(AR) -cruv $(LIBFILE) $(OBJS)
	$(RANLIB) $(LIBFILE)

clean distclean:
	rm -f *~ *.o *.a

.PHONY: all clean distclean
