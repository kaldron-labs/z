METASOURCES = AUTO
.cc.s:
	$(CXXCOMPILE) -c -S $<
%.i.cc: %.cc
	$(CXXCOMPILE) -C -E $< -o $@
AM_CPPFLAGS = -I$(top_srcdir)/zu/src -I$(top_srcdir)/zm/src \
	-I$(top_srcdir)/zt/src -I$(top_srcdir)/ze/src -I$(top_srcdir)/zf/src \
	-I$(top_srcdir)/zi/src -I$(top_srcdir)/ztls/src \
	-I$(top_srcdir)/zi/test \
	-I$(top_srcdir)/ztcp/src -I$(top_srcdir)/zquic/src \
	-I$(top_srcdir)/zquic/test -I$(top_srcdir)/zhttp/src \
	-I$(top_srcdir)/zhttp/test -I$(top_srcdir)/zhttp/util \
	-I$(top_srcdir)/zhttp/interop \
	@Z_CPPFLAGS@
AM_CXXFLAGS = @Z_CXXFLAGS@
AM_LDFLAGS = @Z_LDFLAGS@
LDADD = $(top_builddir)/zi/test/libZiTest.la \
	$(top_builddir)/zhttp/util/libzhttputil.la \
	$(top_builddir)/zhttp/src/libZhttp.la \
	$(top_builddir)/zquic/src/libZquic.la \
	$(top_builddir)/ztls/src/libZtls.la \
	$(top_builddir)/zi/src/libZi.la \
	$(top_builddir)/zf/src/libZf.la $(top_builddir)/ze/src/libZe.la \
	$(top_builddir)/zt/src/libZt.la $(top_builddir)/zm/src/libZm.la \
	$(top_builddir)/zu/src/libZu.la \
	@PTLS_LIBS@ @Z_IO_LIBS@ @Z_ZT_LIBS@ @Z_MT_LIBS@ @Z_LIBS@
noinst_PROGRAMS = zhttpmatrix zhttpapptest zhttpmultirequesttest \
	zhttpdtest zhttplifecycletest zhttpclientfallbacktest \
	zhttpserverstreamtest zhttpclientstreamtest \
	zhttpqirtest zhttpqirsmoketest zhttp3interoptest \
	zhttpdarkhttpdcompattest zhttpfallbacktest
noinst_HEADERS = zhttpitestports.hh zhttpinteropports.hh
zhttpmatrix_SOURCES = zhttpmatrix.cc
zhttpapptest_SOURCES = zhttpapptest.cc
zhttpmultirequesttest_SOURCES = zhttpmultirequesttest.cc
zhttpdtest_SOURCES = zhttpdtest.cc zhttpdtestopts.cc
zhttplifecycletest_SOURCES = zhttplifecycletest.cc
zhttpclientfallbacktest_SOURCES = zhttpclientfallbacktest.cc
zhttpserverstreamtest_SOURCES = zhttpserverstreamtest.cc
zhttpclientstreamtest_SOURCES = zhttpclientstreamtest.cc

zhttpqirtest_SOURCES = zhttpqirtest.cc
zhttpqirtest_LDADD = $(top_builddir)/zhttp/interop/libzhttpqir.la $(LDADD)
zhttpqirsmoketest_SOURCES = zhttpqirsmoketest.cc
zhttp3interoptest_SOURCES = zhttp3interoptest.cc
zhttpdarkhttpdcompattest_SOURCES = zhttpdarkhttpdcompattest.cc
zhttpfallbacktest_SOURCES = zhttpfallbacktest.cc

dist_noinst_SCRIPTS = zhttph2interoptest.sh
EXTRA_DIST = qir-script-test.sh \
	testdata/qir-results/fixture/metadata.env \
	testdata/qir-results/fixture/zquic-server_ngtcp2-client/client \
	testdata/qir-results/fixture/zquic-server_ngtcp2-client/collected_at \
	testdata/qir-results/fixture/zquic-server_ngtcp2-client/command \
	testdata/qir-results/fixture/zquic-server_ngtcp2-client/runner.rev \
	testdata/qir-results/fixture/zquic-server_ngtcp2-client/results.toml \
	testdata/qir-results/fixture/zquic-server_ngtcp2-client/server \
	testdata/qir-results/fixture/zquic-server_ngtcp2-client/status \
	testdata/qir-results/fixture/zquic-server_ngtcp2-client/stderr \
	testdata/qir-results/fixture/zquic-server_ngtcp2-client/stdout \
	testdata/qir-results/fixture/zquic-server_ngtcp2-client/tests

.PHONY: test
test: export ZI_LOGDIR = $(abs_top_builddir)/logs
test: $(noinst_PROGRAMS) $(top_builddir)/zhttp/interop/zhttpqir
	LSAN_OPTIONS="$${LSAN_OPTIONS:+$$LSAN_OPTIONS:}suppressions=$(top_builddir)/.lsan-suppressions" \
	prove -j1 $(noinst_PROGRAMS)
	$(SHELL) "$(srcdir)/qir-script-test.sh" "$(top_srcdir)" "$(top_builddir)"
	ZHTTP_MATRIX="./zhttpmatrix" \
	ZHTTP_CLIENT="$(top_builddir)/zhttp/util/zhttp" \
	ZHTTP_SERVER="$(top_builddir)/zhttp/util/zhttpd" \
	$(SHELL) "$(srcdir)/zhttph2interoptest.sh"
