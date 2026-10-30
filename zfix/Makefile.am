METASOURCES = AUTO
SUBDIRS = src test util itest example interop bench
EXTRA_DIST = README.md CONTRACTS.md STATUS.md ENDPOINTS.md AUDIT.md STATE_AUDIT.md ENUM_AUDIT.md

.PHONY: test
test: all
	$(MAKE) $(AM_MAKEFLAGS) -C test test
	$(MAKE) $(AM_MAKEFLAGS) -C itest test
