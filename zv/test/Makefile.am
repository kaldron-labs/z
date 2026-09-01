METASOURCES = AUTO
.cc.s:
	$(CXXCOMPILE) -c -S $<
%.i.cc: %.cc
	$(CXXCOMPILE) -C -E $< -o $@
AM_CPPFLAGS = -I$(top_srcdir)/zu/src -I$(top_srcdir)/zm/src \
	-I$(top_srcdir)/zt/src -I$(top_srcdir)/ze/src -I$(top_srcdir)/zf/src -I$(top_srcdir)/zi/src \
	-I$(top_srcdir)/zi/test \
	-I$(top_srcdir)/zfb/src -I$(top_srcdir)/zv/src \
	@Z_CPPFLAGS@
AM_CXXFLAGS = @Z_CXXFLAGS@
AM_LDFLAGS = @Z_LDFLAGS@
LDADD = $(top_builddir)/zi/test/libZiTest.la \
	$(top_builddir)/zv/src/libZv.la \
	$(top_builddir)/zfb/src/libZfb.la $(top_builddir)/zi/src/libZi.la \
	$(top_builddir)/zf/src/libZf.la $(top_builddir)/ze/src/libZe.la $(top_builddir)/zt/src/libZt.la \
	$(top_builddir)/zm/src/libZm.la $(top_builddir)/zu/src/libZu.la \
	@Z_IO_LIBS@ @Z_ZT_LIBS@ @Z_MT_LIBS@ @Z_LIBS@
noinst_PROGRAMS = \
	ZvCfTest ZvYAMLTest ZvTOMLTest ZtcMgrTest ZtcDBTest ZtcSchemaTest \
	ZtcFilterTest ZtcAppTest ZtcAlertTest
.PHONY: test
test: export ZI_LOGDIR = $(abs_top_builddir)/logs
test: $(noinst_PROGRAMS)
	prove ZvCfTest
	prove ZvYAMLTest
	prove ZvTOMLTest
	prove ZtcMgrTest
	prove ZtcDBTest
	prove ZtcSchemaTest
	prove ZtcFilterTest
	prove ZtcAppTest
	prove ZtcAlertTest
ZvCfTest_SOURCES = ZvCfTest.cc
ZvYAMLTest_SOURCES = ZvYAMLTest.cc
ZvTOMLTest_SOURCES = ZvTOMLTest.cc
ZtcMgrTest_SOURCES = ZtcMgrTest.cc
ZtcDBTest_SOURCES = ZtcDBTest.cc
ZtcSchemaTest_SOURCES = ZtcSchemaTest.cc
ZtcFilterTest_SOURCES = ZtcFilterTest.cc
ZtcAppTest_SOURCES = ZtcAppTest.cc ZtcTestClient.hh
ZtcAlertTest_SOURCES = ZtcAlertTest.cc ZtcTestClient.hh
