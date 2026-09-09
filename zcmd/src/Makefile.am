METASOURCES = AUTO
.cc.s:
	$(CXXCOMPILE) -I$(top_srcdir)/zrl/src -c -S $<
%.i.cc: %.cc
	$(CXXCOMPILE) -I$(top_srcdir)/zrl/src -C -E $< -o $@
AM_CPPFLAGS = \
	-I$(top_srcdir)/zu/src -I$(top_srcdir)/zm/src \
	-I$(top_srcdir)/zt/src -I$(top_srcdir)/ze/src -I$(top_srcdir)/zf/src -I$(top_srcdir)/zi/src \
	-I$(top_srcdir)/zrl/src -I$(top_srcdir)/zv/src -I$(top_srcdir)/ztc/src \
	-I$(top_srcdir)/ztls/src -I$(top_srcdir)/zfb/src \
	-I$(top_srcdir)/zdb/src -I$(top_srcdir)/zum/src \
	@Z_CPPFLAGS@
AM_CXXFLAGS = @Z_CXXFLAGS@
AM_LDFLAGS = @Z_LDFLAGS@
FBS = \
	ztel_request.fbs \
	ztel_reqack.fbs \
	ztel_telemetry.fbs \
	ztel_thread_priority.fbs \
	ztel_engine_state.fbs \
	ztel_link_state.fbs \
	ztel_queue_type.fbs
FBS_H = ${FBS:%.fbs=%_fbs.h}
${FBS_H}: %_fbs.h: fbs/%.fbs $(top_builddir)/scripts/fbscodegen
	$(top_builddir)/scripts/fbscodegen -b --schema --bfbs-gen-embed -c --cpp-std c++17 --cpp-field-case-style lower -I fbs -I $(top_srcdir)/zfb/src/fbs -I $(top_srcdir)/zdb/src/fbs -I $(top_srcdir)/ztc/src/fbs $<
BUILT_SOURCES = ${FBS_H}
CLEANFILES = ${FBS_H}
pkginclude_HEADERS = ZcmdLib.hh \
	ZcmdDispatcher.hh ZcmdHost.hh \
	Ztel.hh ZtelClient.hh ZtelServer.hh
nodist_pkginclude_HEADERS = ${FBS_H}
dist_noinst_DATA = $(addprefix fbs/,${FBS})
lib_LTLIBRARIES = libZcmd.la
libZcmd_la_CPPFLAGS = $(AM_CPPFLAGS) -DZCMD_EXPORTS
libZcmd_la_CXXFLAGS = $(AM_CXXFLAGS) @Z_VISIBILITY_CXXFLAGS@
libZcmd_la_LDFLAGS = @Z_SO_LDFLAGS@
libZcmd_la_SOURCES = ZcmdLib.cc \
	ZcmdDispatcher.cc ZcmdHost.cc Ztel.cc
# no need to include libZrl, the dependency is limited to ZrlTypes.hh
libZcmd_la_LIBADD = \
	$(top_builddir)/zum/src/libZum.la $(top_builddir)/zdb/src/libZdb.la \
	$(top_builddir)/zfb/src/libZfb.la $(top_builddir)/ztls/src/libZtls.la \
	$(top_builddir)/ztc/src/libZtc.la $(top_builddir)/zv/src/libZv.la \
	$(top_builddir)/zi/src/libZi.la $(top_builddir)/zf/src/libZf.la $(top_builddir)/ze/src/libZe.la \
	$(top_builddir)/zt/src/libZt.la $(top_builddir)/zm/src/libZm.la \
	$(top_builddir)/zu/src/libZu.la \
	@FBS_LIBS@ \
	@Z_IO_LIBS@ @Z_ZT_LIBS@ @Z_MT_LIBS@ @Z_LIBS@
bin_PROGRAMS = zcmd
zcmd_CPPFLAGS = $(AM_CPPFLAGS) -I$(top_srcdir)/zrl/src
zcmd_LDADD = libZcmd.la \
	$(top_builddir)/zrl/src/libZrl.la @Z_TERM_LIBS@ @Z_LIBS@
zcmd_SOURCES = zcmd.cc
