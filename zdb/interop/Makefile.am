METASOURCES = AUTO
AM_CPPFLAGS = -I$(top_srcdir)/zu/src -I$(top_srcdir)/zm/src \
	-I$(top_srcdir)/zt/src -I$(top_srcdir)/ze/src -I$(top_srcdir)/zf/src \
	-I$(top_srcdir)/zi/src -I$(top_srcdir)/zdb/src @Z_CPPFLAGS@ \
	@FB_INTEROP_CFLAGS@
AM_CXXFLAGS = @Z_CXXFLAGS@
AM_LDFLAGS = @Z_LDFLAGS@
LDADD = $(top_builddir)/zi/src/libZi.la $(top_builddir)/zf/src/libZf.la \
	$(top_builddir)/ze/src/libZe.la $(top_builddir)/zt/src/libZt.la \
	$(top_builddir)/zm/src/libZm.la $(top_builddir)/zu/src/libZu.la \
	@Z_IO_LIBS@ @Z_ZT_LIBS@ @Z_MT_LIBS@ @Z_LIBS@ \
	@FB_INTEROP_LIBS@
noinst_PROGRAMS = zdbmsginterop
.PHONY: fb-deps
fb-deps:
	pkg-config --exists 'flatbuffers >= 24'
$(am_zdbmsginterop_OBJECTS): | fb-deps
zdbmsginterop_SOURCES = zdbmsginterop.cc
$(am_zdbmsginterop_OBJECTS): dbmsg_generated.h
dbmsg_generated.h: $(srcdir)/dbmsg.fbs
	flatc --cpp --cpp-std c++17 -o . $<
CLEANFILES = dbmsg_generated.h
EXTRA_DIST = dbmsg.fbs README.md results.txt

.PHONY: test
test: all
	prove ./zdbmsginterop
