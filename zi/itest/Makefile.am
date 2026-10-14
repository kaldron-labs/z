METASOURCES = AUTO
AM_CPPFLAGS = -I$(top_srcdir)/zu/src -I$(top_srcdir)/zm/src \
	-I$(top_srcdir)/zt/src -I$(top_srcdir)/ze/src \
	-I$(top_srcdir)/zf/src -I$(top_srcdir)/zi/src \
	-I$(top_srcdir)/zi/test \
	@Z_CPPFLAGS@
AM_CXXFLAGS = @Z_CXXFLAGS@
AM_LDFLAGS = @Z_LDFLAGS@
LDADD = $(top_builddir)/zi/test/libZiTest.la \
	$(top_builddir)/zi/src/libZi.la \
	$(top_builddir)/zf/src/libZf.la $(top_builddir)/ze/src/libZe.la \
	$(top_builddir)/zt/src/libZt.la $(top_builddir)/zm/src/libZm.la \
	$(top_builddir)/zu/src/libZu.la \
	@Z_IO_LIBS@ @Z_ZT_LIBS@ @Z_MT_LIBS@ @Z_LIBS@

tap_drivers = zringmatrix zitestresidue zheaptune zhashtune zflatctest \
	zprotoctest zprotocproto2
noinst_PROGRAMS = $(tap_drivers) zheapwork zhashwork
zringmatrix_SOURCES = zringmatrix.cc
zitestresidue_SOURCES = zitestresidue.cc
zheaptune_SOURCES = zheaptune.cc zheaptune.hh
zheapwork_SOURCES = zheapwork.cc zheaptune.hh
zhashtune_SOURCES = zhashtune.cc zhashtune.hh
zhashwork_SOURCES = zhashwork.cc zhashtune.hh
EXTRA_DIST = README.md zi_test_residue.py zi_test_residue_test.py \
	zi-test-residue.sh zi-test-residue-test.sh \
	residue/go.mod residue/residue.go residue/residue_unix.go \
	residue/residue_windows.go residue/residue_test.go

.PHONY: ZiRingTest
ZiRingTest:
	$(MAKE) $(AM_MAKEFLAGS) -C $(top_builddir)/zi/test ZiRingTest

.PHONY: test
test: export ZI_LOGDIR = $(abs_top_builddir)/logs
test: $(noinst_PROGRAMS) ZiRingTest
	prove -j1 $(tap_drivers)
	PYTHONDONTWRITEBYTECODE=1 python3 $(srcdir)/zi_test_residue_test.py
	$(SHELL) $(srcdir)/zi-test-residue-test.sh
if HAVE_GO
	cd $(srcdir)/residue && GOCACHE=$(abs_top_builddir)/.go-cache $(GO) test ./...
endif


noinst_PROGRAMS += zifbsave zifbcheck zifbmapcheck zifbmapimport zifbmapref \
	zifbmapexport zifbmapgenerated

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
	$(srcdir)/zprotoc_values.proto $(top_builddir)/zi/src/zprotoc$(EXEEXT)
	$(AM_V_GEN)$(MKDIR_P) zprotoc-out
	$(AM_V_at)$(top_builddir)/zi/src/zprotoc $(srcdir)/zprotoc_values.proto \
		-o zprotoc-out/zprotoc_values
zprotoc-out/zprotoc_address.hh zprotoc-out/zprotoc_address.cc &: \
	$(srcdir)/zprotoc_address.proto $(top_builddir)/zi/src/zprotoc$(EXEEXT)
	$(AM_V_GEN)$(MKDIR_P) zprotoc-out
	$(AM_V_at)$(top_builddir)/zi/src/zprotoc $(srcdir)/zprotoc_address.proto \
		-o zprotoc-out/zprotoc_address
zprotoc-out/zprotoc_values_row.hh zprotoc-out/zprotoc_values_row.cc &: \
	$(srcdir)/zprotoc_values_row.proto zprotoc-out/zprotoc_values.hh \
	zprotoc-out/zprotoc_address.hh \
	$(top_builddir)/zi/src/zprotoc$(EXEEXT)
	$(AM_V_GEN)$(MKDIR_P) zprotoc-out
	$(AM_V_at)$(top_builddir)/zi/src/zprotoc -I $(srcdir) \
		$(srcdir)/zprotoc_values_row.proto \
		-o zprotoc-out/zprotoc_values_row
CLEANFILES = zprotoc-out/zprotoc_values.hh zprotoc-out/zprotoc_values.cc \
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
zprotoc-out/simple.hh zprotoc-out/simple.cc &: $(srcdir)/zprotoc_simple.proto $(top_builddir)/zi/src/zprotoc$(EXEEXT)
	$(AM_V_GEN)$(MKDIR_P) zprotoc-out
	$(AM_V_at)$(top_builddir)/zi/src/zprotoc $(srcdir)/zprotoc_simple.proto -o zprotoc-out/simple --export ZiAPI
