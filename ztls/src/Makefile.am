METASOURCES = AUTO
AM_CPPFLAGS = -I$(top_srcdir)/zu/src -I$(top_srcdir)/zm/src \
	-I$(top_srcdir)/zt/src -I$(top_srcdir)/ze/src -I$(top_srcdir)/zf/src -I${top_srcdir}/zi/src \
	@Z_CPPFLAGS@
AM_CXXFLAGS = @Z_CXXFLAGS@
AM_LDFLAGS = @Z_LDFLAGS@ @Z_SO_LDFLAGS@
pkginclude_HEADERS = ZtlsLib.hh Ztls.hh ZtlsCreds.hh \
	ZtlsPico.hh \
	ZtlsBackend.hh \
	ZtlsHMAC.hh ZtlsMD.hh ZtlsTOTP.hh ZtlsRandom.hh \
	ZtlsPK.hh ZtlsPK_Data.hh ZtlsCOSE.hh ZtlsSec.hh
lib_LTLIBRARIES = libZtls.la
libZtls_la_CPPFLAGS = $(AM_CPPFLAGS) @PTLS_CFLAGS@ -DZTLS_EXPORTS
libZtls_la_CXXFLAGS = $(AM_CXXFLAGS) @Z_LIB_CXXFLAGS@
libZtls_la_CFLAGS = $(AM_CFLAGS) @PTLS_CFLAGS@
libZtls_la_SOURCES = ZtlsLib.cc ZtlsBackend.cc ZtlsPico.cc ZtlsOpenSSL.cc ZtlsCreds.cc ZtlsPK.cc ZtlsCOSE.cc ZtlsTOTP.cc ZtlsSec.cc
libZtls_la_LIBADD = $(top_builddir)/zi/src/libZi.la \
	$(top_builddir)/zf/src/libZf.la $(top_builddir)/ze/src/libZe.la \
	$(top_builddir)/zt/src/libZt.la \
	$(top_builddir)/zm/src/libZm.la \
	$(top_builddir)/zu/src/libZu.la \
	@PTLS_LIBS@ @Z_IO_LIBS@ @Z_ZT_LIBS@ @Z_MT_LIBS@ @Z_LIBS@
bin_PROGRAMS = ztotp
ztotp_LDADD = libZtls.la $(libZtls_la_LDADD)
ztotp_SOURCES = ztotp.cc
