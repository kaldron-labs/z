METASOURCES = AUTO
AM_CPPFLAGS = -I$(top_srcdir)/zu/src -I$(top_srcdir)/zm/src -I$(top_srcdir)/zt/src -I$(top_srcdir)/ze/src -I$(top_srcdir)/zf/src -I$(top_srcdir)/zfix/src @Z_CPPFLAGS@
AM_CXXFLAGS = @Z_CXXFLAGS@
AM_LDFLAGS = @Z_LDFLAGS@
EXTRA_DIST = ZfixDemo.hh ZfixTestAllocator.hh README.md SESSION.md
noinst_PROGRAMS = ZfixSignTest ZfixLoadTest ZfixLoadFIXTest ZfixLoadRunTest ZfixDemoTest ZfixWorkTest ZfixSelectTest ZfixContextTest ZfixDispatchTest ZfixTraceTest ZfixIngressTest ZfixRawTest ZfixWireTest ZfixRxFramerTest ZfixDeclTest ZfixLinkTest ZfixCodecTest ZfixGroupTest ZfixUnionTest ZfixFrameTest ZfixSessionTest ZfixSeqTest ZfixSeqTxTest ZfixLinkStateTest ZfixRxTest ZfixTxMsgTest ZfixTxTest ZfixLinkTxTest ZfixManagedTest ZfixRestoreTest
ZfixDemoTest_SOURCES = ZfixDemoTest.cc ZfixDemo.cc
ZfixDemoTest_CPPFLAGS = $(AM_CPPFLAGS) -I$(top_srcdir)/zi/src
ZfixDemoTest_LDADD = $(ZfixRxTest_LDADD)
ZfixWorkTest_SOURCES = ZfixWorkTest.cc
ZfixWorkTest_LDADD = $(ZfixLinkTest_LDADD)
ZfixSelectTest_SOURCES = ZfixSelectTest.cc
ZfixSelectTest_CPPFLAGS = $(AM_CPPFLAGS) -I$(top_srcdir)/zi/src
ZfixSelectTest_LDADD = $(ZfixRxTest_LDADD)
ZfixContextTest_SOURCES = ZfixContextTest.cc
ZfixContextTest_CPPFLAGS = $(AM_CPPFLAGS) -I$(top_srcdir)/zi/src
ZfixContextTest_LDADD = $(ZfixRxTest_LDADD)
ZfixDispatchTest_SOURCES = ZfixDispatchTest.cc
ZfixDispatchTest_CPPFLAGS = $(AM_CPPFLAGS) -I$(top_srcdir)/zi/src
ZfixDispatchTest_LDADD = $(ZfixLinkTest_LDADD)
ZfixTraceTest_SOURCES = ZfixTraceTest.cc
ZfixTraceTest_CPPFLAGS = $(AM_CPPFLAGS) -I$(top_srcdir)/zi/src
ZfixTraceTest_LDADD = $(ZfixRxTest_LDADD)
ZfixIngressTest_SOURCES = ZfixIngressTest.cc
ZfixIngressTest_LDADD = $(ZfixLinkTest_LDADD)
ZfixRawTest_SOURCES = ZfixRawTest.cc
ZfixRawTest_CPPFLAGS = $(AM_CPPFLAGS) -I$(top_srcdir)/zi/src
ZfixRawTest_LDADD = $(ZfixRxTest_LDADD)
ZfixWireTest_SOURCES = ZfixWireTest.cc
ZfixWireTest_CPPFLAGS = $(AM_CPPFLAGS) -I$(top_srcdir)/zi/src
ZfixWireTest_LDADD = $(ZfixRxTest_LDADD)
ZfixRxFramerTest_SOURCES = ZfixRxFramerTest.cc
ZfixRxFramerTest_CPPFLAGS = $(AM_CPPFLAGS) -I$(top_srcdir)/zi/src
ZfixRxFramerTest_LDADD = $(ZfixRxTest_LDADD)
ZfixDeclTest_SOURCES = ZfixDeclTest.cc
ZfixLinkTest_SOURCES = ZfixLinkTest.cc
ZfixLinkTest_CPPFLAGS = $(AM_CPPFLAGS) -I$(top_srcdir)/zi/src \
	-I$(top_srcdir)/ztcp/src -I$(top_srcdir)/ztls/src
