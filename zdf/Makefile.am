METASOURCES = AUTO
SUBDIRS = src# test itest

.PHONY: test
test: all
	$(MAKE) $(AM_MAKEFLAGS) -C test
	$(MAKE) $(AM_MAKEFLAGS) -C itest
