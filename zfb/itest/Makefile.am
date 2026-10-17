METASOURCES = AUTO
AM_CPPFLAGS = -I$(top_srcdir)/zu/src -I$(top_srcdir)/zm/src \
	-I$(top_srcdir)/zt/src -I$(top_srcdir)/ze/src \
	-I$(top_srcdir)/zf/src -I$(top_srcdir)/zi/src \
	-I$(top_srcdir)/zi/test -I$(top_srcdir)/zfb/src \
	@Z_CPPFLAGS@
AM_CXXFLAGS = @Z_CXXFLAGS@
AM_LDFLAGS = @Z_LDFLAGS@
LDADD = $(top_builddir)/zi/test/libZiTest.la \
	$(top_builddir)/zfb/src/libZfb.la \
	$(top_builddir)/zi/src/libZi.la \
	$(top_builddir)/zf/src/libZf.la $(top_builddir)/ze/src/libZe.la \
	$(top_builddir)/zt/src/libZt.la $(top_builddir)/zm/src/libZm.la \
	$(top_builddir)/zu/src/libZu.la \
	@Z_IO_LIBS@ @Z_ZT_LIBS@ @Z_MT_LIBS@ @Z_LIBS@
tap_drivers = zflatctest
noinst_PROGRAMS = $(tap_drivers)
EXTRA_DIST = README.md
CLEANFILES =
.PHONY: test
test: export ZI_LOGDIR = $(abs_top_builddir)/logs
test: $(noinst_PROGRAMS)
	prove -j1 $(tap_drivers)

noinst_PROGRAMS += zfbsave zfbcheck zfbmapcheck zfbmapimport zfbmapref \
	zfbmapexport zfbmapgenerated

zfbmapcheck_SOURCES = zfbmapcheck.cc
nodist_zfbmapcheck_SOURCES = zflatcmap.cc zflatcmap.hh
zfbmapcheck.$(OBJEXT): zflatcmap.hh
zflatcmap.hh zflatcmap.cc &: $(top_srcdir)/zfb/interop/maps.fbs $(top_builddir)/zfb/src/zflatc$(EXEEXT)
	$(AM_V_GEN)$(top_builddir)/zfb/src/zflatc $(top_srcdir)/zfb/interop/maps.fbs -o zflatcmap
CLEANFILES += zflatcmap.hh zflatcmap.cc
zfbmapimport_SOURCES = zfbmapimport.cc zfbmapconsumer.cc
nodist_zfbmapimport_SOURCES = zfbmap_base.cc zfbmap_base.hh \
	zfbmap_user.cc zfbmap_user.hh
zfbmapimport.$(OBJEXT) zfbmapconsumer.$(OBJEXT): \
	zfbmap_base.hh zfbmap_user.hh
zfbmap_base.hh zfbmap_base.cc &: $(srcdir)/zfbmap_base.fbs $(top_builddir)/zfb/src/zflatc$(EXEEXT)
	$(AM_V_GEN)$(top_builddir)/zfb/src/zflatc $(srcdir)/zfbmap_base.fbs -o zfbmap_base
zfbmap_user.hh zfbmap_user.cc &: $(srcdir)/zfbmap_user.fbs zfbmap_base.hh $(top_builddir)/zfb/src/zflatc$(EXEEXT)
	$(AM_V_GEN)$(top_builddir)/zfb/src/zflatc $(srcdir)/zfbmap_user.fbs -o zfbmap_user
CLEANFILES += zfbmap_base.hh zfbmap_base.cc zfbmap_user.hh zfbmap_user.cc
EXTRA_DIST += zfbmap_base.fbs zfbmap_user.fbs
zfbmapref_SOURCES = zfbmapref.cc
nodist_zfbmapref_SOURCES = zflatcmapref.cc zflatcmapref.hh
zfbmapref.$(OBJEXT): zflatcmapref.hh
zflatcmapref.hh zflatcmapref.cc &: $(top_srcdir)/zfb/interop/maps.fbs $(top_builddir)/zfb/src/zflatc$(EXEEXT)
	$(AM_V_GEN)$(top_builddir)/zfb/src/zflatc $(top_srcdir)/zfb/interop/maps.fbs -o zflatcmapref --ref ZmPolymorph
