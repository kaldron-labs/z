METASOURCES = AUTO
EXTRA_DIST = README.md
AM_CPPFLAGS = -I$(top_srcdir)/zu/src -I$(top_srcdir)/zm/src \
	-I$(top_srcdir)/zt/src -I$(top_srcdir)/ze/src -I$(top_srcdir)/zf/src -I$(top_srcdir)/zi/src \
	-I$(top_srcdir)/ztls/src \
	-I$(top_srcdir)/zv/src -I$(top_srcdir)/zdb/src \
	-I$(top_srcdir)/zhttp/src -I$(top_srcdir)/zrest/src \
	-I$(top_srcdir)/ztcp/src -I$(top_srcdir)/zquic/src \
	-I$(top_srcdir)/zum/src \
	@Z_CPPFLAGS@
AM_CXXFLAGS = @Z_CXXFLAGS@
AM_LDFLAGS = @Z_LDFLAGS@
zumd_objdir = $(top_builddir)/zum/src
include $(top_srcdir)/zum/src/zumd_saga_objects.mk
$(zumd_objdir)/zumd-%.$(OBJEXT):
	$(MAKE) -C $(zumd_objdir) $(@F)
zumd_test_objects = \
	$(zumd_objdir)/zumd-zumd_core.$(OBJEXT) \
	$(zumd_objdir)/zumd-zumd_admin.$(OBJEXT) \
	$(zumd_objdir)/zumd-zumd_authorize.$(OBJEXT) \
	$(zumd_objdir)/zumd-zumd_db.$(OBJEXT) \
	$(zumd_objdir)/zumd-zumd_discovery.$(OBJEXT) \
	$(zumd_objdir)/zumd-zumd_jwt.$(OBJEXT) \
	$(zumd_objdir)/zumd-zumd_oauth.$(OBJEXT) \
	$(zumd_objdir)/zumd-zumd_oidc.$(OBJEXT) \
	$(zumd_objdir)/zumd-zumd_passkey.$(OBJEXT) \
	$(zumd_objdir)/zumd-zumd_request.$(OBJEXT) \
	$(zumd_objdir)/zumd-zumd_key_validation.$(OBJEXT) \
	$(zumd_objdir)/zumd-zumd_saga_admin_ops.$(OBJEXT) \
	$(zumd_saga_objects) \
	$(zumd_objdir)/zumd-zumd_schema.$(OBJEXT) \
	$(zumd_objdir)/zumd-zumd_secret.$(OBJEXT) \
	$(zumd_objdir)/zumd-zumd_server.$(OBJEXT) \
	$(zumd_objdir)/zumd-zumd_session.$(OBJEXT) \
	$(zumd_objdir)/zumd-zumd_token.$(OBJEXT) \
	$(zumd_objdir)/zumd-zumd_webauthn.$(OBJEXT)
zumd_oidc_objects = \
	$(zumd_objdir)/zumd-zumd_oauth.$(OBJEXT) \
	$(zumd_objdir)/zumd-zumd_oidc.$(OBJEXT) \
	$(zumd_objdir)/zumd-zumd_key_validation.$(OBJEXT) \
	$(zumd_objdir)/zumd-zumd_saga_admin_ops.$(OBJEXT) \
	$(zumd_saga_objects) \
	$(zumd_objdir)/zumd-zumd_webauthn.$(OBJEXT)
zum_test_libs = $(top_builddir)/zum/src/libZum.la \
	$(top_builddir)/zrest/src/libZrest.la \
	$(top_builddir)/zhttp/src/libZhttp.la \
	$(top_builddir)/zquic/src/libZquic.la \
	$(top_builddir)/zdb/src/libZdbMem.la \
	$(top_builddir)/zdb/src/libZdb.la \
	$(top_builddir)/zv/src/libZv.la \
	$(top_builddir)/ztls/src/libZtls.la \
	$(top_builddir)/zi/src/libZi.la \
	$(top_builddir)/zf/src/libZf.la $(top_builddir)/ze/src/libZe.la $(top_builddir)/zt/src/libZt.la \
	$(top_builddir)/zm/src/libZm.la $(top_builddir)/zu/src/libZu.la \
	@PTLS_LIBS@ @Z_IO_LIBS@ @Z_ZT_LIBS@ @Z_MT_LIBS@ @Z_LIBS@
noinst_LTLIBRARIES = libZumTestSupport.la
libZumTestSupport_la_SOURCES = ZumTestSupport.cc ZumTestSupportIdentity.cc \
	ZumTestSupportOIDC.cc ZumTestSupportToken.cc ZumTestSupportKeys.cc \
	ZumTestSupport.hh
