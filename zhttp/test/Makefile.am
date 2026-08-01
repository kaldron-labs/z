METASOURCES = AUTO
AM_CPPFLAGS = -I$(top_srcdir)/zu/src -I$(top_srcdir)/zm/src \
	-I$(top_srcdir)/zt/src -I$(top_srcdir)/ze/src -I$(top_srcdir)/zf/src \
	-I$(top_srcdir)/zi/src -I$(top_srcdir)/ztls/src \
	-I$(top_srcdir)/ztcp/src \
	-I$(top_srcdir)/zquic/src -I$(top_srcdir)/zquic/test \
	-I$(top_srcdir)/zhttp/src \
	@Z_CPPFLAGS@
AM_CXXFLAGS = @Z_CXXFLAGS@
AM_LDFLAGS = @Z_LDFLAGS@
LDADD = $(top_builddir)/zhttp/src/libZhttp.la \
	$(top_builddir)/zquic/src/libZquic.la \
	$(top_builddir)/ztls/src/libZtls.la \
	$(top_builddir)/zi/src/libZi.la \
	$(top_builddir)/zf/src/libZf.la $(top_builddir)/ze/src/libZe.la \
	$(top_builddir)/zt/src/libZt.la \
	$(top_builddir)/zm/src/libZm.la \
	$(top_builddir)/zu/src/libZu.la \
	@PTLS_LIBS@ @Z_IO_LIBS@ @Z_ZT_LIBS@ @Z_MT_LIBS@ @Z_LIBS@
noinst_PROGRAMS = zhttp zhttpd zhttpmatrix \
	ZhttpParserTest \
	ZhttpCompressionTest ZhttpFieldsTest ZhttpH2FrameTest ZhttpH2SessionTest \
	ZhttpH2MessageTest \
	ZhttpH2EngineTest \
	ZhttpMessageH1TCPTest ZhttpMessageH1TLSTest \
	ZhttpMessageH2Test ZhttpMessageH3Test \
	ZhttpHPackTest ZhttpQPackTest \
	Zhttp3InteropTest ZhttpFallbackTest ZhttpQPackDynamicTest \
	ZhttpAppTest ZhttpMultiRequestTest \
	ZhttpdTest ZhttpDarkhttpdCompatTest ZhttpLifecycleTest \
	ZhttpTransportContractTest ZhttpH1EngineTest ZhttpH3EngineTest \
	ZhttpH3PushTest \
	ZhttpEngineLifecycleTest ZhttpURLTest ZhttpDiscoveryTest \
	ZhttpAdmissionTest ZhttpAgentCancelTest ZhttpRuntimeTest \
	ZhttpServiceIdleTest ZhttpAgentFallbackTest