CLEANFILES += zflatcmapref.hh zflatcmapref.cc
zfbmapexport_SOURCES = zfbmapexport.cc zfbmapfixture.hh zflatcfixture.hh
zfbmapgenerated_SOURCES = zfbmapgenerated.cc zflatcfixture.hh
nodist_zfbmapgenerated_SOURCES = zflatcmapnative.cc zflatcmapnative.hh
zfbmapgenerated.$(OBJEXT): zflatcmapnative.hh
zflatcmapout/native.fbs zflatcmapout/native.bfbs zflatcmapout/native.bin &: zfbmapexport$(EXEEXT)
	$(AM_V_GEN)$(MKDIR_P) zflatcmapout
	$(AM_V_at)./zfbmapexport zflatcmapout/native > zflatcmapout/export.tap
zflatcmapnative.hh zflatcmapnative.cc &: zflatcmapout/native.fbs $(top_builddir)/zfb/src/zflatc$(EXEEXT)
	$(AM_V_GEN)$(top_builddir)/zfb/src/zflatc zflatcmapout/native.fbs -o zflatcmapnative
CLEANFILES += zflatcmapnative.hh zflatcmapnative.cc
zfbsave_SOURCES = zfbsave.cc zflatcfixture.hh
zfbsave_LDFLAGS = @Z_LDFLAGS@
zfbcheck_SOURCES = zfbcheck.cc zfbaux.cc zflatcfixture.hh
nodist_zfbcheck_SOURCES = zflatcgen.cc zflatcgen.hh
zfbcheck_LDFLAGS = @Z_LDFLAGS@
BUILT_SOURCES = zflatcgen.hh

zflatc-out/original.fbs zflatc-out/original.bfbs \
 zflatc-out/original.contract zflatc-out/original.manifest \
 zflatc-out/original.bin zflatc-out/original.point.bin \
 zflatc-out/original.wide.bin zflatc-out/original.bytes.bin \
 zflatc-out/original.none.bin zflatc-out/original.rootbytes.fbs \
 zflatc-out/original.rootbitmap.fbs &: zfbsave$(EXEEXT)
	$(AM_V_GEN)$(MKDIR_P) zflatc-out
	$(AM_V_at)./zfbsave zflatc-out/original > zflatc-out/export.tap

zfbcheck.$(OBJEXT): zflatcgen.hh

zflatcgen.hh zflatcgen.cc &: zflatc-out/original.fbs $(top_builddir)/zfb/src/zflatc$(EXEEXT)
	$(AM_V_GEN)$(top_builddir)/zfb/src/zflatc zflatc-out/original.fbs -o zflatcgen

CLEANFILES += zflatcgen.hh zflatcgen.cc
clean-local:
	rm -rf zflatc-out zflatc-inc zflatc-inc-alt zflatcmapout

zfbaux.$(OBJEXT): zflatcgen.hh

noinst_PROGRAMS += zfbcheck_alt
zfbcheck_alt_SOURCES = zfbcheck.cc zfbaux.cc zflatcfixture.hh
nodist_zfbcheck_alt_SOURCES = zflatcalt.cpp zflatcalt.hpp
zfbcheck_alt_CPPFLAGS = $(AM_CPPFLAGS) -DZFLATC_HEADER='<zflatcalt.hpp>'
zfbcheck_alt_LDFLAGS = @Z_LDFLAGS@
zfbcheck_alt-zfbcheck.$(OBJEXT) zfbcheck_alt-zfbaux.$(OBJEXT): zflatcalt.hpp
zflatcalt.hpp zflatcalt.cpp &: zflatc-out/original.fbs $(top_builddir)/zfb/src/zflatc$(EXEEXT)
	$(AM_V_GEN)$(top_builddir)/zfb/src/zflatc zflatc-out/original.fbs -o zflatcalt --alt
CLEANFILES += zflatcalt.hpp zflatcalt.cpp

noinst_PROGRAMS += zfbcheck_zuobject
zfbcheck_zuobject_SOURCES = zfbcheck.cc zfbaux.cc zflatcfixture.hh
nodist_zfbcheck_zuobject_SOURCES = zflatczuobj.cc zflatczuobj.hh
zfbcheck_zuobject_CPPFLAGS = $(AM_CPPFLAGS) -DZFLATC_HEADER='<zflatczuobj.hh>' -DZFLATC_REF=ZuObject
zfbcheck_zuobject_LDFLAGS = @Z_LDFLAGS@
zfbcheck_zuobject-zfbcheck.$(OBJEXT) zfbcheck_zuobject-zfbaux.$(OBJEXT): zflatczuobj.hh
zflatczuobj.hh zflatczuobj.cc &: zflatc-out/original.fbs $(top_builddir)/zfb/src/zflatc$(EXEEXT)
	$(AM_V_GEN)$(top_builddir)/zfb/src/zflatc zflatc-out/original.fbs -o zflatczuobj --ref ZuObject --export ZfbAPI
