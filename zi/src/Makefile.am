METASOURCES = AUTO
.cc.s:
	$(CXXCOMPILE) -c -S $<
%.i.cc: %.cc
	$(CXXCOMPILE) -C -E $< -o $@
AM_CPPFLAGS = -I$(top_srcdir)/zu/src -I$(top_srcdir)/zm/src \
	-I$(top_srcdir)/zt/src -I$(top_srcdir)/ze/src -I$(top_srcdir)/zf/src \
	-DZI_EXPORTS @Z_CPPFLAGS@
AM_CXXFLAGS = @Z_CXXFLAGS@
AM_LDFLAGS = @Z_LDFLAGS@ @Z_SO_LDFLAGS@
pkginclude_HEADERS = \
	ZiLib.hh ZiPlatform.hh \
	ZiDir.hh ZiFile.hh ZiGlob.hh ZiIP.hh  \
	ZiModule.hh ZiMultiplex.hh ZiMxParams.hh ZiMxConfig.hh \
	ZiProgram.hh ZiRing.hh ZiRingParams.hh ZiStackParams.hh ZiThreadParams.hh ZiDaemon.hh ZiPIDFile.hh \
	ZiIOBuf.hh ZiIOContext.hh ZiRx.hh ZiTx.hh ZiLog.hh ZiAssert.hh ZiCSV.hh \
	ZiTxStream.hh ZiFileTxStream.hh ZiRxStream.hh ZiEventLoop.hh \
	ZiResolver.hh ZiHeapTune.hh ZiHashTune.hh \
	ZtcHub.hh ZtcLink.hh ZtcMx.hh ZtcPool.hh
noinst_HEADERS = ZiMxEpoll.hh ZiMxIOCP.hh
lib_LTLIBRARIES = libZi.la
libZi_la_SOURCES = \
	ZiLib.cc ZiPlatform.cc \
	ZiDir.cc ZiFile.cc ZiGlob.cc ZiIOContext.hh ZiIP.cc \
	ZiMxParams.cc ZiThreadParams.cc ZiRingParams.cc ZiStackParams.cc \
	ZiModule.cc ZiMultiplex.cc ZiProgram.cc \
	ZiRing.cc ZiLog.cc ZiEventLoop.cc ZiDaemon.cc ZiPIDFile.cc \
	ZiResolver.cc ZtcHub.cc \
	ZtcLink.cc ZtcPool.cc
if LINUX
libZi_la_SOURCES += ZiMxEpoll.cc
endif
if MINGW
libZi_la_SOURCES += ZiMxIOCP.cc
endif
libZi_la_LIBADD = $(top_builddir)/zf/src/libZf.la $(top_builddir)/ze/src/libZe.la \
	$(top_builddir)/zt/src/libZt.la \
	$(top_builddir)/zm/src/libZm.la $(top_builddir)/zu/src/libZu.la \
	@Z_IO_LIBS@ @Z_ZT_LIBS@ @Z_MT_LIBS@ @Z_LIBS@
