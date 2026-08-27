METASOURCES = AUTO
EXTRA_DIST = README.md go.mod go.sum interop_test.go

.PHONY: test
test: all
	$(MAKE) $(AM_MAKEFLAGS) -C ../example zmcp zmcpd
	$(GO) test -count=1 -timeout=3m