CLEANFILES += zflatczuobj.hh zflatczuobj.cc

noinst_PROGRAMS += zfbcheck_zupolymorph
zfbcheck_zupolymorph_SOURCES = zfbcheck.cc zfbaux.cc zflatcfixture.hh
nodist_zfbcheck_zupolymorph_SOURCES = zflatczupoly.cc zflatczupoly.hh
zfbcheck_zupolymorph_CPPFLAGS = $(AM_CPPFLAGS) -DZFLATC_HEADER='<zflatczupoly.hh>' -DZFLATC_REF=ZuPolymorph
zfbcheck_zupolymorph_LDFLAGS = @Z_LDFLAGS@
zfbcheck_zupolymorph-zfbcheck.$(OBJEXT) zfbcheck_zupolymorph-zfbaux.$(OBJEXT): zflatczupoly.hh
zflatczupoly.hh zflatczupoly.cc &: zflatc-out/original.fbs $(top_builddir)/zfb/src/zflatc$(EXEEXT)
	$(AM_V_GEN)$(top_builddir)/zfb/src/zflatc zflatc-out/original.fbs -o zflatczupoly --ref ZuPolymorph
CLEANFILES += zflatczupoly.hh zflatczupoly.cc

noinst_PROGRAMS += zfbcheck_zmobject
zfbcheck_zmobject_SOURCES = zfbcheck.cc zfbaux.cc zflatcfixture.hh
nodist_zfbcheck_zmobject_SOURCES = zflatczmobj.cc zflatczmobj.hh
zfbcheck_zmobject_CPPFLAGS = $(AM_CPPFLAGS) -DZFLATC_HEADER='<zflatczmobj.hh>' -DZFLATC_REF=ZmObject
zfbcheck_zmobject_LDFLAGS = @Z_LDFLAGS@
zfbcheck_zmobject-zfbcheck.$(OBJEXT) zfbcheck_zmobject-zfbaux.$(OBJEXT): zflatczmobj.hh
zflatczmobj.hh zflatczmobj.cc &: zflatc-out/original.fbs $(top_builddir)/zfb/src/zflatc$(EXEEXT)
	$(AM_V_GEN)$(top_builddir)/zfb/src/zflatc zflatc-out/original.fbs -o zflatczmobj --ref ZmObject
CLEANFILES += zflatczmobj.hh zflatczmobj.cc

noinst_PROGRAMS += zfbcheck_zmpolymorph
zfbcheck_zmpolymorph_SOURCES = zfbcheck.cc zfbaux.cc zflatcfixture.hh
nodist_zfbcheck_zmpolymorph_SOURCES = zflatczmpoly.cpp zflatczmpoly.hpp
zfbcheck_zmpolymorph_CPPFLAGS = $(AM_CPPFLAGS) -DZFLATC_HEADER='<zflatczmpoly.hpp>' -DZFLATC_REF=ZmPolymorph
zfbcheck_zmpolymorph_LDFLAGS = @Z_LDFLAGS@
zfbcheck_zmpolymorph-zfbcheck.$(OBJEXT) zfbcheck_zmpolymorph-zfbaux.$(OBJEXT): zflatczmpoly.hpp
zflatczmpoly.hpp zflatczmpoly.cpp &: zflatc-out/original.fbs $(top_builddir)/zfb/src/zflatc$(EXEEXT)
	$(AM_V_GEN)$(top_builddir)/zfb/src/zflatc zflatc-out/original.fbs -o zflatczmpoly --alt --ref ZmPolymorph
CLEANFILES += zflatczmpoly.hpp zflatczmpoly.cpp


zflatctest_SOURCES = zflatctest.cc zflatcfixture.hh
zflatctest_CPPFLAGS = $(AM_CPPFLAGS) -DZFLATC_FIXTURES=\"$(abs_srcdir)\"
zflatctest_LDFLAGS = @Z_LDFLAGS@
zflatctest_DEPENDENCIES = $(filter-out -%,$(LDADD)) \
	zfbsave$(EXEEXT) zfbcheck$(EXEEXT) \
	zfbcheck_alt$(EXEEXT) zfbcheck_zuobject$(EXEEXT) \
	zfbcheck_zupolymorph$(EXEEXT) zfbcheck_zmobject$(EXEEXT) \
	zfbcheck_zmpolymorph$(EXEEXT)

EXTRA_DIST += flatcdep.fbs flatcroot.fbs zflatcvec.hh \
	flatcdep.expected.hh flatcdep.expected.cc \
	flatcroot.expected.hh flatcroot.expected.cc
