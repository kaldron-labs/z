METASOURCES = AUTO
AM_CPPFLAGS = -I$(top_srcdir)/zu/src -I$(top_srcdir)/zm/src \
	-I$(top_srcdir)/zt/src -I$(top_srcdir)/ze/src -I$(top_srcdir)/zf/src -I${top_srcdir}/zi/src \
	-I$(top_srcdir)/zpkt/src \
	@Z_CPPFLAGS@
AM_CXXFLAGS = @Z_CXXFLAGS@
AM_LDFLAGS = @Z_LDFLAGS@ @Z_SO_LDFLAGS@
pkginclude_HEADERS = ZutcpLib.hh Zutcp.hh ZutcpAlloc.hh ZutcpChecksum.hh ZutcpIP.hh \
	ZutcpISN.hh ZutcpOptions.hh ZutcpTFO.hh ZutcpQueue.hh
noinst_HEADERS = ZutcpTypes.hh ZutcpBuf.hh ZutcpPQueue.hh Zutcp_.hh \
	ZutcpCxn.hh ZutcpCliCxn.hh ZutcpSrvCxn.hh ZutcpPath.hh ZutcpSock.hh \
	ZutcpConnectionUtil.hh ZutcpSched.hh ZutcpRecovery.hh ZutcpDiag.hh ZutcpUDP.hh
lib_LTLIBRARIES = libZutcp.la
libZutcp_la_CPPFLAGS = $(AM_CPPFLAGS) @CRYPTO_CFLAGS@ -DZUTCP_EXPORTS
libZutcp_la_CFLAGS = $(AM_CFLAGS)
libZutcp_la_SOURCES = ZutcpISN.cc ZutcpOptions.cc ZutcpTFO.cc ZutcpLib.cc ZutcpTypes.cc ZutcpCxn.cc \
	ZutcpAlloc.cc ZutcpChecksum.cc Zutcp.cc ZutcpIP.cc ZutcpUDP.cc \
	ZutcpDiag.cc
libZutcp_la_LIBADD = \
	$(top_builddir)/zpkt/src/libZpkt.la \
	$(top_builddir)/zi/src/libZi.la \
	$(top_builddir)/zf/src/libZf.la $(top_builddir)/ze/src/libZe.la \
	$(top_builddir)/zt/src/libZt.la \
	$(top_builddir)/zm/src/libZm.la \
	$(top_builddir)/zu/src/libZu.la \
	@CRYPTO_LIBS@ @Z_IO_LIBS@ @Z_ZT_LIBS@ @Z_MT_LIBS@ @Z_LIBS@
