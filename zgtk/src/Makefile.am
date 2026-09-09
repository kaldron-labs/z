METASOURCES = AUTO
AM_CPPFLAGS = -I$(top_srcdir)/zu/src -I$(top_srcdir)/zm/src \
	-I$(top_srcdir)/zt/src -I$(top_srcdir)/ze/src -I$(top_srcdir)/zf/src -I$(top_srcdir)/zi/src \
	-I$(top_srcdir)/ztls/src -I$(top_srcdir)/zfb/src \
	-I$(top_srcdir)/zv/src \
	@Z_CPPFLAGS@
AM_CFLAGS = @GTK_CFLAGS@
AM_CXXFLAGS = @Z_CXXFLAGS@ @GTK_CFLAGS@
AM_LDFLAGS = @Z_LDFLAGS@
pkginclude_HEADERS = ZGtkLib.hh \
	ZGtkApp.hh ZGtkCallback.hh ZGtkValue.hh \
	ZGtkTreeModel.hh
lib_LTLIBRARIES = libZGtk.la
libZGtk_la_CPPFLAGS = $(AM_CPPFLAGS) -DZGTK_EXPORTS 
libZGtk_la_CXXFLAGS = $(AM_CXXFLAGS) @Z_VISIBILITY_CXXFLAGS@
libZGtk_la_LDFLAGS = @Z_SO_LDFLAGS@
libZGtk_la_SOURCES = ZGtkLib.cc ZGtkApp.cc
libZGtk_la_LIBADD = $(top_builddir)/zv/src/libZv.la \
	$(top_builddir)/zfb/src/libZfb.la $(top_builddir)/ztls/src/libZtls.la \
	$(top_builddir)/zi/src/libZi.la $(top_builddir)/zf/src/libZf.la $(top_builddir)/ze/src/libZe.la \
	$(top_builddir)/zt/src/libZt.la $(top_builddir)/zm/src/libZm.la \
	$(top_builddir)/zu/src/libZu.la \
	@GTK_LIBS@ @FBS_LIBS@ @PTLS_LIBS@ \
	@Z_IO_LIBS@ @Z_ZT_LIBS@ @Z_MT_LIBS@ @Z_LIBS@
