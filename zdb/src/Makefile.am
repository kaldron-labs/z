METASOURCES = AUTO
AM_CPPFLAGS = -I$(top_srcdir)/zu/src -I$(top_srcdir)/zm/src \
	-I$(top_srcdir)/zt/src -I$(top_srcdir)/ze/src -I$(top_srcdir)/zf/src -I$(top_srcdir)/zi/src \
	-I$(top_srcdir)/ztls/src \
	-I$(top_srcdir)/zv/src \
	-DZDB_EXPORTS @Z_CPPFLAGS@
AM_CXXFLAGS = @Z_CXXFLAGS@
AM_LDFLAGS = @Z_LDFLAGS@ @Z_SO_LDFLAGS@
pkginclude_HEADERS = ZdbLib.hh \
	ZdbTypes.hh ZdbBuf.hh ZdbMsg.hh ZdbStore.hh \
	ZdbSaga.hh Zdb.hh ZdbMemStore.hh \
	ZtcDBTable.hh ZtcDBHost.hh ZtcDB.hh
lib_LTLIBRARIES = libZdb.la libZdbMem.la
libZdb_la_SOURCES = ZdbLib.cc ZdbTypes.cc ZdbSaga.cc Zdb.cc \
	ZtcDB.cc ZtcDBHost.cc ZtcDBTable.cc
libZdb_la_LIBADD = \
	$(top_builddir)/zv/src/libZv.la \
	$(top_builddir)/zi/src/libZi.la $(top_builddir)/zf/src/libZf.la $(top_builddir)/ze/src/libZe.la \
	$(top_builddir)/zt/src/libZt.la $(top_builddir)/zm/src/libZm.la \
	$(top_builddir)/zu/src/libZu.la \
	@Z_IO_LIBS@ @Z_ZT_LIBS@ @Z_MT_LIBS@ @Z_LIBS@
libZdbMem_la_SOURCES = ZdbMemStore.cc
libZdbMem_la_LIBADD = \
	$(top_builddir)/zv/src/libZv.la \
	$(top_builddir)/zi/src/libZi.la $(top_builddir)/zf/src/libZf.la $(top_builddir)/ze/src/libZe.la \
	$(top_builddir)/zt/src/libZt.la $(top_builddir)/zm/src/libZm.la \
	$(top_builddir)/zu/src/libZu.la \
	@Z_IO_LIBS@ @Z_ZT_LIBS@ @Z_MT_LIBS@ @Z_LIBS@
