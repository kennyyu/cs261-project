LDCC.yes=$(CXX)
LDCC.no=$(CC)
LDCC.=false "(try setting CXXLINK correctly")
LDCC=$(LDCC.$(CXXLINK))

all: $(PROG)

include $(TOP)/mk/compile.mk

$(PROG): $(OBJS) $(LINKDEPS)
	$(LDCC) $(LDFLAGS) $(OBJS) $(LIBS) -o $(PROG)

clean distclean:
	rm -f *~ *.o $(PROG)

.PHONY: all clean distclean
