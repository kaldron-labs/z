METASOURCES = AUTO
.cc.s:
	$(CXXCOMPILE) -c -S $<
%.i.cc: %.cc
	$(CXXCOMPILE) -C -E $< -o $@
AM_CPPFLAGS = -I$(top_srcdir)/zu/src -I$(top_srcdir)/zm/src \
	-DZT_EXPORTS @Z_CPPFLAGS@
AM_CXXFLAGS = @Z_CXXFLAGS@
AM_LDFLAGS = @Z_LDFLAGS@ @Z_SO_LDFLAGS@
pkginclude_HEADERS = \
	ZtArray.hh ZtLocalArray.hh ZtBuiltin.hh ZtBitWindow.hh \
	ZtEnum.hh ZtIconv.hh ZtLib.hh ZtPlatform.hh \
	ZtRegex.hh ZtString.hh ZtLocalString.hh ZtHexDump.hh \
	ZtWindow.hh ZtBitmap.hh ZtFmt.hh \
	ZtStruct.hh ZtScanBool.hh ZtCase.hh ZtTimeZone.hh \
	ZtQuote.hh ZtJSON.hh ZtURI.hh ZtCLI.hh ZtASN1.hh ZtBytesFmt.hh ZtCSV.hh
lib_LTLIBRARIES = libZt.la
libZt_la_SOURCES = \
	ZtLib.cc ZtRegex.cc ZtString.cc ZtHexDump.cc ZtTimeZone.cc \
	ZtStruct.cc ZtJSON.cc ZtURI.cc ZtCLI.cc ZtCSV.cc
libZt_la_LIBADD = $(top_builddir)/zm/src/libZm.la \
	$(top_builddir)/zu/src/libZu.la \
	@Z_ZT_LIBS@ @Z_MT_LIBS@ @Z_LIBS@
