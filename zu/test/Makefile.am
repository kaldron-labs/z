METASOURCES = AUTO
.cc.s:
	$(CXXCOMPILE) -c -S $<
%.i.cc: %.cc
	$(CXXCOMPILE) -C -E $< -o $@
AM_CPPFLAGS = -I$(top_srcdir)/zu/src \
	@Z_CPPFLAGS@
AM_CXXFLAGS = @Z_CXXFLAGS@
AM_LDFLAGS = @Z_LDFLAGS@
LDADD = $(top_builddir)/zu/src/libZu.la @Z_MT_LIBS@ @Z_LIBS@
noinst_PROGRAMS = \
	ZuInspectTest ZuHashTest ZuCmpTest ZuBoxTest \
	ZuArrayTest ZuTupleTest ZuIDTest ZuUTFTest ZuBitmapTest \
	ZuArrayFnTest ZuBitfieldTest ZuBitmapCTest ZuSpanTest \
	ZuDecimalTest ZuSearchTest ZuStructTest ZuTLTest \
	ZuAxorTest ZuPtrTest ZuTimeTest ZuEndianTest \
	ZuStreamTest ZuBitStreamTest \
	ZuBase64Test ZuBase64URLTest ZuBase32Test ZuHexTest ZuPercentTest \
	ZuInvokeSwitchTest ZuMArrayIterTest ZuMatchTest ZuLibTest \
	ZuNumConvTest ZuNXTest ZuFPTest ZuIntrinTest ZuPreprocTest \
	ZuTimeCTest ZuTokenizerTest ZuTest ZuDemangleTest ZuSemVerTest
.PHONY: test
test: export ZI_LOGDIR = $(abs_top_builddir)/logs
test: $(noinst_PROGRAMS)
	prove $(noinst_PROGRAMS)
ZuInspectTest_SOURCES = ZuInspectTest.cc
ZuHashTest_SOURCES = ZuHashTest.cc
ZuCmpTest_SOURCES = ZuCmpTest.cc
#ZuCmpTest_CXXFLAGS = $(AM_CXXFLAGS) -fprofile-instr-generate -fcoverage-mapping
ZuSearchTest_SOURCES = ZuSearchTest.cc
ZuBoxTest_SOURCES = ZuBoxTest.cc
ZuArrayTest_SOURCES = ZuArrayTest.cc
ZuArrayFnTest_SOURCES = ZuArrayFnTest.cc
ZuBitfieldTest_SOURCES = ZuBitfieldTest.cc
ZuBitmapCTest_SOURCES = ZuBitmapCTest.cc
ZuSpanTest_SOURCES = ZuSpanTest.cc
ZuTupleTest_SOURCES = ZuTupleTest.cc
ZuIDTest_SOURCES = ZuIDTest.cc
ZuUTFTest_SOURCES = ZuUTFTest.cc
ZuBitmapTest_SOURCES = ZuBitmapTest.cc
ZuDecimalTest_SOURCES = ZuDecimalTest.cc
ZuStructTest_SOURCES = ZuStructTest.cc
ZuTLTest_SOURCES = ZuTLTest.cc
ZuAxorTest_SOURCES = ZuAxorTest.cc
ZuPtrTest_SOURCES = ZuPtrTest.cc
ZuTimeTest_SOURCES = ZuTimeTest.cc
ZuEndianTest_SOURCES = ZuEndianTest.cc
ZuStreamTest_SOURCES = ZuStreamTest.cc
ZuBitStreamTest_SOURCES = ZuBitStreamTest.cc
ZuBase64Test_SOURCES = ZuBase64Test.cc
ZuBase64URLTest_SOURCES = ZuBase64URLTest.cc
ZuBase32Test_SOURCES = ZuBase32Test.cc
ZuHexTest_SOURCES = ZuHexTest.cc
ZuPercentTest_SOURCES = ZuPercentTest.cc
ZuInvokeSwitchTest_SOURCES = ZuInvokeSwitchTest.cc
ZuMArrayIterTest_SOURCES = ZuMArrayIterTest.cc
ZuMatchTest_SOURCES = ZuMatchTest.cc
#ZuMatchTest_CFLAGS = $(filter-out -g, $(AM_CFLAGS)) -O3 -g1
ZuMatchTest_CXXFLAGS = $(AM_CXXFLAGS) -O3 -g1
ZuLibTest_SOURCES = ZuLibTest.cc
ZuNumConvTest_SOURCES = ZuNumConvTest.cc
ZuNXTest_SOURCES = ZuNXTest.cc
ZuFPTest_SOURCES = ZuFPTest.cc
ZuIntrinTest_SOURCES = ZuIntrinTest.cc
ZuPreprocTest_SOURCES = ZuPreprocTest.cc
ZuTimeCTest_SOURCES = ZuTimeCTest.cc
ZuTokenizerTest_SOURCES = ZuTokenizerTest.cc
ZuTest_SOURCES = ZuTest.cc
ZuDemangleTest_SOURCES = ZuDemangleTest.cc

ZuSemVerTest_SOURCES = ZuSemVerTest.cc
