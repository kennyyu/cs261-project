
all depend install clean distclean:
	@for S in $(SUBDIRS); do \
		echo "cd $$S && $(MAKE) $@"; \
		(cd $$S && $(MAKE) $@) || exit 1; \
	 done

.PHONY: all depend install clean distclean