noinst_PROGRAMS += zfbinclude zfbinclude_alt
zfbinclude_SOURCES = zfbinclude.cc
nodist_zfbinclude_SOURCES = zflatc-inc/flatcdep.hh zflatc-inc/flatcdep.cc \
	zflatc-inc/flatcroot.hh zflatc-inc/flatcroot.cc
zfbinclude_LDFLAGS = @Z_LDFLAGS@
zfbinclude.$(OBJEXT): zflatc-inc/flatcdep.hh zflatc-inc/flatcroot.hh
zflatc-inc/flatcdep.hh zflatc-inc/flatcdep.cc &: $(srcdir)/flatcdep.fbs $(top_builddir)/zfb/src/zflatc$(EXEEXT)
	$(AM_V_GEN)$(MKDIR_P) zflatc-inc
	$(AM_V_at)$(top_builddir)/zfb/src/zflatc $(srcdir)/flatcdep.fbs -o zflatc-inc/flatcdep
zflatc-inc/flatcroot.hh zflatc-inc/flatcroot.cc &: $(srcdir)/flatcroot.fbs $(srcdir)/flatcdep.fbs $(top_builddir)/zfb/src/zflatc$(EXEEXT)
	$(AM_V_GEN)$(MKDIR_P) zflatc-inc
	$(AM_V_at)$(top_builddir)/zfb/src/zflatc $(srcdir)/flatcroot.fbs -o zflatc-inc/flatcroot

zfbinclude_alt_SOURCES = zfbinclude.cc
nodist_zfbinclude_alt_SOURCES = zflatc-inc-alt/flatcdep.hpp zflatc-inc-alt/flatcdep.cpp \
	zflatc-inc-alt/flatcroot.hpp zflatc-inc-alt/flatcroot.cpp
zfbinclude_alt_CPPFLAGS = $(AM_CPPFLAGS) -DZFLATC_HEADER='<zflatc-inc-alt/flatcroot.hpp>' -DZFLATC_BUILTIN='<zflatc-inc-alt/flatcbuiltinagain.hpp>'
zfbinclude_alt_LDFLAGS = @Z_LDFLAGS@
zfbinclude_alt-zfbinclude.$(OBJEXT): zflatc-inc-alt/flatcdep.hpp zflatc-inc-alt/flatcroot.hpp
zflatc-inc-alt/flatcdep.hpp zflatc-inc-alt/flatcdep.cpp &: $(srcdir)/flatcdep.fbs $(top_builddir)/zfb/src/zflatc$(EXEEXT)
	$(AM_V_GEN)$(MKDIR_P) zflatc-inc-alt
	$(AM_V_at)$(top_builddir)/zfb/src/zflatc $(srcdir)/flatcdep.fbs --alt -o zflatc-inc-alt/flatcdep
zflatc-inc-alt/flatcroot.hpp zflatc-inc-alt/flatcroot.cpp &: $(srcdir)/flatcroot.fbs $(srcdir)/flatcdep.fbs $(top_builddir)/zfb/src/zflatc$(EXEEXT)
	$(AM_V_GEN)$(MKDIR_P) zflatc-inc-alt
	$(AM_V_at)$(top_builddir)/zfb/src/zflatc $(srcdir)/flatcroot.fbs --alt -o zflatc-inc-alt/flatcroot

zflatctest_DEPENDENCIES += zfbinclude$(EXEEXT) zfbinclude_alt$(EXEEXT)

noinst_PROGRAMS += zfbcheck_wire
zfbcheck_wire_SOURCES = zfbcheck.cc zfbaux.cc zflatcfixture.hh
nodist_zfbcheck_wire_SOURCES = zflatcwire.hh zflatcwire.cc
zfbcheck_wire_CPPFLAGS = $(AM_CPPFLAGS) -DZFLATC_HEADER='<zflatcwire.hh>' -DZFLATC_WIRE
zfbcheck_wire_LDFLAGS = @Z_LDFLAGS@
zfbcheck_wire-zfbcheck.$(OBJEXT) zfbcheck_wire-zfbaux.$(OBJEXT): zflatcwire.hh
zflatc-out/facet.fbs zflatc-out/facet.bfbs \
 zflatc-out/facet.contract zflatc-out/facet.manifest \
 zflatc-out/facet.bin zflatc-out/facet.point.bin \
 zflatc-out/facet.wide.bin zflatc-out/facet.bytes.bin \
 zflatc-out/facet.none.bin &: zfbsave$(EXEEXT)
	$(AM_V_GEN)$(MKDIR_P) zflatc-out
	$(AM_V_at)./zfbsave --facet zflatc-out/facet > zflatc-out/facet.tap
