METASOURCES = AUTO
AM_CPPFLAGS = -I$(top_srcdir)/zu/src -I$(top_srcdir)/zm/src \
	-I$(top_srcdir)/zt/src -I$(top_srcdir)/ze/src -I$(top_srcdir)/zf/src -I${top_srcdir}/zi/src \
	-I$(top_srcdir)/zxdp/src \
	@Z_CPPFLAGS@
AM_CXXFLAGS = @Z_CXXFLAGS@
AM_LDFLAGS = @Z_LDFLAGS@ @Z_SO_LDFLAGS@
pkginclude_HEADERS = ZutcpLib.hh ZutcpTypes.hh ZutcpBuf.hh ZutcpAlloc.hh ZutcpMx.hh ZutcpIP.hh ZutcpPQueue.hh Zutcp.hh \
	ZutcpISN.hh Zutcp_.hh \
	ZutcpConnection.hh ZutcpCliCxn.hh ZutcpSrvCxn.hh \
	ZutcpPath.hh ZutcpSock.hh ZutcpPacket.hh \
	ZutcpFrame.hh ZutcpTransport.hh \
	ZutcpConnectionUtil.hh ZutcpSched.hh ZutcpLog.hh \
	ZutcpRecovery.hh ZutcpDiag.hh
lib_LTLIBRARIES = libZutcp.la
libZutcp_la_CPPFLAGS = $(AM_CPPFLAGS) @CRYPTO_CFLAGS@ -DZUTCP_EXPORTS
libZutcp_la_CFLAGS = $(AM_CFLAGS)
libZutcp_la_SOURCES = ZutcpISN.cc ZutcpLib.cc ZutcpTypes.cc ZutcpConnection.cc \
	ZutcpPacket.cc ZutcpFrame.cc \
	ZutcpTransport.cc Zutcp.cc ZutcpAlloc.cc ZutcpMx.cc ZutcpIP.cc \
	ZutcpDiag.cc ZutcpLog.cc
libZutcp_la_LIBADD = \
	$(top_builddir)/zxdp/src/libZxdp.la \
	$(top_builddir)/zi/src/libZi.la \
	$(top_builddir)/zf/src/libZf.la $(top_builddir)/ze/src/libZe.la \
	$(top_builddir)/zt/src/libZt.la \
	$(top_builddir)/zm/src/libZm.la \
	$(top_builddir)/zu/src/libZu.la \
	@CRYPTO_LIBS@ @Z_IO_LIBS@ @Z_ZT_LIBS@ @Z_MT_LIBS@ @Z_LIBS@
