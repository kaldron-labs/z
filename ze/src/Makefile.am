METASOURCES = AUTO
AM_CPPFLAGS = -I$(top_srcdir)/zu/src -I$(top_srcdir)/zm/src \
	-I$(top_srcdir)/zt/src \
	-DZE_EXPORTS @Z_CPPFLAGS@
AM_CXXFLAGS = @Z_CXXFLAGS@
AM_LDFLAGS = @Z_LDFLAGS@ @Z_SO_LDFLAGS@
pkginclude_HEADERS = ZeLib.hh ZePlatform.hh
lib_LTLIBRARIES = libZe.la
libZe_la_CXXFLAGS = $(AM_CXXFLAGS) @Z_VISIBILITY_CXXFLAGS@
libZe_la_SOURCES = ZeLib.cc ZePlatform.cc
libZe_la_LIBADD = $(top_builddir)/zt/src/libZt.la \
	$(top_builddir)/zm/src/libZm.la \
	$(top_builddir)/zu/src/libZu.la \
	@Z_IO_LIBS@ @Z_ZT_LIBS@ @Z_MT_LIBS@ @Z_LIBS@
