METASOURCES = AUTO
AM_CPPFLAGS = -I$(top_srcdir)/zu/src -I$(top_srcdir)/zm/src \
	-I$(top_srcdir)/zt/src -I$(top_srcdir)/ze/src -I$(top_srcdir)/zf/src \
	@Z_CPPFLAGS@
AM_CXXFLAGS = @Z_CXXFLAGS@
AM_LDFLAGS = @Z_LDFLAGS@
LDADD = $(top_builddir)/zf/src/libZf.la $(top_builddir)/ze/src/libZe.la \
	$(top_builddir)/zt/src/libZt.la $(top_builddir)/zm/src/libZm.la \
	$(top_builddir)/zu/src/libZu.la \
	@Z_IO_LIBS@ @Z_ZT_LIBS@ @Z_MT_LIBS@ @Z_LIBS@
noinst_PROGRAMS = \
	ZfASN1Test ZfCBORTest ZfDBUSTest ZfCfTest ZfCLITest ZfCSVTest ZfStructTest ZfTreeTest \
	ZfURITest ZfYAMLTest
noinst_HEADERS = ZfHeapTest.hh ZfMapTest.hh
.PHONY: test
test: export ZI_LOGDIR = $(abs_top_builddir)/logs
test: $(noinst_PROGRAMS)
	prove $(noinst_PROGRAMS)
ZfASN1Test_SOURCES = ZfASN1Test.cc
ZfCBORTest_SOURCES = ZfCBORTest.cc
ZfDBUSTest_SOURCES = ZfDBUSTest.cc
ZfCfTest_SOURCES = ZfCfTest.cc
ZfCLITest_SOURCES = ZfCLITest.cc
ZfCSVTest_SOURCES = ZfCSVTest.cc
ZfStructTest_SOURCES = ZfStructTest.cc
ZfTreeTest_SOURCES = ZfTreeTest.cc
ZfURITest_SOURCES = ZfURITest.cc
ZfYAMLTest_SOURCES = ZfYAMLTest.cc
