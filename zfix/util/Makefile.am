METASOURCES = AUTO
AM_CPPFLAGS = -I$(top_srcdir)/zu/src -I$(top_srcdir)/zm/src -I$(top_srcdir)/zt/src -I$(top_srcdir)/ze/src -I$(top_srcdir)/zf/src -I$(top_srcdir)/zfix/src @Z_CPPFLAGS@
AM_CXXFLAGS = @Z_CXXFLAGS@
AM_LDFLAGS = @Z_LDFLAGS@
EXTRA_DIST = README.md load.hh load_fix.hh load_run.hh load_cache.hh load_native.hh load_main.hh zfix_load_frame.hh pair-test.py
noinst_LTLIBRARIES = libZfixLoad.la libZfixLoadFIX.la libZfixLoadNative.la
libZfixLoad_la_SOURCES = load.cc load_run.cc
libZfixLoadFIX_la_SOURCES = load_fix.cc
bin_PROGRAMS = zfix zfixd
libZfixLoadNative_la_SOURCES = load_native.cc
libZfixLoadNative_la_CPPFLAGS = $(AM_CPPFLAGS) -I$(top_srcdir)/zi/src -I$(top_srcdir)/ztcp/src -I$(top_srcdir)/ztls/src
zfix_SOURCES = zfix.cc
zfixd_SOURCES = zfixd.cc
ZFIX_NATIVE_LIBS = \
	$(top_builddir)/zfix/src/libZfix.la $(top_builddir)/zfix/src/libZfixFmt.la \
	$(top_builddir)/ztls/src/libZtls.la $(top_builddir)/ztcp/src/libZtcp.la $(top_builddir)/zi/src/libZi.la \
	$(LDADD) @PTLS_LIBS@ @Z_IO_LIBS@
zfix_LDADD = libZfixLoadNative.la libZfixLoadFIX.la libZfixLoad.la $(ZFIX_NATIVE_LIBS)
zfixd_LDADD = $(zfix_LDADD)
LDADD = $(top_builddir)/zf/src/libZf.la $(top_builddir)/ze/src/libZe.la \
	$(top_builddir)/zt/src/libZt.la $(top_builddir)/zm/src/libZm.la $(top_builddir)/zu/src/libZu.la \
	@Z_ZT_LIBS@ @Z_MT_LIBS@ @Z_LIBS@
