METASOURCES = AUTO
AM_CPPFLAGS = -I$(top_srcdir)/zu/src -I$(top_srcdir)/zm/src \
	-I$(top_srcdir)/zt/src -I$(top_srcdir)/ze/src -I$(top_srcdir)/zf/src \
	-I$(top_srcdir)/zi/src -I$(top_srcdir)/zpkt/src \
	-I$(top_srcdir)/zpkt/xdp -I$(top_srcdir)/zpkt/dpdk @Z_CPPFLAGS@
AM_CXXFLAGS = @Z_CXXFLAGS@
AM_LDFLAGS = @Z_LDFLAGS@
LDADD = $(top_builddir)/zpkt/src/libZpkt.la \
	$(top_builddir)/zi/src/libZi.la \
	$(top_builddir)/zf/src/libZf.la $(top_builddir)/ze/src/libZe.la \
	$(top_builddir)/zt/src/libZt.la $(top_builddir)/zm/src/libZm.la \
	$(top_builddir)/zu/src/libZu.la \
	@Z_IO_LIBS@ @Z_ZT_LIBS@ @Z_MT_LIBS@ @Z_LIBS@
noinst_PROGRAMS = ZpktEnvTest ZpktOffloadTest ZpktRingTest ZpktPoolTest ZpktInstanceTest ZpktClassifierTest
noinst_PROGRAMS += ZpktAfpPoolTest ZpktAfpInstanceTest
ZpktAfpPoolTest_SOURCES = ZpktAfpPoolTest.cc
ZpktAfpPoolTest_CPPFLAGS = $(AM_CPPFLAGS) -I$(top_srcdir)/zpkt/afp
ZpktAfpPoolTest_LDADD = $(top_builddir)/zpkt/afp/libZpktAfpCore.la $(LDADD)
ZpktAfpPoolTest_LDFLAGS = $(AM_LDFLAGS) -Wl,--wrap=posix_memalign
ZpktAfpInstanceTest_SOURCES = ZpktAfpInstanceTest.cc ../afp/ZpktAFP.cc
ZpktAfpInstanceTest_CPPFLAGS = $(AM_CPPFLAGS) -I$(top_srcdir)/zpkt/afp \
	-DZPKT_AFP_MODULE='"$(abs_top_builddir)/zpkt/afp/.libs/libZpkt_afp.so"'
ZpktAfpInstanceTest_LDADD = $(top_builddir)/zpkt/afp/libZpktAfpCore.la $(LDADD)
AFP_WRAPPED = socket bind eventfd setsockopt getsockopt recvmmsg sendmmsg poll write
ZpktAfpInstanceTest_LDFLAGS = $(AM_LDFLAGS) $(foreach sym,$(AFP_WRAPPED),-Xlinker --wrap=$(sym))
ZpktOffloadTest_SOURCES = ZpktOffloadTest.cc
ZpktRingTest_SOURCES = ZpktRingTest.cc
ZpktPoolTest_SOURCES = ZpktPoolTest.cc
ZpktInstanceTest_SOURCES = ZpktInstanceTest.cc
ZpktClassifierTest_SOURCES = ZpktClassifierTest.cc
if DPDK
noinst_PROGRAMS += ZpktDpdkPoolTest ZpktDpdkInstanceTest
ZpktDpdkPoolTest_SOURCES = ZpktDpdkPoolTest.cc
ZpktDpdkPoolTest_CPPFLAGS = $(AM_CPPFLAGS) $(DPDK_CFLAGS)
ZpktDpdkPoolTest_LDFLAGS = $(AM_LDFLAGS) -Wl,--no-as-needed -Wl,$(abs_top_builddir)/zpkt/dpdk/.libs/libZpkt_dpdk.so -Wl,--as-needed -Wl,-rpath,$(abs_top_builddir)/zpkt/dpdk/.libs
ZpktDpdkPoolTest_LDADD = $(LDADD) $(DPDK_LIBS)
ZpktDpdkInstanceTest_SOURCES = ZpktDpdkInstanceTest.cc
ZpktDpdkInstanceTest_CPPFLAGS = $(AM_CPPFLAGS) $(DPDK_CFLAGS) \
	-DZPKT_DPDK_MODULE='"$(abs_top_builddir)/zpkt/dpdk/.libs/libZpkt_dpdk.so"'
