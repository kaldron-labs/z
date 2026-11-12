METASOURCES = AUTO
AM_CPPFLAGS = -I$(top_srcdir)/zu/src -I$(top_srcdir)/zm/src \
	-I$(top_srcdir)/zt/src -I$(top_srcdir)/ze/src -I$(top_srcdir)/zf/src \
	-I$(top_srcdir)/zi/src -I$(top_srcdir)/zxdp/src \
	-DZXDP_FIXTURE='"$(abs_top_srcdir)/zxdp/util/zxdpfixture.sh"' \
	-DZXDP_PACKET='"$(abs_top_builddir)/zxdp/util/zxdppacket"' @Z_CPPFLAGS@
AM_CXXFLAGS = @Z_CXXFLAGS@
AM_LDFLAGS = @Z_LDFLAGS@
LDADD = $(top_builddir)/zxdp/src/libZxdp.la \
	$(top_builddir)/zi/src/libZi.la \
	$(top_builddir)/zf/src/libZf.la $(top_builddir)/ze/src/libZe.la \
	$(top_builddir)/zt/src/libZt.la $(top_builddir)/zm/src/libZm.la \
	$(top_builddir)/zu/src/libZu.la \
	@Z_IO_LIBS@ @Z_ZT_LIBS@ @Z_MT_LIBS@ @Z_LIBS@
noinst_PROGRAMS = zxdppackettest zxdpfiltertest zxdplooptest zxdpwaketest
zxdppackettest_SOURCES = zxdppackettest.cc
zxdpwaketest_SOURCES = zxdpwaketest.cc
zxdpfiltertest_SOURCES = zxdpfiltertest.cc
zxdplooptest_SOURCES = zxdplooptest.cc zxdplog.hh

.PHONY: test
test: $(noinst_PROGRAMS)
	prove $(noinst_PROGRAMS)