CLEANFILES += zprotoc-out/simple.hh zprotoc-out/simple.cc
EXTRA_DIST += zprotoc_simple.proto
zprotocproto2_SOURCES = zprotocproto2.cc
nodist_zprotocproto2_SOURCES = zprotoc-out/proto2.cc zprotoc-out/proto2.hh
zprotocproto2_CPPFLAGS = $(AM_CPPFLAGS) -I$(builddir)/zprotoc-out
zprotocproto2-zprotocproto2.$(OBJEXT): zprotoc-out/proto2.hh
zprotoc-out/proto2.hh zprotoc-out/proto2.cc &: \
	$(srcdir)/zprotoc_proto2.proto $(top_builddir)/zi/src/zprotoc$(EXEEXT)
	$(AM_V_GEN)$(MKDIR_P) zprotoc-out
	$(AM_V_at)$(top_builddir)/zi/src/zprotoc $(srcdir)/zprotoc_proto2.proto \
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
	$(srcdir)/zprotoc_base.proto $(top_builddir)/zi/src/zprotoc$(EXEEXT)
	$(AM_V_GEN)$(MKDIR_P) zprotoc-out
	$(AM_V_at)$(top_builddir)/zi/src/zprotoc $(srcdir)/zprotoc_base.proto \
		-o zprotoc-out/zprotoc_base
zprotoc-out/zprotoc_user.hh zprotoc-out/zprotoc_user.cc &: \
	$(srcdir)/zprotoc_import/user.proto zprotoc-out/zprotoc_base.hh \
	$(top_builddir)/zi/src/zprotoc$(EXEEXT)
	$(AM_V_GEN)$(top_builddir)/zi/src/zprotoc -I $(srcdir) \
		$(srcdir)/zprotoc_import/user.proto -o zprotoc-out/zprotoc_user
CLEANFILES += zprotoc-out/zprotoc_base.hh zprotoc-out/zprotoc_base.cc \
	zprotoc-out/zprotoc_user.hh zprotoc-out/zprotoc_user.cc
EXTRA_DIST += zprotoc_base.proto zprotoc_import/user.proto
zifbmapcheck_SOURCES = zifbmapcheck.cc
nodist_zifbmapcheck_SOURCES = zflatcmap.cc zflatcmap.hh
zifbmapcheck.$(OBJEXT): zflatcmap.hh
zflatcmap.hh zflatcmap.cc &: $(top_srcdir)/zi/interop/maps.fbs $(top_builddir)/zi/src/zflatc$(EXEEXT)
	$(AM_V_GEN)$(top_builddir)/zi/src/zflatc $(top_srcdir)/zi/interop/maps.fbs -o zflatcmap
CLEANFILES += zflatcmap.hh zflatcmap.cc
zifbmapimport_SOURCES = zifbmapimport.cc zifbmapconsumer.cc
nodist_zifbmapimport_SOURCES = zifbmap_base.cc zifbmap_base.hh \
	zifbmap_user.cc zifbmap_user.hh
zifbmapimport-zifbmapimport.$(OBJEXT) zifbmapimport-zifbmapconsumer.$(OBJEXT): \
	zifbmap_base.hh zifbmap_user.hh
zifbmap_base.hh zifbmap_base.cc &: $(srcdir)/zifbmap_base.fbs $(top_builddir)/zi/src/zflatc$(EXEEXT)
	$(AM_V_GEN)$(top_builddir)/zi/src/zflatc $(srcdir)/zifbmap_base.fbs -o zifbmap_base
zifbmap_user.hh zifbmap_user.cc &: $(srcdir)/zifbmap_user.fbs zifbmap_base.hh $(top_builddir)/zi/src/zflatc$(EXEEXT)
	$(AM_V_GEN)$(top_builddir)/zi/src/zflatc $(srcdir)/zifbmap_user.fbs -o zifbmap_user
CLEANFILES += zifbmap_base.hh zifbmap_base.cc zifbmap_user.hh zifbmap_user.cc
EXTRA_DIST += zifbmap_base.fbs zifbmap_user.fbs
zifbmapref_SOURCES = zifbmapref.cc
nodist_zifbmapref_SOURCES = zflatcmapref.cc zflatcmapref.hh
zifbmapref-zifbmapref.$(OBJEXT): zflatcmapref.hh
zflatcmapref.hh zflatcmapref.cc &: $(top_srcdir)/zi/interop/maps.fbs $(top_builddir)/zi/src/zflatc$(EXEEXT)
	$(AM_V_GEN)$(top_builddir)/zi/src/zflatc $(top_srcdir)/zi/interop/maps.fbs -o zflatcmapref --ref ZmPolymorph
CLEANFILES += zflatcmapref.hh zflatcmapref.cc
zifbmapexport_SOURCES = zifbmapexport.cc zifbmapfixture.hh zflatcfixture.hh
zifbmapgenerated_SOURCES = zifbmapgenerated.cc zflatcfixture.hh
nodist_zifbmapgenerated_SOURCES = zflatcmapnative.cc zflatcmapnative.hh
zifbmapgenerated.$(OBJEXT): zflatcmapnative.hh
zflatcmapout/native.fbs zflatcmapout/native.bfbs zflatcmapout/native.bin &: zifbmapexport$(EXEEXT)
	$(AM_V_GEN)$(MKDIR_P) zflatcmapout
	$(AM_V_at)./zifbmapexport zflatcmapout/native > zflatcmapout/export.tap
