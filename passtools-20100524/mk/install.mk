
install:
	install -d $(DESTDIR)$(INSTDIR)
	install -c $(INSTFLAGS) $(INST) $(DESTDIR)$(INSTDIR)/
