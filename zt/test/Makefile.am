METASOURCES = AUTO
.cc.s:
	$(CXXCOMPILE) -c -S $<
%.i.cc: %.cc
	$(CXXCOMPILE) -C -E $< -o $@
AM_CPPFLAGS = -I$(top_srcdir)/zu/src -I$(top_srcdir)/zm/src -I$(top_srcdir)/zt/src \
	@Z_CPPFLAGS@
AM_CXXFLAGS = @Z_CXXFLAGS@
AM_LDFLAGS = @Z_LDFLAGS@
LDADD = $(top_builddir)/zt/src/libZt.la $(top_builddir)/zm/src/libZm.la \
	$(top_builddir)/zu/src/libZu.la \
	@Z_ZT_LIBS@ @Z_MT_LIBS@ @Z_LIBS@
noinst_PROGRAMS = \
	ZtArrayTest ZtBitWindowTest ZtBitmapTest \
	ZtDateTest ZtDateFixTest ZtDemangleTest ZtEnumTest \
	ZtFmtTest ZtIconvTest ZtScratchTest ZtPlatformTest \
	ZtQuoteTest ZtRegexTest ZtScanBoolTest \
	ZtStringHash ZtStringTest ZtWindowTest
dist_noinst_DATA = ZtWords
.PHONY: test
test: export ZI_LOGDIR = $(abs_top_builddir)/logs
test: $(noinst_PROGRAMS)
	prove $(noinst_PROGRAMS)
ZtArrayTest_SOURCES = ZtArrayTest.cc
ZtBitWindowTest_SOURCES = ZtBitWindowTest.cc
ZtBitmapTest_SOURCES = ZtBitmapTest.cc
ZtDateTest_SOURCES = ZtDateTest.cc
ZtDateFixTest_SOURCES = ZtDateFixTest.cc
ZtDemangleTest_SOURCES = ZtDemangleTest.cc
ZtEnumTest_SOURCES = ZtEnumTest.cc
ZtFmtTest_SOURCES = ZtFmtTest.cc
ZtIconvTest_SOURCES = ZtIconvTest.cc
ZtScratchTest_SOURCES = ZtScratchTest.cc
ZtPlatformTest_SOURCES = ZtPlatformTest.cc
ZtQuoteTest_SOURCES = ZtQuoteTest.cc
ZtRegexTest_SOURCES = ZtRegexTest.cc
ZtScanBoolTest_SOURCES = ZtScanBoolTest.cc
ZtStringHash_SOURCES = ZtStringHash.cc
ZtStringTest_SOURCES = ZtStringTest.cc
ZtWindowTest_SOURCES = ZtWindowTest.cc