zflatcmapnative.hh zflatcmapnative.cc &: zflatcmapout/native.fbs $(top_builddir)/zi/src/zflatc$(EXEEXT)
	$(AM_V_GEN)$(top_builddir)/zi/src/zflatc zflatcmapout/native.fbs -o zflatcmapnative
CLEANFILES += zflatcmapnative.hh zflatcmapnative.cc
zifbsave_SOURCES = zifbsave.cc zflatcfixture.hh
zifbsave_LDFLAGS = @Z_LDFLAGS@
zifbcheck_SOURCES = zifbcheck.cc zifbaux.cc zflatcfixture.hh
nodist_zifbcheck_SOURCES = zflatcgen.cc zflatcgen.hh
zifbcheck_LDFLAGS = @Z_LDFLAGS@
BUILT_SOURCES = zflatcgen.hh

zflatc-out/original.fbs zflatc-out/original.bfbs \
 zflatc-out/original.contract zflatc-out/original.manifest \
 zflatc-out/original.bin zflatc-out/original.point.bin \
 zflatc-out/original.wide.bin zflatc-out/original.bytes.bin \
 zflatc-out/original.none.bin zflatc-out/original.rootbytes.fbs \
 zflatc-out/original.rootbitmap.fbs &: zifbsave$(EXEEXT)
	$(AM_V_GEN)$(MKDIR_P) zflatc-out
	$(AM_V_at)./zifbsave zflatc-out/original > zflatc-out/export.tap

zifbcheck.$(OBJEXT): zflatcgen.hh

zflatcgen.hh zflatcgen.cc &: zflatc-out/original.fbs $(top_builddir)/zi/src/zflatc$(EXEEXT)
	$(AM_V_GEN)$(top_builddir)/zi/src/zflatc zflatc-out/original.fbs -o zflatcgen

CLEANFILES += zflatcgen.hh zflatcgen.cc
clean-local:
	rm -rf zflatc-out zflatc-inc zflatc-inc-alt zflatcmapout zprotoc-out

zifbaux.$(OBJEXT): zflatcgen.hh

noinst_PROGRAMS += zifbcheck_alt
zifbcheck_alt_SOURCES = zifbcheck.cc zifbaux.cc zflatcfixture.hh
nodist_zifbcheck_alt_SOURCES = zflatcalt.cpp zflatcalt.hpp
zifbcheck_alt_CPPFLAGS = $(AM_CPPFLAGS) -DZFLATC_HEADER='<zflatcalt.hpp>'
zifbcheck_alt_LDFLAGS = @Z_LDFLAGS@
zifbcheck_alt-zifbcheck.$(OBJEXT) zifbcheck_alt-zifbaux.$(OBJEXT): zflatcalt.hpp
zflatcalt.hpp zflatcalt.cpp &: zflatc-out/original.fbs $(top_builddir)/zi/src/zflatc$(EXEEXT)
	$(AM_V_GEN)$(top_builddir)/zi/src/zflatc zflatc-out/original.fbs -o zflatcalt --alt
CLEANFILES += zflatcalt.hpp zflatcalt.cpp

noinst_PROGRAMS += zifbcheck_zuobject
zifbcheck_zuobject_SOURCES = zifbcheck.cc zifbaux.cc zflatcfixture.hh
nodist_zifbcheck_zuobject_SOURCES = zflatczuobj.cc zflatczuobj.hh
zifbcheck_zuobject_CPPFLAGS = $(AM_CPPFLAGS) -DZFLATC_HEADER='<zflatczuobj.hh>' -DZFLATC_REF=ZuObject
zifbcheck_zuobject_LDFLAGS = @Z_LDFLAGS@
zifbcheck_zuobject-zifbcheck.$(OBJEXT) zifbcheck_zuobject-zifbaux.$(OBJEXT): zflatczuobj.hh
zflatczuobj.hh zflatczuobj.cc &: zflatc-out/original.fbs $(top_builddir)/zi/src/zflatc$(EXEEXT)
	$(AM_V_GEN)$(top_builddir)/zi/src/zflatc zflatc-out/original.fbs -o zflatczuobj --ref ZuObject --export ZiAPI
CLEANFILES += zflatczuobj.hh zflatczuobj.cc

noinst_PROGRAMS += zifbcheck_zupolymorph
zifbcheck_zupolymorph_SOURCES = zifbcheck.cc zifbaux.cc zflatcfixture.hh
nodist_zifbcheck_zupolymorph_SOURCES = zflatczupoly.cc zflatczupoly.hh
zifbcheck_zupolymorph_CPPFLAGS = $(AM_CPPFLAGS) -DZFLATC_HEADER='<zflatczupoly.hh>' -DZFLATC_REF=ZuPolymorph
zifbcheck_zupolymorph_LDFLAGS = @Z_LDFLAGS@
zifbcheck_zupolymorph-zifbcheck.$(OBJEXT) zifbcheck_zupolymorph-zifbaux.$(OBJEXT): zflatczupoly.hh
zflatczupoly.hh zflatczupoly.cc &: zflatc-out/original.fbs $(top_builddir)/zi/src/zflatc$(EXEEXT)
	$(AM_V_GEN)$(top_builddir)/zi/src/zflatc zflatc-out/original.fbs -o zflatczupoly --ref ZuPolymorph
