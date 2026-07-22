METASOURCES = AUTO
AM_CPPFLAGS = -I$(top_srcdir)/zu/src -I$(top_srcdir)/zm/src \
	-I$(top_srcdir)/zt/src -I$(top_srcdir)/ze/src -I$(top_srcdir)/zf/src -I$(top_srcdir)/zi/src \
	-I$(top_srcdir)/zrl/src \
	@Z_CPPFLAGS@
AM_CXXFLAGS = @Z_CXXFLAGS@
AM_LDFLAGS = @Z_LDFLAGS@
LDADD = $(top_builddir)/zrl/src/libZrl.la \
	$(top_builddir)/zi/src/libZi.la \
	$(top_builddir)/zf/src/libZf.la $(top_builddir)/ze/src/libZe.la $(top_builddir)/zt/src/libZt.la \
	$(top_builddir)/zm/src/libZm.la $(top_builddir)/zu/src/libZu.la \
	@Z_TERM_LIBS@ @Z_IO_LIBS@ @Z_ZT_LIBS@ @Z_MT_LIBS@ @Z_LIBS@
bin_PROGRAMS = \
	ZrlTest
noinst_PROGRAMS = \
	ZrlLineTest ZrlTerminfoTest ZrlTerminalTest ZrlEditorTest \
	ZrlCLITest
ZrlLineTest_SOURCES = ZrlLineTest.cc
ZrlTerminfoTest_SOURCES = ZrlTerminfoTest.cc
ZrlTerminalTest_SOURCES = ZrlTerminalTest.cc
ZrlEditorTest_SOURCES = ZrlEditorTest.cc
ZrlCLITest_SOURCES = ZrlCLITest.cc
ZrlTest_SOURCES = ZrlTest.cc
