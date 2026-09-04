METASOURCES = AUTO
AM_CPPFLAGS = -I$(top_srcdir)/zu/src -I$(top_srcdir)/zm/src \
	-I$(top_srcdir)/zt/src -I$(top_srcdir)/ze/src -I$(top_srcdir)/zf/src \
	-I$(top_srcdir)/zi/src -I$(top_srcdir)/zrl/src -I$(top_srcdir)/zv/src \
	-I$(top_srcdir)/ztc/src \
	-I$(top_srcdir)/ztls/src -I$(top_srcdir)/zfb/src \
	-I$(top_srcdir)/zdb/src -I$(top_srcdir)/zum/src \
	-I$(top_srcdir)/zcmd/src @Z_CPPFLAGS@
AM_CXXFLAGS = @Z_CXXFLAGS@
AM_LDFLAGS = @Z_LDFLAGS@
LDADD = $(top_builddir)/zcmd/src/libZcmd.la \
	$(top_builddir)/zum/src/libZum.la $(top_builddir)/zdb/src/libZdb.la \
	$(top_builddir)/zfb/src/libZfb.la $(top_builddir)/ztls/src/libZtls.la \
	$(top_builddir)/ztc/src/libZtc.la $(top_builddir)/zv/src/libZv.la \
	$(top_builddir)/zi/src/libZi.la \
	$(top_builddir)/zf/src/libZf.la $(top_builddir)/ze/src/libZe.la \
	$(top_builddir)/zt/src/libZt.la $(top_builddir)/zm/src/libZm.la \
	$(top_builddir)/zu/src/libZu.la @PTLS_LIBS@ @Z_IO_LIBS@ \
	@Z_ZT_LIBS@ @Z_MT_LIBS@ @Z_LIBS@
noinst_PROGRAMS = cmdtest
cmdtest_SOURCES = cmdtest.cc
dist_noinst_SCRIPTS = cmdtest.sh initdb.sh keygen.sh

.PHONY: test
test: $(noinst_PROGRAMS)
	@if test -n "$$ZCMD_ITEST_ARGS"; then \
	  prove ./cmdtest :: $$ZCMD_ITEST_ARGS; \
	else \
	  echo '1..0 # SKIP set ZCMD_ITEST_ARGS to run the command-server integration test'; \
	fi