ZfixCodecTest_SOURCES = ZfixCodecTest.cc
ZfixGroupTest_SOURCES = ZfixGroupTest.cc
ZfixUnionTest_SOURCES = ZfixUnionTest.cc
ZfixFrameTest_SOURCES = ZfixFrameTest.cc
ZfixRestoreTest_SOURCES = ZfixRestoreTest.cc
ZfixRestoreTest_CPPFLAGS = $(AM_CPPFLAGS) -I$(top_srcdir)/zi/src
ZfixRestoreTest_LDADD = $(ZfixRxTest_LDADD)
ZfixManagedTest_SOURCES = ZfixManagedTest.cc
ZfixManagedTest_CPPFLAGS = $(AM_CPPFLAGS) -I$(top_srcdir)/zi/src
ZfixManagedTest_LDADD = $(ZfixRxTest_LDADD)
ZfixLinkTxTest_SOURCES = ZfixLinkTxTest.cc
ZfixLinkTxTest_CPPFLAGS = $(AM_CPPFLAGS) -I$(top_srcdir)/zi/src
ZfixLinkTxTest_LDADD = $(ZfixRxTest_LDADD)
ZfixTxTest_SOURCES = ZfixTxTest.cc
ZfixTxTest_CPPFLAGS = $(AM_CPPFLAGS) -I$(top_srcdir)/zi/src
ZfixTxTest_LDADD = $(ZfixRxTest_LDADD)
ZfixTxMsgTest_SOURCES = ZfixTxMsgTest.cc
ZfixTxMsgTest_CPPFLAGS = $(AM_CPPFLAGS) -I$(top_srcdir)/zi/src
ZfixTxMsgTest_LDADD = $(ZfixRxTest_LDADD)
ZfixRxTest_SOURCES = ZfixRxTest.cc
ZfixRxTest_CPPFLAGS = $(AM_CPPFLAGS) -I$(top_srcdir)/zi/src
ZfixRxTest_LDADD = $(ZfixLinkTest_LDADD) $(top_builddir)/zi/src/libZi.la
ZfixLinkStateTest_SOURCES = ZfixLinkStateTest.cc
ZfixLinkStateTest_CPPFLAGS = $(AM_CPPFLAGS) -I$(top_srcdir)/zi/src
ZfixLinkStateTest_LDADD = $(ZfixLinkTest_LDADD)
ZfixSeqTxTest_SOURCES = ZfixSeqTxTest.cc
ZfixSeqTxTest_LDADD = $(ZfixLinkTest_LDADD)
ZfixSeqTest_SOURCES = ZfixSeqTest.cc
ZfixSeqTest_LDADD = $(ZfixLinkTest_LDADD)
ZfixSessionTest_SOURCES = ZfixSessionTest.cc
ZfixSessionTest_CPPFLAGS = $(AM_CPPFLAGS) -I$(top_srcdir)/zi/src
ZfixSessionTest_LDADD = $(ZfixRxTest_LDADD)
ZfixFrameTest_LDADD = $(ZfixDeclTest_LDADD)
ZfixUnionTest_LDADD = $(ZfixDeclTest_LDADD)
ZfixGroupTest_LDADD = $(ZfixDeclTest_LDADD)
ZfixCodecTest_LDADD = $(ZfixDeclTest_LDADD)
ZfixDeclTest_LDADD = $(top_builddir)/zfix/src/libZfixFmt.la \
	$(top_builddir)/zf/src/libZf.la $(top_builddir)/ze/src/libZe.la \
	$(top_builddir)/zt/src/libZt.la $(top_builddir)/zm/src/libZm.la \
	$(top_builddir)/zu/src/libZu.la @Z_ZT_LIBS@ @Z_MT_LIBS@ @Z_LIBS@
ZfixLinkTest_LDADD = $(top_builddir)/zfix/src/libZfix.la $(ZfixDeclTest_LDADD)

.PHONY: test
test: all schema-test send-schema-test reply-schema-test
	prove ./ZfixLoadTest ./ZfixLoadFIXTest ./ZfixLoadRunTest ./ZfixDemoTest ./ZfixWorkTest ./ZfixSelectTest ./ZfixContextTest ./ZfixDispatchTest ./ZfixTraceTest ./ZfixIngressTest ./ZfixRawTest ./ZfixWireTest ./ZfixRxFramerTest ./ZfixDeclTest ./ZfixLinkTest ./ZfixCodecTest ./ZfixGroupTest ./ZfixUnionTest ./ZfixFrameTest ./ZfixSessionTest ./ZfixSeqTest ./ZfixSeqTxTest ./ZfixLinkStateTest ./ZfixRxTest ./ZfixTxMsgTest ./ZfixTxTest ./ZfixLinkTxTest ./ZfixManagedTest ./ZfixRestoreTest

# Recompile the boundary consumers using only installed Z headers/libraries.
# Override ZFIX_PREFIX to point at a staged installation.
ZFIX_PREFIX = $(prefix)
CLEANFILES = ZfixInstRawTest ZfixInstWireTest ZfixInstRxFramerTest ZfixInstDeclTest ZfixInstLinkTest ZfixInstCodecTest \
	ZfixInstGroupTest ZfixInstUnionTest ZfixInstFrameTest ZfixInstSessionTest ZfixInstSeqTest ZfixInstSeqTxTest ZfixInstLinkStateTest ZfixInstRxTest ZfixInstTxMsgTest ZfixInstTxTest ZfixInstLinkTxTest ZfixInstManagedTest ZfixInstRestoreTest
