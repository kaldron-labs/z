METASOURCES = AUTO
AM_CPPFLAGS = -I$(top_srcdir)/zu/src -I$(top_srcdir)/zm/src \
	-I$(top_srcdir)/zt/src -I$(top_srcdir)/ze/src -I$(top_srcdir)/zf/src \
	-I$(top_srcdir)/zi/src -I$(top_srcdir)/zpkt/src @Z_CPPFLAGS@
AM_CXXFLAGS = @Z_CXXFLAGS@
AM_LDFLAGS = @Z_LDFLAGS@
noinst_HEADERS = ZpktAfpPool.hh ZpktAfpNif.hh ZpktAfpCf.hh
noinst_LTLIBRARIES = libZpktAfpCore.la
libZpktAfpCore_la_SOURCES = ZpktAfpPool.cc ZpktAfpNif.cc
lib_LTLIBRARIES = libZpkt_afp.la
libZpkt_afp_la_SOURCES = ZpktAfp.cc
libZpkt_afp_la_LDFLAGS = $(AM_LDFLAGS) -module -avoid-version
libZpkt_afp_la_LIBADD = libZpktAfpCore.la $(top_builddir)/zpkt/src/libZpkt.la \
	$(top_builddir)/zi/src/libZi.la $(top_builddir)/zf/src/libZf.la \
	$(top_builddir)/ze/src/libZe.la $(top_builddir)/zt/src/libZt.la \
	$(top_builddir)/zm/src/libZm.la $(top_builddir)/zu/src/libZu.la \
	@Z_IO_LIBS@ @Z_ZT_LIBS@ @Z_MT_LIBS@ @Z_LIBS@
