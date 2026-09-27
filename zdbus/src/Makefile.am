METASOURCES = AUTO
AM_CPPFLAGS = -I$(top_srcdir)/zu/src -I$(top_srcdir)/zm/src \
	-I$(top_srcdir)/zt/src -I$(top_srcdir)/ze/src -I$(top_srcdir)/zf/src \
	-I$(top_srcdir)/zi/src @Z_CPPFLAGS@
AM_CXXFLAGS = @Z_CXXFLAGS@
AM_LDFLAGS = @Z_LDFLAGS@ @Z_SO_LDFLAGS@
if LINUX
noinst_HEADERS = ZdbusName.hh ZdbusSerial.hh ZdbusStop.hh ZdbusValue.hh
pkginclude_HEADERS = ZdbusLib.hh Zdbus.hh ZdbusAddress.hh ZdbusAuth.hh \
	ZdbusEnvelope.hh ZdbusMessage.hh ZdbusAdapter.hh ZdbusCatalog.hh \
	ZdbusConnection.hh \
	ZdbusClient.hh ZdbusServer.hh
lib_LTLIBRARIES = libZdbus.la
libZdbus_la_CPPFLAGS = $(AM_CPPFLAGS) -DZDBUS_EXPORTS
libZdbus_la_SOURCES = ZdbusLib.cc ZdbusAddress.cc ZdbusAuth.cc \
	ZdbusEnvelope.cc ZdbusMessage.cc ZdbusAdapter.cc \
	ZdbusConnection.cc \
	ZdbusClient.cc ZdbusServer.cc
libZdbus_la_LIBADD = $(top_builddir)/zi/src/libZi.la \
	$(top_builddir)/zf/src/libZf.la $(top_builddir)/ze/src/libZe.la \
	$(top_builddir)/zt/src/libZt.la $(top_builddir)/zm/src/libZm.la \
	$(top_builddir)/zu/src/libZu.la \
	@Z_IO_LIBS@ @Z_ZT_LIBS@ @Z_MT_LIBS@ @Z_LIBS@
endif
