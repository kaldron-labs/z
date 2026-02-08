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
	ZuInspectTest ZuHashTest ZuCmpTest ZuBoxTest ZuBoxBench \
	ZuArrayTest ZuTupleTest ZuIDTest ZuUTFTest ZuBitmapTest \
	ZuSpanTest \
	ZuDecimalTest ZuSearchTest ZuStructTest ZuTLTest \
	ZuAxorTest ZuPtrTest ZuTimeTest ZuEndianTest \
	ZuStreamTest ZuBitStreamTest ZuSearchBench \
	ZuBase64Test ZuBase64URLTest ZuBase32Test ZuHexTest \
	ZuMatchTest ZuNXTest ZuFPTest ZuIntrinTest ZuMvArrayTest \
	ZuTokenizerTest ZuTest ZuDemangleTest
ZuInspectTest_SOURCES = ZuInspectTest.cc
ZuHashTest_SOURCES = ZuHashTest.cc Analyze.hh
ZuCmpTest_SOURCES = ZuCmpTest.cc
#ZuCmpTest_CXXFLAGS = $(AM_CXXFLAGS) -fprofile-instr-generate -fcoverage-mapping
ZuSearchTest_SOURCES = ZuSearchTest.cc
ZuSearchBench_SOURCES = ZuSearchBench.cc
ZuBoxTest_SOURCES = ZuBoxTest.cc
ZuBoxBench_SOURCES = ZuBoxBench.cc
ZuArrayTest_SOURCES = ZuArrayTest.cc
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
ZuMatchTest_SOURCES = ZuMatchTest.cc
#ZuMatchTest_CFLAGS = $(filter-out -g, $(AM_CFLAGS)) -O3 -g1
ZuMatchTest_CXXFLAGS = $(AM_CXXFLAGS) -O3 -g1
ZuNXTest_SOURCES = ZuNXTest.cc
ZuFPTest_SOURCES = ZuFPTest.cc
ZuIntrinTest_SOURCES = ZuIntrinTest.cc
ZuMvArrayTest_SOURCES = ZuMvArrayTest.cc
ZuTokenizerTest_SOURCES = ZuTokenizerTest.cc
ZuTest_SOURCES = ZuTest.cc
ZuDemangleTest_SOURCES = ZuDemangleTest.cc
