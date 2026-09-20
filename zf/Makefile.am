METASOURCES = AUTO
SUBDIRS = src test
DIST_SUBDIRS = src test

.PHONY: test
test: export ZI_LOGDIR = $(abs_top_builddir)/logs
test: all
	$(MAKE) $(AM_MAKEFLAGS) -C test test