CLEANFILES += zflatczupoly.hh zflatczupoly.cc

noinst_PROGRAMS += zifbcheck_zmobject
zifbcheck_zmobject_SOURCES = zifbcheck.cc zifbaux.cc zflatcfixture.hh
nodist_zifbcheck_zmobject_SOURCES = zflatczmobj.cc zflatczmobj.hh
zifbcheck_zmobject_CPPFLAGS = $(AM_CPPFLAGS) -DZFLATC_HEADER='<zflatczmobj.hh>' -DZFLATC_REF=ZmObject
zifbcheck_zmobject_LDFLAGS = @Z_LDFLAGS@
zifbcheck_zmobject-zifbcheck.$(OBJEXT) zifbcheck_zmobject-zifbaux.$(OBJEXT): zflatczmobj.hh
zflatczmobj.hh zflatczmobj.cc &: zflatc-out/original.fbs $(top_builddir)/zi/src/zflatc$(EXEEXT)
	$(AM_V_GEN)$(top_builddir)/zi/src/zflatc zflatc-out/original.fbs -o zflatczmobj --ref ZmObject
CLEANFILES += zflatczmobj.hh zflatczmobj.cc

noinst_PROGRAMS += zifbcheck_zmpolymorph
zifbcheck_zmpolymorph_SOURCES = zifbcheck.cc zifbaux.cc zflatcfixture.hh
nodist_zifbcheck_zmpolymorph_SOURCES = zflatczmpoly.cpp zflatczmpoly.hpp
zifbcheck_zmpolymorph_CPPFLAGS = $(AM_CPPFLAGS) -DZFLATC_HEADER='<zflatczmpoly.hpp>' -DZFLATC_REF=ZmPolymorph
zifbcheck_zmpolymorph_LDFLAGS = @Z_LDFLAGS@
zifbcheck_zmpolymorph-zifbcheck.$(OBJEXT) zifbcheck_zmpolymorph-zifbaux.$(OBJEXT): zflatczmpoly.hpp
zflatczmpoly.hpp zflatczmpoly.cpp &: zflatc-out/original.fbs $(top_builddir)/zi/src/zflatc$(EXEEXT)
	$(AM_V_GEN)$(top_builddir)/zi/src/zflatc zflatc-out/original.fbs -o zflatczmpoly --alt --ref ZmPolymorph
CLEANFILES += zflatczmpoly.hpp zflatczmpoly.cpp


zflatctest_SOURCES = zflatctest.cc zflatcfixture.hh
zflatctest_CPPFLAGS = $(AM_CPPFLAGS) -DZFLATC_FIXTURES=\"$(abs_srcdir)\"
zflatctest_LDFLAGS = @Z_LDFLAGS@
zflatctest_DEPENDENCIES = $(LDADD) zifbsave$(EXEEXT) zifbcheck$(EXEEXT) \
	zifbcheck_alt$(EXEEXT) zifbcheck_zuobject$(EXEEXT) \
	zifbcheck_zupolymorph$(EXEEXT) zifbcheck_zmobject$(EXEEXT) \
	zifbcheck_zmpolymorph$(EXEEXT)

EXTRA_DIST += flatcdep.fbs flatcroot.fbs zflatcvec.hh \
	flatcdep.expected.hh flatcdep.expected.cc \
	flatcroot.expected.hh flatcroot.expected.cc
noinst_PROGRAMS += zifbinclude zifbinclude_alt
zifbinclude_SOURCES = zifbinclude.cc
nodist_zifbinclude_SOURCES = zflatc-inc/flatcdep.hh zflatc-inc/flatcdep.cc \
	zflatc-inc/flatcroot.hh zflatc-inc/flatcroot.cc
zifbinclude_LDFLAGS = @Z_LDFLAGS@
zifbinclude.$(OBJEXT): zflatc-inc/flatcdep.hh zflatc-inc/flatcroot.hh
zflatc-inc/flatcdep.hh zflatc-inc/flatcdep.cc &: $(srcdir)/flatcdep.fbs $(top_builddir)/zi/src/zflatc$(EXEEXT)
	$(AM_V_GEN)$(MKDIR_P) zflatc-inc
	$(AM_V_at)$(top_builddir)/zi/src/zflatc $(srcdir)/flatcdep.fbs -o zflatc-inc/flatcdep
zflatc-inc/flatcroot.hh zflatc-inc/flatcroot.cc &: $(srcdir)/flatcroot.fbs $(srcdir)/flatcdep.fbs $(top_builddir)/zi/src/zflatc$(EXEEXT)
	$(AM_V_GEN)$(MKDIR_P) zflatc-inc
	$(AM_V_at)$(top_builddir)/zi/src/zflatc $(srcdir)/flatcroot.fbs -o zflatc-inc/flatcroot

zifbinclude_alt_SOURCES = zifbinclude.cc
nodist_zifbinclude_alt_SOURCES = zflatc-inc-alt/flatcdep.hpp zflatc-inc-alt/flatcdep.cpp \
	zflatc-inc-alt/flatcroot.hpp zflatc-inc-alt/flatcroot.cpp
