METASOURCES = AUTO
AM_CPPFLAGS = -I$(top_srcdir)/zu/src -I$(top_srcdir)/zm/src \
	-I$(top_srcdir)/zt/src -I$(top_srcdir)/ze/src -I$(top_srcdir)/zf/src \
	-I$(top_srcdir)/zi/src -I$(top_srcdir)/zpkt/src \
	-I$(top_srcdir)/zpkt/xdp -I$(top_srcdir)/zpkt/test \
	-DZPKT_XDP_MODULE='"$(abs_top_builddir)/zpkt/xdp/.libs/libZpkt_xdp.so"' \
	-DZPKT_DPDK_MODULE='"$(abs_top_builddir)/zpkt/dpdk/.libs/libZpkt_dpdk.so"' \
	-DZPKT_FIXTURE='"$(abs_top_srcdir)/zpkt/util/zpktfixture.sh"' \
	-DZPKT_PACKET='"$(abs_top_builddir)/zpkt/util/zpktpacket"' @Z_CPPFLAGS@
AM_CXXFLAGS = @Z_CXXFLAGS@
AM_LDFLAGS = @Z_LDFLAGS@
XDP_PRIVATE_LINK = -Wl,--no-as-needed -Wl,$(abs_top_builddir)/zpkt/xdp/.libs/libZpkt_xdp.so -Wl,-rpath,$(abs_top_builddir)/zpkt/xdp/.libs -Wl,--as-needed
DPDK_PRIVATE_LINK = -Wl,--no-as-needed -Wl,$(abs_top_builddir)/zpkt/dpdk/.libs/libZpkt_dpdk.so -Wl,-rpath,$(abs_top_builddir)/zpkt/dpdk/.libs -Wl,--as-needed
LDADD = $(top_builddir)/zpkt/src/libZpkt.la \
	$(top_builddir)/zi/src/libZi.la \
	$(top_builddir)/zf/src/libZf.la $(top_builddir)/ze/src/libZe.la \
	$(top_builddir)/zt/src/libZt.la $(top_builddir)/zm/src/libZm.la \
	$(top_builddir)/zu/src/libZu.la \
	@Z_IO_LIBS@ @Z_ZT_LIBS@ @Z_MT_LIBS@ @Z_LIBS@
noinst_PROGRAMS = zpktpackettest zpktfiltertest zpktlooptest zpktwaketest zpktniftest
noinst_PROGRAMS += zpktafp_test
zpktafp_test_SOURCES = zpktafp_test.cc
zpktafp_test_CPPFLAGS = $(AM_CPPFLAGS) \
	-DZPKT_AFP_MODULE='"$(abs_top_builddir)/zpkt/afp/.libs/libZpkt_afp.so"' \
	-DZPKT_AFP_FIXTURE='"$(abs_top_srcdir)/zpkt/itest/zpktafp_fixture.sh"'
EXTRA_DIST = README.md zpktafp_fixture.sh
zpktniftest_SOURCES = zpktniftest.cc
zpktpackettest_SOURCES = zpktpackettest.cc zpktfixture.hh
zpktpackettest_CPPFLAGS = $(AM_CPPFLAGS) \
	-DZPKT_XDP_TEST_MODULE='"$(abs_builddir)/.libs/libZpktTestXdp.so"'
noinst_LTLIBRARIES = libZpktTestXdp.la
libZpktTestXdp_la_SOURCES = ../xdp/ZpktXdp.cc
libZpktTestXdp_la_CPPFLAGS = $(AM_CPPFLAGS) -DZPKT_XDP_TEST
libZpktTestXdp_la_LDFLAGS = $(AM_LDFLAGS) -module -avoid-version \
	-rpath $(abs_builddir)/.libs $(XDP_PRIVATE_LINK)
libZpktTestXdp_la_LIBADD = $(LDADD)
zpktwaketest_SOURCES = zpktwaketest.cc zpktfixture.hh
zpktfiltertest_SOURCES = zpktfiltertest.cc
zpktpackettest_LDFLAGS = $(AM_LDFLAGS) $(XDP_PRIVATE_LINK) \
	-Wl,--export-dynamic-symbol=ZpktTestXdpDesc -Wl,--export-dynamic-symbol=ZpktTestXdpPublish \
	-Wl,--export-dynamic-symbol=ZpktTestXdpCQ
