METASOURCES = AUTO
.cc.s:
	$(CXXCOMPILE) -c -S $<
%.i.cc: %.cc
	$(CXXCOMPILE) -C -E $< -o $@
AM_CPPFLAGS = -I$(top_srcdir)/zu/src -I$(top_srcdir)/zm/src \
	-I$(top_srcdir)/zt/src -I$(top_srcdir)/ze/src -I$(top_srcdir)/zf/src \
	-I$(top_srcdir)/zi/src -I$(top_srcdir)/ztls/src \
	-I$(top_srcdir)/ztcp/src -I$(top_srcdir)/zquic/src \
	-I$(top_srcdir)/zhttp/src -I$(top_srcdir)/zhttp/util \
	@Z_CPPFLAGS@
AM_CXXFLAGS = @Z_CXXFLAGS@
AM_LDFLAGS = @Z_LDFLAGS@
ZHTTP_LDADD = $(top_builddir)/zhttp/src/libZhttp.la \
	$(top_builddir)/zquic/src/libZquic.la \
	$(top_builddir)/ztls/src/libZtls.la \
	$(top_builddir)/zi/src/libZi.la \
	$(top_builddir)/zf/src/libZf.la $(top_builddir)/ze/src/libZe.la \
	$(top_builddir)/zt/src/libZt.la $(top_builddir)/zm/src/libZm.la \
	$(top_builddir)/zu/src/libZu.la \
	@PTLS_LIBS@ @Z_IO_LIBS@ @Z_ZT_LIBS@ @Z_MT_LIBS@ @Z_LIBS@
noinst_LTLIBRARIES = libzhttputil.la
noinst_PROGRAMS = zhttp zhttpd
noinst_HEADERS = zhttpput.hh zhttpd.hh
dist_noinst_DATA = zhttpboundary.md
libzhttputil_la_SOURCES = zhttpd.cc zhttpdutil.cc
libzhttputil_la_LIBADD = $(ZHTTP_LDADD)
zhttp_SOURCES = zhttp.cc
zhttp_LDADD = $(ZHTTP_LDADD)
zhttpd_SOURCES = zhttpdmain.cc
zhttpd_LDADD = libzhttputil.la $(ZHTTP_LDADD)
