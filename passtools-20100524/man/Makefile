TOP=..
include $(TOP)/mk/base.mk

all depend clean distclean:;

install:
	install -d $(DESTDIR)$(PREFIX)/man/man1
	install -d $(DESTDIR)$(PREFIX)/man/man5
	install -d $(DESTDIR)$(PREFIX)/man/man8
	install -c -m644 *.1 $(DESTDIR)$(PREFIX)/man/man1/
	install -c -m644 *.5 $(DESTDIR)$(PREFIX)/man/man5/
	install -c -m644 *.8 $(DESTDIR)$(PREFIX)/man/man8/
