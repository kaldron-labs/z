METASOURCES = AUTO
SUBDIRS = src test itest

.PHONY: test
test: export ZI_LOGDIR = $(abs_top_builddir)/logs
test: all
	$(MAKE) $(AM_MAKEFLAGS) -C test test
	$(MAKE) $(AM_MAKEFLAGS) -C itest test