.PHONY: installed-test
installed-test:
	$(CXX) $(CPPFLAGS) @Z_CPPFLAGS@ $(AM_CXXFLAGS) $(CXXFLAGS) \
	  -I$(ZFIX_PREFIX)/include $(srcdir)/ZfixDeclTest.cc \
	  -L$(ZFIX_PREFIX)/lib -Wl,-rpath-link,$(ZFIX_PREFIX)/lib -lZfixFmt -lZf -lZe -lZt -lZm -lZu \
	  @Z_ZT_LIBS@ @Z_MT_LIBS@ @Z_LIBS@ -o ZfixInstDeclTest
	$(CXX) $(CPPFLAGS) @Z_CPPFLAGS@ $(AM_CXXFLAGS) $(CXXFLAGS) \
	  -I$(ZFIX_PREFIX)/include $(srcdir)/ZfixLinkTest.cc \
	  -L$(ZFIX_PREFIX)/lib -Wl,-rpath-link,$(ZFIX_PREFIX)/lib -lZfix -lZfixFmt -lZi -lZf -lZe -lZt -lZm -lZu \
	  @Z_ZT_LIBS@ @Z_MT_LIBS@ @Z_LIBS@ -o ZfixInstLinkTest
	$(CXX) $(CPPFLAGS) @Z_CPPFLAGS@ $(AM_CXXFLAGS) $(CXXFLAGS) \
	  -I$(ZFIX_PREFIX)/include $(srcdir)/ZfixCodecTest.cc \
	  -L$(ZFIX_PREFIX)/lib -Wl,-rpath-link,$(ZFIX_PREFIX)/lib \
	  -lZfixFmt -lZf -lZe -lZt -lZm -lZu \
	  @Z_ZT_LIBS@ @Z_MT_LIBS@ @Z_LIBS@ -o ZfixInstCodecTest
	@for zfix_test in Group Union Frame Seq SeqTx; do \
	  $(CXX) $(CPPFLAGS) @Z_CPPFLAGS@ $(AM_CXXFLAGS) $(CXXFLAGS) \
	    -I$(ZFIX_PREFIX)/include $(srcdir)/Zfix$${zfix_test}Test.cc \
	    -L$(ZFIX_PREFIX)/lib -Wl,-rpath-link,$(ZFIX_PREFIX)/lib \
	    -lZfixFmt -lZf -lZe -lZt -lZm -lZu \
	    @Z_ZT_LIBS@ @Z_MT_LIBS@ @Z_LIBS@ -o ZfixInst$${zfix_test}Test || exit $$?; \
	done
	@for zfix_test in Raw Session LinkState Rx RxFramer Wire TxMsg Tx LinkTx Managed Restore; do \
	  $(CXX) $(CPPFLAGS) @Z_CPPFLAGS@ $(AM_CXXFLAGS) $(CXXFLAGS) \
	    -I$(ZFIX_PREFIX)/include $(srcdir)/Zfix$${zfix_test}Test.cc \
	    -L$(ZFIX_PREFIX)/lib -Wl,-rpath-link,$(ZFIX_PREFIX)/lib \
	    -lZfix -lZfixFmt -lZi -lZf -lZe -lZt -lZm -lZu \
	    @Z_ZT_LIBS@ @Z_MT_LIBS@ @Z_LIBS@ -o ZfixInst$${zfix_test}Test || exit $$?; \
	done
	LD_LIBRARY_PATH="$(ZFIX_PREFIX)/lib:$${LD_LIBRARY_PATH}" \
	  prove ./ZfixInstRawTest ./ZfixInstWireTest ./ZfixInstRxFramerTest ./ZfixInstDeclTest ./ZfixInstLinkTest ./ZfixInstCodecTest \
	    ./ZfixInstGroupTest ./ZfixInstUnionTest ./ZfixInstFrameTest ./ZfixInstSessionTest ./ZfixInstSeqTest ./ZfixInstSeqTxTest ./ZfixInstLinkStateTest ./ZfixInstRxTest ./ZfixInstTxMsgTest ./ZfixInstTxTest ./ZfixInstLinkTxTest ./ZfixInstManagedTest ./ZfixInstRestoreTest

# Check a positive fixture first so toolchain/include failures cannot count as
# successful schema rejection. Each negative case instantiates Schema itself.
EXTRA_DIST += ZfixSchemaTest.cc
CLEANFILES += ZfixSchemaTest*.log
.PHONY: schema-test
schema-test:
	$(CXX) $(DEFS) $(AM_CPPFLAGS) $(CPPFLAGS) $(AM_CXXFLAGS) $(CXXFLAGS) \
	  -fsyntax-only -DZFIX_SCHEMA_CASE=0 $(srcdir)/ZfixSchemaTest.cc
	@for zfix_case in 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16 17 18 19 20 21 22 23 24 25 26 27 28 29 30 31 32 33 34 35 36 37 38; do \
	  if $(CXX) $(DEFS) $(AM_CPPFLAGS) $(CPPFLAGS) $(AM_CXXFLAGS) $(CXXFLAGS) \
	    -fsyntax-only -DZFIX_SCHEMA_CASE=$$zfix_case \
	    $(srcdir)/ZfixSchemaTest.cc > ZfixSchemaTest$$zfix_case.log 2>&1; then \
	    echo "schema case $$zfix_case unexpectedly compiled"; exit 1; \
	  fi; \
	  if ! grep -q 'static assertion failed' ZfixSchemaTest$$zfix_case.log || \
	    grep -Eq 'frontend command failed|internal compiler error|PLEASE submit' \
	      ZfixSchemaTest$$zfix_case.log; then \
	    cat ZfixSchemaTest$$zfix_case.log; exit 1; \
	  fi; \
	done

# Optional Clang libFuzzer/ASan target. Compile codec sources into the instrumented
# executable; foundation dependencies remain the configured build's libraries.
FUZZ_CXX = $(CXX)
EXTRA_DIST += ZfixFrameFuzz.cc fuzz/Scalar fuzz/Text fuzz/Zero fuzz/Nested \
	fuzz/Frame fuzz/TextFrame fuzz/TruncatedType fuzz/SubIdsFrame
