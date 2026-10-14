METASOURCES = AUTO
EXTRA_DIST = zumd_saga_objects.mk
AM_CPPFLAGS = -I$(top_srcdir)/zu/src -I$(top_srcdir)/zm/src \
	-I$(top_srcdir)/zt/src -I$(top_srcdir)/ze/src -I$(top_srcdir)/zf/src -I$(top_srcdir)/zi/src \
	-I$(top_srcdir)/ztls/src \
	-I$(top_srcdir)/zhttp/src -I$(top_srcdir)/zrest/src \
	-I$(top_srcdir)/zdb/src \
	@Z_CPPFLAGS@
AM_CXXFLAGS = @Z_CXXFLAGS@
AM_LDFLAGS = @Z_LDFLAGS@
pkginclude_HEADERS = ZumLib.hh ZumTypes.hh ZumURI.hh ZumJWTVerify.hh ZumService.hh ZumMgmt.hh
noinst_HEADERS = zum_cli.hh ZumVaultClient.hh ZumVaultService.hh ZumVaultUtil.hh \
	zumd.hh zumd_admin.hh zumd_authorize.hh zumd_db.hh zumd_discovery.hh zumd_secret.hh \
	zumd_scope.hh zumd_identity_db.hh zumd_app_db.hh zumd_provider_db.hh \
	zumd_key_db.hh zumd_request_db.hh zumd_db_ops.hh zumd_saga.hh zumd_saga_key_binding.hh \
	zumd_saga_session_cleanup.hh zumd_saga_session_cleanup_data.hh \
	zumd_saga_refresh_cleanup.hh zumd_saga_app_cleanup.hh \
	zumd_saga_grant_cleanup.hh zumd_saga_secret_rekey.hh \
	zumd_saga_ssf_delivery_add.hh zumd_saga_revoke.hh \
	zumd_saga_revoke_result.hh zumd_saga_consent_code.hh \
	zumd_saga_assignment_add.hh zumd_saga_catalog_publish.hh \
	zumd_saga_admin_result.hh zumd_saga_admin_ops.hh \
	zumd_saga_grant_delete.hh \
	zumd_saga_enrollment.hh zumd_saga_credential_add.hh \
	zumd_saga_recovery_start.hh zumd_saga_recovery_enroll.hh \
	zumd_saga_code_family.hh zumd_saga_app_enrollment.hh \
	zumd_saga_external_projection.hh \
	zumd_saga_role_edit.hh zumd_saga_action_edit.hh \
	zumd_saga_role_map_delete.hh zumd_saga_role_map_put.hh \
	zumd_saga_role_add.hh zumd_saga_role_delete.hh \
	zumd_saga_app_change.hh zumd_saga_user_edit.hh \
	zumd_saga_cred_edit.hh zumd_saga_provider_edit.hh \
	zumd_saga_client_edit.hh zumd_saga_key_retire.hh \
	zumd_saga_client_access_state.hh zumd_saga_admin_access_state.hh \
	zumd_saga_policy_put.hh zumd_saga_client_access_put.hh \
	zumd_saga_admin_access_put.hh zumd_saga_provider_add.hh \
	zumd_saga_user_invite.hh zumd_saga_client_add.hh \
	zumd_saga_key_add.hh zumd_saga_app_action_add.hh \
	zumd_saga_assignment_change.hh \
	zumd_saga_cleanup_data.hh \
	zumd_rekey.hh \
	zumd_saga_image.hh zumd_catalog.hh zumd_revoke.hh \
	zumd_ssf_db.hh \
	zumd_http.hh zumd_jwt.hh zumd_oauth.hh zumd_oidc.hh zumd_passkey.hh \
	zumd_request.hh zumd_server.hh zumd_session.hh zumd_token.hh zumd_webauthn.hh
lib_LTLIBRARIES = libZum.la
bin_PROGRAMS = zum zumd
libZum_la_CPPFLAGS = $(AM_CPPFLAGS) -DZUM_EXPORTS
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
	$(top_builddir)/zquic/src/libZquic.la \
	$(top_builddir)/ztls/src/libZtls.la \
	$(top_builddir)/zi/src/libZi.la $(top_builddir)/zf/src/libZf.la \
	$(top_builddir)/ze/src/libZe.la $(top_builddir)/zt/src/libZt.la \
	$(top_builddir)/zm/src/libZm.la $(top_builddir)/zu/src/libZu.la \
	@PTLS_LIBS@ @Z_MT_LIBS@
