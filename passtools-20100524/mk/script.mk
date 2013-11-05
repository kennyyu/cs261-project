all: $(SCRIPT)

$(SCRIPT): $(SCRIPT).sh
	echo '#!/bin/sh' > $@
	echo 'PREFIX="$(PREFIX)"' >> $@
	echo 'ROOTPREFIX="$(ROOTPREFIX)"' >> $@
	cat $(SCRIPT).sh >> $@
	chmod a+x $@

depend: scriptdepend
scriptdepend:;

clean distclean: scriptclean
scriptclean:
	rm -f $(SCRIPT)

.PHONY: all depend scriptdepend clean distclean scriptclean
