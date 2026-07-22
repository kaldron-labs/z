METASOURCES = AUTO
AM_CPPFLAGS = \
	-I$(top_srcdir)/zu/src -I$(top_srcdir)/zm/src \
	-I$(top_srcdir)/zt/src -I$(top_srcdir)/ze/src -I$(top_srcdir)/zf/src \
	-I$(top_srcdir)/zi/src -I$(top_srcdir)/ztls/src \
	-I$(top_srcdir)/zfb/src -I$(top_srcdir)/zrl/src \
	-I$(top_srcdir)/zv/src -I$(top_srcdir)/zdb/src \
	-I$(top_srcdir)/zum/src -I$(top_srcdir)/zcmd/src \
	@Z_CPPFLAGS@
AM_CXXFLAGS = @Z_CXXFLAGS@
AM_LDFLAGS = @Z_LDFLAGS@ @Z_SO_LDFLAGS@
bin_PROGRAMS = zproxy
zproxy_LDADD = \
	$(top_builddir)/zcmd/src/libZcmd.la $(top_builddir)/zum/src/libZum.la \
	$(top_builddir)/zdb/src/libZdb.la $(top_builddir)/zv/src/libZv.la \
	$(top_builddir)/zrl/src/libZrl.la $(top_builddir)/zfb/src/libZfb.la \
	$(top_builddir)/ztls/src/libZtls.la $(top_builddir)/zi/src/libZi.la \
	$(top_builddir)/zf/src/libZf.la $(top_builddir)/ze/src/libZe.la $(top_builddir)/zt/src/libZt.la \
	$(top_builddir)/zm/src/libZm.la $(top_builddir)/zu/src/libZu.la \
	@Z_TERM_LIBS@ @Z_IO_LIBS@ @Z_ZT_LIBS@ @Z_MT_LIBS@ @Z_LIBS@
zproxy_SOURCES = zproxy.cc