zum_SOURCES = zum.cc
zumd_CPPFLAGS = $(AM_CPPFLAGS) -I$(top_srcdir)/zv/src \
	-I$(top_srcdir)/ztcp/src -I$(top_srcdir)/zquic/src \
	-I$(top_srcdir)/ztc/src
zumd_LDADD = $(top_builddir)/ztc/src/libZtcApp.la libZum.la \
	$(top_builddir)/zrest/src/libZrest.la \
	$(top_builddir)/zhttp/src/libZhttp.la \
	$(top_builddir)/zquic/src/libZquic.la \
	$(top_builddir)/zdb/src/libZdb.la \
	$(top_builddir)/ztls/src/libZtls.la \
	$(top_builddir)/zv/src/libZv.la \
	$(top_builddir)/zi/src/libZi.la $(top_builddir)/zf/src/libZf.la \
	$(top_builddir)/ze/src/libZe.la $(top_builddir)/zt/src/libZt.la \
	$(top_builddir)/zm/src/libZm.la $(top_builddir)/zu/src/libZu.la \
	@PTLS_LIBS@ @Z_IO_LIBS@ @Z_ZT_LIBS@ @Z_MT_LIBS@ @Z_LIBS@
zumd_SOURCES = zumd.cc zumd_bootstrap.cc zumd_daemon.cc \
	zumd_daemon_admin.cc zumd_role_delete.cc \
	zumd_core.cc zumd_admin.cc zumd_authorize.cc zumd_db.cc \
	zumd_schema.cc zumd_key_validation.cc zumd_saga_admin_ops.cc \
	zumd_saga_catalog.cc \
	zumd_saga_submit.cc zumd_saga_key_binding.cc zumd_saga_session_cleanup.cc \
	zumd_saga_refresh_cleanup.cc zumd_saga_app_cleanup.cc \
	zumd_saga_grant_cleanup.cc zumd_saga_secret_rekey.cc \
	zumd_saga_ssf_delivery_add.cc zumd_saga_revoke.cc \
	zumd_saga_consent_code.cc zumd_saga_assignment_add.cc \
	zumd_saga_catalog_publish.cc \
	zumd_saga_enrollment.cc zumd_saga_credential_add.cc \
	zumd_saga_recovery_start.cc zumd_saga_recovery_enroll.cc \
	zumd_saga_code_family.cc zumd_saga_app_enrollment.cc \
	zumd_saga_external_projection.cc \
	zumd_saga_role_edit.cc zumd_saga_action_edit.cc \
	zumd_saga_role_map_delete.cc zumd_saga_role_map_put.cc \
	zumd_saga_role_add.cc zumd_saga_role_delete.cc \
	zumd_saga_app_change.cc zumd_saga_user_edit.cc \
	zumd_saga_cred_edit.cc zumd_saga_provider_edit.cc \
	zumd_saga_client_edit.cc zumd_saga_key_retire.cc \
	zumd_saga_client_access_state.cc zumd_saga_admin_access_state.cc \
	zumd_saga_policy_put.cc zumd_saga_client_access_put.cc \
	zumd_saga_admin_access_put.cc zumd_saga_provider_add.cc \
	zumd_saga_user_invite.cc zumd_saga_client_add.cc \
	zumd_saga_key_add.cc zumd_saga_app_action_add.cc \
	zumd_saga_assignment_change.cc \
	zumd_secret.cc zumd_rekey.cc \
	zumd_discovery.cc zumd_jwt.cc zumd_oauth.cc zumd_oidc.cc \
	zumd_passkey.cc zumd_request.cc zumd_server.cc zumd_session.cc zumd_token.cc \
	zumd_webauthn.cc zumd_revoke.cc

# The top-level traversal reaches Zum before Ztc. Build only the Ztc
# libraries here; ztc/src builds its Zum-dependent executables later.
$(top_builddir)/ztc/src/libZtcApp.la: libZum.la
	$(MAKE) -C $(top_builddir)/ztc/src lib-stage
zumd-zumd.$(OBJEXT): $(top_builddir)/ztc/src/libZtcApp.la
