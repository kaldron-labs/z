METASOURCES = AUTO
.cc.s:
	$(CXXCOMPILE) -c -S $<
%.i.cc: %.cc
	$(CXXCOMPILE) -C -E $< -o $@
AM_CPPFLAGS = -I$(top_srcdir)/zu/src -I$(top_srcdir)/zm/src \
	-I$(top_srcdir)/zt/src -I$(top_srcdir)/ze/src -I$(top_srcdir)/zf/src -I$(top_srcdir)/zi/src \
	-I$(top_srcdir)/zfb/src \
	@Z_CPPFLAGS@
AM_CXXFLAGS = @Z_CXXFLAGS@
AM_LDFLAGS = @Z_LDFLAGS@
FBS = ztc_db.fbs ztc_types.fbs ztc_telemetry.fbs \
	ztc_request.fbs ztc_msg.fbs
FBS_H = ${FBS:%.fbs=%_fbs.h}
${FBS_H}: %_fbs.h: fbs/%.fbs $(top_builddir)/scripts/fbscodegen
	$(top_builddir)/scripts/fbscodegen -b --schema --bfbs-gen-embed --gen-mutable -c --cpp-std c++17 --cpp-field-case-style lower -I fbs -I $(top_srcdir)/zfb/src/fbs $<
BUILT_SOURCES = ${FBS_H}
CLEANFILES = ${FBS_H}
pkginclude_HEADERS = ZvLib.hh \
	ZtcRAGMap.hh ZtcAppTypes.hh ZtcFB.hh ZtcMsg.hh ZtcApp.hh \
	ZtcDBTable.hh ZtcDBHost.hh ZtcDB.hh \
	ZvCf.hh ZvYAML.hh ZvTOML.hh \
	ZvEngine.hh \
	ZvRingParams.hh ZvThreadParams.hh \
	ZvMxParams.hh \
	${FBS_H}
noinst_HEADERS = ZtcAlert.hh ZtcFilter.hh
lib_LTLIBRARIES = libZv.la
libZv_la_CPPFLAGS = $(AM_CPPFLAGS) -DZV_EXPORTS 
libZv_la_LDFLAGS = @Z_SO_LDFLAGS@
libZv_la_SOURCES = ZvLib.cc \
	ZtcDB.cc ZtcDBHost.cc ZtcDBTable.cc ZtcApp.cc ZtcFB.cc ZtcRAGMap.cc \
	ZvCf.cc ZvYAML.cc ZvTOML.cc ZvEngine.cc ZvThreadParams.cc
libZv_la_LIBADD = \
	$(top_builddir)/zfb/src/libZfb.la $(top_builddir)/zi/src/libZi.la \
	$(top_builddir)/zf/src/libZf.la $(top_builddir)/ze/src/libZe.la $(top_builddir)/zt/src/libZt.la \
	$(top_builddir)/zm/src/libZm.la $(top_builddir)/zu/src/libZu.la \
	@Z_IO_LIBS@ @Z_ZT_LIBS@ @Z_MT_LIBS@ @Z_LIBS@
