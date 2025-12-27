METASOURCES = AUTO
AM_CPPFLAGS = -I$(top_srcdir)/zu/src -I$(top_srcdir)/zm/src \
	-I$(top_srcdir)/zt/src -I$(top_srcdir)/ze/src -I${top_srcdir}/zi/src \
	@Z_CPPFLAGS@
AM_CXXFLAGS = @Z_CXXFLAGS@
AM_LDFLAGS = @Z_LDFLAGS@ @Z_SO_LDFLAGS@
pkginclude_HEADERS = ZtlsLib.hh Ztls.hh \
	ZtlsHMAC.hh ZtlsMD.hh ZtlsTOTP.hh ZtlsRandom.hh \
	ZtlsPK.hh ZtlsPK_Data.hh Ztls_ed25519.h
lib_LTLIBRARIES = libZtls.la
libZtls_la_CPPFLAGS = $(AM_CPPFLAGS) @PTLS_CFLAGS@ -DZTLS_EXPORTS \
	-I./ed25519 -DED25519_CUSTOMHASH -DED25519_CUSTOMRANDOM
libZtls_la_CFLAGS = $(AM_CFLAGS) @PTLS_CFLAGS@
libZtls_la_SOURCES = ZtlsLib.cc ZtlsTOTP.cc \
	ed25519/ed25519.c
libZtls_la_LIBADD = $(top_builddir)/zi/src/libZi.la \
	$(top_builddir)/ze/src/libZe.la \
	$(top_builddir)/zt/src/libZt.la \
	$(top_builddir)/zm/src/libZm.la \
	$(top_builddir)/zu/src/libZu.la \
	@PTLS_LIBS@ @Z_IO_LIBS@ @Z_ZT_LIBS@ @Z_MT_LIBS@ @Z_LIBS@
bin_PROGRAMS = ztotp
ztotp_LDADD = libZtls.la $(libZtls_la_LDADD)
ztotp_SOURCES = ztotp.cc
