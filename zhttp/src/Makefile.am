METASOURCES = AUTO
AM_CPPFLAGS = -I$(top_srcdir)/zu/src -I$(top_srcdir)/zm/src \
	-I$(top_srcdir)/zt/src -I$(top_srcdir)/ze/src -I${top_srcdir}/zi/src \
	-I$(top_srcdir)/ztls/src \
	-I$(top_srcdir)/zquic/src \
	@Z_CPPFLAGS@
AM_CXXFLAGS = @Z_CXXFLAGS@
AM_LDFLAGS = @Z_LDFLAGS@ @Z_SO_LDFLAGS@
pkginclude_HEADERS = ZhttpLib.hh Zhttp.hh Zhttp3.hh ZhttpHPack.hh ZhttpQPack.hh
lib_LTLIBRARIES = libZhttp.la
libZhttp_la_CPPFLAGS = $(AM_CPPFLAGS) -DZHTTP_EXPORTS 
libZhttp_la_SOURCES = ZhttpLib.cc Zhttp3.cc ZhttpHPack.cc ZhttpQPack.cc
libZhttp_la_LIBADD = \
	$(top_builddir)/zquic/src/libZquic.la \
	$(top_builddir)/ztls/src/libZtls.la \
	$(top_builddir)/zi/src/libZi.la \
	$(top_builddir)/ze/src/libZe.la \
	$(top_builddir)/zt/src/libZt.la \
	$(top_builddir)/zm/src/libZm.la \
	$(top_builddir)/zu/src/libZu.la \
	@PTLS_LIBS@ @Z_IO_LIBS@ @Z_ZT_LIBS@ @Z_MT_LIBS@ @Z_LIBS@
bin_PROGRAMS = zhttp
zhttp_LDADD = libZhttp.la $(libZhttp_la_LDADD)
zhttp_SOURCES = zhttp.cc
