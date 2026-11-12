METASOURCES = AUTO
AM_CPPFLAGS = -I$(top_srcdir)/zu/src -I$(top_srcdir)/zm/src \
	-I$(top_srcdir)/zt/src @Z_CPPFLAGS@
AM_CXXFLAGS = @Z_CXXFLAGS@
AM_LDFLAGS = @Z_LDFLAGS@ @Z_SO_LDFLAGS@
pkginclude_HEADERS = ZxdpLib.hh Zxdp.hh ZxdpAlloc.hh
noinst_HEADERS = ZxdpRing.hh ZxdpUMem.hh ZxdpBPF.hh ZxdpRetire.hh
lib_LTLIBRARIES = libZxdp.la
libZxdp_la_CPPFLAGS = $(AM_CPPFLAGS) -DZXDP_EXPORTS
libZxdp_la_SOURCES = ZxdpLib.cc Zxdp.cc ZxdpAlloc.cc ZxdpUMem.cc ZxdpBPF.cc \
	ZxdpRetire.cc
libZxdp_la_LIBADD = $(top_builddir)/zt/src/libZt.la \
	$(top_builddir)/zm/src/libZm.la $(top_builddir)/zu/src/libZu.la \
	@Z_ZT_LIBS@ @Z_MT_LIBS@ @Z_LIBS@
