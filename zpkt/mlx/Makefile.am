METASOURCES = AUTO
AM_CPPFLAGS = -I$(top_srcdir)/zu/src -I$(top_srcdir)/zm/src \
	-I$(top_srcdir)/zt/src -I$(top_srcdir)/ze/src -I$(top_srcdir)/zf/src \
	-I$(top_srcdir)/zi/src -I$(top_srcdir)/zpkt/src @Z_CPPFLAGS@
AM_CXXFLAGS = @Z_CXXFLAGS@
AM_LDFLAGS = @Z_LDFLAGS@
noinst_HEADERS = ZpktMLXCf.hh ZpktMlx.hh ZpktMlxEnv.hh \
	ZpktMlxNif.hh ZpktMlxPool.hh ZpktMlxQueue.hh ZpktMlxLoop.hh
lib_LTLIBRARIES = libZpkt_mlx.la
libZpkt_mlx_la_CPPFLAGS = $(AM_CPPFLAGS) $(MLX_CFLAGS)
libZpkt_mlx_la_LDFLAGS = $(AM_LDFLAGS) -module -avoid-version
libZpkt_mlx_la_SOURCES = ZpktMlxPlugin.cc ZpktMlx.cc ZpktMlxEnv.cc ZpktMlxNif.cc \
	ZpktMlxPool.cc ZpktMlxQueue.cc ZpktMlxLoop.cc
libZpkt_mlx_la_LIBADD = $(top_builddir)/zpkt/src/libZpkt.la \
	$(top_builddir)/zi/src/libZi.la $(top_builddir)/zf/src/libZf.la \
	$(top_builddir)/ze/src/libZe.la $(top_builddir)/zt/src/libZt.la \
	$(top_builddir)/zm/src/libZm.la $(top_builddir)/zu/src/libZu.la \
	$(MLX_LIBS) @Z_IO_LIBS@ @Z_ZT_LIBS@ @Z_MT_LIBS@ @Z_LIBS@
