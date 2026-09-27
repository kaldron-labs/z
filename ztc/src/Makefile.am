METASOURCES = AUTO
.cc.s:
	$(CXXCOMPILE) -c -S $<
%.i.cc: %.cc
	$(CXXCOMPILE) -C -E $< -o $@
AM_CPPFLAGS = -I$(top_srcdir)/zu/src -I$(top_srcdir)/zm/src \
	-I$(top_srcdir)/zt/src -I$(top_srcdir)/ze/src -I$(top_srcdir)/zf/src \
	-I$(top_srcdir)/zi/src -I$(top_srcdir)/zv/src \
	-I$(top_srcdir)/zfb/src -I$(top_srcdir)/zdb/src \
	-I$(top_srcdir)/zum/src -I$(top_srcdir)/zrest/src -I$(top_srcdir)/zhttp/src \
	-I$(top_srcdir)/ztls/src \
	@Z_CPPFLAGS@
AM_CXXFLAGS = @Z_CXXFLAGS@
AM_LDFLAGS = @Z_LDFLAGS@
FBS = ztc_types.fbs ztc_telemetry.fbs \
	ztc_request.fbs ztc_msg.fbs
dist_noinst_DATA = $(addprefix fbs/,$(FBS))
FBS_H = ${FBS:%.fbs=%_fbs.h}
${FBS_H}: %_fbs.h: fbs/%.fbs $(top_builddir)/scripts/fbscodegen
	$(top_builddir)/scripts/fbscodegen -b --schema --bfbs-gen-embed --gen-mutable -c --cpp-std c++17 --cpp-field-case-style lower -I fbs -I $(top_srcdir)/zfb/src/fbs -I $(top_srcdir)/zdb/src/fbs $<
BUILT_SOURCES = ${FBS_H}
CLEANFILES = ${FBS_H}
pkginclude_HEADERS = ZtcLib.hh ZtcRing.hh \
	ZtcAppTypes.hh ZtcFB.hh ZtcMsg.hh ZtcApp.hh \
	${FBS_H}
noinst_HEADERS = ZtcAlert.hh ZtcFilter.hh ztcagent_config.hh \
	ztcagent_daemon.hh ztchub_daemon.hh
lib_LTLIBRARIES = libZtc.la libZtcApp.la
bin_PROGRAMS = ztcagent ztchub
libZtc_la_CPPFLAGS = $(AM_CPPFLAGS) -DZTC_EXPORTS
libZtc_la_LDFLAGS = @Z_SO_LDFLAGS@
libZtc_la_SOURCES = ZtcLib.cc ZtcFB.cc
libZtc_la_LIBADD = \
	$(top_builddir)/zum/src/libZum.la \
	$(top_builddir)/zdb/src/libZdb.la \
	$(top_builddir)/zdb/src/libZdbMem.la \
	$(top_builddir)/zfb/src/libZfb.la $(top_builddir)/zv/src/libZv.la \
	$(top_builddir)/zi/src/libZi.la \
	$(top_builddir)/zf/src/libZf.la $(top_builddir)/ze/src/libZe.la \
	$(top_builddir)/zt/src/libZt.la $(top_builddir)/zm/src/libZm.la \
	$(top_builddir)/zu/src/libZu.la \
	@FBS_LIBS@ @Z_IO_LIBS@ @Z_ZT_LIBS@ @Z_MT_LIBS@ @Z_LIBS@

libZtcApp_la_CPPFLAGS = $(AM_CPPFLAGS) -DZTC_EXPORTS
libZtcApp_la_LDFLAGS = @Z_SO_LDFLAGS@
libZtcApp_la_SOURCES = ZtcApp.cc
libZtcApp_la_LIBADD = libZtc.la

AGENT_CPPFLAGS = $(AM_CPPFLAGS) -I$(top_srcdir)/ztcp/src \
	-I$(top_srcdir)/ztls/src -I$(top_srcdir)/zquic/src \
	-I$(top_srcdir)/zhttp/src -I$(top_srcdir)/zws/src
ztcagent_CPPFLAGS = $(AGENT_CPPFLAGS)
ztcagent_SOURCES = ztcagent.cc ztcagent_daemon.cc
ztcagent_LDADD = libZtc.la \
	$(top_builddir)/zum/src/libZum.la \
	$(top_builddir)/zws/src/libZws.la \
	$(top_builddir)/zhttp/src/libZhttp.la \
	$(top_builddir)/ztls/src/libZtls.la \
	$(top_builddir)/zi/src/libZi.la

ztchub_CPPFLAGS = $(AGENT_CPPFLAGS)
ztchub_LDADD = libZtc.la \
	$(top_builddir)/zum/src/libZum.la \
	$(top_builddir)/zdb/src/libZdbMem.la \
	$(top_builddir)/zws/src/libZws.la \
	$(top_builddir)/zhttp/src/libZhttp.la \
	$(top_builddir)/ztls/src/libZtls.la
ztchub_SOURCES = ztchub.cc ztchub_daemon.cc ../../zum/example/pinghttp.cc
