METASOURCES = AUTO
AM_CPPFLAGS = -I$(top_srcdir)/zu/src -I$(top_srcdir)/zm/src \
	-I$(top_srcdir)/zt/src -I$(top_srcdir)/ze/src -I$(top_srcdir)/zf/src -I$(top_srcdir)/zi/src \
	-I$(top_srcdir)/ztls/src -I$(top_srcdir)/zfb/src \
	-I$(top_srcdir)/zv/src -I$(top_srcdir)/ztc/src -I$(top_srcdir)/zdf/src \
	-I$(top_srcdir)/zdb/src -I$(top_srcdir)/zgtk/src -I$(top_srcdir)/zum/src \
	-I$(top_srcdir)/zrest/src -I$(top_srcdir)/ztcp/src -I$(top_srcdir)/zquic/src \
	-I$(top_srcdir)/zhttp/src -I$(top_srcdir)/zws/src \
	@Z_CPPFLAGS@
AM_CFLAGS = @GTK_CFLAGS@
AM_CXXFLAGS = @Z_CXXFLAGS@ @GTK_CFLAGS@
AM_LDFLAGS = @Z_LDFLAGS@
bin_PROGRAMS = zdash
zdash_CPPFLAGS = $(AM_CPPFLAGS) \
	-DDATADIR='"$(datadir)"'
#zdash_CXXFLAGS = ${AM_CXXFLAGS} -ftemplate-backtrace-limit=0 #-fprofile-instr-generate -fcoverage-mapping
zdash_LDADD = $(top_builddir)/zgtk/src/libZGtk.la \
	$(top_builddir)/zdf/src/libZdf.la \
	$(top_builddir)/ztc/src/libZtc.la \
	$(top_builddir)/zdb/src/libZdb.la \
	$(top_builddir)/zrest/src/libZrest.la \
	$(top_builddir)/zws/src/libZws.la \
	$(top_builddir)/zhttp/src/libZhttp.la \
	$(top_builddir)/ztls/src/libZtls.la \
	$(top_builddir)/zfb/src/libZfb.la $(top_builddir)/zv/src/libZv.la \
	$(top_builddir)/zi/src/libZi.la $(top_builddir)/zf/src/libZf.la \
	$(top_builddir)/ze/src/libZe.la $(top_builddir)/zt/src/libZt.la \
	$(top_builddir)/zm/src/libZm.la $(top_builddir)/zu/src/libZu.la \
	@GTK_LIBS@ @FBS_LIBS@ @Z_IO_LIBS@ @Z_ZT_LIBS@ @Z_MT_LIBS@ @Z_LIBS@
if MINGW
zdash_LDFLAGS = ${AM_LDFLAGS} -mwindows
endif
zdash_SOURCES = zdash.cc zdash_oauth.hh zdash_module.hh
dist_pkgdata_DATA = zdash.glade