CLEANFILES += ZfixFrameFuzz
.PHONY: fuzz-build
fuzz-build:
	$(LIBTOOL) --tag=CXX --mode=link $(FUZZ_CXX) $(DEFS) $(AM_CPPFLAGS) \
	  $(CPPFLAGS) $(AM_CXXFLAGS) $(CXXFLAGS) -DZFIXFMT_EXPORTS -fsanitize=fuzzer,address \
	  -fno-omit-frame-pointer $(srcdir)/ZfixFrameFuzz.cc \
	  $(top_srcdir)/zfix/src/ZfixFmt.cc $(top_srcdir)/zfix/src/ZfixFrame.cc \
	  $(top_builddir)/zf/src/libZf.la $(top_builddir)/ze/src/libZe.la \
	  $(top_builddir)/zt/src/libZt.la $(top_builddir)/zm/src/libZm.la \
	  $(top_builddir)/zu/src/libZu.la @Z_ZT_LIBS@ @Z_MT_LIBS@ @Z_LIBS@ \
	  -o ZfixFrameFuzz

# Instrument the codec and conversion consumers without changing the configured
# foundation build. Full dependency instrumentation remains a separate clean run.
CLEANFILES += ZfixAsanRawTest ZfixAsanWireTest ZfixAsanRxFramerTest ZfixAsanCodecTest ZfixAsanGroupTest ZfixAsanUnionTest ZfixAsanFrameTest ZfixAsanSessionTest ZfixAsanSeqTest ZfixAsanSeqTxTest ZfixAsanLinkStateTest ZfixAsanRxTest ZfixAsanTxMsgTest ZfixAsanTxTest ZfixAsanLinkTxTest ZfixAsanManagedTest ZfixAsanRestoreTest
ASAN_TESTS = Raw Wire RxFramer Codec Group Union Frame Session Seq SeqTx LinkState Rx TxMsg Tx LinkTx Managed Restore
.PHONY: asan-test
asan-test:
	@for zfix_test in $(ASAN_TESTS); do \
	  zfix_extra=; zfix_extra_cpp=; zfix_extra_lib=; \
	  case $$zfix_test in \
	    Raw) zfix_extra="$(top_srcdir)/zfix/src/ZfixRaw.cc $(top_srcdir)/zfix/src/ZfixTxMsg.cc"; \
	      zfix_extra_cpp="-I$(top_srcdir)/zi/src"; zfix_extra_lib="$(top_builddir)/zi/src/libZi.la";; \
	    Wire) zfix_extra="$(top_srcdir)/zfix/src/ZfixLinkState.cc $(top_srcdir)/zfix/src/ZfixTxMsg.cc $(top_srcdir)/zfix/src/ZfixTx.cc $(top_srcdir)/zfix/src/ZfixLinkTx.cc"; \
	      zfix_extra_cpp="-I$(top_srcdir)/zi/src"; zfix_extra_lib="$(top_builddir)/zi/src/libZi.la";; \
	    RxFramer|Session) zfix_extra_cpp="-I$(top_srcdir)/zi/src"; zfix_extra_lib="$(top_builddir)/zi/src/libZi.la";; \
	    LinkState) zfix_extra="$(top_srcdir)/zfix/src/ZfixLinkState.cc";; \
	    Restore) zfix_extra="$(top_srcdir)/zfix/src/ZfixLinkState.cc $(top_srcdir)/zfix/src/ZfixRx.cc $(top_srcdir)/zfix/src/ZfixTxMsg.cc $(top_srcdir)/zfix/src/ZfixTx.cc"; \
	      zfix_extra_cpp="-I$(top_srcdir)/zi/src"; zfix_extra_lib="$(top_builddir)/zi/src/libZi.la";; \
	    Managed) zfix_extra="$(top_srcdir)/zfix/src/ZfixLinkState.cc $(top_srcdir)/zfix/src/ZfixRx.cc $(top_srcdir)/zfix/src/ZfixTxMsg.cc $(top_srcdir)/zfix/src/ZfixTx.cc $(top_srcdir)/zfix/src/ZfixLinkTx.cc"; \
	      zfix_extra_cpp="-I$(top_srcdir)/zi/src"; zfix_extra_lib="$(top_builddir)/zi/src/libZi.la";; \
	    LinkTx) zfix_extra="$(top_srcdir)/zfix/src/ZfixLinkState.cc $(top_srcdir)/zfix/src/ZfixTxMsg.cc $(top_srcdir)/zfix/src/ZfixTx.cc $(top_srcdir)/zfix/src/ZfixLinkTx.cc"; \
	      zfix_extra_cpp="-I$(top_srcdir)/zi/src"; zfix_extra_lib="$(top_builddir)/zi/src/libZi.la";; \
	    Tx) zfix_extra="$(top_srcdir)/zfix/src/ZfixLinkState.cc $(top_srcdir)/zfix/src/ZfixTxMsg.cc $(top_srcdir)/zfix/src/ZfixTx.cc"; \
	      zfix_extra_cpp="-I$(top_srcdir)/zi/src"; zfix_extra_lib="$(top_builddir)/zi/src/libZi.la";; \
	    TxMsg) zfix_extra="$(top_srcdir)/zfix/src/ZfixTxMsg.cc"; \
	      zfix_extra_cpp="-I$(top_srcdir)/zi/src"; zfix_extra_lib="$(top_builddir)/zi/src/libZi.la";; \
	    Rx) zfix_extra="$(top_srcdir)/zfix/src/ZfixLinkState.cc $(top_srcdir)/zfix/src/ZfixRx.cc"; \
	      zfix_extra_cpp="-I$(top_srcdir)/zi/src"; zfix_extra_lib="$(top_builddir)/zi/src/libZi.la";; \
	  esac; \
	  $(LIBTOOL) --tag=CXX --mode=link $(CXX) $(DEFS) $(AM_CPPFLAGS) \
	    $(CPPFLAGS) $$zfix_extra_cpp $(AM_CXXFLAGS) $(CXXFLAGS) -DZFIXFMT_EXPORTS -DZFIX_EXPORTS -fsanitize=address \
	    -fno-omit-frame-pointer $(srcdir)/Zfix$${zfix_test}Test.cc \
	    $(top_srcdir)/zfix/src/ZfixFmt.cc $(top_srcdir)/zfix/src/ZfixFrame.cc \
	    $(top_srcdir)/zfix/src/ZfixSession.cc $$zfix_extra \
	    $(top_builddir)/zf/src/libZf.la $(top_builddir)/ze/src/libZe.la \
	    $(top_builddir)/zt/src/libZt.la $(top_builddir)/zm/src/libZm.la \
	    $(top_builddir)/zu/src/libZu.la $$zfix_extra_lib @Z_ZT_LIBS@ @Z_MT_LIBS@ @Z_LIBS@ \
	    -o ZfixAsan$${zfix_test}Test || exit $$?; \
	  ASAN_OPTIONS=detect_leaks=1 $(LIBTOOL) exec ./ZfixAsan$${zfix_test}Test -q || exit $$?; \
	done

