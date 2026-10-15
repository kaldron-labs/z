METASOURCES = AUTO
AM_CPPFLAGS = -I$(top_srcdir)/zu/src -I$(top_srcdir)/zm/src \
	-I$(top_srcdir)/zt/src -I$(top_srcdir)/ze/src \
	-I$(top_srcdir)/zf/src -I$(top_srcdir)/zi/src \
	-I$(top_srcdir)/zi/test -I$(top_srcdir)/zpb/src \
	@Z_CPPFLAGS@
AM_CXXFLAGS = @Z_CXXFLAGS@
AM_LDFLAGS = @Z_LDFLAGS@
LDADD = $(top_builddir)/zi/test/libZiTest.la \
	$(top_builddir)/zpb/src/libZpb.la \
	$(top_builddir)/zi/src/libZi.la \
	$(top_builddir)/zf/src/libZf.la $(top_builddir)/ze/src/libZe.la \
	$(top_builddir)/zt/src/libZt.la $(top_builddir)/zm/src/libZm.la \
	$(top_builddir)/zu/src/libZu.la \
	@Z_IO_LIBS@ @Z_ZT_LIBS@ @Z_MT_LIBS@ @Z_LIBS@
tap_drivers = zprotoctest zprotocproto2
noinst_PROGRAMS = $(tap_drivers)
EXTRA_DIST = README.md
CLEANFILES =
.PHONY: test
test: export ZI_LOGDIR = $(abs_top_builddir)/logs
test: $(noinst_PROGRAMS)
	prove -j1 $(tap_drivers)

noinst_PROGRAMS += zprotocconsumer zprotocimport
tap_drivers += zprotocvalues
noinst_PROGRAMS += zprotocvalues
zprotocvalues_SOURCES = zprotocvalues.cc
nodist_zprotocvalues_SOURCES = zprotoc-out/zprotoc_values.cc \
	zprotoc-out/zprotoc_values.hh zprotoc-out/zprotoc_values_row.cc \
	zprotoc-out/zprotoc_values_row.hh zprotoc-out/zprotoc_address.cc \
	zprotoc-out/zprotoc_address.hh
zprotocvalues_CPPFLAGS = $(AM_CPPFLAGS) -I$(builddir)/zprotoc-out
zprotocvalues-zprotocvalues.$(OBJEXT): zprotoc-out/zprotoc_values_row.hh
zprotoc-out/zprotoc_values.hh zprotoc-out/zprotoc_values.cc &: \
	$(srcdir)/zprotoc_values.proto $(top_builddir)/zpb/src/zprotoc$(EXEEXT)
	$(AM_V_GEN)$(MKDIR_P) zprotoc-out
	$(AM_V_at)$(top_builddir)/zpb/src/zprotoc $(srcdir)/zprotoc_values.proto \
		-o zprotoc-out/zprotoc_values
zprotoc-out/zprotoc_address.hh zprotoc-out/zprotoc_address.cc &: \
	$(srcdir)/zprotoc_address.proto $(top_builddir)/zpb/src/zprotoc$(EXEEXT)
	$(AM_V_GEN)$(MKDIR_P) zprotoc-out
	$(AM_V_at)$(top_builddir)/zpb/src/zprotoc $(srcdir)/zprotoc_address.proto \
		-o zprotoc-out/zprotoc_address
zprotoc-out/zprotoc_values_row.hh zprotoc-out/zprotoc_values_row.cc &: \
	$(srcdir)/zprotoc_values_row.proto zprotoc-out/zprotoc_values.hh \
	zprotoc-out/zprotoc_address.hh \
	$(top_builddir)/zpb/src/zprotoc$(EXEEXT)
	$(AM_V_GEN)$(MKDIR_P) zprotoc-out
	$(AM_V_at)$(top_builddir)/zpb/src/zprotoc -I $(srcdir) \
		$(srcdir)/zprotoc_values_row.proto \
		-o zprotoc-out/zprotoc_values_row
CLEANFILES += zprotoc-out/zprotoc_values.hh zprotoc-out/zprotoc_values.cc \
	zprotoc-out/zprotoc_values_row.hh zprotoc-out/zprotoc_values_row.cc \
	zprotoc-out/zprotoc_address.hh zprotoc-out/zprotoc_address.cc