zflatcwire.hh zflatcwire.cc &: zflatc-out/facet.fbs $(top_builddir)/zfb/src/zflatc$(EXEEXT)
	$(AM_V_GEN)$(top_builddir)/zfb/src/zflatc zflatc-out/facet.fbs -o zflatcwire
CLEANFILES += zflatcwire.hh zflatcwire.cc
zflatctest_DEPENDENCIES += zfbcheck_wire$(EXEEXT)

noinst_PROGRAMS += zfbroot
zfbroot_SOURCES = zfbroot.cc zflatcfixture.hh
nodist_zfbroot_SOURCES = zflatcrbytes.hh zflatcrbytes.cc zflatcrbitmap.hh zflatcrbitmap.cc
zfbroot_CPPFLAGS = $(AM_CPPFLAGS) -DZFLATC_BYTES='<zflatcrbytes.hh>' -DZFLATC_BITMAP='<zflatcrbitmap.hh>'
zfbroot-zfbroot.$(OBJEXT): zflatcrbytes.hh zflatcrbitmap.hh
zflatcrbytes.hh zflatcrbytes.cc &: zflatc-out/original.rootbytes.fbs $(top_builddir)/zfb/src/zflatc$(EXEEXT)
	$(AM_V_GEN)$(top_builddir)/zfb/src/zflatc $< -o zflatcrbytes
CLEANFILES += zflatcrbytes.hh zflatcrbytes.cc
zflatcrbitmap.hh zflatcrbitmap.cc &: zflatc-out/original.rootbitmap.fbs $(top_builddir)/zfb/src/zflatc$(EXEEXT)
	$(AM_V_GEN)$(top_builddir)/zfb/src/zflatc $< -o zflatcrbitmap
CLEANFILES += zflatcrbitmap.hh zflatcrbitmap.cc
zflatctest_DEPENDENCIES += zfbroot$(EXEEXT)

noinst_PROGRAMS += zfbroot_alt
zfbroot_alt_SOURCES = zfbroot.cc zflatcfixture.hh
nodist_zfbroot_alt_SOURCES = zflatcr_altbytes.hpp zflatcr_altbytes.cpp zflatcr_altbitmap.hpp zflatcr_altbitmap.cpp
zfbroot_alt_CPPFLAGS = $(AM_CPPFLAGS) -DZFLATC_BYTES='<zflatcr_altbytes.hpp>' -DZFLATC_BITMAP='<zflatcr_altbitmap.hpp>'
zfbroot_alt-zfbroot.$(OBJEXT): zflatcr_altbytes.hpp zflatcr_altbitmap.hpp
zflatcr_altbytes.hpp zflatcr_altbytes.cpp &: zflatc-out/original.rootbytes.fbs $(top_builddir)/zfb/src/zflatc$(EXEEXT)
	$(AM_V_GEN)$(top_builddir)/zfb/src/zflatc $< -o zflatcr_altbytes --alt
CLEANFILES += zflatcr_altbytes.hpp zflatcr_altbytes.cpp
zflatcr_altbitmap.hpp zflatcr_altbitmap.cpp &: zflatc-out/original.rootbitmap.fbs $(top_builddir)/zfb/src/zflatc$(EXEEXT)
	$(AM_V_GEN)$(top_builddir)/zfb/src/zflatc $< -o zflatcr_altbitmap --alt
CLEANFILES += zflatcr_altbitmap.hpp zflatcr_altbitmap.cpp
zflatctest_DEPENDENCIES += zfbroot_alt$(EXEEXT)

noinst_PROGRAMS += zfbroot_zuobject
zfbroot_zuobject_SOURCES = zfbroot.cc zflatcfixture.hh
nodist_zfbroot_zuobject_SOURCES = zflatcr_zuobjectbytes.hh zflatcr_zuobjectbytes.cc zflatcr_zuobjectbitmap.hh zflatcr_zuobjectbitmap.cc
zfbroot_zuobject_CPPFLAGS = $(AM_CPPFLAGS) -DZFLATC_BYTES='<zflatcr_zuobjectbytes.hh>' -DZFLATC_BITMAP='<zflatcr_zuobjectbitmap.hh>' -DZFLATC_REF=ZuObject
zfbroot_zuobject-zfbroot.$(OBJEXT): zflatcr_zuobjectbytes.hh zflatcr_zuobjectbitmap.hh
zflatcr_zuobjectbytes.hh zflatcr_zuobjectbytes.cc &: zflatc-out/original.rootbytes.fbs $(top_builddir)/zfb/src/zflatc$(EXEEXT)
	$(AM_V_GEN)$(top_builddir)/zfb/src/zflatc $< -o zflatcr_zuobjectbytes --ref ::ZuObject