CLEANFILES += ZfixInstIngressTest
.PHONY: installed-ingress-test
installed-ingress-test:
	$(CXX) $(CPPFLAGS) @Z_CPPFLAGS@ $(AM_CXXFLAGS) $(CXXFLAGS) \
	  -I$(ZFIX_PREFIX)/include $(srcdir)/ZfixIngressTest.cc \
	  -L$(ZFIX_PREFIX)/lib -Wl,-rpath-link,$(ZFIX_PREFIX)/lib \
	  -lZfix -lZfixFmt -lZi -lZf -lZe -lZt -lZm -lZu \
	  @Z_IO_LIBS@ @Z_ZT_LIBS@ @Z_MT_LIBS@ @Z_LIBS@ -o ZfixInstIngressTest
	LD_LIBRARY_PATH="$(ZFIX_PREFIX)/lib:$${LD_LIBRARY_PATH}" prove ./ZfixInstIngressTest

CLEANFILES += ZfixAsanTraceTest ZfixInstTraceTest
.PHONY: asan-trace-test installed-trace-test
asan-trace-test:
	$(LIBTOOL) --tag=CXX --mode=link $(CXX) $(DEFS) $(AM_CPPFLAGS) \
	  -I$(top_srcdir)/zi/src $(CPPFLAGS) $(AM_CXXFLAGS) $(CXXFLAGS) \
	  -DZFIX_EXPORTS -fsanitize=address -fno-omit-frame-pointer \
	  $(srcdir)/ZfixTraceTest.cc $(top_srcdir)/zfix/src/ZfixTrace.cc \
	  $(ZfixRxTest_LDADD) -o ZfixAsanTraceTest
	ASAN_OPTIONS=detect_leaks=1 prove ./ZfixAsanTraceTest
installed-trace-test:
	$(CXX) $(CPPFLAGS) @Z_CPPFLAGS@ $(AM_CXXFLAGS) $(CXXFLAGS) \
	  -I$(ZFIX_PREFIX)/include $(srcdir)/ZfixTraceTest.cc \
	  -L$(ZFIX_PREFIX)/lib -Wl,-rpath-link,$(ZFIX_PREFIX)/lib \
	  -lZfix -lZfixFmt -lZi -lZf -lZe -lZt -lZm -lZu \
	  @Z_IO_LIBS@ @Z_ZT_LIBS@ @Z_MT_LIBS@ @Z_LIBS@ -o ZfixInstTraceTest
	LD_LIBRARY_PATH="$(ZFIX_PREFIX)/lib:$${LD_LIBRARY_PATH}" prove ./ZfixInstTraceTest

CLEANFILES += ZfixAsanDispatchTest ZfixInstDispatchTest
.PHONY: asan-dispatch-test installed-dispatch-test
asan-dispatch-test:
	$(LIBTOOL) --tag=CXX --mode=link $(CXX) $(DEFS) $(AM_CPPFLAGS) \
	  $(CPPFLAGS) $(AM_CXXFLAGS) $(CXXFLAGS) -DZFIXFMT_EXPORTS -fsanitize=address \
	  -fno-omit-frame-pointer $(srcdir)/ZfixDispatchTest.cc \
	  $(top_srcdir)/zfix/src/ZfixFmt.cc $(top_srcdir)/zfix/src/ZfixFrame.cc \
	  $(ZfixDeclTest_LDADD) -o ZfixAsanDispatchTest
	ASAN_OPTIONS=detect_leaks=1 prove ./ZfixAsanDispatchTest
installed-dispatch-test:
	$(CXX) $(CPPFLAGS) @Z_CPPFLAGS@ $(AM_CXXFLAGS) $(CXXFLAGS) \
	  -I$(ZFIX_PREFIX)/include $(srcdir)/ZfixDispatchTest.cc \
	  -L$(ZFIX_PREFIX)/lib -Wl,-rpath-link,$(ZFIX_PREFIX)/lib \
	  -lZfix -lZfixFmt -lZi -lZf -lZe -lZt -lZm -lZu \
	  @Z_IO_LIBS@ @Z_ZT_LIBS@ @Z_MT_LIBS@ @Z_LIBS@ -o ZfixInstDispatchTest
	LD_LIBRARY_PATH="$(ZFIX_PREFIX)/lib:$${LD_LIBRARY_PATH}" prove ./ZfixInstDispatchTest

