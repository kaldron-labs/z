METASOURCES = AUTO
AM_CPPFLAGS = -I$(top_srcdir)/zu/src -I$(top_srcdir)/zm/src \
	-I$(top_srcdir)/zt/src -I$(top_srcdir)/ze/src -I$(top_srcdir)/zf/src -I$(top_srcdir)/zi/src \
	-I$(top_srcdir)/ztls/src -I$(top_srcdir)/zfb/src \
	-I$(top_srcdir)/zhttp/src -I$(top_srcdir)/zrest/src \
	-I$(top_srcdir)/zdb/src \
	@Z_CPPFLAGS@
AM_CXXFLAGS = @Z_CXXFLAGS@
AM_LDFLAGS = @Z_LDFLAGS@
FBS = \
	zum_issuer.fbs \
	zum_user.fbs \
	zum_cred.fbs \
	zum_action.fbs \
	zum_role.fbs \
	zum_scope.fbs \
	zum_client.fbs \
	zum_grant.fbs \
	zum_saga.fbs \
	zum_sign_key.fbs \
	zum_audit.fbs
FBS_H = ${FBS:%.fbs=%_fbs.h}
${FBS_H}: %_fbs.h: fbs/%.fbs $(top_builddir)/scripts/fbscodegen
	$(top_builddir)/scripts/fbscodegen -b --schema --bfbs-gen-embed -c --cpp-std c++17 --cpp-field-case-style lower -I fbs -I $(top_srcdir)/zfb/src/fbs $<
BUILT_SOURCES = ${FBS_H}
CLEANFILES = ${FBS_H}
pkginclude_HEADERS = ZumLib.hh Zum.hh ZumAdmin.hh ZumAuthorize.hh ZumDB.hh ZumDiscovery.hh ZumHTTP.hh ZumJWT.hh ZumOAuth.hh ZumOIDC.hh \
	ZumPasskey.hh ZumRequest.hh ZumServer.hh ZumToken.hh ZumWebAuthn.hh
nodist_pkginclude_HEADERS = ${FBS_H}
dist_noinst_DATA = $(addprefix fbs/,${FBS})
#fbsdir = $(datarootdir)/${PACKAGE}/fbs
#dist_fbs_DATA = fbs/telemetry.fbs
lib_LTLIBRARIES = libZum.la
bin_PROGRAMS = zum
libZum_la_CPPFLAGS = $(AM_CPPFLAGS) -DZUM_EXPORTS
libZum_la_CXXFLAGS = $(AM_CXXFLAGS) @Z_LIB_CXXFLAGS@
libZum_la_LDFLAGS = @Z_SO_LDFLAGS@
libZum_la_SOURCES = ZumLib.cc Zum.cc ZumAdmin.cc ZumAuthorize.cc ZumDB.cc ZumDiscovery.cc ZumJWT.cc ZumOAuth.cc ZumOIDC.cc \
	ZumPasskey.cc ZumRequest.cc ZumServer.cc ZumToken.cc ZumWebAuthn.cc
libZum_la_LIBADD = \
	$(top_builddir)/zrest/src/libZrest.la $(top_builddir)/zhttp/src/libZhttp.la \
	$(top_builddir)/zdb/src/libZdb.la \
	$(top_builddir)/zfb/src/libZfb.la $(top_builddir)/ztls/src/libZtls.la \
	$(top_builddir)/zi/src/libZi.la $(top_builddir)/zf/src/libZf.la $(top_builddir)/ze/src/libZe.la \
	$(top_builddir)/zt/src/libZt.la $(top_builddir)/zm/src/libZm.la \
	$(top_builddir)/zu/src/libZu.la \
	@FBS_LIBS@ @PTLS_LIBS@ \
	@Z_IO_LIBS@ @Z_ZT_LIBS@ @Z_MT_LIBS@ @Z_LIBS@
zum_CPPFLAGS = $(AM_CPPFLAGS) -I$(top_srcdir)/zv/src
zum_LDADD = libZum.la $(top_builddir)/zv/src/libZv.la
zum_SOURCES = zum.cc