CLEANFILES += zflatcr_zuobjectbytes.hh zflatcr_zuobjectbytes.cc
zflatcr_zuobjectbitmap.hh zflatcr_zuobjectbitmap.cc &: zflatc-out/original.rootbitmap.fbs $(top_builddir)/zfb/src/zflatc$(EXEEXT)
	$(AM_V_GEN)$(top_builddir)/zfb/src/zflatc $< -o zflatcr_zuobjectbitmap --ref ::ZuObject
CLEANFILES += zflatcr_zuobjectbitmap.hh zflatcr_zuobjectbitmap.cc
zflatctest_DEPENDENCIES += zfbroot_zuobject$(EXEEXT)

noinst_PROGRAMS += zfbroot_zupolymorph
zfbroot_zupolymorph_SOURCES = zfbroot.cc zflatcfixture.hh
nodist_zfbroot_zupolymorph_SOURCES = zflatcr_zupolymorphbytes.hh zflatcr_zupolymorphbytes.cc zflatcr_zupolymorphbitmap.hh zflatcr_zupolymorphbitmap.cc
zfbroot_zupolymorph_CPPFLAGS = $(AM_CPPFLAGS) -DZFLATC_BYTES='<zflatcr_zupolymorphbytes.hh>' -DZFLATC_BITMAP='<zflatcr_zupolymorphbitmap.hh>' -DZFLATC_REF=ZuPolymorph
zfbroot_zupolymorph-zfbroot.$(OBJEXT): zflatcr_zupolymorphbytes.hh zflatcr_zupolymorphbitmap.hh
zflatcr_zupolymorphbytes.hh zflatcr_zupolymorphbytes.cc &: zflatc-out/original.rootbytes.fbs $(top_builddir)/zfb/src/zflatc$(EXEEXT)
	$(AM_V_GEN)$(top_builddir)/zfb/src/zflatc $< -o zflatcr_zupolymorphbytes --ref ZuPolymorph
CLEANFILES += zflatcr_zupolymorphbytes.hh zflatcr_zupolymorphbytes.cc
zflatcr_zupolymorphbitmap.hh zflatcr_zupolymorphbitmap.cc &: zflatc-out/original.rootbitmap.fbs $(top_builddir)/zfb/src/zflatc$(EXEEXT)
	$(AM_V_GEN)$(top_builddir)/zfb/src/zflatc $< -o zflatcr_zupolymorphbitmap --ref ZuPolymorph
CLEANFILES += zflatcr_zupolymorphbitmap.hh zflatcr_zupolymorphbitmap.cc
zflatctest_DEPENDENCIES += zfbroot_zupolymorph$(EXEEXT)

noinst_PROGRAMS += zfbroot_zmobject
zfbroot_zmobject_SOURCES = zfbroot.cc zflatcfixture.hh
nodist_zfbroot_zmobject_SOURCES = zflatcr_zmobjectbytes.hh zflatcr_zmobjectbytes.cc zflatcr_zmobjectbitmap.hh zflatcr_zmobjectbitmap.cc
zfbroot_zmobject_CPPFLAGS = $(AM_CPPFLAGS) -DZFLATC_BYTES='<zflatcr_zmobjectbytes.hh>' -DZFLATC_BITMAP='<zflatcr_zmobjectbitmap.hh>' -DZFLATC_REF=ZmObject
zfbroot_zmobject-zfbroot.$(OBJEXT): zflatcr_zmobjectbytes.hh zflatcr_zmobjectbitmap.hh
zflatcr_zmobjectbytes.hh zflatcr_zmobjectbytes.cc &: zflatc-out/original.rootbytes.fbs $(top_builddir)/zfb/src/zflatc$(EXEEXT)
	$(AM_V_GEN)$(top_builddir)/zfb/src/zflatc $< -o zflatcr_zmobjectbytes --ref ZmObject
CLEANFILES += zflatcr_zmobjectbytes.hh zflatcr_zmobjectbytes.cc
zflatcr_zmobjectbitmap.hh zflatcr_zmobjectbitmap.cc &: zflatc-out/original.rootbitmap.fbs $(top_builddir)/zfb/src/zflatc$(EXEEXT)
	$(AM_V_GEN)$(top_builddir)/zfb/src/zflatc $< -o zflatcr_zmobjectbitmap --ref ZmObject
