METASOURCES = AUTO
AM_CPPFLAGS = -I$(top_srcdir)/zu/src -I$(top_srcdir)/zm/src \
	-I$(top_srcdir)/zt/src -I$(top_srcdir)/ze/src -I$(top_srcdir)/zi/src \
	@Z_CPPFLAGS@
AM_CXXFLAGS = @Z_CXXFLAGS@
AM_LDFLAGS = @Z_LDFLAGS@
LDADD = $(top_builddir)/zi/src/libZi.la $(top_builddir)/ze/src/libZe.la \
	$(top_builddir)/zt/src/libZt.la $(top_builddir)/zm/src/libZm.la \
	$(top_builddir)/zu/src/libZu.la \
	@Z_IO_LIBS@ @Z_ZT_LIBS@ @Z_MT_LIBS@ @Z_LIBS@
noinst_PROGRAMS = \
	ZiFileTest ZiFileAgeTest ZiGlobTest \
	ZiRingTest ZiRingTest2 \
	ZiMxClient ZiMxServer ZiMxUDPClient ZiMxUDPServer \
	ZiIOBufTest
noinst_HEADERS = Global.hh HttpHeader.hh
if NETLINK
noinst_PROGRAMS += ZiNetlinkTest
endif
ZiFileTest_SOURCES = ZiFileTest.cc
ZiFileAgeTest_SOURCES = ZiFileAgeTest.cc
ZiGlobTest_SOURCES = ZiGlobTest.cc
ZiRingTest_SOURCES = ZiRingTest.cc
ZiRingTest2_SOURCES = ZiRingTest2.cc
ZiMxClient_SOURCES = ZiMxClient.cc
ZiMxServer_SOURCES = ZiMxServer.cc
ZiMxUDPClient_SOURCES = ZiMxUDPClient.cc
ZiMxUDPServer_SOURCES = ZiMxUDPServer.cc
ZiIOBufTest_SOURCES = ZiIOBufTest.cc
if NETLINK
ZiNetlinkTest_SOURCES = ZiNetlinkTest.cc
endif
