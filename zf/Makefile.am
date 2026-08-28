METASOURCES = AUTO
SUBDIRS = src test interop
DIST_SUBDIRS = src test interop

.PHONY: test
test: export ZI_LOGDIR = $(abs_top_builddir)/logs
test: all
	$(MAKE) $(AM_MAKEFLAGS) -C test test
	$(MAKE) $(AM_MAKEFLAGS) -C interop test