CLEANFILES += zflatcr_zmobjectbitmap.hh zflatcr_zmobjectbitmap.cc
zflatctest_DEPENDENCIES += zfbroot_zmobject$(EXEEXT)

noinst_PROGRAMS += zfbroot_zmpolymorph
zfbroot_zmpolymorph_SOURCES = zfbroot.cc zflatcfixture.hh
nodist_zfbroot_zmpolymorph_SOURCES = zflatcr_zmpolymorphbytes.hpp zflatcr_zmpolymorphbytes.cpp zflatcr_zmpolymorphbitmap.hpp zflatcr_zmpolymorphbitmap.cpp
zfbroot_zmpolymorph_CPPFLAGS = $(AM_CPPFLAGS) -DZFLATC_BYTES='<zflatcr_zmpolymorphbytes.hpp>' -DZFLATC_BITMAP='<zflatcr_zmpolymorphbitmap.hpp>' -DZFLATC_REF=ZmPolymorph
zfbroot_zmpolymorph-zfbroot.$(OBJEXT): zflatcr_zmpolymorphbytes.hpp zflatcr_zmpolymorphbitmap.hpp
zflatcr_zmpolymorphbytes.hpp zflatcr_zmpolymorphbytes.cpp &: zflatc-out/original.rootbytes.fbs $(top_builddir)/zfb/src/zflatc$(EXEEXT)
	$(AM_V_GEN)$(top_builddir)/zfb/src/zflatc $< -o zflatcr_zmpolymorphbytes --alt --ref ZmPolymorph
CLEANFILES += zflatcr_zmpolymorphbytes.hpp zflatcr_zmpolymorphbytes.cpp
zflatcr_zmpolymorphbitmap.hpp zflatcr_zmpolymorphbitmap.cpp &: zflatc-out/original.rootbitmap.fbs $(top_builddir)/zfb/src/zflatc$(EXEEXT)
	$(AM_V_GEN)$(top_builddir)/zfb/src/zflatc $< -o zflatcr_zmpolymorphbitmap --alt --ref ZmPolymorph
CLEANFILES += zflatcr_zmpolymorphbitmap.hpp zflatcr_zmpolymorphbitmap.cpp
zflatctest_DEPENDENCIES += zfbroot_zmpolymorph$(EXEEXT)

EXTRA_DIST += flatcbuiltin.fbs flatcbuiltinroot.fbs flatcbuiltinagain.fbs

zfbinclude_CPPFLAGS = $(AM_CPPFLAGS) -DZFLATC_HEADER='<zflatc-inc/flatcroot.hh>' -DZFLATC_BUILTIN='<zflatc-inc/flatcbuiltinagain.hh>'
zfbinclude-zfbinclude.$(OBJEXT): zflatc-inc/flatcroot.hh zflatc-inc/flatcbuiltinagain.hh zflatc-inc/flatcbuiltinroot.hh zflatc-inc/flatcbuiltin.hh
nodist_zfbinclude_SOURCES += zflatc-inc/flatcbuiltin.hh zflatc-inc/flatcbuiltin.cc
zflatc-inc/flatcbuiltin.hh zflatc-inc/flatcbuiltin.cc &: $(srcdir)/flatcbuiltin.fbs $(srcdir)/flatcbuiltin.fbs $(srcdir)/flatcbuiltinroot.fbs $(top_builddir)/zfb/src/zflatc$(EXEEXT)
	$(AM_V_GEN)$(MKDIR_P) zflatc-inc
	$(AM_V_at)$(top_builddir)/zfb/src/zflatc $(srcdir)/flatcbuiltin.fbs -o zflatc-inc/flatcbuiltin
nodist_zfbinclude_SOURCES += zflatc-inc/flatcbuiltinroot.hh zflatc-inc/flatcbuiltinroot.cc
zflatc-inc/flatcbuiltinroot.hh zflatc-inc/flatcbuiltinroot.cc &: $(srcdir)/flatcbuiltinroot.fbs $(srcdir)/flatcbuiltin.fbs $(srcdir)/flatcbuiltinroot.fbs $(top_builddir)/zfb/src/zflatc$(EXEEXT)
	$(AM_V_GEN)$(MKDIR_P) zflatc-inc
	$(AM_V_at)$(top_builddir)/zfb/src/zflatc $(srcdir)/flatcbuiltinroot.fbs -o zflatc-inc/flatcbuiltinroot
