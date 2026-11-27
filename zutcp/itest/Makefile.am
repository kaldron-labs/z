METASOURCES = AUTO
AM_CPPFLAGS = -I$(top_srcdir)/zu/src -I$(top_srcdir)/zm/src \
	-I$(top_srcdir)/zt/src -I$(top_srcdir)/ze/src -I$(top_srcdir)/zf/src \
	-I$(top_srcdir)/zi/src -I$(top_srcdir)/zi/test \
	-I$(top_srcdir)/zutcp/src -I$(top_srcdir)/zpkt/src \
	-I$(top_srcdir)/zutcp/test -I$(top_srcdir)/zpkt/test \
	-DZUTCP_XDP_MODULE='"$(abs_top_builddir)/zpkt/xdp/.libs/libZpkt_xdp.so"' \
	-DZUTCP_TEST_PLUGIN='"$(abs_top_builddir)/zpkt/test/.libs/libZpktTestPlugin.so"' @Z_CPPFLAGS@
AM_CXXFLAGS = @Z_CXXFLAGS@
AM_LDFLAGS = @Z_LDFLAGS@
LDADD = $(top_builddir)/zi/test/libZiTest.la \
	$(top_builddir)/zutcp/src/libZutcp.la $(top_builddir)/zpkt/src/libZpkt.la \
	$(top_builddir)/zi/src/libZi.la \
	$(top_builddir)/zf/src/libZf.la $(top_builddir)/ze/src/libZe.la \
	$(top_builddir)/zt/src/libZt.la $(top_builddir)/zm/src/libZm.la \
	$(top_builddir)/zu/src/libZu.la \
	@Z_IO_LIBS@ @Z_ZT_LIBS@ @Z_MT_LIBS@ @Z_LIBS@
noinst_PROGRAMS = zutcppairtest zutcppooltest zutcpstacktest zutcpiptest zutcpmxtest zutcpapptest \
	zutcptlsapptest zutcpafptest
EXTRA_PROGRAMS = zutcpudptest
zutcppairtest_SOURCES = zutcppairtest.cc
zutcppooltest_SOURCES = zutcppooltest.cc
zutcpstacktest_SOURCES = zutcpstacktest.cc
zutcpiptest_SOURCES = zutcpiptest.cc
zutcpmxtest_SOURCES = zutcpmxtest.cc
zutcpapptest_SOURCES = zutcpapptest.cc
zutcpapptest_CPPFLAGS = $(AM_CPPFLAGS) -I$(top_srcdir)/ztcp/src
zutcpapptest_LDADD = $(LDADD) $(top_builddir)/ztcp/src/libZtcp.la
zutcptlsapptest_SOURCES = zutcpapptest.cc
zutcptlsapptest_CPPFLAGS = $(AM_CPPFLAGS) -DZUTCP_APP_TLS \
	-I$(top_srcdir)/ztls/src -I$(top_srcdir)/zdbus/src
zutcptlsapptest_LDADD = $(LDADD) $(top_builddir)/ztls/src/libZtls.la \
	$(top_builddir)/zdbus/src/libZdbus.la
zutcpafptest_SOURCES = zutcpafptest.cc
zutcpafptest_CPPFLAGS = $(AM_CPPFLAGS) -I$(top_srcdir)/ztcp/src \
	-DZUTCP_AFP_MODULE='"$(abs_top_builddir)/zpkt/afp/.libs/libZpkt_afp.so"' \
	-DZUTCP_AFP_FIXTURE='"$(abs_top_srcdir)/zutcp/itest/zutcpafpfixture.sh"' \
	-DZUTCP_AFP_PROGRAM='"$(abs_builddir)/zutcpafptest"'
zutcpafptest_LDADD = $(LDADD) $(top_builddir)/ztcp/src/libZtcp.la
zutcpudptest_SOURCES = zutcpudptest.cc
EXTRA_DIST = README.md zutcpipfixture.sh zutcppairfixture.sh zutcpmxfixture.sh \
	zutcpappfixture.sh zutcpmxlossfixture.sh zutcprecovery.py zutcpoptions.py zutcptfo.py \
	zutcpudpfixture.sh zutcpafpfixture.sh

if MLX
noinst_PROGRAMS += zutcpmlxtest
zutcpmlxtest_SOURCES = zutcpmlxtest.cc
zutcpmlxtest_CPPFLAGS = $(AM_CPPFLAGS) \
	-DZUTCP_MLX_MODULE='"$(abs_top_builddir)/zpkt/mlx/.libs/libZpkt_mlx.so"'
endif

if DPDK
noinst_PROGRAMS += zutcpdpdktest
zutcpdpdktest_SOURCES = zutcpdpdktest.cc zutcpdpdkfixture.hh zutcpdpdkfixture.cc
zutcpdpdktest_CPPFLAGS = $(AM_CPPFLAGS) $(DPDK_CFLAGS) \
	-I$(top_srcdir)/zpkt/itest -I$(top_srcdir)/zpkt/dpdk -I$(top_srcdir)/ztcp/src
zutcpdpdktest_LDFLAGS = $(AM_LDFLAGS) -Wl,--no-as-needed -Wl,$(abs_top_builddir)/zpkt/dpdk/.libs/libZpkt_dpdk.so -Wl,-rpath,$(abs_top_builddir)/zpkt/dpdk/.libs -Wl,--as-needed
zutcpdpdktest_LDADD = $(top_builddir)/zpkt/itest/libZpktDpdkFixture.la \
	$(LDADD) $(top_builddir)/ztcp/src/libZtcp.la $(DPDK_LIBS) -lrte_net_ring
endif

.PHONY: test
test: all
	prove $(noinst_PROGRAMS)
