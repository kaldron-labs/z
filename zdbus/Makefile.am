METASOURCES = AUTO
if LINUX
SUBDIRS = src test util itest example interop
else
SUBDIRS =
endif
DIST_SUBDIRS = src test util itest example interop

.PHONY: test
test: all
if LINUX
	$(MAKE) $(AM_MAKEFLAGS) -C test test
	$(MAKE) $(AM_MAKEFLAGS) -C itest test
	$(MAKE) $(AM_MAKEFLAGS) -C interop test
endif