nodist_zfbinclude_SOURCES += zflatc-inc/flatcbuiltinagain.hh zflatc-inc/flatcbuiltinagain.cc
zflatc-inc/flatcbuiltinagain.hh zflatc-inc/flatcbuiltinagain.cc &: $(srcdir)/flatcbuiltinagain.fbs $(srcdir)/flatcbuiltin.fbs $(srcdir)/flatcbuiltinroot.fbs $(top_builddir)/zfb/src/zflatc$(EXEEXT)
	$(AM_V_GEN)$(MKDIR_P) zflatc-inc
	$(AM_V_at)$(top_builddir)/zfb/src/zflatc $(srcdir)/flatcbuiltinagain.fbs -o zflatc-inc/flatcbuiltinagain
zfbinclude_alt-zfbinclude.$(OBJEXT): zflatc-inc-alt/flatcroot.hpp zflatc-inc-alt/flatcbuiltinagain.hpp zflatc-inc-alt/flatcbuiltinroot.hpp zflatc-inc-alt/flatcbuiltin.hpp
nodist_zfbinclude_alt_SOURCES += zflatc-inc-alt/flatcbuiltin.hpp zflatc-inc-alt/flatcbuiltin.cpp
zflatc-inc-alt/flatcbuiltin.hpp zflatc-inc-alt/flatcbuiltin.cpp &: $(srcdir)/flatcbuiltin.fbs $(srcdir)/flatcbuiltin.fbs $(srcdir)/flatcbuiltinroot.fbs $(top_builddir)/zfb/src/zflatc$(EXEEXT)
	$(AM_V_GEN)$(MKDIR_P) zflatc-inc-alt
	$(AM_V_at)$(top_builddir)/zfb/src/zflatc $(srcdir)/flatcbuiltin.fbs -o zflatc-inc-alt/flatcbuiltin --alt
nodist_zfbinclude_alt_SOURCES += zflatc-inc-alt/flatcbuiltinroot.hpp zflatc-inc-alt/flatcbuiltinroot.cpp
zflatc-inc-alt/flatcbuiltinroot.hpp zflatc-inc-alt/flatcbuiltinroot.cpp &: $(srcdir)/flatcbuiltinroot.fbs $(srcdir)/flatcbuiltin.fbs $(srcdir)/flatcbuiltinroot.fbs $(top_builddir)/zfb/src/zflatc$(EXEEXT)
	$(AM_V_GEN)$(MKDIR_P) zflatc-inc-alt
	$(AM_V_at)$(top_builddir)/zfb/src/zflatc $(srcdir)/flatcbuiltinroot.fbs -o zflatc-inc-alt/flatcbuiltinroot --alt
nodist_zfbinclude_alt_SOURCES += zflatc-inc-alt/flatcbuiltinagain.hpp zflatc-inc-alt/flatcbuiltinagain.cpp
zflatc-inc-alt/flatcbuiltinagain.hpp zflatc-inc-alt/flatcbuiltinagain.cpp &: $(srcdir)/flatcbuiltinagain.fbs $(srcdir)/flatcbuiltin.fbs $(srcdir)/flatcbuiltinroot.fbs $(top_builddir)/zfb/src/zflatc$(EXEEXT)
	$(AM_V_GEN)$(MKDIR_P) zflatc-inc-alt
	$(AM_V_at)$(top_builddir)/zfb/src/zflatc $(srcdir)/flatcbuiltinagain.fbs -o zflatc-inc-alt/flatcbuiltinagain --alt

noinst_PROGRAMS += zfbcheck_custom
zfbcheck_custom_SOURCES = zfbcheck.cc zfbaux.cc zflatcfixture.hh flatcref.hh
nodist_zfbcheck_custom_SOURCES = zflatccustom.hh zflatccustom.cc
zfbcheck_custom_CPPFLAGS = $(AM_CPPFLAGS) -include $(srcdir)/flatcref.hh -DZFLATC_HEADER='<zflatccustom.hh>' -DZFLATC_REF=flatcref::Base
zfbcheck_custom_LDFLAGS = @Z_LDFLAGS@
zfbcheck_custom-zfbcheck.$(OBJEXT) zfbcheck_custom-zfbaux.$(OBJEXT): zflatccustom.hh
zflatccustom.hh zflatccustom.cc &: zflatc-out/original.fbs $(top_builddir)/zfb/src/zflatc$(EXEEXT)
	$(AM_V_GEN)$(top_builddir)/zfb/src/zflatc $< -o zflatccustom --ref ::flatcref::Base
CLEANFILES += zflatccustom.hh zflatccustom.cc
zflatctest_DEPENDENCIES += zfbcheck_custom$(EXEEXT)
