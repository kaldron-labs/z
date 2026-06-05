METASOURCES = AUTO
AM_CPPFLAGS = -I$(top_srcdir)/zu/src -I$(top_srcdir)/zm/src \
	-I$(top_srcdir)/zt/src -I$(top_srcdir)/ze/src \
	-I$(top_srcdir)/zi/src -I$(top_srcdir)/ztls/src \
	-I$(top_srcdir)/zquic/src -I$(top_srcdir)/zhttp/src \
	@Z_CPPFLAGS@
AM_CXXFLAGS = @Z_CXXFLAGS@
AM_LDFLAGS = @Z_LDFLAGS@
LDADD = $(top_builddir)/zhttp/src/libZhttp.la \
	$(top_builddir)/zquic/src/libZquic.la \
	$(top_builddir)/ztls/src/libZtls.la \
	$(top_builddir)/zi/src/libZi.la \
	$(top_builddir)/ze/src/libZe.la \
	$(top_builddir)/zt/src/libZt.la \
	$(top_builddir)/zm/src/libZm.la \
	$(top_builddir)/zu/src/libZu.la \
	@PTLS_LIBS@ @Z_IO_LIBS@ @Z_ZT_LIBS@ @Z_MT_LIBS@ @Z_LIBS@
noinst_PROGRAMS = zhttptest zhttptestd \
	ZhttpHPackTest ZhttpQPackTest Zhttp3Test Zhttp3LoopTest \
	Zhttp3InteropTest ZhttpFallbackTest ZhttpQPackDynamicTest \
	Zhttp3StateTest
noinst_HEADERS = ZhttpCaddyInterop.hh
EXTRA_DIST = caddy-h3.json
zhttptest_SOURCES = zhttptest.cc
zhttptestd_SOURCES = zhttptestd.cc
ZhttpHPackTest_SOURCES = ZhttpHPackTest.cc
ZhttpQPackTest_SOURCES = ZhttpQPackTest.cc
Zhttp3Test_SOURCES = Zhttp3Test.cc
Zhttp3LoopTest_SOURCES = Zhttp3LoopTest.cc
Zhttp3InteropTest_SOURCES = Zhttp3InteropTest.cc
ZhttpFallbackTest_SOURCES = ZhttpFallbackTest.cc
ZhttpQPackDynamicTest_SOURCES = ZhttpQPackDynamicTest.cc
Zhttp3StateTest_SOURCES = Zhttp3StateTest.cc

.PHONY: test
test: $(noinst_PROGRAMS)
	prove ZhttpHPackTest ZhttpQPackTest Zhttp3Test Zhttp3LoopTest \
		Zhttp3InteropTest ZhttpFallbackTest ZhttpQPackDynamicTest \
		Zhttp3StateTest
