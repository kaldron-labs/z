METASOURCES = AUTO
AM_CPPFLAGS = \
	-I$(top_srcdir)/zu/src -I$(top_srcdir)/zm/src \
	-I$(top_srcdir)/zt/src -I$(top_srcdir)/ze/src -I$(top_srcdir)/zi/src \
	-DZRL_EXPORTS @Z_CPPFLAGS@
AM_CXXFLAGS = @Z_CXXFLAGS@
AM_LDFLAGS = @Z_LDFLAGS@ @Z_SO_LDFLAGS@
pkginclude_HEADERS = ZrlLib.hh ZrlTypes.hh Zrl.hh ZrlCLI.hh \
	ZrlLine.hh ZrlTerminal.hh ZrlApp.hh ZrlEditor.hh \
	ZrlGlobber.hh ZrlHistory.hh
lib_LTLIBRARIES = libZrl.la
libZrl_la_SOURCES = ZrlLib.cc Zrl.cc ZrlCLI.cc \
	ZrlLine.cc ZrlTerminal.cc ZrlEditor.cc \
	ZrlHistory.cc ZrlGlobber.cc
libZrl_la_LIBADD = \
	$(top_builddir)/zi/src/libZi.la \
	$(top_builddir)/ze/src/libZe.la $(top_builddir)/zt/src/libZt.la \
	$(top_builddir)/zm/src/libZm.la $(top_builddir)/zu/src/libZu.la \
	@Z_TERM_LIBS@ @Z_IO_LIBS@ @Z_ZT_LIBS@ @Z_MT_LIBS@ @Z_LIBS@
