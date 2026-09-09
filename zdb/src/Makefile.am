METASOURCES = AUTO
AM_CPPFLAGS = -I$(top_srcdir)/zu/src -I$(top_srcdir)/zm/src \
	-I$(top_srcdir)/zt/src -I$(top_srcdir)/ze/src -I$(top_srcdir)/zf/src -I$(top_srcdir)/zi/src \
	-I$(top_srcdir)/ztls/src -I$(top_srcdir)/zfb/src \
	-I$(top_srcdir)/zv/src \
	-DZDB_EXPORTS @Z_CPPFLAGS@
AM_CXXFLAGS = @Z_CXXFLAGS@
AM_LDFLAGS = @Z_LDFLAGS@ @Z_SO_LDFLAGS@
FBS = zdb_.fbs zdb_saga.fbs zdb_saga_step.fbs zdb_saga_type.fbs ztc_db.fbs
FBS_H = ${FBS:%.fbs=%_fbs.h}
${FBS_H}: %_fbs.h: fbs/%.fbs $(top_builddir)/scripts/fbscodegen
	$(top_builddir)/scripts/fbscodegen -b --schema --bfbs-gen-embed -c --cpp-std c++17 --cpp-field-case-style lower -I fbs -I $(top_srcdir)/zfb/src/fbs $<
BUILT_SOURCES = ${FBS_H}
CLEANFILES = ${FBS_H}
pkginclude_HEADERS = ZdbLib.hh \
	ZdbTypes.hh ZdbBuf.hh ZdbMsg.hh ZdbStore.hh \
	ZdbSaga.hh Zdb.hh ZdbMemStore.hh \
	ZtcDBTable.hh ZtcDBHost.hh ZtcDB.hh \
	${FBS_H}
#fbsdir = $(datarootdir)/${PACKAGE}/fbs
#dist_fbs_DATA = fbs/telemetry.fbs
lib_LTLIBRARIES = libZdb.la libZdbMem.la
libZdb_la_CXXFLAGS = $(AM_CXXFLAGS) @Z_VISIBILITY_CXXFLAGS@
libZdb_la_SOURCES = ZdbLib.cc ZdbTypes.cc ZdbSaga.cc Zdb.cc \
	ZtcDB.cc ZtcDBHost.cc ZtcDBTable.cc
libZdb_la_LIBADD = \
	$(top_builddir)/zv/src/libZv.la \
	$(top_builddir)/zfb/src/libZfb.la \
	$(top_builddir)/zi/src/libZi.la $(top_builddir)/zf/src/libZf.la $(top_builddir)/ze/src/libZe.la \
	$(top_builddir)/zt/src/libZt.la $(top_builddir)/zm/src/libZm.la \
	$(top_builddir)/zu/src/libZu.la \
	@FBS_LIBS@ @Z_IO_LIBS@ @Z_ZT_LIBS@ @Z_MT_LIBS@ @Z_LIBS@
libZdbMem_la_SOURCES = ZdbMemStore.cc
libZdbMem_la_CXXFLAGS = $(AM_CXXFLAGS) @Z_VISIBILITY_CXXFLAGS@
libZdbMem_la_LIBADD = \
	$(top_builddir)/zv/src/libZv.la \
	$(top_builddir)/zfb/src/libZfb.la \
	$(top_builddir)/zi/src/libZi.la $(top_builddir)/zf/src/libZf.la $(top_builddir)/ze/src/libZe.la \
	$(top_builddir)/zt/src/libZt.la $(top_builddir)/zm/src/libZm.la \
	$(top_builddir)/zu/src/libZu.la \
	@FBS_LIBS@ @Z_IO_LIBS@ @Z_ZT_LIBS@ @Z_MT_LIBS@ @Z_LIBS@
