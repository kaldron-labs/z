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
	ZiIOBufTest ZiEventLoopTest \
	ZiDirTest ZiIPTest ZiModuleTest ZiPlatformTest ZiIOContextTest ZiCSVTest \
	ZiRxTest ZiTxTest ZiTxStreamTest ZiRxStreamTest ZiLogTest ZiMxLoopTest \
	ZiDaemonTest
noinst_HEADERS = ZiTestResidue.hh
.PHONY: test
test: $(noinst_PROGRAMS)
	prove $(noinst_PROGRAMS)
ZiFileTest_SOURCES = ZiFileTest.cc
ZiFileAgeTest_SOURCES = ZiFileAgeTest.cc
ZiGlobTest_SOURCES = ZiGlobTest.cc
ZiRingTest_SOURCES = ZiRingTest.cc
ZiRingTest2_SOURCES = ZiRingTest2.cc
ZiIOBufTest_SOURCES = ZiIOBufTest.cc
ZiEventLoopTest_SOURCES = ZiEventLoopTest.cc
ZiDirTest_SOURCES = ZiDirTest.cc
ZiIPTest_SOURCES = ZiIPTest.cc
ZiModuleTest_SOURCES = ZiModuleTest.cc
ZiPlatformTest_SOURCES = ZiPlatformTest.cc
ZiIOContextTest_SOURCES = ZiIOContextTest.cc
ZiCSVTest_SOURCES = ZiCSVTest.cc
ZiRxTest_SOURCES = ZiRxTest.cc
ZiTxTest_SOURCES = ZiTxTest.cc
ZiTxStreamTest_SOURCES = ZiTxStreamTest.cc
ZiRxStreamTest_SOURCES = ZiRxStreamTest.cc
ZiLogTest_SOURCES = ZiLogTest.cc
ZiMxLoopTest_SOURCES = ZiMxLoopTest.cc
ZiDaemonTest_SOURCES = ZiDaemonTest.cc
if NETLINK
noinst_PROGRAMS += ZiNetlinkMsgTest
ZiNetlinkMsgTest_SOURCES = ZiNetlinkMsgTest.cc
endif