CLEANFILES += ZfixAsanContextTest ZfixInstContextTest
.PHONY: asan-context-test installed-context-test
asan-context-test:
	$(LIBTOOL) --tag=CXX --mode=link $(CXX) $(DEFS) $(AM_CPPFLAGS) \
	  -I$(top_srcdir)/zi/src $(CPPFLAGS) $(AM_CXXFLAGS) $(CXXFLAGS) \
	  -fsanitize=address -fno-omit-frame-pointer $(srcdir)/ZfixContextTest.cc \
	  $(ZfixRxTest_LDADD) -o ZfixAsanContextTest
	ASAN_OPTIONS=detect_leaks=1 prove ./ZfixAsanContextTest
installed-context-test:
	$(CXX) $(CPPFLAGS) @Z_CPPFLAGS@ $(AM_CXXFLAGS) $(CXXFLAGS) \
	  -I$(ZFIX_PREFIX)/include $(srcdir)/ZfixContextTest.cc \
	  -L$(ZFIX_PREFIX)/lib -Wl,-rpath-link,$(ZFIX_PREFIX)/lib \
	  -lZfix -lZfixFmt -lZi -lZf -lZe -lZt -lZm -lZu \
	  @Z_IO_LIBS@ @Z_ZT_LIBS@ @Z_MT_LIBS@ @Z_LIBS@ -o ZfixInstContextTest
	LD_LIBRARY_PATH="$(ZFIX_PREFIX)/lib:$${LD_LIBRARY_PATH}" prove ./ZfixInstContextTest

CLEANFILES += ZfixAsanSelectTest ZfixInstSelectTest
.PHONY: asan-select-test installed-select-test
asan-select-test:
	$(LIBTOOL) --tag=CXX --mode=link $(CXX) $(DEFS) $(AM_CPPFLAGS) \
	  -I$(top_srcdir)/zi/src $(CPPFLAGS) $(AM_CXXFLAGS) $(CXXFLAGS) \
	  -DZFIX_EXPORTS -DZFIXFMT_EXPORTS -fsanitize=address -fno-omit-frame-pointer \
	  $(srcdir)/ZfixSelectTest.cc $(top_srcdir)/zfix/src/ZfixTxMsg.cc \
	  $(top_srcdir)/zfix/src/ZfixFmt.cc $(top_srcdir)/zfix/src/ZfixFrame.cc \
	  $(ZfixRxTest_LDADD) -o ZfixAsanSelectTest
	ASAN_OPTIONS=detect_leaks=1 prove ./ZfixAsanSelectTest
installed-select-test:
	$(CXX) $(CPPFLAGS) @Z_CPPFLAGS@ $(AM_CXXFLAGS) $(CXXFLAGS) \
	  -I$(ZFIX_PREFIX)/include $(srcdir)/ZfixSelectTest.cc \
	  -L$(ZFIX_PREFIX)/lib -Wl,-rpath-link,$(ZFIX_PREFIX)/lib \
	  -lZfix -lZfixFmt -lZi -lZf -lZe -lZt -lZm -lZu \
	  @Z_IO_LIBS@ @Z_ZT_LIBS@ @Z_MT_LIBS@ @Z_LIBS@ -o ZfixInstSelectTest
	LD_LIBRARY_PATH="$(ZFIX_PREFIX)/lib:$${LD_LIBRARY_PATH}" prove ./ZfixInstSelectTest

EXTRA_DIST += ZfixSendSchemaTest.cc
CLEANFILES += ZfixSendSchemaTest.log
.PHONY: send-schema-test
send-schema-test:
	@if $(CXX) $(DEFS) $(AM_CPPFLAGS) -I$(top_srcdir)/zi/src \
	  -I$(top_srcdir)/ztcp/src -I$(top_srcdir)/ztls/src \
	  $(CPPFLAGS) $(AM_CXXFLAGS) $(CXXFLAGS) -fsyntax-only \
	  $(srcdir)/ZfixSendSchemaTest.cc > ZfixSendSchemaTest.log 2>&1; then \
	  cat ZfixSendSchemaTest.log; exit 1; \
	fi
	@grep -q 'ZuTypeIn<Entry' ZfixSendSchemaTest.log || \
	  { cat ZfixSendSchemaTest.log; exit 1; }
	@echo 'typed send outside catalog: expected failure'

EXTRA_DIST += ZfixReplySchemaTest.cc
CLEANFILES += ZfixReplySchemaTest-0.log ZfixReplySchemaTest-1.log ZfixReplySchemaTest-2.log
.PHONY: reply-schema-test
reply-schema-test:
	@for zfix_case in 0 1 2; do \
	  if $(CXX) $(DEFS) $(AM_CPPFLAGS) -I$(top_srcdir)/zi/src \
	    -I$(top_srcdir)/ztcp/src -I$(top_srcdir)/ztls/src \
	    $(CPPFLAGS) $(AM_CXXFLAGS) $(CXXFLAGS) -fsyntax-only \
	    -DZFIX_REPLY_CASE=$$zfix_case $(srcdir)/ZfixReplySchemaTest.cc \
	    > ZfixReplySchemaTest-$$zfix_case.log 2>&1; then \
	    test $$zfix_case = 0 || exit 1; \
	  else \
	    test $$zfix_case != 0 || { cat ZfixReplySchemaTest-0.log; exit 1; }; \
	    grep -q 'ZuTypeIn<Response' ZfixReplySchemaTest-$$zfix_case.log || \
	      { cat ZfixReplySchemaTest-$$zfix_case.log; exit 1; }; \
	  fi; \
	done
	@echo 'typed reply: positive fixture and two expected failures'

