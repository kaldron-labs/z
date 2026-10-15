METASOURCES = AUTO
AM_CPPFLAGS = -I$(top_srcdir)/zu/src -I$(top_srcdir)/zm/src \
	-I$(top_srcdir)/zt/src -I$(top_srcdir)/ze/src -I$(top_srcdir)/zf/src \
	-I$(top_srcdir)/zi/src -I$(top_srcdir)/zfb/src @Z_CPPFLAGS@ @FB_INTEROP_CFLAGS@
AM_CXXFLAGS = @Z_CXXFLAGS@
AM_LDFLAGS = @Z_LDFLAGS@
LDADD = $(top_builddir)/zfb/src/libZfb.la \
	$(top_builddir)/zi/src/libZi.la $(top_builddir)/zf/src/libZf.la \
	$(top_builddir)/ze/src/libZe.la $(top_builddir)/zt/src/libZt.la \
	$(top_builddir)/zm/src/libZm.la $(top_builddir)/zu/src/libZu.la \
	@Z_IO_LIBS@ @Z_ZT_LIBS@ @Z_MT_LIBS@ @Z_LIBS@ @FB_INTEROP_LIBS@
noinst_PROGRAMS = zfbiobench zfbinterop zfbmapinterop zfbmapbench
zfbiobench_SOURCES = zfbiobench.cc iobaseline.hh
.PHONY: fb-deps
fb-deps:
	pkg-config --exists 'flatbuffers >= 24'
$(am_zfbiobench_OBJECTS): | fb-deps
$(am_zfbiobench_OBJECTS): bench_generated.h
bench_generated.h: $(srcdir)/bench.fbs
	flatc --cpp --cpp-std c++17 -o . $<
CLEANFILES = bench_generated.h maps_generated.h

zfbinterop_SOURCES = zfbinterop.cc
$(am_zfbinterop_OBJECTS): | fb-deps

zfbmapinterop_SOURCES = zfbmapinterop.cc
$(am_zfbmapinterop_OBJECTS): | fb-deps
$(am_zfbmapinterop_OBJECTS): maps_generated.h
maps_generated.h: $(srcdir)/maps.fbs
	flatc --cpp --cpp-std c++17 -o . $<
zfbmapbench_SOURCES = zfbmapbench.cc
$(am_zfbmapbench_OBJECTS): | fb-deps
$(am_zfbmapbench_OBJECTS): maps_generated.h

EXTRA_DIST = README.md bench.fbs maps.fbs trace-growth.py contracts.md codec-history.md
.PHONY: test
test: $(noinst_PROGRAMS)
	prove zfbinterop zfbmapinterop
