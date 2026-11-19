METASOURCES = AUTO
EXTRA_DIST = engine/README.md engine/LICENSE.Apache-2.0
AM_CPPFLAGS = -I$(srcdir) -I$(top_srcdir)/zu/src -I$(top_srcdir)/zm/src \
	-I$(top_srcdir)/zt/src -I$(top_srcdir)/ze/src -I$(top_srcdir)/zf/src -I${top_srcdir}/zi/src \
	@Z_CPPFLAGS@
AM_CXXFLAGS = @Z_CXXFLAGS@
AM_LDFLAGS = @Z_LDFLAGS@
libZtls_la_LDFLAGS = $(AM_LDFLAGS) @Z_SO_LDFLAGS@ $(ZTLS_BROTLI_LIBS)
pkginclude_HEADERS = ZtlsLib.hh Ztls.hh \
	ZtlsBuf.hh ZtlsCredentials.hh ZtlsCxn.hh ZtlsAlg.hh ZtlsKDF.hh ZtlsAEAD.hh ZtlsPKOps.hh \
	ZtlsHMAC.hh ZtlsMD.hh ZtlsTOTP.hh ZtlsRandom.hh \
	ZtlsPK.hh ZtlsPK_Data.hh ZtlsKEM.hh ZtlsHPKE.hh ZtlsAge.hh ZtlsCOSE.hh ZtlsSec.hh \
	ZtlsVault.hh ZtlsVaultStore.hh
pkginclude_HEADERS += ZtlsEngine.hh ZtlsEngineOpenSSL.hh ZtlsFusion.hh
noinst_HEADERS = ZtlsVaultSS.hh engine/chacha20poly1305.h \
	engine/certificate_compression.h
lib_LTLIBRARIES = libZtls.la
ZTLS_BROTLI_PACKAGES = libbrotlidec libbrotlienc
ZTLS_BROTLI_LIBS = $(shell $(PKG_CONFIG) --libs $(ZTLS_BROTLI_PACKAGES) 2>/dev/null)
ZTLS_BROTLI_CPPFLAGS = $(if $(ZTLS_BROTLI_LIBS),$(shell $(PKG_CONFIG) --cflags $(ZTLS_BROTLI_PACKAGES)) -DZtls_USE_BROTLI=1)
libZtls_la_CPPFLAGS = $(AM_CPPFLAGS) $(ZTLS_DBUS_CPPFLAGS) $(ZTLS_BROTLI_CPPFLAGS) @CRYPTO_CFLAGS@ -DZTLS_EXPORTS

# Keep Fusion ISA options on its implementation translation unit.
ZTLS_FUSION_CXXFLAGS = $(if $(filter x86_64 i386 i486 i586 i686,$(host_cpu)),-mavx2 -maes -mpclmul -mvaes -mvpclmulqdq)
noinst_LTLIBRARIES = libZtlsFusion.la
libZtlsFusion_la_CPPFLAGS = $(libZtls_la_CPPFLAGS)
libZtlsFusion_la_CXXFLAGS = $(AM_CXXFLAGS) $(ZTLS_FUSION_CXXFLAGS)
libZtlsFusion_la_SOURCES = engine/fusion.cc
libZtls_la_SOURCES = ZtlsLib.cc ZtlsAlg.cc ZtlsMD.cc ZtlsHMAC.cc ZtlsKDF.cc ZtlsAEAD.cc ZtlsPKOps.cc ZtlsRandom.cc ZtlsCredentials.cc ZtlsPK.cc ZtlsKEM.cc ZtlsHPKE.cc ZtlsAge.cc ZtlsCOSE.cc ZtlsTOTP.cc ZtlsSec.cc ZtlsVault.cc
libZtls_la_SOURCES += engine/tls13.cc engine/openssl.cc engine/hpke.cc \
	engine/certificate_compression.cc
if !LINUX
libZtls_la_SOURCES += ZtlsVaultWindows.cc
endif
if LINUX
libZtls_la_SOURCES += ZtlsVaultLinux.cc
ZTLS_DBUS_CPPFLAGS = -I$(top_srcdir)/zdbus/src
ZTLS_DBUS_LIB = $(top_builddir)/zdbus/src/libZdbus.la
endif
libZtls_la_LIBADD = libZtlsFusion.la $(top_builddir)/zi/src/libZi.la \
	$(top_builddir)/zf/src/libZf.la $(top_builddir)/ze/src/libZe.la \
	$(top_builddir)/zt/src/libZt.la \
	$(top_builddir)/zm/src/libZm.la \
	$(top_builddir)/zu/src/libZu.la \
	$(ZTLS_DBUS_LIB) \
	@CRYPTO_LIBS@ @Z_IO_LIBS@ @Z_ZT_LIBS@ @Z_MT_LIBS@ @Z_LIBS@
bin_PROGRAMS = ztotp
ztotp_LDADD = libZtls.la $(libZtls_la_LIBADD)
ztotp_SOURCES = ztotp.cc
