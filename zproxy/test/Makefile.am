METASOURCES = AUTO
AM_CPPFLAGS = -I$(top_srcdir)/zu/src -I$(top_srcdir)/zproxy/src @Z_CPPFLAGS@
AM_CXXFLAGS = @Z_CXXFLAGS@
AM_LDFLAGS = @Z_LDFLAGS@
LDADD = $(top_builddir)/zu/src/libZu.la @Z_MT_LIBS@ @Z_LIBS@
noinst_PROGRAMS = ZproxyConfigTest
ZproxyConfigTest_SOURCES = ZproxyConfigTest.cc

.PHONY: test
test: $(noinst_PROGRAMS)
	prove $(noinst_PROGRAMS)
