METASOURCES = AUTO
AM_CPPFLAGS = -I$(top_srcdir)/zu/src -I$(top_srcdir)/zm/src \
	-I$(top_srcdir)/zt/src -I$(top_srcdir)/ze/src -I$(top_srcdir)/zf/src \
	-I$(top_srcdir)/zi/src -I$(top_srcdir)/ztls/src $(ZTLS_DBUS_CPPFLAGS) \
	@Z_CPPFLAGS@ @PTLS_CFLAGS@
AM_CXXFLAGS = @Z_CXXFLAGS@
AM_LDFLAGS = @Z_LDFLAGS@
LDADD = $(top_builddir)/ztls/src/libZtls.la \
	$(top_builddir)/zi/src/libZi.la \
	$(top_builddir)/zf/src/libZf.la $(top_builddir)/ze/src/libZe.la \
	$(top_builddir)/zt/src/libZt.la \
	$(top_builddir)/zm/src/libZm.la \
	$(top_builddir)/zu/src/libZu.la $(ZTLS_DBUS_LIB) \
	@PTLS_LIBS@ @Z_IO_LIBS@ @Z_ZT_LIBS@ @Z_MT_LIBS@ @Z_LIBS@
EXTRA_PROGRAMS = ztlsageinterop
ztlsageinterop_SOURCES = ztlsageinterop.cc
if LINUX
ZTLS_DBUS_CPPFLAGS = -I$(top_srcdir)/zdbus/src
ZTLS_DBUS_LIB = $(top_builddir)/zdbus/src/libZdbus.la
noinst_PROGRAMS = ztlsvaultinterop
ztlsvaultinterop_SOURCES = ztlsvaultinterop.cc
endif

.PHONY: test
test: all
if LINUX
	prove ztlsvaultinterop
endif
