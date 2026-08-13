METASOURCES = AUTO
EXTRA_DIST = go/Makefile go/cert.conf go/client.go go/server.go \
	go/go.mod go/go.sum

if HAVE_GO
all-local:
	$(MAKE) -C $(srcdir)/go GO="$(GO)" all

clean-local:
	$(MAKE) -C $(srcdir)/go GO="$(GO)" clean
endif
