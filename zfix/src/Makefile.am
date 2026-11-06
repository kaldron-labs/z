METASOURCES = AUTO
AM_CPPFLAGS = -I$(top_srcdir)/zu/src -I$(top_srcdir)/zm/src -I$(top_srcdir)/zt/src -I$(top_srcdir)/ze/src -I$(top_srcdir)/zf/src -I$(top_srcdir)/zfix/src @Z_CPPFLAGS@
AM_CXXFLAGS = @Z_CXXFLAGS@
AM_LDFLAGS = @Z_LDFLAGS@
pkginclude_HEADERS = ZfixFmtLib.hh ZfixFmt.hh ZfixFields.hh ZfixFrame.hh ZfixLib.hh \
	Zfix.hh ZfixIOBuf.hh ZfixWork.hh ZfixContext.hh ZfixDispatch.hh ZfixSession.hh ZfixHooks.hh ZfixLinkState.hh ZfixSeq.hh ZfixRxMsg.hh ZfixRx.hh ZfixRxFramer.hh ZfixWire.hh ZfixLink.hh ZfixNet.hh ZfixRaw.hh ZfixIngress.hh ZfixStats.hh ZfixTrace.hh ZfixTxMsg.hh ZfixTx.hh ZfixNotice.hh ZfixLinkTx.hh ZfixClient.hh ZfixServer.hh
lib_LTLIBRARIES = libZfixFmt.la libZfix.la
libZfixFmt_la_CPPFLAGS = $(AM_CPPFLAGS) -DZFIXFMT_EXPORTS
libZfixFmt_la_LDFLAGS = @Z_SO_LDFLAGS@
libZfixFmt_la_SOURCES = ZfixFmtLib.cc ZfixFmt.cc ZfixFrame.cc
libZfixFmt_la_LIBADD = $(top_builddir)/zf/src/libZf.la $(top_builddir)/ze/src/libZe.la $(top_builddir)/zt/src/libZt.la $(top_builddir)/zm/src/libZm.la $(top_builddir)/zu/src/libZu.la @Z_ZT_LIBS@ @Z_MT_LIBS@ @Z_LIBS@
libZfix_la_CPPFLAGS = $(AM_CPPFLAGS) -I$(top_srcdir)/zi/src \
	-I$(top_srcdir)/ztcp/src -I$(top_srcdir)/ztls/src -DZFIX_EXPORTS
libZfix_la_LDFLAGS = @Z_SO_LDFLAGS@
libZfix_la_SOURCES = ZfixLib.cc Zfix.cc ZfixSession.cc ZfixLinkState.cc ZfixRx.cc \
	ZfixTxMsg.cc ZfixTx.cc ZfixLinkTx.cc ZfixRaw.cc \
	ZfixIngress.cc ZfixTrace.cc ZfixWork.cc
libZfix_la_LIBADD = libZfixFmt.la \
	$(top_builddir)/ztls/src/libZtls.la $(top_builddir)/ztcp/src/libZtcp.la \
	$(top_builddir)/zi/src/libZi.la $(top_builddir)/zf/src/libZf.la $(top_builddir)/ze/src/libZe.la $(top_builddir)/zt/src/libZt.la $(top_builddir)/zm/src/libZm.la $(top_builddir)/zu/src/libZu.la \
	@PTLS_LIBS@ @Z_IO_LIBS@ @Z_ZT_LIBS@ @Z_MT_LIBS@ @Z_LIBS@
