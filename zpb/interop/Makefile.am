METASOURCES = AUTO
AM_CPPFLAGS = -I$(top_srcdir)/zu/src -I$(top_srcdir)/zm/src \
	-I$(top_srcdir)/zt/src -I$(top_srcdir)/ze/src -I$(top_srcdir)/zf/src \
	-I$(top_srcdir)/zi/src -I$(top_srcdir)/zpb/src @Z_CPPFLAGS@ @PB_INTEROP_CFLAGS@
AM_CXXFLAGS = @Z_CXXFLAGS@
AM_LDFLAGS = @Z_LDFLAGS@
LDADD = $(top_builddir)/zpb/src/libZpb.la \
	$(top_builddir)/zi/src/libZi.la $(top_builddir)/zf/src/libZf.la \
	$(top_builddir)/ze/src/libZe.la $(top_builddir)/zt/src/libZt.la \
	$(top_builddir)/zm/src/libZm.la $(top_builddir)/zu/src/libZu.la \
	@Z_IO_LIBS@ @Z_ZT_LIBS@ @Z_MT_LIBS@ @Z_LIBS@ @PB_INTEROP_LIBS@
noinst_PROGRAMS =
if HAVE_PB_INTEROP
noinst_PROGRAMS += zpbinterop
PB_INTEROP_TEST = zpbinterop
endif
zpbinterop_SOURCES = zpbinterop.cc
nodist_zpbinterop_SOURCES = zpb-gen/interop.cc zpb-gen/interop.hh \
	zpb-gen/zprotoc_values.cc zpb-gen/zprotoc_values.hh \
	zpb-gen/zprotoc_address.cc zpb-gen/zprotoc_address.hh \
	zpb-gen/zprotoc_values_row.cc zpb-gen/zprotoc_values_row.hh
zpbinterop_CPPFLAGS = $(AM_CPPFLAGS) -I$(builddir)/zpb-gen \
	-DZPB_INTEROP_PROTO='"$(abs_srcdir)/zpbinterop_generated.proto"' \
	-DZPB_VALUES_PROTO='"$(abs_top_srcdir)/zpb/itest/zprotoc_values.proto"' \
	-DZPB_ADDRESS_PROTO='"$(abs_top_srcdir)/zpb/itest/zprotoc_address.proto"' \
	-DZPB_VALUES_ROW_PROTO='"$(abs_top_srcdir)/zpb/itest/zprotoc_values_row.proto"'
zpbinterop-zpbinterop.$(OBJEXT): zpb-gen/interop.hh \
	zpb-gen/zprotoc_values_row.hh
zpb-gen/interop.hh zpb-gen/interop.cc &: \
	$(srcdir)/zpbinterop_generated.proto $(top_builddir)/zpb/src/zprotoc$(EXEEXT)
	$(AM_V_GEN)$(MKDIR_P) zpb-gen
	$(AM_V_at)$(top_builddir)/zpb/src/zprotoc \
		$(srcdir)/zpbinterop_generated.proto -o zpb-gen/interop
zpb-gen/zprotoc_values.hh zpb-gen/zprotoc_values.cc &: \
	$(top_srcdir)/zpb/itest/zprotoc_values.proto \
	$(top_builddir)/zpb/src/zprotoc$(EXEEXT)
	$(AM_V_GEN)$(MKDIR_P) zpb-gen
	$(AM_V_at)$(top_builddir)/zpb/src/zprotoc \
		$(top_srcdir)/zpb/itest/zprotoc_values.proto \
		-o zpb-gen/zprotoc_values
zpb-gen/zprotoc_address.hh zpb-gen/zprotoc_address.cc &: \
	$(top_srcdir)/zpb/itest/zprotoc_address.proto \
	$(top_builddir)/zpb/src/zprotoc$(EXEEXT)
	$(AM_V_GEN)$(MKDIR_P) zpb-gen
	$(AM_V_at)$(top_builddir)/zpb/src/zprotoc \
		$(top_srcdir)/zpb/itest/zprotoc_address.proto \
		-o zpb-gen/zprotoc_address
zpb-gen/zprotoc_values_row.hh zpb-gen/zprotoc_values_row.cc &: \
	$(top_srcdir)/zpb/itest/zprotoc_values_row.proto \
	zpb-gen/zprotoc_values.hh zpb-gen/zprotoc_address.hh \
	$(top_builddir)/zpb/src/zprotoc$(EXEEXT)
	$(AM_V_GEN)$(MKDIR_P) zpb-gen
	$(AM_V_at)$(top_builddir)/zpb/src/zprotoc -I $(top_srcdir)/zpb/itest \
		$(top_srcdir)/zpb/itest/zprotoc_values_row.proto \
		-o zpb-gen/zprotoc_values_row
CLEANFILES = zpb-gen/interop.hh zpb-gen/interop.cc \
	zpb-gen/zprotoc_values.hh zpb-gen/zprotoc_values.cc \
	zpb-gen/zprotoc_address.hh zpb-gen/zprotoc_address.cc \
	zpb-gen/zprotoc_values_row.hh zpb-gen/zprotoc_values_row.cc
zpbinterop_CXXFLAGS = @Z_CXXFLAGS@ @PB_INTEROP_CFLAGS@
zpbinterop_LDADD = $(top_builddir)/zpb/src/libZpb.la \
	$(top_builddir)/zi/src/libZi.la \
	$(top_builddir)/zf/src/libZf.la $(top_builddir)/ze/src/libZe.la \
	$(top_builddir)/zt/src/libZt.la $(top_builddir)/zm/src/libZm.la \
	$(top_builddir)/zu/src/libZu.la \
	@Z_IO_LIBS@ @Z_ZT_LIBS@ @Z_MT_LIBS@ @Z_LIBS@ @PB_INTEROP_LIBS@
EXTRA_DIST = README.md zpb.md zpbinterop_generated.proto zpbinterop.valgrind.supp
.PHONY: test
if HAVE_PB_INTEROP
test: zpbinterop
	prove zpbinterop
else
test:
	@:
endif
