METASOURCES = AUTO
.cc.s:
	$(CXXCOMPILE) -c -S $<
%.i.cc: %.cc
	$(CXXCOMPILE) -C -E $< -o $@
AM_CPPFLAGS = -I$(top_srcdir)/zu/src -I$(top_srcdir)/zm/src \
	-I$(top_srcdir)/zt/src -I$(top_srcdir)/ze/src -I$(top_srcdir)/zi/src \
	-I$(top_srcdir)/zfb/src -I$(top_srcdir)/zv/src \
	@Z_CPPFLAGS@
AM_CXXFLAGS = @Z_CXXFLAGS@
AM_LDFLAGS = @Z_LDFLAGS@
LDADD = $(top_builddir)/zv/src/libZv.la \
	$(top_builddir)/zfb/src/libZfb.la $(top_builddir)/zi/src/libZi.la \
	$(top_builddir)/ze/src/libZe.la $(top_builddir)/zt/src/libZt.la \
	$(top_builddir)/zm/src/libZm.la $(top_builddir)/zu/src/libZu.la \
	@Z_IO_LIBS@ @Z_ZT_LIBS@ @Z_MT_LIBS@ @Z_LIBS@
noinst_PROGRAMS = \
	CfTest CfFlatten ZvNewCfTest ZvEngineTest
CfTest_SOURCES = CfTest.cc
CfFlatten_SOURCES = CfFlatten.cc
ZvNewCfTest_SOURCES = ZvNewCfTest.cc
ZvEngineTest_SOURCES = ZvEngineTest.cc
