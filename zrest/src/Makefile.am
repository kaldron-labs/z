METASOURCES = AUTO
AM_CPPFLAGS = -I$(top_srcdir)/zu/src -I$(top_srcdir)/zm/src \
	-I$(top_srcdir)/zt/src -I$(top_srcdir)/ze/src -I${top_srcdir}/zi/src \
	-I$(top_srcdir)/ztls/src -I$(top_srcdir)/zhttp/src \
	-I$(top_srcdir)/zv/src \
	@Z_CPPFLAGS@
AM_CXXFLAGS = @Z_CXXFLAGS@
AM_LDFLAGS = @Z_LDFLAGS@ @Z_SO_LDFLAGS@
pkginclude_HEADERS = ZrestLib.hh Zrest.hh
lib_LTLIBRARIES = libZrest.la
libZrest_la_CPPFLAGS = $(AM_CPPFLAGS) -DZREST_EXPORTS 
libZrest_la_SOURCES = ZrestLib.cc
libZrest_la_LIBADD = \
	$(top_builddir)/zv/src/libZv.la \
	$(top_builddir)/zhttp/src/libZhttp.la \
	$(top_builddir)/ztls/src/libZtls.la \
	$(top_builddir)/zi/src/libZi.la \
	$(top_builddir)/ze/src/libZe.la \
	$(top_builddir)/zt/src/libZt.la \
	$(top_builddir)/zm/src/libZm.la \
	$(top_builddir)/zu/src/libZu.la \
	@MBEDTLS_LIBS@ @Z_IO_LIBS@ @Z_ZT_LIBS@ @Z_MT_LIBS@ @Z_LIBS@
bin_PROGRAMS = zrest
zrest_LDADD = libZrest.la $(libZrest_la_LDADD)
zrest_SOURCES = zrest.cc