zifbinclude_alt_CPPFLAGS = $(AM_CPPFLAGS) -DZFLATC_HEADER='<zflatc-inc-alt/flatcroot.hpp>' -DZFLATC_BUILTIN='<zflatc-inc-alt/flatcbuiltinagain.hpp>'
zifbinclude_alt_LDFLAGS = @Z_LDFLAGS@
zifbinclude_alt-zifbinclude.$(OBJEXT): zflatc-inc-alt/flatcdep.hpp zflatc-inc-alt/flatcroot.hpp
zflatc-inc-alt/flatcdep.hpp zflatc-inc-alt/flatcdep.cpp &: $(srcdir)/flatcdep.fbs $(top_builddir)/zi/src/zflatc$(EXEEXT)
	$(AM_V_GEN)$(MKDIR_P) zflatc-inc-alt
	$(AM_V_at)$(top_builddir)/zi/src/zflatc $(srcdir)/flatcdep.fbs --alt -o zflatc-inc-alt/flatcdep
zflatc-inc-alt/flatcroot.hpp zflatc-inc-alt/flatcroot.cpp &: $(srcdir)/flatcroot.fbs $(srcdir)/flatcdep.fbs $(top_builddir)/zi/src/zflatc$(EXEEXT)
	$(AM_V_GEN)$(MKDIR_P) zflatc-inc-alt
	$(AM_V_at)$(top_builddir)/zi/src/zflatc $(srcdir)/flatcroot.fbs --alt -o zflatc-inc-alt/flatcroot

zflatctest_DEPENDENCIES += zifbinclude$(EXEEXT) zifbinclude_alt$(EXEEXT)

noinst_PROGRAMS += zifbcheck_wire
zifbcheck_wire_SOURCES = zifbcheck.cc zifbaux.cc zflatcfixture.hh
nodist_zifbcheck_wire_SOURCES = zflatcwire.hh zflatcwire.cc
zifbcheck_wire_CPPFLAGS = $(AM_CPPFLAGS) -DZFLATC_HEADER='<zflatcwire.hh>' -DZFLATC_WIRE
zifbcheck_wire_LDFLAGS = @Z_LDFLAGS@
zifbcheck_wire-zifbcheck.$(OBJEXT) zifbcheck_wire-zifbaux.$(OBJEXT): zflatcwire.hh
zflatc-out/facet.fbs zflatc-out/facet.bfbs \
 zflatc-out/facet.contract zflatc-out/facet.manifest \
 zflatc-out/facet.bin zflatc-out/facet.point.bin \
 zflatc-out/facet.wide.bin zflatc-out/facet.bytes.bin \
 zflatc-out/facet.none.bin &: zifbsave$(EXEEXT)
	$(AM_V_GEN)$(MKDIR_P) zflatc-out
	$(AM_V_at)./zifbsave --facet zflatc-out/facet > zflatc-out/facet.tap
zflatcwire.hh zflatcwire.cc &: zflatc-out/facet.fbs $(top_builddir)/zi/src/zflatc$(EXEEXT)
	$(AM_V_GEN)$(top_builddir)/zi/src/zflatc zflatc-out/facet.fbs -o zflatcwire
CLEANFILES += zflatcwire.hh zflatcwire.cc
zflatctest_DEPENDENCIES += zifbcheck_wire$(EXEEXT)

noinst_PROGRAMS += zifbroot
zifbroot_SOURCES = zifbroot.cc zflatcfixture.hh
nodist_zifbroot_SOURCES = zflatcrbytes.hh zflatcrbytes.cc zflatcrbitmap.hh zflatcrbitmap.cc
zifbroot_CPPFLAGS = $(AM_CPPFLAGS) -DZFLATC_BYTES='<zflatcrbytes.hh>' -DZFLATC_BITMAP='<zflatcrbitmap.hh>'
zifbroot-zifbroot.$(OBJEXT): zflatcrbytes.hh zflatcrbitmap.hh
zflatcrbytes.hh zflatcrbytes.cc &: zflatc-out/original.rootbytes.fbs $(top_builddir)/zi/src/zflatc$(EXEEXT)
	$(AM_V_GEN)$(top_builddir)/zi/src/zflatc $< -o zflatcrbytes
CLEANFILES += zflatcrbytes.hh zflatcrbytes.cc
zflatcrbitmap.hh zflatcrbitmap.cc &: zflatc-out/original.rootbitmap.fbs $(top_builddir)/zi/src/zflatc$(EXEEXT)
	$(AM_V_GEN)$(top_builddir)/zi/src/zflatc $< -o zflatcrbitmap
CLEANFILES += zflatcrbitmap.hh zflatcrbitmap.cc
zflatctest_DEPENDENCIES += zifbroot$(EXEEXT)

