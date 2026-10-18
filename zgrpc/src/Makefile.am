METASOURCES = AUTO
AM_CPPFLAGS = -I$(top_srcdir)/zu/src -I$(top_srcdir)/zm/src \
	-I$(top_srcdir)/zt/src -I$(top_srcdir)/ze/src \
	-I$(top_srcdir)/zf/src -I$(top_srcdir)/zi/src \
	-I$(top_srcdir)/ztcp/src -I$(top_srcdir)/ztls/src \
	-I$(top_srcdir)/zquic/src -I$(top_srcdir)/zhttp/src \
	-I$(top_srcdir)/zpb/src \
	@Z_CPPFLAGS@
AM_CXXFLAGS = @Z_CXXFLAGS@
AM_LDFLAGS = @Z_LDFLAGS@
pkginclude_HEADERS = ZgrpcLib.hh Zgrpc.hh ZgrpcFrame.hh ZgrpcMeta.hh ZgrpcCall.hh ZgrpcIO.hh ZgrpcHTTP.hh ZgrpcMulti.hh ZgrpcEndpoint.hh ZgrpcClient.hh ZgrpcServer.hh
lib_LTLIBRARIES = libZgrpc.la
libZgrpc_la_CPPFLAGS = $(AM_CPPFLAGS) -DZGRPC_EXPORTS
libZgrpc_la_LDFLAGS = @Z_SO_LDFLAGS@
libZgrpc_la_SOURCES = ZgrpcLib.cc Zgrpc.cc ZgrpcFrame.cc ZgrpcMeta.cc ZgrpcCall.cc ZgrpcIO.cc ZgrpcEndpoint.cc
libZgrpc_la_LIBADD = \
	$(top_builddir)/zpb/src/libZpb.la \
	$(top_builddir)/zhttp/src/libZhttp.la $(top_builddir)/zquic/src/libZquic.la \
	$(top_builddir)/ztls/src/libZtls.la $(top_builddir)/ztcp/src/libZtcp.la \
	$(top_builddir)/zi/src/libZi.la $(top_builddir)/zf/src/libZf.la \
	$(top_builddir)/ze/src/libZe.la $(top_builddir)/zt/src/libZt.la \
	$(top_builddir)/zm/src/libZm.la $(top_builddir)/zu/src/libZu.la \
	@PTLS_LIBS@ @Z_IO_LIBS@ @Z_ZT_LIBS@ @Z_MT_LIBS@ @Z_LIBS@

pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = Zgrpc.pc
EXTRA_DIST = Zgrpc.pc.in
