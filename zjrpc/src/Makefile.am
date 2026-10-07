METASOURCES = AUTO
AM_CPPFLAGS = -I$(top_srcdir)/zu/src -I$(top_srcdir)/zm/src \
	-I$(top_srcdir)/zt/src -I$(top_srcdir)/ze/src \
	-I$(top_srcdir)/zf/src -I$(top_srcdir)/zi/src \
	-I$(top_srcdir)/ztcp/src -I$(top_srcdir)/ztls/src \
	-I$(top_srcdir)/zquic/src -I$(top_srcdir)/zhttp/src \
	-I$(top_srcdir)/zws/src \
	@Z_CPPFLAGS@
AM_CXXFLAGS = @Z_CXXFLAGS@
AM_LDFLAGS = @Z_LDFLAGS@ @Z_SO_LDFLAGS@
pkginclude_HEADERS = ZjrpcLib.hh Zjrpc.hh ZjrpcPending.hh \
	ZjrpcCompletion.hh ZjrpcInbound.hh ZjrpcIO.hh ZjrpcStdio.hh ZjrpcHTTP.hh \
	ZjrpcDispatch.hh ZjrpcWS.hh ZjrpcClient.hh ZjrpcServer.hh
lib_LTLIBRARIES = libZjrpc.la
libZjrpc_la_CPPFLAGS = $(AM_CPPFLAGS) -DZJRPC_EXPORTS
libZjrpc_la_SOURCES = ZjrpcLib.cc
libZjrpc_la_LIBADD = \
	$(top_builddir)/zws/src/libZws.la \
	$(top_builddir)/zhttp/src/libZhttp.la $(top_builddir)/zquic/src/libZquic.la \
	$(top_builddir)/ztls/src/libZtls.la $(top_builddir)/ztcp/src/libZtcp.la \
	$(top_builddir)/zi/src/libZi.la $(top_builddir)/zf/src/libZf.la \
	$(top_builddir)/ze/src/libZe.la $(top_builddir)/zt/src/libZt.la \
	$(top_builddir)/zm/src/libZm.la $(top_builddir)/zu/src/libZu.la \
	@PTLS_LIBS@ @Z_IO_LIBS@ @Z_ZT_LIBS@ @Z_MT_LIBS@ @Z_LIBS@
