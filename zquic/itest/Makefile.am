METASOURCES = AUTO
AM_CPPFLAGS = -I$(top_srcdir)/zu/src -I$(top_srcdir)/zm/src \
	-I$(top_srcdir)/zt/src -I$(top_srcdir)/ze/src -I$(top_srcdir)/zf/src \
	-I$(top_srcdir)/zi/src -I$(top_srcdir)/ztls/src \
	-I$(top_srcdir)/zquic/src @Z_CPPFLAGS@
AM_CXXFLAGS = @Z_CXXFLAGS@
AM_LDFLAGS = @Z_LDFLAGS@
LDADD = $(top_builddir)/zquic/src/libZquic.la \
	$(top_builddir)/ztls/src/libZtls.la $(top_builddir)/zi/src/libZi.la \
	$(top_builddir)/zf/src/libZf.la $(top_builddir)/ze/src/libZe.la \
	$(top_builddir)/zt/src/libZt.la $(top_builddir)/zm/src/libZm.la \
	$(top_builddir)/zu/src/libZu.la \
	@Z_IO_LIBS@ @Z_ZT_LIBS@ @Z_MT_LIBS@ @Z_LIBS@
noinst_PROGRAMS = zquictxqueuetest
zquictxqueuetest_SOURCES = zquictxqueuetest.cc
if LINUX
noinst_PROGRAMS += zquicnativeudpqueuetest
zquicnativeudpqueuetest_SOURCES = zquicnativeudpqueuetest.cc
zquicnativeudpqueuetest_CPPFLAGS = $(AM_CPPFLAGS) -I$(top_srcdir)/zutcp/src \
	-I$(top_srcdir)/zutcp/test -I$(top_srcdir)/zpkt/src -I$(top_srcdir)/zpkt/test \
	-DZUTCP_TEST_PLUGIN='"$(abs_top_builddir)/zpkt/test/.libs/libZpktTestPlugin.so"' \
	-DZUTCP_XDP_MODULE='"$(abs_top_builddir)/zpkt/xdp/.libs/libZpkt_xdp.so"'
zquicnativeudpqueuetest_LDADD = $(top_builddir)/zutcp/src/libZutcp.la \
	$(top_builddir)/zpkt/src/libZpkt.la $(LDADD)
endif
EXTRA_DIST = README.md

.PHONY: test
test: all
	prove $(noinst_PROGRAMS)
