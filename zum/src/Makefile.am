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
	zum_request.fbs \
	zum_ssf.fbs \
	zum_ssf_delivery.fbs
FBS_H = ${FBS:%.fbs=%_fbs.h}
${FBS_H}: %_fbs.h: fbs/%.fbs $(top_builddir)/scripts/fbscodegen
	$(top_builddir)/scripts/fbscodegen -b --schema --bfbs-gen-embed -c --cpp-std c++17 --cpp-field-case-style lower -I fbs -I $(top_srcdir)/zfb/src/fbs $<
BUILT_SOURCES = ${FBS_H}
CLEANFILES = ${FBS_H}
pkginclude_HEADERS = ZumLib.hh ZumTypes.hh ZumURI.hh ZumJWTVerify.hh ZumService.hh ZumMgmt.hh
noinst_HEADERS = zumd.hh zumd_admin.hh zumd_authorize.hh zumd_db.hh zumd_discovery.hh zumd_secret.hh \
	zumd_db_context.hh zumd_identity_db.hh zumd_app_db.hh zumd_provider_db.hh \
	zumd_key_db.hh zumd_request_db.hh zumd_db_ops.hh zumd_sagas.hh zumd_role_delete.hh zumd_rekey.hh \
	zumd_saga_image.hh zumd_catalog.hh zumd_revoke.hh zumd_consent.hh \
	zumd_ssf_db.hh \
	zumd_http.hh zumd_jwt.hh zumd_oauth.hh zumd_oidc.hh zumd_passkey.hh \
	zumd_request.hh zumd_server.hh zumd_session.hh zumd_token.hh zumd_webauthn.hh
nodist_noinst_HEADERS = ${FBS_H}
dist_noinst_DATA = $(addprefix fbs/,${FBS})
lib_LTLIBRARIES = libZum.la
bin_PROGRAMS = zum zumd
libZum_la_CPPFLAGS = $(AM_CPPFLAGS) -DZUM_EXPORTS
libZum_la_CXXFLAGS = $(AM_CXXFLAGS) @Z_LIB_CXXFLAGS@
libZum_la_LDFLAGS = @Z_SO_LDFLAGS@
libZum_la_SOURCES = ZumLib.cc ZumJWTVerify.cc ZumService.cc ZumMgmt.cc
libZum_la_LIBADD = \
	$(top_builddir)/zhttp/src/libZhttp.la \
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
zum_SOURCES = zum.cc
zumd_CPPFLAGS = $(AM_CPPFLAGS) -I$(top_srcdir)/zv/src \
	-I$(top_srcdir)/ztcp/src -I$(top_srcdir)/zquic/src
zumd_LDADD = libZum.la \
	$(top_builddir)/zrest/src/libZrest.la \
	$(top_builddir)/zdb/src/libZdb.la \
	$(top_builddir)/zv/src/libZv.la
zumd_SOURCES = zumd.cc zumd_bootstrap.cc zumd_daemon.cc \
	zumd_daemon_admin.cc zumd_role_delete.cc \
	zumd_core.cc zumd_admin.cc zumd_authorize.cc zumd_db.cc \
	zumd_schema.cc zumd_sagas.cc zumd_secret.cc zumd_rekey.cc \
	zumd_discovery.cc zumd_jwt.cc zumd_oauth.cc zumd_oidc.cc \
	zumd_passkey.cc zumd_request.cc zumd_server.cc zumd_session.cc zumd_token.cc \
	zumd_webauthn.cc zumd_revoke.cc