dist_noinst_SCRIPTS = ZhttpBoundaryTest.sh ZhttpH2InteropTest.sh
noinst_HEADERS = ZhttpTestUtil.hh ZhttpPut.hh Zhttpd.hh ZhttpBoundary.md
zhttp_SOURCES = zhttp.cc
zhttpd_SOURCES = zhttpd.cc Zhttpd.cc
zhttpmatrix_SOURCES = zhttpmatrix.cc
ZhttpParserTest_SOURCES = ZhttpParserTest.cc
ZhttpCompressionTest_SOURCES = ZhttpCompressionTest.cc
ZhttpFieldsTest_SOURCES = ZhttpFieldsTest.cc
ZhttpH2FrameTest_SOURCES = ZhttpH2FrameTest.cc
ZhttpH2SessionTest_SOURCES = ZhttpH2SessionTest.cc
ZhttpH2MessageTest_SOURCES = ZhttpH2MessageTest.cc
ZhttpH2EngineTest_SOURCES = ZhttpH2EngineTest.cc
ZhttpMessageH1TCPTest_SOURCES = ZhttpMessageEngineTest.cc
ZhttpMessageH1TCPTest_CPPFLAGS = $(AM_CPPFLAGS) -DZHTTP_TEST_H1TCP
ZhttpMessageH1TLSTest_SOURCES = ZhttpMessageEngineTest.cc
ZhttpMessageH1TLSTest_CPPFLAGS = $(AM_CPPFLAGS) -DZHTTP_TEST_H1TLS
ZhttpMessageH2Test_SOURCES = ZhttpMessageEngineTest.cc
ZhttpMessageH2Test_CPPFLAGS = $(AM_CPPFLAGS) -DZHTTP_TEST_H2
ZhttpMessageH3Test_SOURCES = ZhttpMessageEngineTest.cc
ZhttpMessageH3Test_CPPFLAGS = $(AM_CPPFLAGS) -DZHTTP_TEST_H3
ZhttpHPackTest_SOURCES = ZhttpHPackTest.cc
ZhttpQPackTest_SOURCES = ZhttpQPackTest.cc
Zhttp3InteropTest_SOURCES = Zhttp3InteropTest.cc
ZhttpFallbackTest_SOURCES = ZhttpFallbackTest.cc
ZhttpQPackDynamicTest_SOURCES = ZhttpQPackDynamicTest.cc
ZhttpAppTest_SOURCES = ZhttpAppTest.cc
ZhttpMultiRequestTest_SOURCES = ZhttpMultiRequestTest.cc
ZhttpdTest_SOURCES = ZhttpdTest.cc Zhttpd.cc
ZhttpDarkhttpdCompatTest_SOURCES = ZhttpDarkhttpdCompatTest.cc
ZhttpLifecycleTest_SOURCES = ZhttpLifecycleTest.cc
ZhttpTransportContractTest_SOURCES = ZhttpTransportContractTest.cc
ZhttpH1EngineTest_SOURCES = ZhttpH1EngineTest.cc
ZhttpH3EngineTest_SOURCES = ZhttpH3EngineTest.cc
ZhttpH3PushTest_SOURCES = ZhttpH3PushTest.cc
ZhttpEngineLifecycleTest_SOURCES = ZhttpEngineLifecycleTest.cc
ZhttpURLTest_SOURCES = ZhttpURLTest.cc
ZhttpDiscoveryTest_SOURCES = ZhttpDiscoveryTest.cc
ZhttpAdmissionTest_SOURCES = ZhttpAdmissionTest.cc
ZhttpAgentCancelTest_SOURCES = ZhttpAgentCancelTest.cc
ZhttpRuntimeTest_SOURCES = ZhttpRuntimeTest.cc
ZhttpServiceIdleTest_SOURCES = ZhttpServiceIdleTest.cc
ZhttpAgentFallbackTest_SOURCES = ZhttpAgentFallbackTest.cc

.PHONY: test
test: $(noinst_PROGRAMS)
	LSAN_OPTIONS="$${LSAN_OPTIONS:+$$LSAN_OPTIONS:}suppressions=$(top_builddir)/.lsan-suppressions" \
	prove -j1 ZhttpParserTest ZhttpCompressionTest ZhttpFieldsTest \
		ZhttpH2FrameTest \
		ZhttpH2SessionTest \
		ZhttpH2MessageTest \
		ZhttpH2EngineTest \
		ZhttpMessageH1TCPTest ZhttpMessageH1TLSTest \
		ZhttpMessageH2Test ZhttpMessageH3Test \
		ZhttpHPackTest ZhttpQPackTest \
		ZhttpFallbackTest ZhttpQPackDynamicTest \
		ZhttpAppTest ZhttpMultiRequestTest zhttpmatrix \
		ZhttpdTest ZhttpDarkhttpdCompatTest Zhttp3InteropTest \
		ZhttpLifecycleTest ZhttpTransportContractTest ZhttpH1EngineTest \
		ZhttpH3EngineTest ZhttpH3PushTest \
		ZhttpEngineLifecycleTest ZhttpURLTest \
		ZhttpDiscoveryTest ZhttpAdmissionTest ZhttpAgentCancelTest \
		ZhttpRuntimeTest ZhttpServiceIdleTest ZhttpAgentFallbackTest
	./ZhttpBoundaryTest.sh