CLEANFILES += ZfixAsanWorkTest ZfixInstWorkTest
.PHONY: asan-work-test installed-work-test
asan-work-test:
	$(LIBTOOL) --tag=CXX --mode=link $(CXX) $(DEFS) $(AM_CPPFLAGS) \
	  $(CPPFLAGS) $(AM_CXXFLAGS) $(CXXFLAGS) -fsanitize=address \
	  -fno-omit-frame-pointer -DZFIX_EXPORTS $(srcdir)/ZfixWorkTest.cc \
	  $(top_srcdir)/zfix/src/ZfixWork.cc $(ZfixLinkTest_LDADD) -o ZfixAsanWorkTest
	ASAN_OPTIONS=detect_leaks=1 prove ./ZfixAsanWorkTest
installed-work-test:
	$(CXX) $(CPPFLAGS) @Z_CPPFLAGS@ $(AM_CXXFLAGS) $(CXXFLAGS) \
	  -I$(ZFIX_PREFIX)/include $(srcdir)/ZfixWorkTest.cc \
	  -L$(ZFIX_PREFIX)/lib -Wl,-rpath-link,$(ZFIX_PREFIX)/lib \
	  -lZfix -lZfixFmt -lZi -lZf -lZe -lZt -lZm -lZu \
	  @Z_ZT_LIBS@ @Z_MT_LIBS@ @Z_LIBS@ -o ZfixInstWorkTest
	LD_LIBRARY_PATH="$(ZFIX_PREFIX)/lib:$${LD_LIBRARY_PATH}" prove ./ZfixInstWorkTest

CLEANFILES += ZfixAsanDemoTest ZfixInstDemoTest
.PHONY: asan-demo-test installed-demo-test
asan-demo-test:
	$(LIBTOOL) --tag=CXX --mode=link $(CXX) $(DEFS) $(ZfixDemoTest_CPPFLAGS) \
	  $(CPPFLAGS) $(AM_CXXFLAGS) $(CXXFLAGS) -fsanitize=address \
	  -fno-omit-frame-pointer $(srcdir)/ZfixDemoTest.cc \
	  $(srcdir)/ZfixDemo.cc $(ZfixDemoTest_LDADD) -o ZfixAsanDemoTest
	ASAN_OPTIONS=detect_leaks=1 prove ./ZfixAsanDemoTest
installed-demo-test:
	$(CXX) $(CPPFLAGS) @Z_CPPFLAGS@ $(AM_CXXFLAGS) $(CXXFLAGS) \
	  -I$(ZFIX_PREFIX)/include $(srcdir)/ZfixDemoTest.cc \
	  $(srcdir)/ZfixDemo.cc \
	  -L$(ZFIX_PREFIX)/lib -Wl,-rpath-link,$(ZFIX_PREFIX)/lib \
	  -lZfix -lZfixFmt -lZi -lZf -lZe -lZt -lZm -lZu \
	  @Z_ZT_LIBS@ @Z_MT_LIBS@ @Z_LIBS@ -o ZfixInstDemoTest
	LD_LIBRARY_PATH="$(ZFIX_PREFIX)/lib:$${LD_LIBRARY_PATH}" prove ./ZfixInstDemoTest

# Utility policy/mapping tests compile their own utility sources: test precedes
# util in the clean module traversal and must not require its convenience libs.
ZFIX_LOAD_LIBS = $(top_builddir)/zf/src/libZf.la $(top_builddir)/ze/src/libZe.la \
	$(top_builddir)/zt/src/libZt.la $(top_builddir)/zm/src/libZm.la \
	$(top_builddir)/zu/src/libZu.la @Z_ZT_LIBS@ @Z_MT_LIBS@ @Z_LIBS@
ZfixLoadTest_SOURCES = ZfixLoadTest.cc $(top_srcdir)/zfix/util/load.cc
ZfixLoadTest_LDADD = $(ZFIX_LOAD_LIBS)
ZfixLoadFIXTest_SOURCES = ZfixLoadFIXTest.cc $(top_srcdir)/zfix/util/load.cc $(top_srcdir)/zfix/util/load_fix.cc
ZfixLoadFIXTest_CPPFLAGS = $(AM_CPPFLAGS) -I$(top_srcdir)/zi/src
ZfixLoadFIXTest_LDADD = $(ZfixLinkTest_LDADD)
ZfixLoadRunTest_SOURCES = ZfixLoadRunTest.cc $(top_srcdir)/zfix/util/load.cc $(top_srcdir)/zfix/util/load_run.cc
ZfixLoadRunTest_LDADD = $(ZFIX_LOAD_LIBS)

