METASOURCES = AUTO
AM_CPPFLAGS = -I$(top_srcdir)/zu/src -I$(top_srcdir)/zm/src \
	-I$(top_srcdir)/zt/src -I$(top_srcdir)/ze/src -I$(top_srcdir)/zf/src -I${top_srcdir}/zi/src \
	-I$(top_srcdir)/ztcp/src -I$(top_srcdir)/ztls/src \
	-I$(top_srcdir)/zquic/src -I$(top_srcdir)/zhttp/src \
	@Z_CPPFLAGS@
AM_CXXFLAGS = @Z_CXXFLAGS@
AM_LDFLAGS = @Z_LDFLAGS@ @Z_SO_LDFLAGS@
pkginclude_HEADERS = ZwsLib.hh Zws.hh ZwsProtocol.hh ZwsURI.hh \
	ZwsHandshake.hh ZwsTx.hh ZwsCodec.hh \
	ZwsH1.hh ZwsH1Hub.hh ZwsExtended.hh ZwsHub.hh
lib_LTLIBRARIES = libZws.la
libZws_la_CPPFLAGS = $(AM_CPPFLAGS) -DZWS_EXPORTS 
libZws_la_CXXFLAGS = $(AM_CXXFLAGS) @Z_VISIBILITY_CXXFLAGS@
libZws_la_SOURCES = ZwsLib.cc ZwsProtocol.cc ZwsURI.cc ZwsHandshake.cc
libZws_la_LIBADD = \
	$(top_builddir)/zhttp/src/libZhttp.la \
	$(top_builddir)/zquic/src/libZquic.la \
	$(top_builddir)/ztcp/src/libZtcp.la \
	$(top_builddir)/ztls/src/libZtls.la \
	$(top_builddir)/zi/src/libZi.la \
	$(top_builddir)/zf/src/libZf.la $(top_builddir)/ze/src/libZe.la \
	$(top_builddir)/zt/src/libZt.la \
	$(top_builddir)/zm/src/libZm.la \
	$(top_builddir)/zu/src/libZu.la \
	@PTLS_LIBS@ @Z_IO_LIBS@ @Z_ZT_LIBS@ @Z_MT_LIBS@ @Z_LIBS@
bin_PROGRAMS = zws zwsd
zws_LDADD = libZws.la $(libZws_la_LDADD)
zws_SOURCES = zws.cc
zwsd_LDADD = libZws.la $(libZws_la_LDADD)
zwsd_SOURCES = zwsd.cc
