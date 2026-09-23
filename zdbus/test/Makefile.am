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
noinst_PROGRAMS = ZdbusAddressTest ZdbusAuthTest ZdbusEnvelopeTest \
	ZdbusMessageTest ZdbusAdapterTest ZdbusClientTest ZdbusServerTest
ZdbusAddressTest_SOURCES = ZdbusAddressTest.cc
ZdbusAuthTest_SOURCES = ZdbusAuthTest.cc
ZdbusEnvelopeTest_SOURCES = ZdbusEnvelopeTest.cc
ZdbusMessageTest_SOURCES = ZdbusMessageTest.cc
ZdbusAdapterTest_SOURCES = ZdbusAdapterTest.cc
ZdbusClientTest_SOURCES = ZdbusClientTest.cc
ZdbusServerTest_SOURCES = ZdbusServerTest.cc
endif

.PHONY: test
test: all
if LINUX
	prove ZdbusAddressTest ZdbusAuthTest ZdbusEnvelopeTest \
	ZdbusMessageTest ZdbusAdapterTest ZdbusClientTest ZdbusServerTest
endif
