METASOURCES = AUTO
AM_CPPFLAGS = -I$(top_srcdir)/zu/src -I$(top_srcdir)/zm/src \
	-I$(top_srcdir)/zt/src -I$(top_srcdir)/ze/src -I$(top_srcdir)/zi/src \
	-I$(top_srcdir)/zf/src -I$(top_srcdir)/zfb/src \
	-I$(top_srcdir)/ztc/src -I$(top_srcdir)/zdb/src \
	-I$(top_srcdir)/zdf/src -I$(top_srcdir)/zv/src -I$(top_srcdir)/zgtk/src \
	@Z_CPPFLAGS@ \
	-DZDASH_SRCDIR='"$(abs_top_srcdir)/zdash/src"'
AM_CXXFLAGS = @Z_CXXFLAGS@ @GTK_CFLAGS@
AM_LDFLAGS = @Z_LDFLAGS@
LDADD = $(top_builddir)/zi/src/libZi.la $(top_builddir)/ze/src/libZe.la \
	$(top_builddir)/zt/src/libZt.la $(top_builddir)/zm/src/libZm.la \
	$(top_builddir)/zu/src/libZu.la \
	@Z_IO_LIBS@ @Z_ZT_LIBS@ @Z_MT_LIBS@ @Z_LIBS@
noinst_PROGRAMS = ZdashSchemaTest
ZdashSchemaTest_SOURCES = ZdashSchemaTest.cc
noinst_LTLIBRARIES = zdash_test.la
zdash_test_la_SOURCES = ZdashTest.cc ../src/zdash_module.hh
zdash_test_la_LDFLAGS = $(AM_LDFLAGS) -module -avoid-version -rpath $(abs_builddir)
zdash_test_la_LIBADD = $(top_builddir)/ztc/src/libZtc.la \
	$(top_builddir)/zdf/src/libZdf.la $(top_builddir)/zdb/src/libZdb.la \
	$(top_builddir)/zfb/src/libZfb.la $(top_builddir)/zv/src/libZv.la \
	$(top_builddir)/zf/src/libZf.la $(LDADD) @GTK_LIBS@ @FBS_LIBS@
EXTRA_DIST = ZdashTest.sh fontconfig.lsan fontconfig.valgrind README.md

.PHONY: test
test: all
	prove $(noinst_PROGRAMS)
	ZDASH_BIN='$(abs_top_builddir)/zdash/src/zdash' \
	ZDASH_TEST='$(abs_builddir)/.libs/zdash_test.so' \
	ZDASH_SRCDIR='$(abs_top_srcdir)/zdash/src' \
	prove --exec '$(SHELL)' $(srcdir)/ZdashTest.sh