EXTRA_DIST += zprotoc_values.proto zprotoc_values_row.proto \
	zprotoc_address.proto
zprotoctest_SOURCES = zprotoctest.cc
zprotoctest_DEPENDENCIES = $(LDADD) zprotocconsumer$(EXEEXT) zprotocimport$(EXEEXT)
zprotocconsumer_SOURCES = zprotocconsumer.cc
nodist_zprotocconsumer_SOURCES = zprotoc-out/simple.cc zprotoc-out/simple.hh
zprotocconsumer_CPPFLAGS = $(AM_CPPFLAGS) -I$(builddir)/zprotoc-out
zprotocconsumer-zprotocconsumer.$(OBJEXT): zprotoc-out/simple.hh
zprotoc-out/simple.hh zprotoc-out/simple.cc &: $(srcdir)/zprotoc_simple.proto $(top_builddir)/zpb/src/zprotoc$(EXEEXT)
	$(AM_V_GEN)$(MKDIR_P) zprotoc-out
	$(AM_V_at)$(top_builddir)/zpb/src/zprotoc $(srcdir)/zprotoc_simple.proto -o zprotoc-out/simple --export ZpbAPI
CLEANFILES += zprotoc-out/simple.hh zprotoc-out/simple.cc
EXTRA_DIST += zprotoc_simple.proto
zprotocproto2_SOURCES = zprotocproto2.cc
nodist_zprotocproto2_SOURCES = zprotoc-out/proto2.cc zprotoc-out/proto2.hh
zprotocproto2_CPPFLAGS = $(AM_CPPFLAGS) -I$(builddir)/zprotoc-out
zprotocproto2-zprotocproto2.$(OBJEXT): zprotoc-out/proto2.hh
zprotoc-out/proto2.hh zprotoc-out/proto2.cc &: \
	$(srcdir)/zprotoc_proto2.proto $(top_builddir)/zpb/src/zprotoc$(EXEEXT)
	$(AM_V_GEN)$(MKDIR_P) zprotoc-out
	$(AM_V_at)$(top_builddir)/zpb/src/zprotoc $(srcdir)/zprotoc_proto2.proto \
		-o zprotoc-out/proto2
CLEANFILES += zprotoc-out/proto2.hh zprotoc-out/proto2.cc
EXTRA_DIST += zprotoc_proto2.proto
zprotocimport_SOURCES = zprotocimport.cc
nodist_zprotocimport_SOURCES = zprotoc-out/zprotoc_base.cc \
	zprotoc-out/zprotoc_base.hh zprotoc-out/zprotoc_user.cc \
	zprotoc-out/zprotoc_user.hh
zprotocimport_CPPFLAGS = $(AM_CPPFLAGS) -I$(builddir)/zprotoc-out
zprotocimport-zprotocimport.$(OBJEXT): zprotoc-out/zprotoc_base.hh \
	zprotoc-out/zprotoc_user.hh
zprotoc-out/zprotoc_base.hh zprotoc-out/zprotoc_base.cc &: \
	$(srcdir)/zprotoc_base.proto $(top_builddir)/zpb/src/zprotoc$(EXEEXT)
	$(AM_V_GEN)$(MKDIR_P) zprotoc-out
	$(AM_V_at)$(top_builddir)/zpb/src/zprotoc $(srcdir)/zprotoc_base.proto \
		-o zprotoc-out/zprotoc_base
zprotoc-out/zprotoc_user.hh zprotoc-out/zprotoc_user.cc &: \
	$(srcdir)/zprotoc_import/user.proto zprotoc-out/zprotoc_base.hh \
	$(top_builddir)/zpb/src/zprotoc$(EXEEXT)
	$(AM_V_GEN)$(top_builddir)/zpb/src/zprotoc -I $(srcdir) \
		$(srcdir)/zprotoc_import/user.proto -o zprotoc-out/zprotoc_user
CLEANFILES += zprotoc-out/zprotoc_base.hh zprotoc-out/zprotoc_base.cc \
	zprotoc-out/zprotoc_user.hh zprotoc-out/zprotoc_user.cc
EXTRA_DIST += zprotoc_base.proto zprotoc_import/user.proto
