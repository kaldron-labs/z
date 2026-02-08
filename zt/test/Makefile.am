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
	ZtArrayTest ZtDateTest ZtDateFixTest ZtRegexTest \
	ZtStringHash ZtStringTest vsntest ZtIconvTest ZtBitWindowTest \
	ZtStructTest ZtBitmapTest ZtStackTest ZtURITest ZtCLITest \
	ZtASN1Test ZtDemangleTest
ZtArrayTest_SOURCES = ZtArrayTest.cc
ZtDateTest_SOURCES = ZtDateTest.cc
ZtDateFixTest_SOURCES = ZtDateFixTest.cc
ZtRegexTest_SOURCES = ZtRegexTest.cc
ZtStringHash_SOURCES = ZtStringHash.cc
ZtStringTest_SOURCES = ZtStringTest.cc
vsntest_SOURCES = vsntest.cc
ZtIconvTest_SOURCES = ZtIconvTest.cc
ZtBitWindowTest_SOURCES = ZtBitWindowTest.cc
ZtStructTest_SOURCES = ZtStructTest.cc
ZtBitmapTest_SOURCES = ZtBitmapTest.cc
ZtStackTest_SOURCES = ZtStackTest.cc
ZtURITest_SOURCES = ZtURITest.cc
ZtCLITest_SOURCES = ZtCLITest.cc
ZtASN1Test_SOURCES = ZtASN1Test.cc
ZtDemangleTest_SOURCES = ZtDemangleTest.cc