ZpktDpdkInstanceTest_LDFLAGS = $(AM_LDFLAGS) -Wl,--no-as-needed -Wl,$(abs_top_builddir)/zpkt/dpdk/.libs/libZpkt_dpdk.so -Wl,--as-needed -Wl,-rpath,$(abs_top_builddir)/zpkt/dpdk/.libs
ZpktDpdkInstanceTest_LDADD = $(LDADD) $(DPDK_LIBS)
endif
ZpktOffloadTest_LDFLAGS = $(AM_LDFLAGS) -Wl,--no-as-needed -Wl,$(abs_top_builddir)/zpkt/xdp/.libs/libZpkt_xdp.so -Wl,--as-needed -Wl,-rpath,$(abs_top_builddir)/zpkt/xdp/.libs
ZpktOffloadTest_LDADD = $(LDADD)
ZpktRingTest_LDFLAGS = $(AM_LDFLAGS) -Wl,--no-as-needed -Wl,$(abs_top_builddir)/zpkt/xdp/.libs/libZpkt_xdp.so -Wl,--as-needed -Wl,-rpath,$(abs_top_builddir)/zpkt/xdp/.libs
ZpktRingTest_LDADD = $(LDADD)
ZpktPoolTest_LDFLAGS = $(AM_LDFLAGS) -Wl,--no-as-needed -Wl,$(abs_top_builddir)/zpkt/xdp/.libs/libZpkt_xdp.so -Wl,--as-needed -Wl,-rpath,$(abs_top_builddir)/zpkt/xdp/.libs
ZpktPoolTest_LDADD = $(LDADD)
ZpktInstanceTest_LDFLAGS = $(AM_LDFLAGS) -Wl,--no-as-needed -Wl,$(abs_top_builddir)/zpkt/xdp/.libs/libZpkt_xdp.so -Wl,--as-needed -Wl,-rpath,$(abs_top_builddir)/zpkt/xdp/.libs
ZpktInstanceTest_LDADD = $(LDADD)
ZpktClassifierTest_LDFLAGS = $(AM_LDFLAGS) -Wl,--no-as-needed -Wl,$(abs_top_builddir)/zpkt/xdp/.libs/libZpkt_xdp.so -Wl,--as-needed -Wl,-rpath,$(abs_top_builddir)/zpkt/xdp/.libs
ZpktClassifierTest_LDADD = $(LDADD)

ZpktEnvTest_SOURCES = ZpktEnvTest.cc
ZpktEnvTest_CPPFLAGS = $(AM_CPPFLAGS) -DZPKT_TEST_MODULE_DIR='"$(abs_builddir)/.libs"'
noinst_HEADERS = ZpktTestCf.hh ZpktTestPlugin.hh
noinst_LTLIBRARIES = libZpktTestPlugin.la libZpktNullPlugin.la \
  libZpktThrowPlugin.la libZpktInitFailPlugin.la libZpktNoFactory.la
TEST_MODULE_FLAGS = $(AM_LDFLAGS) -module -avoid-version -rpath $(abs_builddir)/.libs
libZpktTestPlugin_la_SOURCES = ZpktTestPlugin.cc
libZpktTestPlugin_la_LDFLAGS = $(TEST_MODULE_FLAGS)
libZpktTestPlugin_la_LIBADD = $(LDADD)
libZpktNullPlugin_la_SOURCES = ZpktTestPlugin.cc
libZpktNullPlugin_la_CPPFLAGS = $(AM_CPPFLAGS) -DZPKT_TEST_NULL
libZpktNullPlugin_la_LDFLAGS = $(TEST_MODULE_FLAGS)
libZpktNullPlugin_la_LIBADD = $(LDADD)
libZpktThrowPlugin_la_SOURCES = ZpktTestPlugin.cc
libZpktThrowPlugin_la_CPPFLAGS = $(AM_CPPFLAGS) -DZPKT_TEST_THROW
libZpktThrowPlugin_la_LDFLAGS = $(TEST_MODULE_FLAGS)
libZpktThrowPlugin_la_LIBADD = $(LDADD)
libZpktInitFailPlugin_la_SOURCES = ZpktTestPlugin.cc
libZpktInitFailPlugin_la_CPPFLAGS = $(AM_CPPFLAGS) -DZPKT_TEST_INIT_FAIL
libZpktInitFailPlugin_la_LDFLAGS = $(TEST_MODULE_FLAGS)
libZpktInitFailPlugin_la_LIBADD = $(LDADD)
libZpktNoFactory_la_SOURCES = ZpktNoFactory.cc
libZpktNoFactory_la_LDFLAGS = $(TEST_MODULE_FLAGS)

EXTRA_DIST = README.md

.PHONY: test
test: $(noinst_PROGRAMS) $(noinst_LTLIBRARIES)
	prove $(noinst_PROGRAMS)
