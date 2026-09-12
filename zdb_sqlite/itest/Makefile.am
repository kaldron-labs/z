METASOURCES = AUTO
AM_CPPFLAGS = -I$(top_srcdir)/zu/src -I$(top_srcdir)/zm/src \
	-I$(top_srcdir)/zt/src -I$(top_srcdir)/ze/src \
	-I$(top_srcdir)/zf/src -I$(top_srcdir)/zi/src \
	-I$(top_srcdir)/ztc/src \
	-I$(top_srcdir)/zfb/src -I$(top_srcdir)/zv/src \
	-I$(top_srcdir)/zdb/src -I$(top_srcdir)/zdb/test \
	-I$(top_builddir)/zdb/test -I$(top_srcdir)/zdb_sqlite/src \
	@SQLITE_CFLAGS@ @Z_CPPFLAGS@
AM_CXXFLAGS = @Z_CXXFLAGS@
AM_LDFLAGS = @Z_LDFLAGS@
FBS = zdbsltypes.fbs
FBS_H = ${FBS:%.fbs=%_fbs.h}
${FBS_H}: %_fbs.h: fbs/%.fbs $(top_builddir)/scripts/fbscodegen
	$(top_builddir)/scripts/fbscodegen -b --schema --bfbs-gen-embed -c \
	  --cpp-std c++17 --cpp-field-case-style lower \
	  -I fbs -I $(top_srcdir)/zfb/src/fbs $<
BUILT_SOURCES = ${FBS_H}
CLEANFILES = ${FBS_H}
noinst_HEADERS = ZdbSLTypes.hh ${FBS_H}
LDADD = $(top_builddir)/zdb/test/libZdbTest.la \
	$(top_builddir)/zdb_sqlite/src/libZdbSL.la \
	$(top_builddir)/zdb/src/libZdb.la \
	$(top_builddir)/ztc/src/libZtc.la \
	$(top_builddir)/zv/src/libZv.la $(top_builddir)/zfb/src/libZfb.la \
	$(top_builddir)/zi/src/libZi.la $(top_builddir)/zf/src/libZf.la \
	$(top_builddir)/ze/src/libZe.la $(top_builddir)/zt/src/libZt.la \
	$(top_builddir)/zm/src/libZm.la $(top_builddir)/zu/src/libZu.la \
	@SQLITE_LIBS@ @FBS_LIBS@ @Z_IO_LIBS@ @Z_ZT_LIBS@ @Z_MT_LIBS@ @Z_LIBS@
noinst_PROGRAMS = zdbsltest zdbsagatest zdbslrepltest
dist_noinst_SCRIPTS = zdbslconfigtest zdbslinspecttest zdbslschematest \
	zdbslplantest
zdbsltest_SOURCES = zdbsltest.cc
zdbsagatest_SOURCES = zdbsagatest.cc
zdbslrepltest_SOURCES = zdbslrepltest.cc

.PHONY: test
test: $(noinst_PROGRAMS)
	@db=$$(mktemp --suffix=.db); \
	trap 'rm -f "$$db" "$$db-wal" "$$db-shm" \
	  "$$db.leader"* "$$db.follower"*' EXIT; \
	ZDB_MODULE=$(abs_top_builddir)/zdb_sqlite/src/.libs/libZdbSL.so \
	ZDB_CONNECT="$$db" \
	ZDBSL_INSPECT=$(abs_top_builddir)/zdb_sqlite/src/.libs/libZdbSLInspect.so \
	prove -j1 ./zdbsltest ./zdbsagatest ./zdbslrepltest \
	  ./zdbslconfigtest \
	  ./zdbslinspecttest ./zdbslschematest ./zdbslplantest
