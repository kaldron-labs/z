METASOURCES = AUTO
.cc.s:
	$(CXXCOMPILE) -c -S $<
%.i.cc: %.cc
	$(CXXCOMPILE) -C -E $< -o $@
AM_CPPFLAGS = -I$(top_srcdir)/zu/src -I$(top_srcdir)/zm/src \
	-I$(top_srcdir)/zt/src -I$(top_srcdir)/ze/src \
	-DZF_EXPORTS @Z_CPPFLAGS@
AM_CXXFLAGS = @Z_CXXFLAGS@
AM_LDFLAGS = @Z_LDFLAGS@ @Z_SO_LDFLAGS@
pkginclude_HEADERS = \
	ZfLib.hh ZfStruct.hh ZfJSON.hh ZfURI.hh ZfCBOR.hh ZfCLI.hh ZfASN1.hh ZfCSV.hh \
	ZfTree.hh ZfTreeLoad.hh ZfCf.hh ZfYAML.hh ZfTOML.hh
lib_LTLIBRARIES = libZf.la
libZf_la_CXXFLAGS = $(AM_CXXFLAGS) @Z_VISIBILITY_CXXFLAGS@
libZf_la_SOURCES = \
	ZfLib.cc ZfStruct.cc ZfJSON.cc ZfURI.cc ZfCBOR.cc ZfCLI.cc ZfCSV.cc ZfCf.cc \
	ZfYAML.cc ZfTOML.cc
libZf_la_LIBADD = $(top_builddir)/ze/src/libZe.la \
	$(top_builddir)/zt/src/libZt.la \
	$(top_builddir)/zm/src/libZm.la $(top_builddir)/zu/src/libZu.la \
	@Z_IO_LIBS@ @Z_ZT_LIBS@ @Z_MT_LIBS@ @Z_LIBS@
