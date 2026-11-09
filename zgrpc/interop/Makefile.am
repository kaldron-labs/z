METASOURCES = AUTO
AM_CPPFLAGS = -I$(top_srcdir)/zu/src -I$(top_srcdir)/zm/src \
	-I$(top_srcdir)/zt/src -I$(top_srcdir)/ze/src \
	-I$(top_srcdir)/zf/src -I$(top_srcdir)/zi/src \
	-I$(top_srcdir)/ztcp/src -I$(top_srcdir)/ztls/src \
	-I$(top_srcdir)/zquic/src -I$(top_srcdir)/zhttp/src \
	-I$(top_srcdir)/zpb/src -I$(top_srcdir)/zgrpc/src \
	@Z_CPPFLAGS@
AM_CXXFLAGS = @Z_CXXFLAGS@
AM_LDFLAGS = @Z_LDFLAGS@
LDADD = $(top_builddir)/zgrpc/src/libZgrpc.la \
	$(top_builddir)/zpb/src/libZpb.la \
	$(top_builddir)/zhttp/src/libZhttp.la $(top_builddir)/zquic/src/libZquic.la \
	$(top_builddir)/ztls/src/libZtls.la $(top_builddir)/ztcp/src/libZtcp.la \
	$(top_builddir)/zi/src/libZi.la $(top_builddir)/zf/src/libZf.la \
	$(top_builddir)/ze/src/libZe.la $(top_builddir)/zt/src/libZt.la \
	$(top_builddir)/zm/src/libZm.la $(top_builddir)/zu/src/libZu.la \
	@PTLS_LIBS@ @Z_IO_LIBS@ @Z_ZT_LIBS@ @Z_MT_LIBS@ @Z_LIBS@
noinst_PROGRAMS = zgrpcgoclient zgrpcgoserver
noinst_HEADERS = zgrpcmatrix.hh
zgrpcgoserver_SOURCES = zgrpcgoserver.cc
zgrpcgoclient_SOURCES = zgrpcgoclient.cc

EXTRA_DIST = run-go.py README.md zgrpc.proto go/Makefile go/go.mod go/go.sum go/h3/main.go go/h3/main_test.go \
	go/main.go go/peer.go go/peer_test.go \
	go/pb/zgrpc.pb.go go/pb/zgrpc_grpc.pb.go
EXTRA_DIST += run-dotnet.py dotnet/Makefile dotnet/Peer.csproj \
	dotnet/Program.cs dotnet/packages.lock.json dotnet/.gitignore

# Canonical gRPC/protobuf dependencies are confined to explicit interop targets.
.PHONY: go-peer go-test peer-test peer-race test
if HAVE_GO
go-peer:
	$(MAKE) -C $(srcdir)/go GO="$(GO)" EXEEXT="$(EXEEXT)" all
go-test: all go-peer
	python3 $(srcdir)/run-go.py --peer=$(srcdir)/go/zgrpcpeer$(EXEEXT) --client=./zgrpcgoclient$(EXEEXT) --server=./zgrpcgoserver$(EXEEXT)
	python3 $(srcdir)/run-go.py --h3 --peer=$(srcdir)/go/zgrpch3peer$(EXEEXT) --client=./zgrpcgoclient$(EXEEXT) --server=./zgrpcgoserver$(EXEEXT)
peer-test: go-peer
	$(MAKE) -C $(srcdir)/go GO="$(GO)" test
peer-race: go-peer
	$(MAKE) -C $(srcdir)/go GO="$(GO)" race
clean-local:
	$(MAKE) -C $(srcdir)/go clean
else
go-peer go-test peer-test peer-race:
	@echo "Go is required for the explicit gRPC interop peer" >&2
	@exit 1
endif

# grpc-dotnet supplies an independent canonical HTTP/3 stack in both roles.
.PHONY: dotnet-peer dotnet-peer-test dotnet-test
dotnet-peer:
	$(MAKE) -C $(srcdir)/dotnet all
dotnet-peer-test:
	$(MAKE) -C $(srcdir)/dotnet test
dotnet-test: all dotnet-peer
	python3 $(srcdir)/run-dotnet.py --peer=$(srcdir)/dotnet/bin/Release/net10.0/Peer --client=./zgrpcgoclient$(EXEEXT) --server=./zgrpcgoserver$(EXEEXT)
test: go-test dotnet-test
