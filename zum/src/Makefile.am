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
	zum_app.fbs \
	zum_user.fbs \
	zum_cred.fbs \
	zum_membership.fbs \
	zum_action.fbs \
	zum_role.fbs \
	zum_scope.fbs \
	zum_audience.fbs \
	zum_client.fbs \
	zum_client_access.fbs \
	zum_admin_access.fbs \
	zum_provider.fbs \
	zum_auth_policy.fbs \
	zum_ext_identity.fbs \
	zum_role_map.fbs \
	zum_evidence.fbs \
	zum_session.fbs \
	zum_consent.fbs \
	zum_grant.fbs \
	zum_saga.fbs \
	zum_sign_key.fbs \
	zum_request.fbs
FBS_H = ${FBS:%.fbs=%_fbs.h}
${FBS_H}: %_fbs.h: fbs/%.fbs $(top_builddir)/scripts/fbscodegen
	$(top_builddir)/scripts/fbscodegen -b --schema --bfbs-gen-embed -c --cpp-std c++17 --cpp-field-case-style lower -I fbs -I $(top_srcdir)/zfb/src/fbs $<
BUILT_SOURCES = ${FBS_H}
CLEANFILES = ${FBS_H}
pkginclude_HEADERS = ZumLib.hh ZumTypes.hh ZumJWTVerify.hh ZumService.hh
noinst_HEADERS = Zum.hh ZumAdmin.hh ZumAuthorize.hh ZumDB.hh ZumDiscovery.hh ZumSecret.hh \
	ZumDBContext.hh ZumIdentityDB.hh ZumAppDB.hh ZumProviderDB.hh \
	ZumKeyDB.hh ZumRequestDB.hh ZumDBOps.hh ZumSagas.hh ZumRoleDelete.hh ZumRekey.hh \
	ZumSagaImage.hh ZumCatalog.hh ZumRevoke.hh ZumConsent.hh \
	ZumHTTP.hh ZumJWT.hh ZumOAuth.hh ZumOIDC.hh ZumMgmt.hh ZumPasskey.hh \
	ZumRequest.hh ZumServer.hh ZumSession.hh ZumToken.hh ZumWebAuthn.hh
nodist_noinst_HEADERS = ${FBS_H}
dist_noinst_DATA = $(addprefix fbs/,${FBS})
#fbsdir = $(datarootdir)/${PACKAGE}/fbs
#dist_fbs_DATA = fbs/telemetry.fbs
lib_LTLIBRARIES = libZum.la
noinst_LTLIBRARIES = libZumServer.la libZumUpstream.la
bin_PROGRAMS = zum zumd
libZum_la_CPPFLAGS = $(AM_CPPFLAGS) -DZUM_EXPORTS
libZum_la_CXXFLAGS = $(AM_CXXFLAGS) @Z_LIB_CXXFLAGS@
libZum_la_LDFLAGS = @Z_SO_LDFLAGS@
libZum_la_SOURCES = ZumLib.cc ZumJWTVerify.cc ZumService.cc
libZum_la_LIBADD = \
	$(top_builddir)/ztls/src/libZtls.la \
	$(top_builddir)/zi/src/libZi.la $(top_builddir)/zf/src/libZf.la $(top_builddir)/ze/src/libZe.la \
	$(top_builddir)/zt/src/libZt.la $(top_builddir)/zm/src/libZm.la \
	$(top_builddir)/zu/src/libZu.la \
	@PTLS_LIBS@ \
	@Z_IO_LIBS@ @Z_ZT_LIBS@ @Z_MT_LIBS@ @Z_LIBS@
zum_CPPFLAGS = $(AM_CPPFLAGS) -I$(top_srcdir)/zv/src \
	-I$(top_srcdir)/ztcp/src -I$(top_srcdir)/zquic/src
zum_LDADD = libZum.la \
	$(top_builddir)/zrest/src/libZrest.la \
	$(top_builddir)/zhttp/src/libZhttp.la \
	$(top_builddir)/ztls/src/libZtls.la \
	$(top_builddir)/zi/src/libZi.la $(top_builddir)/zf/src/libZf.la \
	$(top_builddir)/ze/src/libZe.la $(top_builddir)/zt/src/libZt.la \
	$(top_builddir)/zm/src/libZm.la $(top_builddir)/zu/src/libZu.la
zum_SOURCES = zum.cc ZumMgmt.cc ZumMgmt.hh
zumd_CPPFLAGS = $(AM_CPPFLAGS) -I$(top_srcdir)/zv/src \
	-I$(top_srcdir)/ztcp/src -I$(top_srcdir)/zquic/src
zumd_LDADD = libZumServer.la \
	libZumUpstream.la \
	libZum.la \
	$(top_builddir)/zrest/src/libZrest.la \
	$(top_builddir)/zdb/src/libZdb.la \
	$(top_builddir)/zv/src/libZv.la
zumd_SOURCES = zumd.cc ZumBootstrap.cc ZumBootstrap.hh ZumDaemon.cc ZumDaemon.hh \
	ZumRoleDelete.cc
libZumServer_la_SOURCES = Zum.cc ZumAdmin.cc ZumAuthorize.cc ZumDB.cc \
	ZumSchema.cc ZumSagas.cc ZumSecret.cc ZumRekey.cc \
	ZumDiscovery.cc ZumJWT.cc ZumMgmt.cc ZumOAuth.cc ZumOIDC.cc \
	ZumPasskey.cc ZumRequest.cc ZumServer.cc ZumSession.cc ZumToken.cc \
	ZumWebAuthn.cc
libZumServer_la_CPPFLAGS = $(zumd_CPPFLAGS) -DZUM_EXPORTS
libZumUpstream_la_SOURCES = ZumUpstream.cc ZumUpstream.hh
libZumUpstream_la_CPPFLAGS = $(zumd_CPPFLAGS)
