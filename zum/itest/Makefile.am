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
LDADD = $(top_builddir)/zum/src/libZumServer.la \
	$(top_builddir)/zum/src/libZum.la \
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
noinst_PROGRAMS = zumrestarttest zummigrationtest zumhttptest zumupstreamtest zumfederationtest zumclustertest
zumrestarttest_SOURCES = zumrestarttest.cc
zummigrationtest_SOURCES = zummigrationtest.cc
zumhttptest_SOURCES = zumhttptest.cc
zumhttptest_LDADD = $(top_builddir)/zu/src/libZu.la @Z_LIBS@
zumfederationtest_SOURCES = zumfederationtest.cc
zumfederationtest_LDADD = $(top_builddir)/zu/src/libZu.la @Z_LIBS@
zumclustertest_SOURCES = zumclustertest.cc
zumclustertest_LDADD = $(top_builddir)/zu/src/libZu.la @Z_LIBS@
zumupstreamtest_SOURCES = zumupstreamtest.cc
zumupstreamtest_LDADD = $(top_builddir)/zum/src/libZumUpstream.la $(LDADD)
EXTRA_DIST = README.md zumhttp.py zumd.cf zumidp.py zumfederation.py zumcluster.py

.PHONY: test
test: $(noinst_PROGRAMS)
	@dir=$$(mktemp -d); \
	trap 'for db in restart migration-source migration-target http federation cluster-a cluster-b; do \
	  rm -f "$$dir/$$db.db" "$$dir/$$db.db-wal" "$$dir/$$db.db-shm"; \
	done; rmdir "$$dir"' EXIT; \
	ZUM_TEST_MODULE=$(abs_top_builddir)/zdb_sqlite/src/.libs/libZdbSL.so \
	ZUM_TEST_CONNECT="$$dir/restart.db" \
	ZUM_MIGRATION_SOURCE_CONNECT="$$dir/migration-source.db" \
	ZUM_MIGRATION_TARGET_CONNECT="$$dir/migration-target.db" \
	ZUM_HTTP_CONNECT="$$dir/http.db" \
	ZUM_FEDERATION_CONNECT="$$dir/federation.db" \
	ZUM_CLUSTER_CONNECT_A="$$dir/cluster-a.db" \
	ZUM_CLUSTER_CONNECT_B="$$dir/cluster-b.db" \
	prove -j1 $(noinst_PROGRAMS)
