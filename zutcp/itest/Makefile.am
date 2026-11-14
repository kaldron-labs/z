METASOURCES = AUTO
AM_CPPFLAGS = -I$(top_srcdir)/zu/src -I$(top_srcdir)/zm/src \
	-I$(top_srcdir)/zt/src -I$(top_srcdir)/ze/src -I$(top_srcdir)/zf/src \
	-I$(top_srcdir)/zi/src -I$(top_srcdir)/zi/test \
	-I$(top_srcdir)/zutcp/src -I$(top_srcdir)/zxdp/src @Z_CPPFLAGS@
AM_CXXFLAGS = @Z_CXXFLAGS@
AM_LDFLAGS = @Z_LDFLAGS@
LDADD = $(top_builddir)/zi/test/libZiTest.la \
	$(top_builddir)/zutcp/src/libZutcp.la $(top_builddir)/zxdp/src/libZxdp.la \
	$(top_builddir)/zi/src/libZi.la \
	$(top_builddir)/zf/src/libZf.la $(top_builddir)/ze/src/libZe.la \
	$(top_builddir)/zt/src/libZt.la $(top_builddir)/zm/src/libZm.la \
	$(top_builddir)/zu/src/libZu.la \
	@Z_IO_LIBS@ @Z_ZT_LIBS@ @Z_MT_LIBS@ @Z_LIBS@
noinst_PROGRAMS = zutcppairtest zutcppooltest zutcpstacktest zutcpiptest zutcpmxtest zutcpapptest \
	zutcptlsapptest
zutcppairtest_SOURCES = zutcppairtest.cc
zutcppooltest_SOURCES = zutcppooltest.cc
zutcpstacktest_SOURCES = zutcpstacktest.cc
zutcpiptest_SOURCES = zutcpiptest.cc
zutcpmxtest_SOURCES = zutcpmxtest.cc
zutcpapptest_SOURCES = zutcpapptest.cc
zutcpapptest_CPPFLAGS = $(AM_CPPFLAGS) -I$(top_srcdir)/ztcp/src
zutcpapptest_LDADD = $(LDADD) $(top_builddir)/ztcp/src/libZtcp.la
zutcptlsapptest_SOURCES = zutcpapptest.cc
zutcptlsapptest_CPPFLAGS = $(AM_CPPFLAGS) -DZUTCP_APP_TLS \
	-I$(top_srcdir)/ztls/src -I$(top_srcdir)/zdbus/src @PTLS_CFLAGS@
zutcptlsapptest_LDFLAGS = $(AM_LDFLAGS) -R@PTLS_LIBDIR@
zutcptlsapptest_LDADD = $(LDADD) $(top_builddir)/ztls/src/libZtls.la \
	$(top_builddir)/zdbus/src/libZdbus.la @PTLS_LIBS@
EXTRA_DIST = README.md zutcpipfixture.sh zutcppairfixture.sh zutcpmxfixture.sh \
	zutcpappfixture.sh zutcpmxlossfixture.sh zutcprecovery.py

.PHONY: test
test: all
	prove $(noinst_PROGRAMS)