LDADD = $(zumd_test_objects) libZumTestSupport.la $(zum_test_libs)
noinst_PROGRAMS = ZumRecordsTest ZumMgmtTest ZumHTTPTest ZumOAuthTest \
	ZumAuthTest ZumDiscoveryTest ZumJWTTest ZumWebAuthnTest \
	ZumSagaCatalogTest ZumSagaIdentityTest ZumSagaOrganizationTest \
	ZumSagaAdminTest ZumSagaSessionTest ZumSagaKeysTest \
	ZumRuntimeIdentityTest ZumRuntimeOIDCTest ZumRuntimeTokenTest \
	ZumRuntimeRecoveryTest ZumRuntimeKeysTest \
	ZumRuntimeManagementTest ZumRuntimeRoleTest ZumRuntimeConsentTest \
	ZumRuntimeRevocationTest \
	ZumOIDCHTTPTest ZumClientTest ZumSecretTest ZumScopeTest
ZumSecretTest_SOURCES = ZumSecretTest.cc
ZumSecretTest_LDADD = $(zumd_objdir)/zumd-zumd_secret.$(OBJEXT) $(zum_test_libs)
ZumRecordsTest_SOURCES = ZumRecordsTest.cc
ZumMgmtTest_SOURCES = ZumMgmtTest.cc
ZumHTTPTest_SOURCES = ZumHTTPTest.cc
ZumOAuthTest_SOURCES = ZumOAuthTest.cc
ZumAuthTest_SOURCES = ZumAuthTest.cc
ZumDiscoveryTest_SOURCES = ZumDiscoveryTest.cc
ZumJWTTest_SOURCES = ZumJWTTest.cc
ZumWebAuthnTest_SOURCES = ZumWebAuthnTest.cc
ZumSagaCatalogTest_SOURCES = ZumSagaCatalogTest.cc
ZumSagaIdentityTest_SOURCES = ZumSagaIdentityTest.cc ZumSagaTestSupport.hh
ZumSagaOrganizationTest_SOURCES = ZumSagaOrganizationTest.cc ZumSagaTestSupport.hh
ZumSagaAdminTest_SOURCES = ZumSagaAdminTest.cc ZumSagaTestSupport.hh
ZumSagaSessionTest_SOURCES = ZumSagaSessionTest.cc ZumSagaTestSupport.hh
ZumSagaKeysTest_SOURCES = ZumSagaKeysTest.cc ZumSagaTestSupport.hh
ZumRuntimeIdentityTest_SOURCES = ZumRuntimeIdentityTest.cc
ZumRuntimeOIDCTest_SOURCES = ZumRuntimeOIDCTest.cc
ZumRuntimeTokenTest_SOURCES = ZumRuntimeTokenTest.cc
ZumRuntimeRecoveryTest_SOURCES = ZumRuntimeRecoveryTest.cc
ZumRuntimeKeysTest_SOURCES = ZumRuntimeKeysTest.cc
ZumRuntimeManagementTest_SOURCES = ZumRuntimeManagementTest.cc
ZumRuntimeRoleTest_SOURCES = ZumRuntimeRoleTest.cc
ZumRuntimeConsentTest_SOURCES = ZumRuntimeConsentTest.cc
ZumRuntimeRevocationTest_SOURCES = ZumRuntimeRevocationTest.cc
ZumOIDCHTTPTest_SOURCES = ZumOIDCHTTPTest.cc
ZumOIDCHTTPTest_LDADD = $(zumd_oidc_objects) $(zum_test_libs)
ZumClientTest_SOURCES = ZumClientTest.cc
ZumScopeTest_SOURCES = ZumScopeTest.cc
ZumScopeTest_LDADD = $(zumd_objdir)/zumd-zumd_core.$(OBJEXT) $(zum_test_libs)
ZumClientTest_LDADD = $(top_builddir)/zum/src/libZum.la \
	$(top_builddir)/ztls/src/libZtls.la \
	$(top_builddir)/zi/src/libZi.la \
	$(top_builddir)/zf/src/libZf.la $(top_builddir)/ze/src/libZe.la \
	$(top_builddir)/zt/src/libZt.la $(top_builddir)/zm/src/libZm.la \
	$(top_builddir)/zu/src/libZu.la \
	@PTLS_LIBS@ @Z_IO_LIBS@ @Z_ZT_LIBS@ @Z_MT_LIBS@ @Z_LIBS@

.PHONY: test
test: $(noinst_PROGRAMS)
	prove -j1 $(noinst_PROGRAMS)