CLEANFILES += ZfixAsanLoadTest ZfixAsanLoadFIXTest ZfixAsanRunTest ZfixInstLoadTest ZfixInstLoadFIXTest ZfixInstRunTest
.PHONY: asan-load-test installed-load-test
asan-test: asan-load-test
asan-load-test:
	$(LIBTOOL) --tag=CXX --mode=link $(CXX) $(DEFS) $(AM_CPPFLAGS) \
	  $(CPPFLAGS) $(AM_CXXFLAGS) $(CXXFLAGS) -fsanitize=address -fno-omit-frame-pointer \
	  $(srcdir)/ZfixLoadTest.cc $(top_srcdir)/zfix/util/load.cc $(ZFIX_LOAD_LIBS) -o ZfixAsanLoadTest
	$(LIBTOOL) --tag=CXX --mode=link $(CXX) $(DEFS) $(AM_CPPFLAGS) \
	  $(CPPFLAGS) $(AM_CXXFLAGS) $(CXXFLAGS) -fsanitize=address -fno-omit-frame-pointer \
	  $(srcdir)/ZfixLoadFIXTest.cc $(top_srcdir)/zfix/util/load.cc $(top_srcdir)/zfix/util/load_fix.cc \
	  $(ZfixDeclTest_LDADD) -o ZfixAsanLoadFIXTest
	$(LIBTOOL) --tag=CXX --mode=link $(CXX) $(DEFS) $(AM_CPPFLAGS) \
	  $(CPPFLAGS) $(AM_CXXFLAGS) $(CXXFLAGS) -fsanitize=address -fno-omit-frame-pointer \
	  $(srcdir)/ZfixLoadRunTest.cc $(top_srcdir)/zfix/util/load_run.cc $(top_srcdir)/zfix/util/load.cc \
	  $(ZFIX_LOAD_LIBS) -o ZfixAsanRunTest
	ASAN_OPTIONS=detect_leaks=1:exitcode=99 LSAN_OPTIONS=exitcode=99 prove ./ZfixAsanLoadTest ./ZfixAsanLoadFIXTest ./ZfixAsanRunTest

installed-test: installed-load-test
installed-load-test:
	$(CXX) $(CPPFLAGS) @Z_CPPFLAGS@ $(AM_CXXFLAGS) $(CXXFLAGS) \
	  -I$(ZFIX_PREFIX)/include $(srcdir)/ZfixLoadTest.cc $(top_srcdir)/zfix/util/load.cc \
	  -L$(ZFIX_PREFIX)/lib -Wl,-rpath-link,$(ZFIX_PREFIX)/lib \
	  -lZf -lZe -lZt -lZm -lZu @Z_ZT_LIBS@ @Z_MT_LIBS@ @Z_LIBS@ -o ZfixInstLoadTest
	$(CXX) $(CPPFLAGS) @Z_CPPFLAGS@ $(AM_CXXFLAGS) $(CXXFLAGS) \
	  -I$(ZFIX_PREFIX)/include $(srcdir)/ZfixLoadFIXTest.cc $(top_srcdir)/zfix/util/load.cc $(top_srcdir)/zfix/util/load_fix.cc \
	  -L$(ZFIX_PREFIX)/lib -Wl,-rpath-link,$(ZFIX_PREFIX)/lib \
	  -lZfix -lZfixFmt -lZi -lZf -lZe -lZt -lZm -lZu @Z_ZT_LIBS@ @Z_MT_LIBS@ @Z_LIBS@ -o ZfixInstLoadFIXTest
	$(CXX) $(CPPFLAGS) @Z_CPPFLAGS@ $(AM_CXXFLAGS) $(CXXFLAGS) \
	  -I$(ZFIX_PREFIX)/include $(srcdir)/ZfixLoadRunTest.cc $(top_srcdir)/zfix/util/load_run.cc $(top_srcdir)/zfix/util/load.cc \
	  -L$(ZFIX_PREFIX)/lib -Wl,-rpath-link,$(ZFIX_PREFIX)/lib \
	  -lZf -lZe -lZt -lZm -lZu @Z_ZT_LIBS@ @Z_MT_LIBS@ @Z_LIBS@ -o ZfixInstRunTest
	LD_LIBRARY_PATH="$(ZFIX_PREFIX)/lib:$${LD_LIBRARY_PATH}" prove ./ZfixInstLoadTest ./ZfixInstLoadFIXTest ./ZfixInstRunTest

ZfixSignTest_SOURCES = ZfixSignTest.cc
ZfixSignTest_CPPFLAGS = $(AM_CPPFLAGS) -I$(top_srcdir)/zi/src -I$(top_srcdir)/ztls/src
ZfixSignTest_LDADD = $(ZfixRxTest_LDADD) $(top_builddir)/ztls/src/libZtls.la @PTLS_LIBS@
test: sign-test
.PHONY: sign-test
sign-test: all
	prove ./ZfixSignTest
CLEANFILES += ZfixInstSignTest
installed-test: installed-sign-test
.PHONY: installed-sign-test
installed-sign-test:
	$(CXX) $(CPPFLAGS) @Z_CPPFLAGS@ $(AM_CXXFLAGS) $(CXXFLAGS) \
	  -I$(ZFIX_PREFIX)/include $(srcdir)/ZfixSignTest.cc \
	  -L$(ZFIX_PREFIX)/lib -Wl,-rpath-link,$(ZFIX_PREFIX)/lib \
	  -lZfix -lZfixFmt -lZtls -lZi -lZf -lZe -lZt -lZm -lZu \
	  @PTLS_LIBS@ @Z_IO_LIBS@ @Z_ZT_LIBS@ @Z_MT_LIBS@ @Z_LIBS@ -o ZfixInstSignTest
	LD_LIBRARY_PATH="$(ZFIX_PREFIX)/lib:$${LD_LIBRARY_PATH}" prove ./ZfixInstSignTest
