METASOURCES = AUTO
AM_CPPFLAGS = -I$(top_srcdir)/zu/src -I$(top_srcdir)/zm/src \
	-I$(top_srcdir)/zt/src -I$(top_srcdir)/ze/src -I$(top_srcdir)/zf/src \
	-I$(top_srcdir)/zi/src -I$(top_srcdir)/zpb/src @Z_CPPFLAGS@
AM_CXXFLAGS = @Z_CXXFLAGS@
AM_LDFLAGS = @Z_LDFLAGS@
LDADD = $(top_builddir)/zpb/src/libZpb.la \
	$(top_builddir)/zi/src/libZi.la $(top_builddir)/zf/src/libZf.la \
	$(top_builddir)/ze/src/libZe.la $(top_builddir)/zt/src/libZt.la \
	$(top_builddir)/zm/src/libZm.la $(top_builddir)/zu/src/libZu.la \
	@Z_IO_LIBS@ @Z_ZT_LIBS@ @Z_MT_LIBS@ @Z_LIBS@
noinst_PROGRAMS = zpbexample
EXTRA_DIST = README.md zpbexample.proto
zpbexample_SOURCES = zpbexample.cc
nodist_zpbexample_SOURCES = zpb-gen/note.cc zpb-gen/note.hh
zpbexample_CPPFLAGS = $(AM_CPPFLAGS) -I$(builddir)/zpb-gen
zpbexample-zpbexample.$(OBJEXT): zpb-gen/note.hh
zpb-gen/note.hh zpb-gen/note.cc &: \
	$(srcdir)/zpbexample.proto $(top_builddir)/zpb/src/zprotoc$(EXEEXT)
	$(AM_V_GEN)$(MKDIR_P) zpb-gen
	$(AM_V_at)$(top_builddir)/zpb/src/zprotoc \
		$(srcdir)/zpbexample.proto -o zpb-gen/note
CLEANFILES = zpb-gen/note.hh zpb-gen/note.cc