noinst_PROGRAMS += zifbroot_alt
zifbroot_alt_SOURCES = zifbroot.cc zflatcfixture.hh
nodist_zifbroot_alt_SOURCES = zflatcr_altbytes.hpp zflatcr_altbytes.cpp zflatcr_altbitmap.hpp zflatcr_altbitmap.cpp
zifbroot_alt_CPPFLAGS = $(AM_CPPFLAGS) -DZFLATC_BYTES='<zflatcr_altbytes.hpp>' -DZFLATC_BITMAP='<zflatcr_altbitmap.hpp>'
zifbroot_alt-zifbroot.$(OBJEXT): zflatcr_altbytes.hpp zflatcr_altbitmap.hpp
zflatcr_altbytes.hpp zflatcr_altbytes.cpp &: zflatc-out/original.rootbytes.fbs $(top_builddir)/zi/src/zflatc$(EXEEXT)
	$(AM_V_GEN)$(top_builddir)/zi/src/zflatc $< -o zflatcr_altbytes --alt
CLEANFILES += zflatcr_altbytes.hpp zflatcr_altbytes.cpp
zflatcr_altbitmap.hpp zflatcr_altbitmap.cpp &: zflatc-out/original.rootbitmap.fbs $(top_builddir)/zi/src/zflatc$(EXEEXT)
	$(AM_V_GEN)$(top_builddir)/zi/src/zflatc $< -o zflatcr_altbitmap --alt
CLEANFILES += zflatcr_altbitmap.hpp zflatcr_altbitmap.cpp
zflatctest_DEPENDENCIES += zifbroot_alt$(EXEEXT)

noinst_PROGRAMS += zifbroot_zuobject
zifbroot_zuobject_SOURCES = zifbroot.cc zflatcfixture.hh
nodist_zifbroot_zuobject_SOURCES = zflatcr_zuobjectbytes.hh zflatcr_zuobjectbytes.cc zflatcr_zuobjectbitmap.hh zflatcr_zuobjectbitmap.cc
zifbroot_zuobject_CPPFLAGS = $(AM_CPPFLAGS) -DZFLATC_BYTES='<zflatcr_zuobjectbytes.hh>' -DZFLATC_BITMAP='<zflatcr_zuobjectbitmap.hh>' -DZFLATC_REF=ZuObject
zifbroot_zuobject-zifbroot.$(OBJEXT): zflatcr_zuobjectbytes.hh zflatcr_zuobjectbitmap.hh
zflatcr_zuobjectbytes.hh zflatcr_zuobjectbytes.cc &: zflatc-out/original.rootbytes.fbs $(top_builddir)/zi/src/zflatc$(EXEEXT)
	$(AM_V_GEN)$(top_builddir)/zi/src/zflatc $< -o zflatcr_zuobjectbytes --ref ::ZuObject
CLEANFILES += zflatcr_zuobjectbytes.hh zflatcr_zuobjectbytes.cc
zflatcr_zuobjectbitmap.hh zflatcr_zuobjectbitmap.cc &: zflatc-out/original.rootbitmap.fbs $(top_builddir)/zi/src/zflatc$(EXEEXT)
	$(AM_V_GEN)$(top_builddir)/zi/src/zflatc $< -o zflatcr_zuobjectbitmap --ref ::ZuObject
CLEANFILES += zflatcr_zuobjectbitmap.hh zflatcr_zuobjectbitmap.cc
zflatctest_DEPENDENCIES += zifbroot_zuobject$(EXEEXT)

noinst_PROGRAMS += zifbroot_zupolymorph
zifbroot_zupolymorph_SOURCES = zifbroot.cc zflatcfixture.hh
nodist_zifbroot_zupolymorph_SOURCES = zflatcr_zupolymorphbytes.hh zflatcr_zupolymorphbytes.cc zflatcr_zupolymorphbitmap.hh zflatcr_zupolymorphbitmap.cc
zifbroot_zupolymorph_CPPFLAGS = $(AM_CPPFLAGS) -DZFLATC_BYTES='<zflatcr_zupolymorphbytes.hh>' -DZFLATC_BITMAP='<zflatcr_zupolymorphbitmap.hh>' -DZFLATC_REF=ZuPolymorph
zifbroot_zupolymorph-zifbroot.$(OBJEXT): zflatcr_zupolymorphbytes.hh zflatcr_zupolymorphbitmap.hh
zflatcr_zupolymorphbytes.hh zflatcr_zupolymorphbytes.cc &: zflatc-out/original.rootbytes.fbs $(top_builddir)/zi/src/zflatc$(EXEEXT)
	$(AM_V_GEN)$(top_builddir)/zi/src/zflatc $< -o zflatcr_zupolymorphbytes --ref ZuPolymorph
CLEANFILES += zflatcr_zupolymorphbytes.hh zflatcr_zupolymorphbytes.cc
zflatcr_zupolymorphbitmap.hh zflatcr_zupolymorphbitmap.cc &: zflatc-out/original.rootbitmap.fbs $(top_builddir)/zi/src/zflatc$(EXEEXT)
	$(AM_V_GEN)$(top_builddir)/zi/src/zflatc $< -o zflatcr_zupolymorphbitmap --ref ZuPolymorph
CLEANFILES += zflatcr_zupolymorphbitmap.hh zflatcr_zupolymorphbitmap.cc
zflatctest_DEPENDENCIES += zifbroot_zupolymorph$(EXEEXT)

