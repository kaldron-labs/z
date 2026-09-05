METASOURCES = AUTO
AM_CPPFLAGS = -I$(top_srcdir)/zu/src -I$(top_srcdir)/zm/src \
	-I$(top_srcdir)/zt/src -I$(top_srcdir)/ze/src -I$(top_srcdir)/zf/src \
	-I$(top_srcdir)/zi/src -I$(top_srcdir)/ztls/src \
	-I$(top_srcdir)/zfb/src -I$(top_srcdir)/zv/src \
	-I$(top_srcdir)/zdb/src -I$(top_srcdir)/zdb/test \
	-I$(top_builddir)/zdb/test @Z_CPPFLAGS@
AM_CXXFLAGS = @Z_CXXFLAGS@
AM_LDFLAGS = @Z_LDFLAGS@
LDADD = $(top_builddir)/zdb/test/libZdbTest.la \
	$(top_builddir)/zdb/src/libZdbMem.la \
	$(top_builddir)/zdb/src/libZdb.la \
	$(top_builddir)/zv/src/libZv.la \
	$(top_builddir)/zfb/src/libZfb.la \
	$(top_builddir)/zi/src/libZi.la $(top_builddir)/zf/src/libZf.la \
	$(top_builddir)/ze/src/libZe.la $(top_builddir)/zt/src/libZt.la \
	$(top_builddir)/zm/src/libZm.la $(top_builddir)/zu/src/libZu.la \
	@FBS_LIBS@ @Z_IO_LIBS@ @Z_ZT_LIBS@ @Z_MT_LIBS@ @Z_LIBS@
noinst_PROGRAMS = zdbsmoketest zdbreptest zdbreptest2 zdbsagareptest
zdbsmoketest_SOURCES = zdbsmoketest.cc
zdbreptest_SOURCES = zdbreptest.cc
zdbreptest2_SOURCES = zdbreptest2.cc
zdbsagareptest_SOURCES = zdbsagareptest.cc
dist_noinst_SCRIPTS = 1.sh 2.sh 3.sh clean.sh
dist_noinst_DATA = README.md 3.cf db.cf zdbbtreetest.cf zdbqueuetest.cf zdbreptest.cf

.PHONY: test
test: export ZI_LOGDIR = $(abs_top_builddir)/logs
test: $(noinst_PROGRAMS)
	prove -j1 $(noinst_PROGRAMS)
