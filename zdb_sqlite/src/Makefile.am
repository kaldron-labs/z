METASOURCES = AUTO
AM_CPPFLAGS = -I$(top_srcdir)/zu/src -I$(top_srcdir)/zm/src \
	-I$(top_srcdir)/zt/src -I$(top_srcdir)/ze/src \
	-I$(top_srcdir)/zf/src -I$(top_srcdir)/zi/src \
	-I$(top_srcdir)/zfb/src -I$(top_srcdir)/zv/src \
	-I$(top_srcdir)/zdb/src -DZDB_SL_EXPORTS \
	@SQLITE_CFLAGS@ @Z_CPPFLAGS@
AM_CXXFLAGS = @Z_CXXFLAGS@
AM_LDFLAGS = @Z_LDFLAGS@ @Z_SO_LDFLAGS@
pkginclude_HEADERS = ZdbSLLib.hh ZdbSL.hh
noinst_HEADERS = ZdbSLCodec.hh
lib_LTLIBRARIES = libZdbSL.la libZdbSLInspect.la
libZdbSL_la_CXXFLAGS = $(AM_CXXFLAGS) @Z_LIB_CXXFLAGS@
libZdbSL_la_SOURCES = ZdbSLLib.cc ZdbSLCodec.cc ZdbSL.cc
libZdbSLInspect_la_CXXFLAGS = $(AM_CXXFLAGS) @Z_LIB_CXXFLAGS@
libZdbSLInspect_la_SOURCES = ZdbSLCodec.cc ZdbSLInspect.cc
ZDBSL_BACKEND_LIBS = \
	$(top_builddir)/zdb/src/libZdb.la \
	$(top_builddir)/zv/src/libZv.la $(top_builddir)/zfb/src/libZfb.la \
	$(top_builddir)/zi/src/libZi.la $(top_builddir)/zf/src/libZf.la \
	$(top_builddir)/ze/src/libZe.la $(top_builddir)/zt/src/libZt.la \
	$(top_builddir)/zm/src/libZm.la $(top_builddir)/zu/src/libZu.la \
	@SQLITE_LIBS@ @FBS_LIBS@ @Z_IO_LIBS@ @Z_ZT_LIBS@ @Z_MT_LIBS@ @Z_LIBS@
ZDBSL_INSPECT_LIBS = \
	$(top_builddir)/zm/src/libZm.la $(top_builddir)/zu/src/libZu.la \
	@HWLOC_LIBS@
libZdbSL_la_LIBADD = $(ZDBSL_BACKEND_LIBS)
libZdbSLInspect_la_LIBADD = $(ZDBSL_INSPECT_LIBS)
