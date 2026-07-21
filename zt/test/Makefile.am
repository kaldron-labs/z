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
	ZtArrayTest ZtASN1Test ZtBitWindowTest ZtBitmapTest ZtCLITest \
	ZtCfTest ZtCSVTest ZtDateTest ZtDateFixTest ZtDemangleTest ZtEnumTest \
	ZtFmtTest ZtIconvTest ZtLocalBufferTest ZtPlatformTest \
	ZtQuoteTest ZtRegexTest ZtScanBoolTest \
	ZtStringHash ZtStringTest ZtStructTest ZtURITest ZtWindowTest
.PHONY: test
test: $(noinst_PROGRAMS)
	prove $(noinst_PROGRAMS)
ZtArrayTest_SOURCES = ZtArrayTest.cc
ZtASN1Test_SOURCES = ZtASN1Test.cc
ZtBitWindowTest_SOURCES = ZtBitWindowTest.cc
ZtBitmapTest_SOURCES = ZtBitmapTest.cc
ZtCLITest_SOURCES = ZtCLITest.cc
ZtCfTest_SOURCES = ZtCfTest.cc
ZtCSVTest_SOURCES = ZtCSVTest.cc
ZtDateTest_SOURCES = ZtDateTest.cc
ZtDateFixTest_SOURCES = ZtDateFixTest.cc
ZtDemangleTest_SOURCES = ZtDemangleTest.cc
ZtEnumTest_SOURCES = ZtEnumTest.cc
ZtFmtTest_SOURCES = ZtFmtTest.cc
ZtIconvTest_SOURCES = ZtIconvTest.cc
ZtLocalBufferTest_SOURCES = ZtLocalBufferTest.cc
ZtPlatformTest_SOURCES = ZtPlatformTest.cc
ZtQuoteTest_SOURCES = ZtQuoteTest.cc
ZtRegexTest_SOURCES = ZtRegexTest.cc
ZtScanBoolTest_SOURCES = ZtScanBoolTest.cc
ZtStringHash_SOURCES = ZtStringHash.cc
ZtStringTest_SOURCES = ZtStringTest.cc
ZtStructTest_SOURCES = ZtStructTest.cc
ZtURITest_SOURCES = ZtURITest.cc
ZtWindowTest_SOURCES = ZtWindowTest.cc
