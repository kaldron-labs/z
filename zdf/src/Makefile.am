METASOURCES = AUTO
.cc.s:
	$(CXXCOMPILE) -c -S $<
%.i.cc: %.cc
	$(CXXCOMPILE) -C -E $< -o $@
AM_CPPFLAGS = -I$(top_srcdir)/zu/src -I$(top_srcdir)/zm/src \
	-I$(top_srcdir)/zt/src -I$(top_srcdir)/ze/src -I$(top_srcdir)/zi/src \
	-I$(top_srcdir)/ztls/src -I$(top_srcdir)/zfb/src \
	-I$(top_srcdir)/zv/src -I$(top_srcdir)/zdb/src \
	-DZDF_EXPORTS @Z_CPPFLAGS@
AM_CXXFLAGS = @Z_CXXFLAGS@
AM_LDFLAGS = @Z_LDFLAGS@
FBS = zdf_blk_data.fbs zdf_blk_fixed.fbs zdf_blk_float.fbs \
	zdf_series_fixed.fbs zdf_series_float.fbs
FBS_H = ${FBS:%.fbs=%_fbs.h}
${FBS_H}: %_fbs.h: fbs/%.fbs
	flatc -b --schema --bfbs-gen-embed -c --cpp-std c++17 --cpp-field-case-style lower -I fbs -I $(top_srcdir)/zfb/src/fbs $<
	mv ${@:%_fbs.h=%_generated.h} $@
	mv ${@:%_fbs.h=%_bfbs_generated.h} ${@:%_fbs.h=%_bfbs.h}
	clang-format -i $@ ${@:%_fbs.h=%_bfbs.h}
	perl -pi -e 's|^#include "(\w+)_bfbs_generated\.h"|#include "$${1}_bfbs.h"|; s|^#include "(\w+)_generated\.h"|#include <zlib/$${1}_fbs.h>|; s|^#include "flatbuffers/flatbuffers.h"|#include <flatbuffers/flatbuffers.h>|; s/^(#(?:ifndef|define|endif \/\/) )FLATBUFFERS_GENERATED_(\w+?)(?:(FBS_H)|FBS_(BFBS_H))/$${1}$${2}$${3}$${4}/; s/(\w+)BinarySchema/$${1}Schema/;' $@ ${@:%_fbs.h=%_bfbs.h}
BUILT_SOURCES = ${FBS_H}
CLEANFILES = ${FBS_H}
pkginclude_HEADERS = ZdfLib.hh \
	ZdfTypes.hh ZdfCompress.hh ZdfSchema.hh ZdfBlk.hh \
	ZdfStore.hh Zdf.hh ZdfSeries.hh ZdfStats.hh \
	${FBS_H} \
	stats_tree.hh
#fbsdir = $(datarootdir)/${PACKAGE}/fbs
#dist_fbs_DATA = fbs/telemetry.fbs
lib_LTLIBRARIES = libZdf.la
libZdf_la_LDFLAGS = @Z_SO_LDFLAGS@
libZdf_la_SOURCES = ZdfLib.cc ZdfStore.cc
libZdf_la_LIBADD = \
	$(top_builddir)/zdb/src/libZdb.la \
	$(top_builddir)/zv/src/libZv.la \
	$(top_builddir)/zfb/src/libZfb.la \
	$(top_builddir)/ztls/src/libZtls.la \
	$(top_builddir)/zi/src/libZi.la $(top_builddir)/ze/src/libZe.la \
	$(top_builddir)/zt/src/libZt.la $(top_builddir)/zm/src/libZm.la \
	$(top_builddir)/zu/src/libZu.la \
	@FBS_LIBS@ @PTLS_LIBS@ @Z_IO_LIBS@ @Z_ZT_LIBS@ @Z_MT_LIBS@ @Z_LIBS@
