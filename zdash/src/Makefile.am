METASOURCES = AUTO
AM_CPPFLAGS = -I$(top_srcdir)/zu/src -I$(top_srcdir)/zm/src \
	-I$(top_srcdir)/zt/src -I$(top_srcdir)/ze/src -I$(top_srcdir)/zf/src -I$(top_srcdir)/zi/src \
	-I$(top_srcdir)/ztls/src -I$(top_srcdir)/zfb/src \
	-I$(top_srcdir)/zv/src -I$(top_srcdir)/zdf/src
	@Z_CPPFLAGS@
AM_CFLAGS = @GTK_CFLAGS@
AM_CXXFLAGS = @Z_CXXFLAGS@ @GTK_CFLAGS@
AM_LDFLAGS = @Z_LDFLAGS@
FBS = request.fbs reqack.fbs link.fbs version.fbs
FBS_H = ${FBS:%.fbs=%_fbs.h}
${FBS_H}: %_fbs.h: fbs/%.fbs $(top_builddir)/scripts/fbscodegen
	$(top_builddir)/scripts/fbscodegen -b --schema --bfbs-gen-embed -c --cpp-std c++17 --cpp-field-case-style lower -I fbs -I $(top_srcdir)/zfb/src/fbs -I $(top_srcdir)/zv/src/fbs $<
BUILT_SOURCES = ${FBS_H}
CLEANFILES = ${FBS_H}
#fbsdir = $(datarootdir)/${PACKAGE}/fbs
#dist_fbs_DATA = ${FBS:%=fbs/%}
bin_PROGRAMS = zdash
zdash_CPPFLAGS = $(AM_CPPFLAGS) \
	-DDATADIR='"$(datadir)"'
#zdash_CXXFLAGS = ${AM_CXXFLAGS} -ftemplate-backtrace-limit=0 #-fprofile-instr-generate -fcoverage-mapping
zdash_LDADD = libZGtk.la \
	$(top_builddir)/zdf/src/libZdf.la \
	$(libZGtk_la_LDADD) @Z_LIBS@
if MINGW
zdash_LDFLAGS = ${AM_LDFLAGS} -mwindows
endif
zdash_SOURCES = zdash.cc
