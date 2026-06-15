METASOURCES = AUTO
AM_CPPFLAGS = -I$(top_srcdir)/zu/src -I$(top_srcdir)/zm/src \
	-I$(top_srcdir)/zt/src -I$(top_srcdir)/ze/src -I${top_srcdir}/zi/src \
	-I$(top_srcdir)/ztls/src \
	@Z_CPPFLAGS@
AM_CXXFLAGS = @Z_CXXFLAGS@
AM_LDFLAGS = @Z_LDFLAGS@ @Z_SO_LDFLAGS@
pkginclude_HEADERS = ZquicLib.hh ZquicTypes.hh ZquicBuf.hh ZquicPQueue.hh Zquic.hh \
	ZquicPath.hh ZquicSock.hh ZquicPacket.hh \
	ZquicFrame.hh ZquicTransport.hh \
	ZquicCrypto.hh ZquicEndpoint.hh ZquicStream.hh ZquicSched.hh \
	ZquicRecovery.hh ZquicDiag.hh
noinst_HEADERS = ZquicOpenSSL.hh
lib_LTLIBRARIES = libZquic.la
libZquic_la_CPPFLAGS = $(AM_CPPFLAGS) @PTLS_CFLAGS@ -DZQUIC_EXPORTS
libZquic_la_CFLAGS = $(AM_CFLAGS) @PTLS_CFLAGS@
libZquic_la_SOURCES = ZquicLib.cc ZquicEndpoint.cc ZquicPath.cc \
	ZquicSock.cc ZquicPacket.cc ZquicFrame.cc \
	ZquicTransport.cc ZquicCrypto.cc Zquic.cc ZquicStream.cc \
	ZquicSched.cc ZquicRecovery.cc ZquicDiag.cc
libZquic_la_LIBADD = \
	$(top_builddir)/ztls/src/libZtls.la \
	$(top_builddir)/zi/src/libZi.la \
	$(top_builddir)/ze/src/libZe.la \
	$(top_builddir)/zt/src/libZt.la \
	$(top_builddir)/zm/src/libZm.la \
	$(top_builddir)/zu/src/libZu.la \
	@PTLS_LIBS@ @Z_IO_LIBS@ @Z_ZT_LIBS@ @Z_MT_LIBS@ @Z_LIBS@
