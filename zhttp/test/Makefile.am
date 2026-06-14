METASOURCES = AUTO
AM_CPPFLAGS = -I$(top_srcdir)/zu/src -I$(top_srcdir)/zm/src \
	-I$(top_srcdir)/zt/src -I$(top_srcdir)/ze/src \
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
	$(top_builddir)/ze/src/libZe.la \
	$(top_builddir)/zt/src/libZt.la \
	$(top_builddir)/zm/src/libZm.la \
	$(top_builddir)/zu/src/libZu.la \
	@PTLS_LIBS@ @Z_IO_LIBS@ @Z_ZT_LIBS@ @Z_MT_LIBS@ @Z_LIBS@
noinst_PROGRAMS = zhttptest \
	ZhttpParserTest \
	ZhttpHPackTest ZhttpQPackTest \
	Zhttp3InteropTest ZhttpFallbackTest ZhttpQPackDynamicTest \
	ZhttpAppTest ZhttpMultiRequestTest \
	ZhttpdTest ZhttpDarkhttpdCompatTest
noinst_HEADERS = ZhttpTestUtil.hh
zhttptest_SOURCES = zhttptest.cc
ZhttpParserTest_SOURCES = ZhttpParserTest.cc
ZhttpHPackTest_SOURCES = ZhttpHPackTest.cc
ZhttpQPackTest_SOURCES = ZhttpQPackTest.cc
Zhttp3InteropTest_SOURCES = Zhttp3InteropTest.cc
ZhttpFallbackTest_SOURCES = ZhttpFallbackTest.cc
ZhttpQPackDynamicTest_SOURCES = ZhttpQPackDynamicTest.cc
ZhttpAppTest_SOURCES = ZhttpAppTest.cc
ZhttpMultiRequestTest_SOURCES = ZhttpMultiRequestTest.cc
ZhttpdTest_SOURCES = ZhttpdTest.cc
ZhttpDarkhttpdCompatTest_SOURCES = ZhttpDarkhttpdCompatTest.cc

.PHONY: test
test: $(noinst_PROGRAMS)
	prove ZhttpParserTest ZhttpHPackTest ZhttpQPackTest \
		Zhttp3InteropTest ZhttpFallbackTest ZhttpQPackDynamicTest \
		ZhttpAppTest ZhttpMultiRequestTest \
		ZhttpdTest ZhttpDarkhttpdCompatTest