zpktpackettest_DEPENDENCIES = libZpktTestXdp.la $(filter %.la,$(LDADD))
zpktwaketest_LDFLAGS = $(AM_LDFLAGS) $(XDP_PRIVATE_LINK)
zpktfiltertest_LDFLAGS = $(AM_LDFLAGS) $(XDP_PRIVATE_LINK)
zpktlooptest_SOURCES = zpktlooptest.cc zpktlog.hh

if DPDK
noinst_LTLIBRARIES += libZpktDpdkFixture.la libZpktTestDpdk.la
libZpktDpdkFixture_la_SOURCES = zpktdkfixture.cc zpktdkfixture.hh
libZpktDpdkFixture_la_CPPFLAGS = $(AM_CPPFLAGS) $(DPDK_CFLAGS) -I$(top_srcdir)/zpkt/dpdk
libZpktTestDpdk_la_SOURCES = ../dpdk/ZpktDpdkPlugin.cc ../dpdk/ZpktDpdk.cc \
	../dpdk/ZpktDpdkNif.cc ../dpdk/ZpktDpdkPool.cc
libZpktTestDpdk_la_CPPFLAGS = $(AM_CPPFLAGS) $(DPDK_CFLAGS) -DZPKT_DPDK_TEST -DZPKT_DPDK_EXPORTS
libZpktTestDpdk_la_LDFLAGS = $(AM_LDFLAGS) -module -avoid-version -rpath $(abs_builddir)/.libs
libZpktTestDpdk_la_LIBADD = $(LDADD) $(DPDK_LIBS)
DPDK_FIXTURE_LIBS = libZpktDpdkFixture.la $(LDADD) $(DPDK_LIBS) -lrte_net_ring
noinst_PROGRAMS += zpktdkpackettest zpktdkwaketest zpktdklooptest
zpktniftest_CPPFLAGS = $(AM_CPPFLAGS) $(DPDK_CFLAGS) -I$(top_srcdir)/zpkt/dpdk
zpktniftest_LDADD = $(DPDK_FIXTURE_LIBS)
zpktniftest_LDFLAGS = $(AM_LDFLAGS) $(DPDK_PRIVATE_LINK)
zpktdkpackettest_SOURCES = zpktdkpackettest.cc
zpktdkpackettest_CPPFLAGS = $(AM_CPPFLAGS) $(DPDK_CFLAGS) -DZPKT_DPDK_TEST \
	-DZPKT_DPDK_TEST_MODULE='"$(abs_builddir)/.libs/libZpktTestDpdk.so"'
zpktdkpackettest_LDADD = $(DPDK_FIXTURE_LIBS)
zpktdkpackettest_LDFLAGS = $(AM_LDFLAGS) \
	-Wl,--export-dynamic-symbol=ZpktTestDpdkReturns
zpktdkpackettest_DEPENDENCIES = libZpktDpdkFixture.la libZpktTestDpdk.la $(filter %.la,$(LDADD))
zpktdkwaketest_SOURCES = zpktdkwaketest.cc
zpktdkwaketest_CPPFLAGS = $(AM_CPPFLAGS) $(DPDK_CFLAGS)
zpktdkwaketest_LDADD = $(DPDK_FIXTURE_LIBS)
zpktdkwaketest_LDFLAGS = $(AM_LDFLAGS) $(DPDK_PRIVATE_LINK)
zpktdklooptest_SOURCES = zpktdklooptest.cc
zpktdklooptest_CPPFLAGS = $(AM_CPPFLAGS) $(DPDK_CFLAGS)
zpktdklooptest_LDADD = $(DPDK_FIXTURE_LIBS)
zpktdklooptest_LDFLAGS = $(AM_LDFLAGS) $(DPDK_PRIVATE_LINK)
endif

.PHONY: test
test: $(noinst_PROGRAMS) $(noinst_LTLIBRARIES)
	prove $(noinst_PROGRAMS)
