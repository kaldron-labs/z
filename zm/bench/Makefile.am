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
noinst_PROGRAMS = \
	ZmBench ZmRingBench ZmFnBench \
	ZmCacheTest ZmHashTest ZmHeapTest ZmHeapTest2 ZmLockTest ZmLHBench \
	ZmRWTest ZmRWLockTest ZmHashThread ZmLockTest2
ZmBench_SOURCES = ZmBench.cc
ZmRingBench_SOURCES = ZmRingBench.cc
ZmFnBench_SOURCES = ZmFnBench.cc
ZmCacheTest_SOURCES = ZmCacheTest.cc
ZmHashTest_SOURCES = ZmHashTest.cc
ZmHeapTest_SOURCES = ZmHeapTest.cc
ZmHeapTest2_SOURCES = ZmHeapTest2.cc
ZmLockTest_SOURCES = ZmLockTest.cc
ZmLHBench_SOURCES = ZmLHBench.cc
ZmRWTest_SOURCES = ZmRWTest.cc
ZmRWLockTest_SOURCES = ZmRWLockTest.cc
ZmHashThread_SOURCES = ZmHashThread.cc
ZmLockTest2_SOURCES = ZmLockTest2.cc
