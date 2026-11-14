METASOURCES = AUTO
AM_CPPFLAGS = -I$(top_srcdir)/zu/src -I$(top_srcdir)/zm/src \
	-I$(top_srcdir)/zt/src -I$(top_srcdir)/ze/src -I$(top_srcdir)/zf/src \
	-I$(top_srcdir)/zi/src \
	-I$(top_srcdir)/zi/test \
	-I$(top_srcdir)/zutcp/src -I$(top_srcdir)/zxdp/src \
	@Z_CPPFLAGS@
AM_CXXFLAGS = @Z_CXXFLAGS@
AM_LDFLAGS = @Z_LDFLAGS@
LDADD = $(top_builddir)/zi/test/libZiTest.la \
	$(top_builddir)/zutcp/src/libZutcp.la $(top_builddir)/zxdp/src/libZxdp.la \
	$(top_builddir)/zi/src/libZi.la \
	$(top_builddir)/zf/src/libZf.la $(top_builddir)/ze/src/libZe.la \
	$(top_builddir)/zt/src/libZt.la \
	$(top_builddir)/zm/src/libZm.la \
	$(top_builddir)/zu/src/libZu.la \
	@Z_IO_LIBS@ @Z_ZT_LIBS@ @Z_MT_LIBS@ @Z_LIBS@
noinst_PROGRAMS = ZutcpTFOTest ZutcpISNTest ZutcpOptionsTest ZutcpConnectionTest ZutcpIPTest ZutcpAPITest ZutcpSockTest \
	ZutcpHandshakeTest ZutcpRecoveryTest ZutcpRttLossTest \
	ZutcpConnectionStateTest ZutcpPMTUDTest \
	ZutcpDataTest ZutcpFlowTest ZutcpBufferTest \
	ZutcpCongestionTest ZutcpPQueueTest ZutcpLoopTest ZutcpRuntimeTest \
	ZutcpTimerTest ZutcpLogTest ZutcpRxAccountingTest ZutcpRxAdmissionTest
noinst_HEADERS = ZutcpTestAlloc.hh ZutcpConnectionFixture.hh ZutcpTestPorts.hh
ZutcpTFOTest_SOURCES = ZutcpTFOTest.cc
ZutcpISNTest_SOURCES = ZutcpISNTest.cc
ZutcpOptionsTest_SOURCES = ZutcpOptionsTest.cc
ZutcpConnectionTest_SOURCES = ZutcpConnectionTest.cc
ZutcpIPTest_SOURCES = ZutcpIPTest.cc
ZutcpAPITest_SOURCES = ZutcpAPITest.cc
ZutcpSockTest_SOURCES = ZutcpSockTest.cc
ZutcpHandshakeTest_SOURCES = ZutcpHandshakeTest.cc
ZutcpRecoveryTest_SOURCES = ZutcpRecoveryTest.cc
ZutcpRttLossTest_SOURCES = ZutcpRttLossTest.cc
ZutcpConnectionStateTest_SOURCES = ZutcpConnectionStateTest.cc
ZutcpPMTUDTest_SOURCES = ZutcpPMTUDTest.cc
ZutcpDataTest_SOURCES = ZutcpDataTest.cc
ZutcpFlowTest_SOURCES = ZutcpFlowTest.cc
ZutcpBufferTest_SOURCES = ZutcpBufferTest.cc
ZutcpCongestionTest_SOURCES = ZutcpCongestionTest.cc
ZutcpPQueueTest_SOURCES = ZutcpPQueueTest.cc
ZutcpLoopTest_SOURCES = ZutcpLoopTest.cc
ZutcpRuntimeTest_SOURCES = ZutcpRuntimeTest.cc
ZutcpTimerTest_SOURCES = ZutcpTimerTest.cc
ZutcpLogTest_SOURCES = ZutcpLogTest.cc
ZutcpRxAccountingTest_SOURCES = ZutcpRxAccountingTest.cc
ZutcpRxAdmissionTest_SOURCES = ZutcpRxAdmissionTest.cc
EXTRA_DIST = README.md

.PHONY: test
test: export ZI_LOGDIR = $(abs_top_builddir)/logs
test: $(noinst_PROGRAMS)
	prove $(noinst_PROGRAMS)
