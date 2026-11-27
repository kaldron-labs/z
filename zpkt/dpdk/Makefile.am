METASOURCES = AUTO
AM_CPPFLAGS = -I$(top_srcdir)/zu/src -I$(top_srcdir)/zm/src \
	-I$(top_srcdir)/zt/src -I$(top_srcdir)/ze/src -I$(top_srcdir)/zf/src \
	-I$(top_srcdir)/zi/src -I$(top_srcdir)/zpkt/src @Z_CPPFLAGS@
AM_CXXFLAGS = @Z_CXXFLAGS@
AM_LDFLAGS = @Z_LDFLAGS@
noinst_HEADERS = ZpktDpdkLib.hh ZpktDpdkCf.hh ZpktDpdk.hh ZpktDpdkNif.hh ZpktDpdkPool.hh
lib_LTLIBRARIES = libZpkt_dpdk.la
libZpkt_dpdk_la_CPPFLAGS = $(AM_CPPFLAGS) $(DPDK_CFLAGS) -DZPKT_DPDK_EXPORTS
libZpkt_dpdk_la_LDFLAGS = $(AM_LDFLAGS) -module -avoid-version
libZpkt_dpdk_la_SOURCES = ZpktDpdkPlugin.cc ZpktDpdk.cc ZpktDpdkNif.cc ZpktDpdkPool.cc
libZpkt_dpdk_la_LIBADD = $(top_builddir)/zpkt/src/libZpkt.la \
	$(top_builddir)/zi/src/libZi.la $(top_builddir)/zf/src/libZf.la \
	$(top_builddir)/ze/src/libZe.la $(top_builddir)/zt/src/libZt.la \
	$(top_builddir)/zm/src/libZm.la $(top_builddir)/zu/src/libZu.la \
	$(DPDK_LIBS) @Z_IO_LIBS@ @Z_ZT_LIBS@ @Z_MT_LIBS@ @Z_LIBS@
