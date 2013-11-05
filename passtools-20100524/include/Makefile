TOP=..
include $(TOP)/mk/base.mk

all depend clean distclean:;

install:
	install -d $(DESTDIR)$(PREFIX)/include
	install -d $(DESTDIR)$(PREFIX)/include/pass
	install -c -m644 pass/dpapi.h $(DESTDIR)$(PREFIX)/include/pass/
	install -c -m644 pass/provabi.h $(DESTDIR)$(PREFIX)/include/pass/

.PHONY: all depend install clean distclean
