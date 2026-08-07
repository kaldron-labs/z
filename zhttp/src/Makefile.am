METASOURCES = AUTO
.cc.s:
	$(CXXCOMPILE) -c -S $<
%.i.cc: %.cc
	$(CXXCOMPILE) -C -E $< -o $@
AM_CPPFLAGS = -I$(top_srcdir)/zu/src -I$(top_srcdir)/zm/src \
	-I$(top_srcdir)/zt/src -I$(top_srcdir)/ze/src -I$(top_srcdir)/zf/src -I${top_srcdir}/zi/src \
	-I$(top_srcdir)/ztcp/src -I$(top_srcdir)/ztls/src \
	-I$(top_srcdir)/zquic/src \
	@Z_CPPFLAGS@
AM_CXXFLAGS = @Z_CXXFLAGS@
AM_LDFLAGS = @Z_LDFLAGS@ @Z_SO_LDFLAGS@
pkginclude_HEADERS = ZhttpLib.hh ZhttpCore.hh ZhttpURL.hh ZhttpFields.hh \
	ZhttpCompression.hh ZhttpHPack.hh ZhttpQPack.hh \
	ZhttpH1.hh ZhttpH2.hh ZhttpH2Hub.hh ZhttpH3.hh \
	ZhttpTransport.hh ZhttpDiscovery.hh Zhttp.hh \
	ZhttpClient.hh ZhttpServer.hh
lib_LTLIBRARIES = libZhttp.la
libZhttp_la_CPPFLAGS = $(AM_CPPFLAGS) -DZHTTP_EXPORTS 
libZhttp_la_SOURCES = ZhttpLib.cc ZhttpCore.cc ZhttpURL.cc \
	ZhttpAltSvc.cc ZhttpDiscovery.cc ZhttpTransport.cc \
	ZhttpCompression.cc ZhttpFields.cc \
	ZhttpH1.cc ZhttpH2.cc ZhttpH3.cc ZhttpHPack.cc ZhttpQPack.cc \
	ZhttpClient.cc ZhttpServer.cc
libZhttp_la_LIBADD = \
	$(top_builddir)/ztcp/src/libZtcp.la \
	$(top_builddir)/ztls/src/libZtls.la \
	$(top_builddir)/zquic/src/libZquic.la \
	$(top_builddir)/zi/src/libZi.la \
	$(top_builddir)/zf/src/libZf.la $(top_builddir)/ze/src/libZe.la \
	$(top_builddir)/zt/src/libZt.la \
	$(top_builddir)/zm/src/libZm.la \
	$(top_builddir)/zu/src/libZu.la \
	@PTLS_LIBS@ @Z_IO_LIBS@ @Z_ZT_LIBS@ @Z_MT_LIBS@ @Z_LIBS@
