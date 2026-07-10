METASOURCES = AUTO
SUBDIRS = src test interop

.PHONY: test
test: all
	$(MAKE) $(AM_MAKEFLAGS) -C test test
	$(MAKE) $(AM_MAKEFLAGS) -C interop test
