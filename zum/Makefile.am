METASOURCES = AUTO
SUBDIRS = src test itest example interop
dist_noinst_DATA = MANAGEMENT.md

.PHONY: test
test: all
	$(MAKE) $(AM_MAKEFLAGS) -C test test
	$(MAKE) $(AM_MAKEFLAGS) -C itest test
	$(MAKE) $(AM_MAKEFLAGS) -C interop test
