METASOURCES = AUTO
.cc.s:
	$(CXXCOMPILE) -c -S $<
%.i.cc: %.cc
	$(CXXCOMPILE) -C -E $< -o $@
AM_CPPFLAGS = -I$(top_srcdir)/zu/src -I$(top_srcdir)/zm/src \
	-I$(top_srcdir)/zt/src -I$(top_srcdir)/ze/src -I$(top_srcdir)/zf/src \
	-I$(top_srcdir)/zi/src @Z_CPPFLAGS@
AM_CXXFLAGS = @Z_CXXFLAGS@
AM_LDFLAGS = @Z_LDFLAGS@
pkginclude_HEADERS = ZpbLib.hh Zpb.hh ZpbReflect.hh
lib_LTLIBRARIES = libZpb.la
libZpb_la_CPPFLAGS = $(AM_CPPFLAGS) -DZPB_EXPORTS
libZpb_la_LDFLAGS = @Z_SO_LDFLAGS@
libZpb_la_SOURCES = ZpbLib.cc Zpb.cc ZpbReflect.cc
libZpb_la_LIBADD = $(top_builddir)/zi/src/libZi.la \
	$(top_builddir)/zf/src/libZf.la $(top_builddir)/ze/src/libZe.la \
	$(top_builddir)/zt/src/libZt.la $(top_builddir)/zm/src/libZm.la \
	$(top_builddir)/zu/src/libZu.la \
	@Z_IO_LIBS@ @Z_ZT_LIBS@ @Z_MT_LIBS@ @Z_LIBS@

bin_PROGRAMS = zprotoc
zprotoc_SOURCES = zprotoc.cc
zprotoc_LDFLAGS = @Z_LDFLAGS@
zprotoc_LDADD = libZpb.la $(top_builddir)/zi/src/libZi.la \
	$(top_builddir)/zf/src/libZf.la $(top_builddir)/ze/src/libZe.la \
	$(top_builddir)/zt/src/libZt.la $(top_builddir)/zm/src/libZm.la \
	$(top_builddir)/zu/src/libZu.la \
	@Z_IO_LIBS@ @Z_ZT_LIBS@ @Z_MT_LIBS@ @Z_LIBS@
