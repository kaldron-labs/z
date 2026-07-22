METASOURCES = AUTO
AM_CPPFLAGS = -I$(top_srcdir)/zu/src -I$(top_srcdir)/zm/src \
	-I$(top_srcdir)/zt/src -I$(top_srcdir)/ze/src -I$(top_srcdir)/zf/src -I$(top_srcdir)/zi/src \
	@Z_CPPFLAGS@
AM_CXXFLAGS = @Z_CXXFLAGS@
AM_LDFLAGS = @Z_LDFLAGS@
LDADD = $(top_builddir)/zi/src/libZi.la $(top_builddir)/zf/src/libZf.la $(top_builddir)/ze/src/libZe.la \
	$(top_builddir)/zt/src/libZt.la $(top_builddir)/zm/src/libZm.la \
	$(top_builddir)/zu/src/libZu.la \
	@Z_IO_LIBS@ @Z_ZT_LIBS@ @Z_MT_LIBS@ @Z_LIBS@
noinst_PROGRAMS = \
	ZiMxClient ZiMxServer ZiMxUDPClient ZiMxUDPServer \
	ZiLogTest
noinst_HEADERS = Global.hh HttpHeader.hh
ZiMxClient_SOURCES = ZiMxClient.cc
ZiMxServer_SOURCES = ZiMxServer.cc
ZiMxUDPClient_SOURCES = ZiMxUDPClient.cc
ZiMxUDPServer_SOURCES = ZiMxUDPServer.cc
ZiLogTest_SOURCES = ZiLogTest.cc
