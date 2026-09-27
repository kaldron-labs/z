METASOURCES = AUTO
AM_CPPFLAGS = -I$(top_srcdir)/zu/src -I$(top_srcdir)/zm/src \
	-I$(top_srcdir)/zt/src -I$(top_srcdir)/ze/src -I$(top_srcdir)/zf/src \
	-I$(top_srcdir)/zi/src -I$(top_srcdir)/ztls/src \
	-I$(top_srcdir)/zhttp/src \
	-I$(top_srcdir)/zfb/src -I$(top_srcdir)/zv/src \
	-I$(top_srcdir)/zdb/src -I$(top_srcdir)/zum/src \
	@Z_CPPFLAGS@
AM_CXXFLAGS = @Z_CXXFLAGS@
AM_LDFLAGS = @Z_LDFLAGS@
zumd_objdir = $(top_builddir)/zum/src
zumd_test_objects = \
	$(zumd_objdir)/zumd-zumd_core.$(OBJEXT) \
	$(zumd_objdir)/zumd-zumd_oauth.$(OBJEXT) \
	$(zumd_objdir)/zumd-zumd_oidc.$(OBJEXT) \
	$(zumd_objdir)/zumd-zumd_request.$(OBJEXT) \
	$(zumd_objdir)/zumd-zumd_sagas.$(OBJEXT) \
	$(zumd_objdir)/zumd-zumd_schema.$(OBJEXT) \
	$(zumd_objdir)/zumd-zumd_webauthn.$(OBJEXT)
LDADD = $(zumd_test_objects) $(top_builddir)/zum/src/libZum.la \
	$(top_builddir)/zrest/src/libZrest.la \
	$(top_builddir)/zhttp/src/libZhttp.la \
	$(top_builddir)/zdb/src/libZdb.la \
	$(top_builddir)/zv/src/libZv.la \
	$(top_builddir)/zfb/src/libZfb.la \
	$(top_builddir)/ztls/src/libZtls.la \
	$(top_builddir)/zi/src/libZi.la $(top_builddir)/zf/src/libZf.la \
	$(top_builddir)/ze/src/libZe.la $(top_builddir)/zt/src/libZt.la \
	$(top_builddir)/zm/src/libZm.la $(top_builddir)/zu/src/libZu.la \
	@FBS_LIBS@ @PTLS_LIBS@ @Z_IO_LIBS@ @Z_ZT_LIBS@ @Z_MT_LIBS@ @Z_LIBS@
noinst_PROGRAMS = zumrestarttest zumhttptest zumoidchttptest zumfederationtest zumclustertest zumvaulttest
zumvaulttest_SOURCES = zumvaulttest.cc
zumrestarttest_SOURCES = zumrestarttest.cc
zumhttptest_SOURCES = zumhttptest.cc
zumhttptest_LDADD = $(top_builddir)/zu/src/libZu.la @Z_LIBS@
zumfederationtest_SOURCES = zumfederationtest.cc
zumfederationtest_LDADD = $(top_builddir)/zu/src/libZu.la @Z_LIBS@
zumclustertest_SOURCES = zumclustertest.cc
zumclustertest_LDADD = $(top_builddir)/zu/src/libZu.la @Z_LIBS@
zumoidchttptest_SOURCES = zumoidchttptest.cc
EXTRA_DIST = README.md zumhttp.py zumssf.py zumdvault.py zumdrekey.py \
	zumd.cf zumidp.py zumfederation.py \
	zumcluster.py zumpingdiscoverytest.py

.PHONY: test
test: $(noinst_PROGRAMS)
	@set -e; \
	export PYTHONDONTWRITEBYTECODE=1; \
	prove ./zumpingdiscoverytest.py; \
	prove ./zumvaulttest; \
	dir=$$(mktemp -d); \
	trap 'for db in restart http ssf vault rekey federation cluster-a cluster-b; do \
	  rm -f "$$dir/$$db.db" "$$dir/$$db.db-wal" "$$dir/$$db.db-shm"; \
	done; rmdir "$$dir"' EXIT; \
	module=$(abs_top_builddir)/zdb_sqlite/src/.libs/libZdbSL.so; \
	ZDB_MODULE="$$module" ZDB_CONNECT="$$dir/restart.db" \
	  prove ./zumrestarttest; \
	ZDB_MODULE="$$module" ZDB_CONNECT="$$dir/http.db" \
	  prove ./zumhttptest; \
	ZDB_MODULE="$$module" ZDB_CONNECT="$$dir/vault.db" \
	  prove ./zumdvault.py; \
	ZDB_MODULE="$$module" ZDB_CONNECT="$$dir/rekey.db" \
	  prove ./zumdrekey.py; \
	ZDB_MODULE="$$module" ZDB_CONNECT="$$dir/ssf.db" \
	  prove ./zumssf.py; \
	prove ./zumoidchttptest; \
	ZDB_MODULE="$$module" ZDB_CONNECT="$$dir/federation.db" \
	  prove ./zumfederationtest; \
	ZDB_MODULE="$$module" ZDB_CONNECT="$$dir/cluster-a.db" \
	  ZUM_CLUSTER_PEER_CONNECT="$$dir/cluster-b.db" \
	  prove ./zumclustertest
