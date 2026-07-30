METASOURCES = AUTO
AM_CPPFLAGS = -I$(top_srcdir)/zu/src \
	-DZM_EXPORTS @Z_CPPFLAGS@
AM_CXXFLAGS = @Z_CXXFLAGS@
AM_LDFLAGS = @Z_LDFLAGS@ @Z_SO_LDFLAGS@
pkginclude_HEADERS = \
	ZmAssert.hh ZmAtomic.hh \
	ZmAlloc.hh ZmAllocator.hh ZmHeap.hh ZmVAlloc.hh ZmVHeap.hh \
	ZmBackTrace.hh ZmBackTrace_.hh ZmBackTrace_print.hh \
	ZmBackTracer.hh ZmBackoff.hh ZmBitmap.hh ZmBlock.hh \
	ZmCleanup.hh ZmCondition.hh ZmQueue.hh \
	ZmContext.hh ZmFn.hh ZmFn_.hh ZmGlobal.hh ZmGuard.hh \
	ZmHash.hh ZmHashMgr.hh ZmNode.hh ZmNodeFn.hh ZmLHash.hh \
	ZmLib.hh ZmList.hh ZmLock.hh ZmLockTraits.hh ZmNoLock.hh \
	ZmObject.hh ZmObjectDebug.hh ZmPolymorph.hh ZmPLock.hh \
	ZmPQueue.hh ZmPlatform.hh ZmRBTree.hh ZmRWLock.hh ZmRandom.hh \
	ZmRef.hh ZmRing.hh ZmRingFn.hh \
	ZmScheduler.hh ZmSemaphore.hh \
	ZmSingleton.hh ZmSpecific.hh ZmSpinLock.hh \
	ZmThread.hh ZmTime.hh \
	ZmTimeInterval.hh ZmTimeout.hh ZmTopology.hh ZmTrap.hh \
	ZmEngine.hh ZmCache.hh ZmCacheStats.hh ZmDemangle.hh \
	ZmPolyHash.hh ZmPolyCache.hh ZmCodec.hh \
	ZmLocal.hh ZmStack.hh ZmStackAvail.hh \
	\
	ZtcTypes.hh ZtcHash.hh ZtcHeap.hh ZtcQueue.hh ZtcThread.hh
lib_LTLIBRARIES = libZm.la
libZm_la_SOURCES = \
	ZmAssert.cc ZmBackTrace.cc ZmGlobal.cc ZmHashMgr.cc ZmHeap.cc \
	ZmLib.cc ZmLock.cc ZmObjectDebug.cc ZmRandom.cc \
	ZmRing.cc ZmScheduler.cc \
	ZmSingleton.cc ZmSpecific.cc \
	ZmTime.cc ZmThread.cc ZmTrap.cc
libZm_la_LIBADD = $(top_builddir)/zu/src/libZu.la @Z_MT_LIBS@ -lbfd -liberty @Z_LIBS@
