METASOURCES = AUTO
EXTRA_DIST = README.md
AM_CPPFLAGS = -I$(top_srcdir)/zu/src -I$(top_srcdir)/zm/src \
	-I$(top_srcdir)/zt/src -I$(top_srcdir)/ze/src -I$(top_srcdir)/zf/src -I$(top_srcdir)/zi/src \
	-I$(top_srcdir)/ztls/src \
	-I$(top_srcdir)/zfb/src -I$(top_srcdir)/zv/src -I$(top_srcdir)/zdb/src \
	-I$(top_srcdir)/zhttp/src -I$(top_srcdir)/zrest/src \
	-I$(top_srcdir)/ztcp/src -I$(top_srcdir)/zquic/src \
	-I$(top_srcdir)/zum/src \
	@Z_CPPFLAGS@
AM_CXXFLAGS = @Z_CXXFLAGS@
AM_LDFLAGS = @Z_LDFLAGS@
zumd_objdir = $(top_builddir)/zum/src
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
	$(zumd_objdir)/zumd-zumd_sagas.$(OBJEXT) \
	$(zumd_objdir)/zumd-zumd_schema.$(OBJEXT) \
	$(zumd_objdir)/zumd-zumd_secret.$(OBJEXT) \
	$(zumd_objdir)/zumd-zumd_server.$(OBJEXT) \
	$(zumd_objdir)/zumd-zumd_session.$(OBJEXT) \
	$(zumd_objdir)/zumd-zumd_token.$(OBJEXT) \
	$(zumd_objdir)/zumd-zumd_webauthn.$(OBJEXT)
zumd_oidc_objects = \
	$(zumd_objdir)/zumd-zumd_oauth.$(OBJEXT) \
	$(zumd_objdir)/zumd-zumd_oidc.$(OBJEXT) \
	$(zumd_objdir)/zumd-zumd_sagas.$(OBJEXT) \
	$(zumd_objdir)/zumd-zumd_webauthn.$(OBJEXT)
zum_test_libs = $(top_builddir)/zum/src/libZum.la \
	$(top_builddir)/zrest/src/libZrest.la \
	$(top_builddir)/zhttp/src/libZhttp.la \
	$(top_builddir)/zdb/src/libZdbMem.la \
	$(top_builddir)/zdb/src/libZdb.la \
	$(top_builddir)/zv/src/libZv.la \
	$(top_builddir)/zfb/src/libZfb.la \
	$(top_builddir)/ztls/src/libZtls.la \
	$(top_builddir)/zi/src/libZi.la \
	$(top_builddir)/zf/src/libZf.la $(top_builddir)/ze/src/libZe.la $(top_builddir)/zt/src/libZt.la \
	$(top_builddir)/zm/src/libZm.la $(top_builddir)/zu/src/libZu.la \
	@PTLS_LIBS@ @Z_IO_LIBS@ @Z_ZT_LIBS@ @Z_MT_LIBS@ @Z_LIBS@
LDADD = $(zumd_test_objects) $(zum_test_libs)
noinst_PROGRAMS = ZumTest ZumOIDCHTTPTest ZumClientTest ZumSecretTest
ZumSecretTest_SOURCES = ZumSecretTest.cc
ZumSecretTest_LDADD = $(zumd_objdir)/zumd-zumd_secret.$(OBJEXT) $(zum_test_libs)
ZumTest_SOURCES = ZumTest.cc
ZumOIDCHTTPTest_SOURCES = ZumOIDCHTTPTest.cc
ZumOIDCHTTPTest_LDADD = $(zumd_oidc_objects) $(zum_test_libs)
ZumClientTest_SOURCES = ZumClientTest.cc
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
