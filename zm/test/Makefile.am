METASOURCES = AUTO
.cc.s:
	$(CXXCOMPILE) -c -S $<
%.i.cc: %.cc
	$(CXXCOMPILE) -C -E $< -o $@
AM_CPPFLAGS = -I$(top_srcdir)/zu/src -I$(top_srcdir)/zm/src \
	@Z_CPPFLAGS@
AM_CXXFLAGS = @Z_CXXFLAGS@
AM_LDFLAGS = @Z_LDFLAGS@
LDADD = $(top_builddir)/zm/src/libZm.la $(top_builddir)/zu/src/libZu.la \
	@Z_MT_LIBS@ @Z_LIBS@
lib_LTLIBRARIES = libZmBTTest.la libZmBTTest2.la
libZmBTTest_la_SOURCES = libZmBTTest.cc
libZmBTTest_la_LDFLAGS = @Z_LDFLAGS@ @Z_SO_LDFLAGS@
libZmBTTest_la_LIBADD = libZmBTTest2.la
libZmBTTest2_la_SOURCES = libZmBTTest2.cc
libZmBTTest2_la_LDFLAGS = @Z_LDFLAGS@ @Z_SO_LDFLAGS@
TESTPROGS = \
	ZmRingTest2 ZmRingTest3 \
	ZmFnTest ZmRBTest \
	ZmBitmapTest ZmSchedTest ZmTest ZmHashTest2 \
	ZmLHTest ZmHashCleanup \
	ZmTIDTest ZmAllocTest ZmDemangleTest \
	ZmPQueueTest ZmPQueueTest2 ZmPQueueTest3 ZmPQueueTest4 \
	ZmTimeTest ZmAssertTest ZmPolyHashTest ZmPolyCacheTest
if MINGW
bin_PROGRAMS = ZmBTTest
noinst_PROGRAMS = ${TESTPROGS}
else
noinst_PROGRAMS = ${TESTPROGS} ZmBTTest
endif
.PHONY: test
test: $(noinst_PROGRAMS)
	prove $(noinst_PROGRAMS)
ZmBTTest_SOURCES = ZmBTTest.cc
ZmBTTest_LDADD = libZmBTTest.la ${LDADD}
ZmFnTest_SOURCES = ZmFnTest.cc
ZmRBTest_SOURCES = ZmRBTest.cc
ZmBitmapTest_SOURCES = ZmBitmapTest.cc
ZmSchedTest_SOURCES = ZmSchedTest.cc
ZmTest_SOURCES = ZmTest.cc
ZmHashTest2_SOURCES = ZmHashTest2.cc
ZmLHTest_SOURCES = ZmLHTest.cc
ZmHashCleanup_SOURCES = ZmHashCleanup.cc
ZmPQueueTest_SOURCES = ZmPQueueTest.cc
ZmPQueueTest2_SOURCES = ZmPQueueTest2.cc
ZmPQueueTest3_SOURCES = ZmPQueueTest3.cc
ZmPQueueTest4_SOURCES = ZmPQueueTest4.cc
ZmRingTest2_SOURCES = ZmRingTest2.cc
ZmTIDTest_SOURCES = ZmTIDTest.cc
ZmAllocTest_SOURCES = ZmAllocTest.cc
ZmDemangleTest_SOURCES = ZmDemangleTest.cc
ZmTimeTest_SOURCES = ZmTimeTest.cc
ZmAssertTest_SOURCES = ZmAssertTest.cc
ZmPolyHashTest_SOURCES = ZmPolyHashTest.cc
ZmPolyCacheTest_SOURCES = ZmPolyCacheTest.cc
ZmRingTest3_SOURCES = ZmRingTest3.cc
