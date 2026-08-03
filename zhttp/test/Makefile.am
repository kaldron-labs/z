METASOURCES = AUTO
AM_CPPFLAGS = -I$(top_srcdir)/zu/src -I$(top_srcdir)/zm/src \
	-I$(top_srcdir)/zt/src -I$(top_srcdir)/ze/src -I$(top_srcdir)/zf/src \
	-I$(top_srcdir)/zi/src -I$(top_srcdir)/ztls/src \
	-I$(top_srcdir)/ztcp/src \
	-I$(top_srcdir)/zquic/src -I$(top_srcdir)/zquic/test \
	-I$(top_srcdir)/zhttp/src \
	-DZHTTP_TEST_SRCDIR='"$(abs_srcdir)"' \
	-DZHTTP_UTIL_SRCDIR='"$(abs_top_srcdir)/zhttp/util"' \
	-DZHTTP_LIB_SRCDIR='"$(abs_top_srcdir)/zhttp/src"' \
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
noinst_PROGRAMS = \
	ZhttpParserTest \
	ZhttpCompressionTest ZhttpFieldsTest ZhttpH2FrameTest ZhttpH2StreamTest \
	ZhttpH2MessageTest ZhttpH2HubTest \
	ZhttpMessageH1TCPTest ZhttpMessageH1TLSTest \
	ZhttpMessageH2Test ZhttpMessageH3Test \
	ZhttpHPackTest ZhttpQPackTest ZhttpQPackDynamicTest \
	ZhttpTransportContractTest ZhttpH1HubTest ZhttpH3HubTest \
	ZhttpH3PushTest ZhttpHubLifecycleTest ZhttpURLTest ZhttpAltSvcTest \
	ZhttpDiscoveryTest ZhttpAdmissionTest ZhttpClientCancelTest \
	ZhttpRuntimeTest ZhttpServiceIdleTest ZhttpBoundaryTest
dist_noinst_SCRIPTS = ZhttpBoundaryFixture.sh
dist_noinst_DATA = ZhttpJ2N100000HashTune.csv \
	ZhttpJ2N100000HeapTune.csv
noinst_HEADERS = ZhttpTestUtil.hh
ZhttpParserTest_SOURCES = ZhttpParserTest.cc
ZhttpCompressionTest_SOURCES = ZhttpCompressionTest.cc
ZhttpFieldsTest_SOURCES = ZhttpFieldsTest.cc
ZhttpH2FrameTest_SOURCES = ZhttpH2FrameTest.cc
ZhttpH2StreamTest_SOURCES = ZhttpH2StreamTest.cc
ZhttpH2MessageTest_SOURCES = ZhttpH2MessageTest.cc
ZhttpH2HubTest_SOURCES = ZhttpH2HubTest.cc
ZhttpMessageH1TCPTest_SOURCES = ZhttpMessageHubTest.cc
ZhttpMessageH1TCPTest_CPPFLAGS = $(AM_CPPFLAGS) -DZHTTP_TEST_H1TCP
ZhttpMessageH1TLSTest_SOURCES = ZhttpMessageHubTest.cc
ZhttpMessageH1TLSTest_CPPFLAGS = $(AM_CPPFLAGS) -DZHTTP_TEST_H1TLS
ZhttpMessageH2Test_SOURCES = ZhttpMessageHubTest.cc
ZhttpMessageH2Test_CPPFLAGS = $(AM_CPPFLAGS) -DZHTTP_TEST_H2
ZhttpMessageH3Test_SOURCES = ZhttpMessageHubTest.cc
ZhttpMessageH3Test_CPPFLAGS = $(AM_CPPFLAGS) -DZHTTP_TEST_H3
ZhttpHPackTest_SOURCES = ZhttpHPackTest.cc
ZhttpQPackTest_SOURCES = ZhttpQPackTest.cc
ZhttpQPackDynamicTest_SOURCES = ZhttpQPackDynamicTest.cc
ZhttpTransportContractTest_SOURCES = ZhttpTransportContractTest.cc
ZhttpH1HubTest_SOURCES = ZhttpH1HubTest.cc
ZhttpH3HubTest_SOURCES = ZhttpH3HubTest.cc
ZhttpH3PushTest_SOURCES = ZhttpH3PushTest.cc
ZhttpHubLifecycleTest_SOURCES = ZhttpHubLifecycleTest.cc
ZhttpURLTest_SOURCES = ZhttpURLTest.cc
ZhttpAltSvcTest_SOURCES = ZhttpAltSvcTest.cc
ZhttpDiscoveryTest_SOURCES = ZhttpDiscoveryTest.cc
ZhttpAdmissionTest_SOURCES = ZhttpAdmissionTest.cc
ZhttpClientCancelTest_SOURCES = ZhttpClientCancelTest.cc
ZhttpRuntimeTest_SOURCES = ZhttpRuntimeTest.cc
ZhttpServiceIdleTest_SOURCES = ZhttpServiceIdleTest.cc
ZhttpBoundaryTest_SOURCES = ZhttpBoundaryTest.cc

.PHONY: test
test: $(noinst_PROGRAMS)
	LSAN_OPTIONS="$${LSAN_OPTIONS:+$$LSAN_OPTIONS:}suppressions=$(top_builddir)/.lsan-suppressions" \
	prove -j1 $(noinst_PROGRAMS)
