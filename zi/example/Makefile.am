METASOURCES = AUTO
AM_CPPFLAGS = -I$(top_srcdir)/zu/src -I$(top_srcdir)/zm/src \
	-I$(top_srcdir)/zt/src -I$(top_srcdir)/ze/src -I$(top_srcdir)/zf/src -I$(top_srcdir)/zi/src \
	@Z_CPPFLAGS@
AM_CXXFLAGS = @Z_CXXFLAGS@
AM_LDFLAGS = @Z_LDFLAGS@
LDADD = $(top_builddir)/zi/src/libZi.la $(top_builddir)/zf/src/libZf.la $(top_builddir)/ze/src/libZe.la \
	$(top_builddir)/zt/src/libZt.la $(top_builddir)/zm/src/libZm.la \
	$(top_builddir)/zu/src/libZu.la \
	@Z_IO_LIBS@ @Z_ZT_LIBS@ @Z_MT_LIBS@ @Z_LIBS@
noinst_PROGRAMS = \
	zimxclient zimxserver zimxudpclient zimxudpserver \
	zilogtest zipbexample
noinst_HEADERS = global.hh httpheader.hh
EXTRA_DIST = zipbexample.proto README.md
zipbexample_SOURCES = zipbexample.cc
nodist_zipbexample_SOURCES = zipb-gen/note.cc zipb-gen/note.hh
zipbexample_CPPFLAGS = $(AM_CPPFLAGS) -I$(builddir)/zipb-gen
zipbexample-zipbexample.$(OBJEXT): zipb-gen/note.hh
zipb-gen/note.hh zipb-gen/note.cc &: \
	$(srcdir)/zipbexample.proto $(top_builddir)/zi/src/zprotoc$(EXEEXT)
	$(AM_V_GEN)$(MKDIR_P) zipb-gen
	$(AM_V_at)$(top_builddir)/zi/src/zprotoc \
		$(srcdir)/zipbexample.proto -o zipb-gen/note
CLEANFILES = zipb-gen/note.hh zipb-gen/note.cc
zimxclient_SOURCES = zimxclient.cc
zimxserver_SOURCES = zimxserver.cc
zimxudpclient_SOURCES = zimxudpclient.cc
zimxudpserver_SOURCES = zimxudpserver.cc
zilogtest_SOURCES = zilogtest.cc
