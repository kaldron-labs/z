METASOURCES = AUTO
SUBDIRS = src test util itest interop

.PHONY: test
test: all
	$(MAKE) $(AM_MAKEFLAGS) -C test test
	$(MAKE) $(AM_MAKEFLAGS) -C itest test
	$(MAKE) $(AM_MAKEFLAGS) -C interop test
