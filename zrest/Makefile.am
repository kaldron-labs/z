METASOURCES = AUTO
SUBDIRS = src test itest example interop

.PHONY: test
test: all
	$(MAKE) $(AM_MAKEFLAGS) -C test test
	$(MAKE) $(AM_MAKEFLAGS) -C itest test
