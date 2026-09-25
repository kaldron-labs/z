METASOURCES = AUTO
AM_CPPFLAGS = -I$(top_srcdir)/zu/src -I$(top_srcdir)/zm/src \
	-I$(top_srcdir)/zt/src -I$(top_srcdir)/ze/src -I$(top_srcdir)/zf/src \
	-I$(top_srcdir)/zi/src -I$(top_srcdir)/ztls/src \
	-I$(top_srcdir)/zi/test \
	@Z_CPPFLAGS@ @PTLS_CFLAGS@
AM_CXXFLAGS = @Z_CXXFLAGS@
AM_LDFLAGS = @Z_LDFLAGS@
LDADD = $(top_builddir)/zi/test/libZiTest.la \
	$(top_builddir)/ztls/src/libZtls.la \
	$(top_builddir)/zi/src/libZi.la \
	$(top_builddir)/zf/src/libZf.la $(top_builddir)/ze/src/libZe.la \
	$(top_builddir)/zt/src/libZt.la \
	$(top_builddir)/zm/src/libZm.la \
	$(top_builddir)/zu/src/libZu.la \
	@PTLS_LIBS@ @Z_IO_LIBS@ @Z_ZT_LIBS@ @Z_MT_LIBS@ @Z_LIBS@
noinst_PROGRAMS = ZtlsPKTest ZtlsX25519Test ZtlsKEMTest ZtlsHybridTest ZtlsHPKETest ZtlsAgeTest ZtlsBufHookTest ZtlsAsyncTest ZtlsSecTest
noinst_HEADERS = ZtlsTestPorts.hh
ZtlsPKTest_SOURCES = ZtlsPKTest.cc
ZtlsX25519Test_SOURCES = ZtlsX25519Test.cc
ZtlsKEMTest_SOURCES = ZtlsKEMTest.cc
ZtlsHybridTest_SOURCES = ZtlsHybridTest.cc
ZtlsHPKETest_SOURCES = ZtlsHPKETest.cc
ZtlsAgeTest_SOURCES = ZtlsAgeTest.cc ZtlsAgeVectors.hh ZtlsAgeSSHVectors.hh
ZtlsBufHookTest_SOURCES = ZtlsBufHookTest.cc
ZtlsAsyncTest_SOURCES = ZtlsAsyncTest.cc
ZtlsSecTest_SOURCES = ZtlsSecTest.cc
dist_noinst_SCRIPTS = Keygen.sh

.PHONY: test
test: export ZI_LOGDIR = $(abs_top_builddir)/logs
test: $(noinst_PROGRAMS)
	prove $(noinst_PROGRAMS)
