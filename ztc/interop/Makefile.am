METASOURCES = AUTO
AM_CPPFLAGS = -I$(top_srcdir)/zu/src -I$(top_srcdir)/zm/src \
	-I$(top_srcdir)/zt/src -I$(top_srcdir)/ze/src -I$(top_srcdir)/zf/src \
	-I$(top_srcdir)/zi/src -I$(top_srcdir)/zfb/src -I$(top_srcdir)/zv/src -I$(top_srcdir)/zdb/src \
	-I$(top_srcdir)/ztc/src @Z_CPPFLAGS@ \
	@FB_INTEROP_CFLAGS@
AM_CXXFLAGS = @Z_CXXFLAGS@
AM_LDFLAGS = @Z_LDFLAGS@
LDADD = $(top_builddir)/ztc/src/libZtc.la $(top_builddir)/zdb/src/libZdb.la \
	$(top_builddir)/zv/src/libZv.la $(top_builddir)/zfb/src/libZfb.la $(top_builddir)/zi/src/libZi.la \
	$(top_builddir)/zf/src/libZf.la $(top_builddir)/ze/src/libZe.la \
	$(top_builddir)/zt/src/libZt.la $(top_builddir)/zm/src/libZm.la \
	$(top_builddir)/zu/src/libZu.la \
	@Z_IO_LIBS@ @Z_ZT_LIBS@ @Z_MT_LIBS@ @Z_LIBS@ \
	@FB_INTEROP_LIBS@
noinst_PROGRAMS = ztcmsginterop
ztcmsginterop_SOURCES = ztcmsginterop.cc
.PHONY: fb-deps
fb-deps:
	pkg-config --exists 'flatbuffers >= 24'
$(am_ztcmsginterop_OBJECTS): telemetry_generated.h | fb-deps
telemetry_generated.h: $(srcdir)/telemetry.fbs
	flatc --cpp --cpp-std c++17 -o . $<
CLEANFILES = telemetry_generated.h
EXTRA_DIST = telemetry.fbs README.md

.PHONY: test
test: all
	prove ./ztcmsginterop
