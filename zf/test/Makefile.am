METASOURCES = AUTO
AM_CPPFLAGS = -I$(top_srcdir)/zu/src -I$(top_srcdir)/zm/src \
	-I$(top_srcdir)/zt/src -I$(top_srcdir)/ze/src -I$(top_srcdir)/zf/src \
	@Z_CPPFLAGS@
AM_CXXFLAGS = @Z_CXXFLAGS@
AM_LDFLAGS = @Z_LDFLAGS@
ZfTOMLTest_CPPFLAGS = $(AM_CPPFLAGS) -DZF_TOML_TEST_SRCDIR='"$(srcdir)"'
LDADD = $(top_builddir)/zf/src/libZf.la $(top_builddir)/ze/src/libZe.la \
	$(top_builddir)/zt/src/libZt.la $(top_builddir)/zm/src/libZm.la \
	$(top_builddir)/zu/src/libZu.la \
	@Z_IO_LIBS@ @Z_ZT_LIBS@ @Z_MT_LIBS@ @Z_LIBS@
noinst_PROGRAMS = \
	ZfASN1Test ZfCfTest ZfCLITest ZfCSVTest ZfStructTest ZfTreeTest \
	ZfURITest ZfYAMLTest ZfTOMLTest
noinst_HEADERS = ZfHeapTest.hh ZfMapTest.hh
.PHONY: test
test: $(noinst_PROGRAMS)
	prove $(noinst_PROGRAMS)
ZfASN1Test_SOURCES = ZfASN1Test.cc
ZfCfTest_SOURCES = ZfCfTest.cc
ZfCLITest_SOURCES = ZfCLITest.cc
ZfCSVTest_SOURCES = ZfCSVTest.cc
ZfStructTest_SOURCES = ZfStructTest.cc
ZfTreeTest_SOURCES = ZfTreeTest.cc
ZfURITest_SOURCES = ZfURITest.cc
ZfYAMLTest_SOURCES = ZfYAMLTest.cc
ZfTOMLTest_SOURCES = ZfTOMLTest.cc
EXTRA_DIST = \
	TOML-AUDIT.md \
	toml-test/README.md \
	toml-test/valid/arrays.toml \
	toml-test/valid/datetime.toml \
	toml-test/valid/strings.toml \
	toml-test/valid/tables.toml \
	toml-test/invalid/array-conflict.toml \
	toml-test/invalid/duplicate.toml \
	toml-test/invalid/inline-sealed.toml \
	toml-test/invalid/value-date.toml \
	toml-test/invalid/value-number.toml
