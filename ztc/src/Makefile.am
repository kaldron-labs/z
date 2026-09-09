METASOURCES = AUTO
.cc.s:
	$(CXXCOMPILE) -c -S $<
%.i.cc: %.cc
	$(CXXCOMPILE) -C -E $< -o $@
AM_CPPFLAGS = -I$(top_srcdir)/zu/src -I$(top_srcdir)/zm/src \
	-I$(top_srcdir)/zt/src -I$(top_srcdir)/ze/src -I$(top_srcdir)/zf/src \
	-I$(top_srcdir)/zi/src -I$(top_srcdir)/zv/src \
	-I$(top_srcdir)/zfb/src -I$(top_srcdir)/zdb/src \
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
pkginclude_HEADERS = ZtcLib.hh ZtcVer.hh ZtcRing.hh \
	ZtcAppTypes.hh ZtcFB.hh ZtcMsg.hh ZtcApp.hh \
	${FBS_H}
noinst_HEADERS = ZtcAlert.hh ZtcFilter.hh ZtcAgent.hh
lib_LTLIBRARIES = libZtc.la
noinst_LTLIBRARIES = libZtcAgent.la
bin_PROGRAMS = ztcagent
libZtc_la_CPPFLAGS = $(AM_CPPFLAGS) -DZTC_EXPORTS
libZtc_la_CXXFLAGS = $(AM_CXXFLAGS) @Z_LIB_CXXFLAGS@
libZtc_la_LDFLAGS = @Z_SO_LDFLAGS@
libZtc_la_SOURCES = ZtcLib.cc ZtcApp.cc ZtcFB.cc
libZtc_la_LIBADD = \
	$(top_builddir)/zdb/src/libZdb.la \
	$(top_builddir)/zfb/src/libZfb.la $(top_builddir)/zv/src/libZv.la \
	$(top_builddir)/zi/src/libZi.la \
	$(top_builddir)/zf/src/libZf.la $(top_builddir)/ze/src/libZe.la \
	$(top_builddir)/zt/src/libZt.la $(top_builddir)/zm/src/libZm.la \
	$(top_builddir)/zu/src/libZu.la \
	@FBS_LIBS@ @Z_IO_LIBS@ @Z_ZT_LIBS@ @Z_MT_LIBS@ @Z_LIBS@

AGENT_CPPFLAGS = $(AM_CPPFLAGS) -I$(top_srcdir)/ztcp/src \
	-I$(top_srcdir)/ztls/src -I$(top_srcdir)/zquic/src \
	-I$(top_srcdir)/zhttp/src -I$(top_srcdir)/zws/src
libZtcAgent_la_CPPFLAGS = $(AGENT_CPPFLAGS)
libZtcAgent_la_SOURCES = ZtcAgent.cc ZtcAgent.hh
libZtcAgent_la_LIBADD = libZtc.la \
	$(top_builddir)/zws/src/libZws.la \
	$(top_builddir)/zhttp/src/libZhttp.la \
	$(top_builddir)/ztls/src/libZtls.la

ztcagent_CPPFLAGS = $(AGENT_CPPFLAGS)
ztcagent_SOURCES = ztcagent.cc
ztcagent_LDADD = libZtcAgent.la
