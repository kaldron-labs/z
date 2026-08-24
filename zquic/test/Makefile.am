METASOURCES = AUTO
AM_CPPFLAGS = -I$(top_srcdir)/zu/src -I$(top_srcdir)/zm/src \
	-I$(top_srcdir)/zt/src -I$(top_srcdir)/ze/src -I$(top_srcdir)/zf/src \
	-I$(top_srcdir)/zi/src -I$(top_srcdir)/ztls/src \
	-I$(top_srcdir)/zquic/src \
	@Z_CPPFLAGS@ @PTLS_CFLAGS@
AM_CXXFLAGS = @Z_CXXFLAGS@
AM_LDFLAGS = @Z_LDFLAGS@
LDADD = $(top_builddir)/zquic/src/libZquic.la \
	$(top_builddir)/ztls/src/libZtls.la \
	$(top_builddir)/zi/src/libZi.la \
	$(top_builddir)/zf/src/libZf.la $(top_builddir)/ze/src/libZe.la \
	$(top_builddir)/zt/src/libZt.la \
	$(top_builddir)/zm/src/libZm.la \
	$(top_builddir)/zu/src/libZu.la \
	@PTLS_LIBS@ @Z_IO_LIBS@ @Z_ZT_LIBS@ @Z_MT_LIBS@ @Z_LIBS@
noinst_PROGRAMS = ZquicAPITest ZquicSockTest \
	ZquicCodecTest ZquicCryptoTest ZquicPacketProtectionTest \
	ZquicHandshakeTest ZquicRecoveryTest \
	ZquicStreamStateTest ZquicPMTUDTest ZquicVersionTest ZquicCIDTest \
	ZquicStreamTest ZquicFlowTest ZquicBufferTest \
	ZquicCongestionTest ZquicPQueueTest ZquicLoopTest ZquicRuntimeTest \
	ZquicTimerTest ZquicLogTest
noinst_HEADERS = ZquicInteropTest.hh ZquicTestPorts.hh
ZquicAPITest_SOURCES = ZquicAPITest.cc
ZquicSockTest_SOURCES = ZquicSockTest.cc
ZquicCodecTest_SOURCES = ZquicCodecTest.cc
ZquicCryptoTest_SOURCES = ZquicCryptoTest.cc
ZquicPacketProtectionTest_SOURCES = ZquicPacketProtectionTest.cc
ZquicHandshakeTest_SOURCES = ZquicHandshakeTest.cc
ZquicRecoveryTest_SOURCES = ZquicRecoveryTest.cc
ZquicStreamStateTest_SOURCES = ZquicStreamStateTest.cc
ZquicPMTUDTest_SOURCES = ZquicPMTUDTest.cc
ZquicVersionTest_SOURCES = ZquicVersionTest.cc
ZquicCIDTest_SOURCES = ZquicCIDTest.cc
ZquicStreamTest_SOURCES = ZquicStreamTest.cc
ZquicFlowTest_SOURCES = ZquicFlowTest.cc
ZquicBufferTest_SOURCES = ZquicBufferTest.cc
ZquicCongestionTest_SOURCES = ZquicCongestionTest.cc
ZquicPQueueTest_SOURCES = ZquicPQueueTest.cc
ZquicLoopTest_SOURCES = ZquicLoopTest.cc
ZquicRuntimeTest_SOURCES = ZquicRuntimeTest.cc
ZquicTimerTest_SOURCES = ZquicTimerTest.cc
ZquicLogTest_SOURCES = ZquicLogTest.cc

.PHONY: test
test: $(noinst_PROGRAMS)
	prove $(noinst_PROGRAMS)
