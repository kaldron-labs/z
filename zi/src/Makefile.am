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
	ZiDir.hh ZiFile.hh ZiGlob.hh ZiIP.hh ZiLib.hh ZiModule.hh \
	ZiMultiplex.hh ZiPlatform.hh ZiProgram.hh ZiRing.hh ZiDaemon.hh ZiPIDFile.hh \
	ZiIOBuf.hh ZiRx.hh ZiTx.hh ZiLog.hh ZiAssert.hh ZiCSV.hh \
	ZiTxStream.hh ZiFileTxStream.hh ZiRxStream.hh ZiEventLoop.hh \
	ZiResolver.hh ZiHeapCSV.hh ZiHashCSV.hh \
	ZtcHub.hh ZtcLink.hh ZtcMx.hh ZtcPool.hh
lib_LTLIBRARIES = libZi.la
libZi_la_SOURCES = \
	ZiDir.cc ZiFile.cc ZiGlob.cc ZiIOContext.hh ZiIP.cc ZiLib.cc \
	ZiModule.cc ZiMultiplex.cc ZiPlatform.cc ZiProgram.cc \
	ZiRing.cc ZiLog.cc ZiEventLoop.cc ZiDaemon.cc ZiPIDFile.cc \
	ZiResolver.cc ZtcHub.cc \
	ZtcLink.cc ZtcPool.cc
libZi_la_LIBADD = $(top_builddir)/zf/src/libZf.la $(top_builddir)/ze/src/libZe.la \
	$(top_builddir)/zt/src/libZt.la \
	$(top_builddir)/zm/src/libZm.la $(top_builddir)/zu/src/libZu.la \
	@Z_IO_LIBS@ @Z_ZT_LIBS@ @Z_MT_LIBS@ @Z_LIBS@
