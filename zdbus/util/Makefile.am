METASOURCES = AUTO
AM_CPPFLAGS = -I$(top_srcdir)/zu/src -I$(top_srcdir)/zm/src \
	-I$(top_srcdir)/zt/src -I$(top_srcdir)/ze/src -I$(top_srcdir)/zf/src \
	-I$(top_srcdir)/zi/src -I$(top_srcdir)/zdbus/src @Z_CPPFLAGS@
AM_CXXFLAGS = @Z_CXXFLAGS@
AM_LDFLAGS = @Z_LDFLAGS@
if LINUX
LDADD = $(top_builddir)/zdbus/src/libZdbus.la \
	$(top_builddir)/zi/src/libZi.la $(top_builddir)/zf/src/libZf.la \
	$(top_builddir)/ze/src/libZe.la $(top_builddir)/zt/src/libZt.la \
	$(top_builddir)/zm/src/libZm.la $(top_builddir)/zu/src/libZu.la \
	@Z_IO_LIBS@ @Z_ZT_LIBS@ @Z_MT_LIBS@ @Z_LIBS@
noinst_PROGRAMS = zdbusbus
zdbusbus_SOURCES = zdbusbus.cc
if HAVE_DBUS
noinst_PROGRAMS += zdbuspeer
zdbuspeer_SOURCES = zdbuspeer.cc
zdbuspeer_CPPFLAGS = $(AM_CPPFLAGS) @DBUS_CFLAGS@
zdbuspeer_LDADD = @DBUS_LIBS@
endif
endif
