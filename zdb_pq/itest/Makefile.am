METASOURCES = AUTO
AM_CPPFLAGS = -I$(top_srcdir)/zu/src -I$(top_srcdir)/zm/src \
	-I$(top_srcdir)/zt/src -I$(top_srcdir)/ze/src -I$(top_srcdir)/zf/src \
	-I$(top_srcdir)/zi/src -I$(top_srcdir)/ztls/src \
	-I$(top_srcdir)/zfb/src -I$(top_srcdir)/zv/src \
	-I$(top_srcdir)/zdb/src -I$(top_srcdir)/zdb/test \
	-I$(top_builddir)/zdb/test -I$(top_srcdir)/zdb_pq/src @Z_CPPFLAGS@
AM_CXXFLAGS = @Z_CXXFLAGS@
AM_LDFLAGS = @Z_LDFLAGS@
LDADD = $(top_builddir)/zdb/test/libZdbTest.la \
	$(top_builddir)/zdb_pq/src/libZdbPQ.la \
	$(top_builddir)/zdb/src/libZdb.la \
	$(top_builddir)/zv/src/libZv.la $(top_builddir)/zfb/src/libZfb.la \
	$(top_builddir)/zi/src/libZi.la $(top_builddir)/zf/src/libZf.la \
	$(top_builddir)/ze/src/libZe.la $(top_builddir)/zt/src/libZt.la \
	$(top_builddir)/zm/src/libZm.la $(top_builddir)/zu/src/libZu.la \
	@FBS_LIBS@ @Z_IO_LIBS@ @Z_ZT_LIBS@ @Z_MT_LIBS@ @Z_LIBS@
noinst_PROGRAMS = zdbpqtest
zdbpqtest_SOURCES = zdbpqtest.cc

.PHONY: test
test: $(noinst_PROGRAMS)
	@if test -n "$$ZDB_MODULE" && test -n "$$ZDB_CONNECT"; then \
	  prove ./zdbpqtest; \
	else \
	  echo '1..0 # SKIP ZDB_MODULE and ZDB_CONNECT are required'; \
	fi