noinst_PROGRAMS += zifbroot_zmobject
zifbroot_zmobject_SOURCES = zifbroot.cc zflatcfixture.hh
nodist_zifbroot_zmobject_SOURCES = zflatcr_zmobjectbytes.hh zflatcr_zmobjectbytes.cc zflatcr_zmobjectbitmap.hh zflatcr_zmobjectbitmap.cc
zifbroot_zmobject_CPPFLAGS = $(AM_CPPFLAGS) -DZFLATC_BYTES='<zflatcr_zmobjectbytes.hh>' -DZFLATC_BITMAP='<zflatcr_zmobjectbitmap.hh>' -DZFLATC_REF=ZmObject
zifbroot_zmobject-zifbroot.$(OBJEXT): zflatcr_zmobjectbytes.hh zflatcr_zmobjectbitmap.hh
zflatcr_zmobjectbytes.hh zflatcr_zmobjectbytes.cc &: zflatc-out/original.rootbytes.fbs $(top_builddir)/zi/src/zflatc$(EXEEXT)
	$(AM_V_GEN)$(top_builddir)/zi/src/zflatc $< -o zflatcr_zmobjectbytes --ref ZmObject
CLEANFILES += zflatcr_zmobjectbytes.hh zflatcr_zmobjectbytes.cc
zflatcr_zmobjectbitmap.hh zflatcr_zmobjectbitmap.cc &: zflatc-out/original.rootbitmap.fbs $(top_builddir)/zi/src/zflatc$(EXEEXT)
	$(AM_V_GEN)$(top_builddir)/zi/src/zflatc $< -o zflatcr_zmobjectbitmap --ref ZmObject
CLEANFILES += zflatcr_zmobjectbitmap.hh zflatcr_zmobjectbitmap.cc
zflatctest_DEPENDENCIES += zifbroot_zmobject$(EXEEXT)

noinst_PROGRAMS += zifbroot_zmpolymorph
zifbroot_zmpolymorph_SOURCES = zifbroot.cc zflatcfixture.hh
nodist_zifbroot_zmpolymorph_SOURCES = zflatcr_zmpolymorphbytes.hpp zflatcr_zmpolymorphbytes.cpp zflatcr_zmpolymorphbitmap.hpp zflatcr_zmpolymorphbitmap.cpp
zifbroot_zmpolymorph_CPPFLAGS = $(AM_CPPFLAGS) -DZFLATC_BYTES='<zflatcr_zmpolymorphbytes.hpp>' -DZFLATC_BITMAP='<zflatcr_zmpolymorphbitmap.hpp>' -DZFLATC_REF=ZmPolymorph
zifbroot_zmpolymorph-zifbroot.$(OBJEXT): zflatcr_zmpolymorphbytes.hpp zflatcr_zmpolymorphbitmap.hpp
zflatcr_zmpolymorphbytes.hpp zflatcr_zmpolymorphbytes.cpp &: zflatc-out/original.rootbytes.fbs $(top_builddir)/zi/src/zflatc$(EXEEXT)
	$(AM_V_GEN)$(top_builddir)/zi/src/zflatc $< -o zflatcr_zmpolymorphbytes --alt --ref ZmPolymorph
CLEANFILES += zflatcr_zmpolymorphbytes.hpp zflatcr_zmpolymorphbytes.cpp
zflatcr_zmpolymorphbitmap.hpp zflatcr_zmpolymorphbitmap.cpp &: zflatc-out/original.rootbitmap.fbs $(top_builddir)/zi/src/zflatc$(EXEEXT)
	$(AM_V_GEN)$(top_builddir)/zi/src/zflatc $< -o zflatcr_zmpolymorphbitmap --alt --ref ZmPolymorph
CLEANFILES += zflatcr_zmpolymorphbitmap.hpp zflatcr_zmpolymorphbitmap.cpp
zflatctest_DEPENDENCIES += zifbroot_zmpolymorph$(EXEEXT)

EXTRA_DIST += flatcbuiltin.fbs flatcbuiltinroot.fbs flatcbuiltinagain.fbs

zifbinclude_CPPFLAGS = $(AM_CPPFLAGS) -DZFLATC_HEADER='<zflatc-inc/flatcroot.hh>' -DZFLATC_BUILTIN='<zflatc-inc/flatcbuiltinagain.hh>'
zifbinclude-zifbinclude.$(OBJEXT): zflatc-inc/flatcroot.hh zflatc-inc/flatcbuiltinagain.hh zflatc-inc/flatcbuiltinroot.hh zflatc-inc/flatcbuiltin.hh
nodist_zifbinclude_SOURCES += zflatc-inc/flatcbuiltin.hh zflatc-inc/flatcbuiltin.cc
zflatc-inc/flatcbuiltin.hh zflatc-inc/flatcbuiltin.cc &: $(srcdir)/flatcbuiltin.fbs $(srcdir)/flatcbuiltin.fbs $(srcdir)/flatcbuiltinroot.fbs $(top_builddir)/zi/src/zflatc$(EXEEXT)
	$(AM_V_GEN)$(MKDIR_P) zflatc-inc
	$(AM_V_at)$(top_builddir)/zi/src/zflatc $(srcdir)/flatcbuiltin.fbs -o zflatc-inc/flatcbuiltin
