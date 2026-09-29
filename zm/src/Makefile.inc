# Shared sources for libZm and the separately instrumented heap test executable.
zm_common_sources = \
	$(zm_srcdir)/ZmAssert.cc \
	$(zm_srcdir)/ZmBackTrace.cc \
	$(zm_srcdir)/ZmGlobal.cc \
	$(zm_srcdir)/ZmHashMgr.cc \
	$(zm_srcdir)/ZmLib.cc \
	$(zm_srcdir)/ZmLock.cc \
	$(zm_srcdir)/ZmObjectDebug.cc \
	$(zm_srcdir)/ZmRandom.cc \
	$(zm_srcdir)/ZmRing.cc \
	$(zm_srcdir)/ZmScheduler.cc \
	$(zm_srcdir)/ZmSingleton.cc \
	$(zm_srcdir)/ZmSpecific.cc \
	$(zm_srcdir)/ZmTime.cc \
	$(zm_srcdir)/ZmThread.cc \
	$(zm_srcdir)/ZmTrap.cc \
	$(zm_srcdir)/ZtcQueue.cc \
	$(zm_srcdir)/ZtcHeap.cc \
	$(zm_srcdir)/ZtcHash.cc
