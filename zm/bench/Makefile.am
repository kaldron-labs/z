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
	ZmCacheBench ZmHashBench ZmHeapBench ZmHeapTest2 ZmLockBench ZmLHBench \
	ZmRWBench ZmRWLockBench ZmHashThread ZmLockTest2
ZmBench_SOURCES = ZmBench.cc
ZmRingBench_SOURCES = ZmRingBench.cc
ZmFnBench_SOURCES = ZmFnBench.cc
ZmCacheBench_SOURCES = ZmCacheBench.cc
ZmHashBench_SOURCES = ZmHashBench.cc
ZmHeapBench_SOURCES = ZmHeapBench.cc
ZmHeapTest2_SOURCES = ZmHeapTest2.cc
ZmLockBench_SOURCES = ZmLockBench.cc
ZmLHBench_SOURCES = ZmLHBench.cc
ZmRWBench_SOURCES = ZmRWBench.cc
ZmRWLockBench_SOURCES = ZmRWLockBench.cc
ZmHashThread_SOURCES = ZmHashThread.cc
ZmLockTest2_SOURCES = ZmLockTest2.cc