nodist_zifbinclude_SOURCES += zflatc-inc/flatcbuiltinroot.hh zflatc-inc/flatcbuiltinroot.cc
zflatc-inc/flatcbuiltinroot.hh zflatc-inc/flatcbuiltinroot.cc &: $(srcdir)/flatcbuiltinroot.fbs $(srcdir)/flatcbuiltin.fbs $(srcdir)/flatcbuiltinroot.fbs $(top_builddir)/zi/src/zflatc$(EXEEXT)
	$(AM_V_GEN)$(MKDIR_P) zflatc-inc
	$(AM_V_at)$(top_builddir)/zi/src/zflatc $(srcdir)/flatcbuiltinroot.fbs -o zflatc-inc/flatcbuiltinroot
nodist_zifbinclude_SOURCES += zflatc-inc/flatcbuiltinagain.hh zflatc-inc/flatcbuiltinagain.cc
zflatc-inc/flatcbuiltinagain.hh zflatc-inc/flatcbuiltinagain.cc &: $(srcdir)/flatcbuiltinagain.fbs $(srcdir)/flatcbuiltin.fbs $(srcdir)/flatcbuiltinroot.fbs $(top_builddir)/zi/src/zflatc$(EXEEXT)
	$(AM_V_GEN)$(MKDIR_P) zflatc-inc
	$(AM_V_at)$(top_builddir)/zi/src/zflatc $(srcdir)/flatcbuiltinagain.fbs -o zflatc-inc/flatcbuiltinagain
zifbinclude_alt-zifbinclude.$(OBJEXT): zflatc-inc-alt/flatcroot.hpp zflatc-inc-alt/flatcbuiltinagain.hpp zflatc-inc-alt/flatcbuiltinroot.hpp zflatc-inc-alt/flatcbuiltin.hpp
nodist_zifbinclude_alt_SOURCES += zflatc-inc-alt/flatcbuiltin.hpp zflatc-inc-alt/flatcbuiltin.cpp
zflatc-inc-alt/flatcbuiltin.hpp zflatc-inc-alt/flatcbuiltin.cpp &: $(srcdir)/flatcbuiltin.fbs $(srcdir)/flatcbuiltin.fbs $(srcdir)/flatcbuiltinroot.fbs $(top_builddir)/zi/src/zflatc$(EXEEXT)
	$(AM_V_GEN)$(MKDIR_P) zflatc-inc-alt
	$(AM_V_at)$(top_builddir)/zi/src/zflatc $(srcdir)/flatcbuiltin.fbs -o zflatc-inc-alt/flatcbuiltin --alt
nodist_zifbinclude_alt_SOURCES += zflatc-inc-alt/flatcbuiltinroot.hpp zflatc-inc-alt/flatcbuiltinroot.cpp
zflatc-inc-alt/flatcbuiltinroot.hpp zflatc-inc-alt/flatcbuiltinroot.cpp &: $(srcdir)/flatcbuiltinroot.fbs $(srcdir)/flatcbuiltin.fbs $(srcdir)/flatcbuiltinroot.fbs $(top_builddir)/zi/src/zflatc$(EXEEXT)
	$(AM_V_GEN)$(MKDIR_P) zflatc-inc-alt
	$(AM_V_at)$(top_builddir)/zi/src/zflatc $(srcdir)/flatcbuiltinroot.fbs -o zflatc-inc-alt/flatcbuiltinroot --alt
nodist_zifbinclude_alt_SOURCES += zflatc-inc-alt/flatcbuiltinagain.hpp zflatc-inc-alt/flatcbuiltinagain.cpp
zflatc-inc-alt/flatcbuiltinagain.hpp zflatc-inc-alt/flatcbuiltinagain.cpp &: $(srcdir)/flatcbuiltinagain.fbs $(srcdir)/flatcbuiltin.fbs $(srcdir)/flatcbuiltinroot.fbs $(top_builddir)/zi/src/zflatc$(EXEEXT)
	$(AM_V_GEN)$(MKDIR_P) zflatc-inc-alt
	$(AM_V_at)$(top_builddir)/zi/src/zflatc $(srcdir)/flatcbuiltinagain.fbs -o zflatc-inc-alt/flatcbuiltinagain --alt

noinst_PROGRAMS += zifbcheck_custom
zifbcheck_custom_SOURCES = zifbcheck.cc zifbaux.cc zflatcfixture.hh flatcref.hh
nodist_zifbcheck_custom_SOURCES = zflatccustom.hh zflatccustom.cc
zifbcheck_custom_CPPFLAGS = $(AM_CPPFLAGS) -include $(srcdir)/flatcref.hh -DZFLATC_HEADER='<zflatccustom.hh>' -DZFLATC_REF=flatcref::Base
zifbcheck_custom_LDFLAGS = @Z_LDFLAGS@
zifbcheck_custom-zifbcheck.$(OBJEXT) zifbcheck_custom-zifbaux.$(OBJEXT): zflatccustom.hh
zflatccustom.hh zflatccustom.cc &: zflatc-out/original.fbs $(top_builddir)/zi/src/zflatc$(EXEEXT)
	$(AM_V_GEN)$(top_builddir)/zi/src/zflatc $< -o zflatccustom --ref ::flatcref::Base
CLEANFILES += zflatccustom.hh zflatccustom.cc
zflatctest_DEPENDENCIES += zifbcheck_custom$(EXEEXT)
