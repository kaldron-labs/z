METASOURCES = AUTO
AM_CPPFLAGS = -I$(top_srcdir)/zu/src -I$(top_srcdir)/zm/src \
	-I$(top_srcdir)/zt/src -I$(top_srcdir)/ze/src -I$(top_srcdir)/zf/src -I${top_srcdir}/zi/src \
	@Z_CPPFLAGS@
AM_CXXFLAGS = @Z_CXXFLAGS@
AM_LDFLAGS = @Z_LDFLAGS@
libZtls_la_LDFLAGS = $(AM_LDFLAGS) @Z_SO_LDFLAGS@
pkginclude_HEADERS = ZtlsLib.hh Ztls.hh \
	ZtlsPico.hh \
	ZtlsBackend.hh \
	ZtlsHMAC.hh ZtlsMD.hh ZtlsTOTP.hh ZtlsRandom.hh \
	ZtlsPK.hh ZtlsPK_Data.hh ZtlsKEM.hh ZtlsHPKE.hh ZtlsAge.hh ZtlsCOSE.hh ZtlsSec.hh \
	ZtlsVault.hh ZtlsVaultStore.hh
noinst_HEADERS = ZtlsVaultSS.hh
lib_LTLIBRARIES = libZtls.la
libZtls_la_CPPFLAGS = $(AM_CPPFLAGS) $(ZTLS_DBUS_CPPFLAGS) @PTLS_CFLAGS@ @OPENSSL_CFLAGS@ -DZTLS_EXPORTS
libZtls_la_CFLAGS = $(AM_CFLAGS) @PTLS_CFLAGS@
libZtls_la_SOURCES = ZtlsLib.cc ZtlsBackend.cc ZtlsPico.cc ZtlsOpenSSL.cc ZtlsPK.cc ZtlsKEM.cc ZtlsHPKE.cc ZtlsAge.cc ZtlsCOSE.cc ZtlsTOTP.cc ZtlsSec.cc ZtlsVault.cc
if !LINUX
libZtls_la_SOURCES += ZtlsVaultWindows.cc
endif
if LINUX
libZtls_la_SOURCES += ZtlsVaultLinux.cc
ZTLS_DBUS_CPPFLAGS = -I$(top_srcdir)/zdbus/src
ZTLS_DBUS_LIB = $(top_builddir)/zdbus/src/libZdbus.la
endif
libZtls_la_LIBADD = $(top_builddir)/zi/src/libZi.la \
	$(top_builddir)/zf/src/libZf.la $(top_builddir)/ze/src/libZe.la \
	$(top_builddir)/zt/src/libZt.la \
	$(top_builddir)/zm/src/libZm.la \
	$(top_builddir)/zu/src/libZu.la \
	$(ZTLS_DBUS_LIB) \
	@PTLS_LIBS@ @OPENSSL_LIBS@ @Z_IO_LIBS@ @Z_ZT_LIBS@ @Z_MT_LIBS@ @Z_LIBS@
bin_PROGRAMS = ztotp
ztotp_LDADD = libZtls.la $(libZtls_la_LDADD)
ztotp_SOURCES = ztotp.cc
