METASOURCES = AUTO
AM_CPPFLAGS = -I$(top_srcdir)/zu/src -I$(top_srcdir)/zm/src \
	-I$(top_srcdir)/zt/src -I$(top_srcdir)/ze/src -I$(top_srcdir)/zf/src \
	-I$(top_srcdir)/zi/src -I$(top_srcdir)/ztcp/src \
	-I$(top_srcdir)/ztls/src -I$(top_srcdir)/zquic/src \
	-I$(top_srcdir)/zhttp/src -I$(top_srcdir)/zhttp/test \
	-I$(top_srcdir)/zquic/test -I$(top_srcdir)/zws/src \
	@Z_CPPFLAGS@
AM_CXXFLAGS = @Z_CXXFLAGS@
AM_LDFLAGS = @Z_LDFLAGS@
LDADD = $(top_builddir)/zws/src/libZws.la \
	$(top_builddir)/zhttp/src/libZhttp.la \
	$(top_builddir)/zquic/src/libZquic.la \
	$(top_builddir)/ztcp/src/libZtcp.la \
	$(top_builddir)/ztls/src/libZtls.la \
	$(top_builddir)/zi/src/libZi.la \
	$(top_builddir)/zf/src/libZf.la $(top_builddir)/ze/src/libZe.la \
	$(top_builddir)/zt/src/libZt.la \
	$(top_builddir)/zm/src/libZm.la \
	$(top_builddir)/zu/src/libZu.la \
	@PTLS_LIBS@ @Z_IO_LIBS@ @Z_ZT_LIBS@ @Z_MT_LIBS@ @Z_LIBS@
noinst_PROGRAMS = \
	ZwsProtocolTest ZwsH1EngineTest ZwsH2EngineTest ZwsH3EngineTest \
	ZwsAutobahnClient
EXTRA_PROGRAMS = ZwsBench
EXTRA_DIST = ZwsSourceBoundaryTest.sh ZwsBinanceTest.sh \
	autobahn/fuzzingclient.json autobahn/fuzzingserver.json \
	autobahn/run-server.sh autobahn/run-client.sh autobahn/README.md
ZwsProtocolTest_SOURCES = ZwsProtocolTest.cc
ZwsH1EngineTest_SOURCES = ZwsH1EngineTest.cc
ZwsH2EngineTest_SOURCES = ZwsH1EngineTest.cc
ZwsH2EngineTest_CPPFLAGS = $(AM_CPPFLAGS) -DZWS_H2_TEST
ZwsH3EngineTest_SOURCES = ZwsH1EngineTest.cc
ZwsH3EngineTest_CPPFLAGS = $(AM_CPPFLAGS) -DZWS_H3_TEST
ZwsAutobahnClient_SOURCES = ZwsAutobahnClient.cc
ZwsBench_SOURCES = ZwsBench.cc

.PHONY: test
test: $(noinst_PROGRAMS)
	prove -j1 $(srcdir)/ZwsSourceBoundaryTest.sh \
		ZwsProtocolTest ZwsH1EngineTest ZwsH2EngineTest ZwsH3EngineTest
