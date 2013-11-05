OBJS1=$(SRCS:.c=.o)
OBJS=$(OBJS1:.cc=.o)

.c.o:
	$(CC) $(CFLAGS) $(CFLAGS_C) -c $<

.cc.o:
	$(CXX) $(CFLAGS) $(CFLAGS_CXX) -c $<

depend:
	$(CC) $(CFLAGS) -MM $(SRCS) |\
	  sed 's,$(BDB_INCDIR)/[a-z]*\.h,,g' > depend.mk

include depend.mk

.PHONY: depend
