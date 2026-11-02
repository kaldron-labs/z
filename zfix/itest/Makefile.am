METASOURCES = AUTO
AM_CPPFLAGS = -I$(top_srcdir)/zu/src -I$(top_srcdir)/zm/src \
	-I$(top_srcdir)/zt/src -I$(top_srcdir)/ze/src -I$(top_srcdir)/zf/src \
	-I$(top_srcdir)/zi/src -I$(top_srcdir)/zi/test -I$(top_srcdir)/ztcp/src -I$(top_srcdir)/ztls/src -I$(top_srcdir)/zfix/src \
	@Z_CPPFLAGS@
AM_CXXFLAGS = @Z_CXXFLAGS@
AM_LDFLAGS = @Z_LDFLAGS@
EXTRA_DIST = README.md tls-test.sh zfix_demo_io.hh zfix_demo_check.sh zfix_demo_tls_check.sh \
	native_io_test.py recovery_swap.hh session_preauth_test.hh managed_partial_test.hh
noinst_LTLIBRARIES = libZfixITest.la
nodist_libZfixITest_la_SOURCES = ZiTestResidue.cc
BUILT_SOURCES = ZiTestResidue.cc
CLEANFILES = ZiTestResidue.cc

ZiTestResidue.cc:
	$(AM_V_GEN)$(LN_S) $(abs_top_srcdir)/zi/test/ZiTestResidue.cc $@

noinst_PROGRAMS = native_io_test zfix_demo zfix_demo_test ingress_tcp_test ingress_tls_test reset_tcp_test reset_tls_test raw_tcp_test raw_tls_test recovery_tcp_test recovery_tls_test tcp_test tls_test session_tcp_test session_tls_test
zfix_demo_SOURCES = zfix_demo.cc $(top_srcdir)/zfix/test/ZfixDemo.cc
native_io_test_SOURCES = native_io_test.cc
native_io_test_LDADD = $(top_builddir)/zu/src/libZu.la @Z_LIBS@
zfix_demo_test_SOURCES = zfix_demo_test.cc
zfix_demo_test_LDADD = $(top_builddir)/zu/src/libZu.la @Z_LIBS@
ingress_tcp_test_SOURCES = ingress_io_test.cc
ingress_tls_test_SOURCES = ingress_io_test.cc
ingress_tls_test_CPPFLAGS = $(AM_CPPFLAGS) -DZFIX_TEST_TLS
reset_tcp_test_SOURCES = reset_io_test.cc
reset_tls_test_SOURCES = reset_io_test.cc
reset_tls_test_CPPFLAGS = $(AM_CPPFLAGS) -DZFIX_TEST_TLS
raw_tcp_test_SOURCES = raw_io_test.cc
raw_tls_test_SOURCES = raw_io_test.cc
raw_tls_test_CPPFLAGS = $(AM_CPPFLAGS) -DZFIX_TEST_TLS
recovery_tcp_test_SOURCES = recovery_io_test.cc
recovery_tcp_test_LDADD = libZfixITest.la $(LDADD)
recovery_tls_test_SOURCES = recovery_io_test.cc
recovery_tls_test_LDADD = libZfixITest.la $(LDADD)
recovery_tls_test_CPPFLAGS = $(AM_CPPFLAGS) -DZFIX_TEST_TLS
session_tcp_test_SOURCES = session_io_test.cc
session_tls_test_SOURCES = session_io_test.cc
session_tls_test_CPPFLAGS = $(AM_CPPFLAGS) -DZFIX_TEST_TLS
tcp_test_SOURCES = transport_test.cc
tls_test_SOURCES = transport_test.cc
tls_test_CPPFLAGS = $(AM_CPPFLAGS) -DZFIX_TEST_TLS
LDADD = $(top_builddir)/zfix/src/libZfix.la \
	$(top_builddir)/zfix/src/libZfixFmt.la $(top_builddir)/ztls/src/libZtls.la \
	$(top_builddir)/ztcp/src/libZtcp.la \
	$(top_builddir)/zi/src/libZi.la $(top_builddir)/zf/src/libZf.la \
	$(top_builddir)/ze/src/libZe.la $(top_builddir)/zt/src/libZt.la \
	$(top_builddir)/zm/src/libZm.la $(top_builddir)/zu/src/libZu.la \
	@PTLS_LIBS@ @Z_IO_LIBS@ @Z_ZT_LIBS@ @Z_MT_LIBS@ @Z_LIBS@

.PHONY: test
test: export ZI_LOGDIR = $(abs_top_builddir)/logs
test: all
	prove ./ingress_tcp_test ./tcp_test ./session_tcp_test ./recovery_tcp_test ./raw_tcp_test ./reset_tcp_test
	$(SHELL) $(srcdir)/tls-test.sh ./ingress_tls_test ./tls_test ./session_tls_test ./recovery_tls_test ./raw_tls_test ./reset_tls_test
	ZFIX_DEMO_DIR=$(srcdir) prove ./zfix_demo_test
	$(MAKE) $(AM_MAKEFLAGS) io-test

ZFIX_IO_SRC = $(top_srcdir)/zfix/util
.PHONY: io-test asan-io-test installed-io-test
io-test: export ZI_LOGDIR = $(abs_top_builddir)/logs
io-test: all
	ZFIX_IO_DIR=$(srcdir) ZFIX_IO_CLIENT=$(top_builddir)/zfix/util/zfix \
	  ZFIX_IO_SERVER=$(top_builddir)/zfix/util/zfixd prove ./native_io_test

CLEANFILES += native_io_asan_test native_io_inst_test zfix_asan_native.o zfix_asan zfixd_asan zfix_inst_native.o zfix_inst zfixd_inst
asan-io-test: export ZI_LOGDIR = $(abs_top_builddir)/logs
asan-io-test:
	$(CXX) $(DEFS) $(AM_CPPFLAGS) $(CPPFLAGS) $(AM_CXXFLAGS) $(CXXFLAGS) \
	  -fsanitize=address -fno-omit-frame-pointer -c $(ZFIX_IO_SRC)/load_native.cc \
	  -o zfix_asan_native.o
	$(LIBTOOL) --tag=CXX --mode=link $(CXX) $(DEFS) $(AM_CPPFLAGS) \
	  $(CPPFLAGS) $(AM_CXXFLAGS) $(CXXFLAGS) -fsanitize=address -fno-omit-frame-pointer \
	  zfix_asan_native.o $(ZFIX_IO_SRC)/zfix.cc $(ZFIX_IO_SRC)/load.cc \
	  $(ZFIX_IO_SRC)/load_run.cc $(ZFIX_IO_SRC)/load_fix.cc $(LDADD) -o zfix_asan
	$(LIBTOOL) --tag=CXX --mode=link $(CXX) $(DEFS) $(AM_CPPFLAGS) \
	  $(CPPFLAGS) $(AM_CXXFLAGS) $(CXXFLAGS) -fsanitize=address -fno-omit-frame-pointer \
	  zfix_asan_native.o $(ZFIX_IO_SRC)/zfixd.cc $(ZFIX_IO_SRC)/load.cc \
	  $(ZFIX_IO_SRC)/load_run.cc $(ZFIX_IO_SRC)/load_fix.cc $(LDADD) -o zfixd_asan
	$(LIBTOOL) --tag=CXX --mode=link $(CXX) $(DEFS) $(AM_CPPFLAGS) \
	  $(CPPFLAGS) $(AM_CXXFLAGS) $(CXXFLAGS) -fsanitize=address -fno-omit-frame-pointer \
	  $(srcdir)/native_io_test.cc $(native_io_test_LDADD) -o native_io_asan_test
	ASAN_OPTIONS=detect_leaks=1:exitcode=99 LSAN_OPTIONS=exitcode=99 \
	  ZFIX_IO_DIR=$(srcdir) ZFIX_IO_CLIENT=./zfix_asan ZFIX_IO_SERVER=./zfixd_asan prove ./native_io_asan_test

ZFIX_IO_INST_LIBS = -L$(ZFIX_PREFIX)/lib -Wl,-rpath-link,$(ZFIX_PREFIX)/lib \
	-lZfix -lZfixFmt -lZtls -lZtcp -lZi -lZf -lZe -lZt -lZm -lZu \
	@PTLS_LIBS@ @Z_IO_LIBS@ @Z_ZT_LIBS@ @Z_MT_LIBS@ @Z_LIBS@
installed-io-test: export ZI_LOGDIR = $(abs_top_builddir)/logs
installed-io-test:
	$(CXX) $(DEFS) $(CPPFLAGS) @Z_CPPFLAGS@ $(AM_CXXFLAGS) $(CXXFLAGS) \
	  -I$(ZFIX_PREFIX)/include -c $(ZFIX_IO_SRC)/load_native.cc -o zfix_inst_native.o
	$(CXX) $(DEFS) $(CPPFLAGS) @Z_CPPFLAGS@ $(AM_CXXFLAGS) $(CXXFLAGS) \
	  -I$(ZFIX_PREFIX)/include zfix_inst_native.o $(ZFIX_IO_SRC)/zfix.cc \
	  $(ZFIX_IO_SRC)/load.cc $(ZFIX_IO_SRC)/load_run.cc \
	  $(ZFIX_IO_SRC)/load_fix.cc $(ZFIX_IO_INST_LIBS) -o zfix_inst
	$(CXX) $(DEFS) $(CPPFLAGS) @Z_CPPFLAGS@ $(AM_CXXFLAGS) $(CXXFLAGS) \
	  -I$(ZFIX_PREFIX)/include zfix_inst_native.o $(ZFIX_IO_SRC)/zfixd.cc \
	  $(ZFIX_IO_SRC)/load.cc $(ZFIX_IO_SRC)/load_run.cc \
	  $(ZFIX_IO_SRC)/load_fix.cc $(ZFIX_IO_INST_LIBS) -o zfixd_inst
	$(CXX) $(CPPFLAGS) @Z_CPPFLAGS@ $(AM_CXXFLAGS) $(CXXFLAGS) \
	  -I$(ZFIX_PREFIX)/include $(srcdir)/native_io_test.cc \
	  -L$(ZFIX_PREFIX)/lib -Wl,-rpath-link,$(ZFIX_PREFIX)/lib -lZu @Z_LIBS@ -o native_io_inst_test
	LD_LIBRARY_PATH="$(ZFIX_PREFIX)/lib:$${LD_LIBRARY_PATH}" \
	  ZFIX_IO_DIR=$(srcdir) ZFIX_IO_CLIENT=./zfix_inst ZFIX_IO_SERVER=./zfixd_inst prove ./native_io_inst_test

# Rich application-policy regression fixtures, separate from public examples.
CLEANFILES += zfix_asan_demo zfix_asan_demo_test zfix_inst_demo zfix_inst_demo_test
.PHONY: asan-demo-test installed-demo-test
asan-demo-test: export ZI_LOGDIR = $(abs_top_builddir)/logs
asan-demo-test:
	$(LIBTOOL) --tag=CXX --mode=link $(CXX) $(DEFS) $(AM_CPPFLAGS) \
	  $(CPPFLAGS) $(AM_CXXFLAGS) $(CXXFLAGS) -fsanitize=address \
	  -fno-omit-frame-pointer $(srcdir)/zfix_demo.cc \
	  $(top_srcdir)/zfix/test/ZfixDemo.cc $(LDADD) -o zfix_asan_demo
	$(LIBTOOL) --tag=CXX --mode=link $(CXX) $(DEFS) $(AM_CPPFLAGS) \
	  $(CPPFLAGS) $(AM_CXXFLAGS) $(CXXFLAGS) -fsanitize=address \
	  -fno-omit-frame-pointer $(srcdir)/zfix_demo_test.cc \
	  $(zfix_demo_test_LDADD) -o zfix_asan_demo_test
	ASAN_OPTIONS=detect_leaks=1:exitcode=99 LSAN_OPTIONS=exitcode=99 \
	  ZFIX_DEMO_DIR=$(srcdir) ZFIX_DEMO_PROGRAM=./zfix_asan_demo prove ./zfix_asan_demo_test

installed-demo-test: export ZI_LOGDIR = $(abs_top_builddir)/logs
installed-demo-test:
	$(CXX) $(CPPFLAGS) @Z_CPPFLAGS@ $(AM_CXXFLAGS) $(CXXFLAGS) \
	  -I$(ZFIX_PREFIX)/include $(srcdir)/zfix_demo.cc \
	  $(top_srcdir)/zfix/test/ZfixDemo.cc \
	  -L$(ZFIX_PREFIX)/lib -Wl,-rpath-link,$(ZFIX_PREFIX)/lib \
	  -lZfix -lZfixFmt -lZtls -lZtcp -lZi -lZf -lZe -lZt -lZm -lZu \
	  @PTLS_LIBS@ @Z_IO_LIBS@ @Z_ZT_LIBS@ @Z_MT_LIBS@ @Z_LIBS@ -o zfix_inst_demo
	$(CXX) $(CPPFLAGS) @Z_CPPFLAGS@ $(AM_CXXFLAGS) $(CXXFLAGS) \
	  -I$(ZFIX_PREFIX)/include $(srcdir)/zfix_demo_test.cc \
	  -L$(ZFIX_PREFIX)/lib -Wl,-rpath-link,$(ZFIX_PREFIX)/lib \
	  -lZu @Z_LIBS@ -o zfix_inst_demo_test
	LD_LIBRARY_PATH="$(ZFIX_PREFIX)/lib:$${LD_LIBRARY_PATH}" \
	  ZFIX_DEMO_DIR=$(srcdir) ZFIX_DEMO_PROGRAM=./zfix_inst_demo prove ./zfix_inst_demo_test

# Instrument fixture and adapter code; configured dependency DSOs are unchanged.
CLEANFILES += asan_tcp_test asan_tls_test asan_session_tcp_test asan_session_tls_test
.PHONY: asan-test
asan-test:
	$(LIBTOOL) --tag=CXX --mode=link $(CXX) $(DEFS) $(AM_CPPFLAGS) \
	  $(CPPFLAGS) $(AM_CXXFLAGS) $(CXXFLAGS) -fsanitize=address \
	  -fno-omit-frame-pointer $(srcdir)/transport_test.cc $(LDADD) \
	  -o asan_tcp_test
	$(LIBTOOL) --tag=CXX --mode=link $(CXX) $(DEFS) $(AM_CPPFLAGS) \
	  $(CPPFLAGS) $(AM_CXXFLAGS) $(CXXFLAGS) -DZFIX_TEST_TLS -fsanitize=address \
	  -fno-omit-frame-pointer $(srcdir)/transport_test.cc $(LDADD) \
	  -o asan_tls_test
	$(LIBTOOL) --tag=CXX --mode=link $(CXX) $(DEFS) $(AM_CPPFLAGS) \
	  $(CPPFLAGS) $(AM_CXXFLAGS) $(CXXFLAGS) -fsanitize=address \
	  -fno-omit-frame-pointer $(srcdir)/session_io_test.cc $(LDADD) \
	  -o asan_session_tcp_test
	$(LIBTOOL) --tag=CXX --mode=link $(CXX) $(DEFS) $(AM_CPPFLAGS) \
	  $(CPPFLAGS) $(AM_CXXFLAGS) $(CXXFLAGS) -DZFIX_TEST_TLS -fsanitize=address \
	  -fno-omit-frame-pointer $(srcdir)/session_io_test.cc $(LDADD) \
	  -o asan_session_tls_test
	ASAN_OPTIONS=detect_leaks=1 prove ./asan_tcp_test ./asan_session_tcp_test
	ASAN_OPTIONS=detect_leaks=1 $(SHELL) $(srcdir)/tls-test.sh ./asan_tls_test ./asan_session_tls_test

ZFIX_PREFIX = $(prefix)
CLEANFILES += inst_session_tcp_test inst_session_tls_test
.PHONY: installed-test
installed-test:
	$(CXX) $(CPPFLAGS) @Z_CPPFLAGS@ $(AM_CXXFLAGS) $(CXXFLAGS) \
	  -I$(ZFIX_PREFIX)/include $(srcdir)/session_io_test.cc \
	  -L$(ZFIX_PREFIX)/lib -Wl,-rpath-link,$(ZFIX_PREFIX)/lib \
	  -lZfix -lZfixFmt -lZtls -lZtcp -lZi -lZf -lZe -lZt -lZm -lZu \
	  @PTLS_LIBS@ @Z_IO_LIBS@ @Z_ZT_LIBS@ @Z_MT_LIBS@ @Z_LIBS@ \
	  -o inst_session_tcp_test
	$(CXX) $(CPPFLAGS) @Z_CPPFLAGS@ $(AM_CXXFLAGS) $(CXXFLAGS) -DZFIX_TEST_TLS \
	  -I$(ZFIX_PREFIX)/include $(srcdir)/session_io_test.cc \
	  -L$(ZFIX_PREFIX)/lib -Wl,-rpath-link,$(ZFIX_PREFIX)/lib \
	  -lZfix -lZfixFmt -lZtls -lZtcp -lZi -lZf -lZe -lZt -lZm -lZu \
	  @PTLS_LIBS@ @Z_IO_LIBS@ @Z_ZT_LIBS@ @Z_MT_LIBS@ @Z_LIBS@ \
	  -o inst_session_tls_test
	LD_LIBRARY_PATH="$(ZFIX_PREFIX)/lib:$${LD_LIBRARY_PATH}" prove ./inst_session_tcp_test
	LD_LIBRARY_PATH="$(ZFIX_PREFIX)/lib:$${LD_LIBRARY_PATH}" \
	  $(SHELL) $(srcdir)/tls-test.sh ./inst_session_tls_test

CLEANFILES += asan_recovery_tcp_test asan_recovery_tls_test
.PHONY: asan-recovery-test
asan-recovery-test: export ZI_LOGDIR = $(abs_top_builddir)/logs
asan-recovery-test: libZfixITest.la
	$(LIBTOOL) --tag=CXX --mode=link $(CXX) $(DEFS) $(AM_CPPFLAGS) \
	  $(CPPFLAGS) $(AM_CXXFLAGS) $(CXXFLAGS) -fsanitize=address \
	  -fno-omit-frame-pointer $(srcdir)/recovery_io_test.cc \
	  libZfixITest.la $(LDADD) -o asan_recovery_tcp_test
	$(LIBTOOL) --tag=CXX --mode=link $(CXX) $(DEFS) $(AM_CPPFLAGS) \
	  $(CPPFLAGS) $(AM_CXXFLAGS) $(CXXFLAGS) -DZFIX_TEST_TLS -fsanitize=address \
	  -fno-omit-frame-pointer $(srcdir)/recovery_io_test.cc \
	  libZfixITest.la $(LDADD) -o asan_recovery_tls_test
	ASAN_OPTIONS=detect_leaks=1 prove ./asan_recovery_tcp_test
	ASAN_OPTIONS=detect_leaks=1 $(SHELL) $(srcdir)/tls-test.sh ./asan_recovery_tls_test

CLEANFILES += inst_recovery_tcp_test inst_recovery_tls_test
.PHONY: installed-recovery-test
installed-recovery-test: export ZI_LOGDIR = $(abs_top_builddir)/logs
installed-recovery-test: libZfixITest.la
	$(CXX) $(CPPFLAGS) @Z_CPPFLAGS@ $(AM_CXXFLAGS) $(CXXFLAGS) \
	  -I$(top_srcdir)/zi/test -I$(ZFIX_PREFIX)/include $(srcdir)/recovery_io_test.cc \
	  .libs/libZfixITest.a \
	  -L$(ZFIX_PREFIX)/lib -Wl,-rpath-link,$(ZFIX_PREFIX)/lib \
	  -lZfix -lZfixFmt -lZtls -lZtcp -lZi -lZf -lZe -lZt -lZm -lZu \
	  @PTLS_LIBS@ @Z_IO_LIBS@ @Z_ZT_LIBS@ @Z_MT_LIBS@ @Z_LIBS@ \
	  -o inst_recovery_tcp_test
	$(CXX) $(CPPFLAGS) @Z_CPPFLAGS@ $(AM_CXXFLAGS) $(CXXFLAGS) -DZFIX_TEST_TLS \
	  -I$(top_srcdir)/zi/test -I$(ZFIX_PREFIX)/include $(srcdir)/recovery_io_test.cc \
	  .libs/libZfixITest.a \
	  -L$(ZFIX_PREFIX)/lib -Wl,-rpath-link,$(ZFIX_PREFIX)/lib \
	  -lZfix -lZfixFmt -lZtls -lZtcp -lZi -lZf -lZe -lZt -lZm -lZu \
	  @PTLS_LIBS@ @Z_IO_LIBS@ @Z_ZT_LIBS@ @Z_MT_LIBS@ @Z_LIBS@ \
	  -o inst_recovery_tls_test
	LD_LIBRARY_PATH="$(ZFIX_PREFIX)/lib:$${LD_LIBRARY_PATH}" prove ./inst_recovery_tcp_test
	LD_LIBRARY_PATH="$(ZFIX_PREFIX)/lib:$${LD_LIBRARY_PATH}" \
	  $(SHELL) $(srcdir)/tls-test.sh ./inst_recovery_tls_test

CLEANFILES += asan_raw_tcp_test asan_raw_tls_test
.PHONY: asan-raw-test
asan-raw-test:
	$(LIBTOOL) --tag=CXX --mode=link $(CXX) $(DEFS) $(AM_CPPFLAGS) \
	  $(CPPFLAGS) $(AM_CXXFLAGS) $(CXXFLAGS) -fsanitize=address \
	  -fno-omit-frame-pointer $(srcdir)/raw_io_test.cc $(LDADD) -o asan_raw_tcp_test
	$(LIBTOOL) --tag=CXX --mode=link $(CXX) $(DEFS) $(AM_CPPFLAGS) \
	  $(CPPFLAGS) $(AM_CXXFLAGS) $(CXXFLAGS) -DZFIX_TEST_TLS -fsanitize=address \
	  -fno-omit-frame-pointer $(srcdir)/raw_io_test.cc $(LDADD) -o asan_raw_tls_test
	ASAN_OPTIONS=detect_leaks=1 prove ./asan_raw_tcp_test
	ASAN_OPTIONS=detect_leaks=1 $(SHELL) $(srcdir)/tls-test.sh ./asan_raw_tls_test

CLEANFILES += inst_raw_tcp_test inst_raw_tls_test
.PHONY: installed-raw-test
installed-raw-test:
	$(CXX) $(CPPFLAGS) @Z_CPPFLAGS@ $(AM_CXXFLAGS) $(CXXFLAGS) \
	  -I$(ZFIX_PREFIX)/include $(srcdir)/raw_io_test.cc \
	  -L$(ZFIX_PREFIX)/lib -Wl,-rpath-link,$(ZFIX_PREFIX)/lib \
	  -lZfix -lZfixFmt -lZtls -lZtcp -lZi -lZf -lZe -lZt -lZm -lZu \
	  @PTLS_LIBS@ @Z_IO_LIBS@ @Z_ZT_LIBS@ @Z_MT_LIBS@ @Z_LIBS@ \
	  -o inst_raw_tcp_test
	$(CXX) $(CPPFLAGS) @Z_CPPFLAGS@ $(AM_CXXFLAGS) $(CXXFLAGS) -DZFIX_TEST_TLS \
	  -I$(ZFIX_PREFIX)/include $(srcdir)/raw_io_test.cc \
	  -L$(ZFIX_PREFIX)/lib -Wl,-rpath-link,$(ZFIX_PREFIX)/lib \
	  -lZfix -lZfixFmt -lZtls -lZtcp -lZi -lZf -lZe -lZt -lZm -lZu \
	  @PTLS_LIBS@ @Z_IO_LIBS@ @Z_ZT_LIBS@ @Z_MT_LIBS@ @Z_LIBS@ \
	  -o inst_raw_tls_test
	LD_LIBRARY_PATH="$(ZFIX_PREFIX)/lib:$${LD_LIBRARY_PATH}" prove ./inst_raw_tcp_test
	LD_LIBRARY_PATH="$(ZFIX_PREFIX)/lib:$${LD_LIBRARY_PATH}" \
	  $(SHELL) $(srcdir)/tls-test.sh ./inst_raw_tls_test

CLEANFILES += asan_reset_tcp_test asan_reset_tls_test
.PHONY: asan-reset-test
asan-reset-test:
	$(LIBTOOL) --tag=CXX --mode=link $(CXX) $(DEFS) $(AM_CPPFLAGS) \
	  $(CPPFLAGS) $(AM_CXXFLAGS) $(CXXFLAGS) -fsanitize=address \
	  -fno-omit-frame-pointer $(srcdir)/reset_io_test.cc $(LDADD) -o asan_reset_tcp_test
	$(LIBTOOL) --tag=CXX --mode=link $(CXX) $(DEFS) $(AM_CPPFLAGS) \
	  $(CPPFLAGS) $(AM_CXXFLAGS) $(CXXFLAGS) -DZFIX_TEST_TLS -fsanitize=address \
	  -fno-omit-frame-pointer $(srcdir)/reset_io_test.cc $(LDADD) -o asan_reset_tls_test
	ASAN_OPTIONS=detect_leaks=1 prove ./asan_reset_tcp_test
	ASAN_OPTIONS=detect_leaks=1 $(SHELL) $(srcdir)/tls-test.sh ./asan_reset_tls_test

CLEANFILES += inst_reset_tcp_test inst_reset_tls_test
.PHONY: installed-reset-test
installed-reset-test:
	$(CXX) $(CPPFLAGS) @Z_CPPFLAGS@ $(AM_CXXFLAGS) $(CXXFLAGS) \
	  -I$(ZFIX_PREFIX)/include $(srcdir)/reset_io_test.cc \
	  -L$(ZFIX_PREFIX)/lib -Wl,-rpath-link,$(ZFIX_PREFIX)/lib \
	  -lZfix -lZfixFmt -lZtls -lZtcp -lZi -lZf -lZe -lZt -lZm -lZu \
	  @PTLS_LIBS@ @Z_IO_LIBS@ @Z_ZT_LIBS@ @Z_MT_LIBS@ @Z_LIBS@ \
	  -o inst_reset_tcp_test
	$(CXX) $(CPPFLAGS) @Z_CPPFLAGS@ $(AM_CXXFLAGS) $(CXXFLAGS) -DZFIX_TEST_TLS \
	  -I$(ZFIX_PREFIX)/include $(srcdir)/reset_io_test.cc \
	  -L$(ZFIX_PREFIX)/lib -Wl,-rpath-link,$(ZFIX_PREFIX)/lib \
	  -lZfix -lZfixFmt -lZtls -lZtcp -lZi -lZf -lZe -lZt -lZm -lZu \
	  @PTLS_LIBS@ @Z_IO_LIBS@ @Z_ZT_LIBS@ @Z_MT_LIBS@ @Z_LIBS@ \
	  -o inst_reset_tls_test
	LD_LIBRARY_PATH="$(ZFIX_PREFIX)/lib:$${LD_LIBRARY_PATH}" prove ./inst_reset_tcp_test
	LD_LIBRARY_PATH="$(ZFIX_PREFIX)/lib:$${LD_LIBRARY_PATH}" \
	  $(SHELL) $(srcdir)/tls-test.sh ./inst_reset_tls_test

CLEANFILES += asan_ingress_tcp_test asan_ingress_tls_test
.PHONY: asan-ingress-test
asan-ingress-test:
	$(LIBTOOL) --tag=CXX --mode=link $(CXX) $(DEFS) $(AM_CPPFLAGS) \
	  $(CPPFLAGS) $(AM_CXXFLAGS) $(CXXFLAGS) -DZFIX_EXPORTS -fsanitize=address \
	  -fno-omit-frame-pointer $(srcdir)/ingress_io_test.cc $(top_srcdir)/zfix/src/ZfixIngress.cc $(LDADD) -o asan_ingress_tcp_test
	$(LIBTOOL) --tag=CXX --mode=link $(CXX) $(DEFS) $(AM_CPPFLAGS) \
	  $(CPPFLAGS) $(AM_CXXFLAGS) $(CXXFLAGS) -DZFIX_TEST_TLS -fsanitize=address \
	  -fno-omit-frame-pointer $(srcdir)/ingress_io_test.cc $(top_srcdir)/zfix/src/ZfixIngress.cc $(LDADD) -o asan_ingress_tls_test
	ASAN_OPTIONS=detect_leaks=1 prove ./asan_ingress_tcp_test
	ASAN_OPTIONS=detect_leaks=1 $(SHELL) $(srcdir)/tls-test.sh ./asan_ingress_tls_test

CLEANFILES += inst_ingress_tcp_test inst_ingress_tls_test
.PHONY: installed-ingress-test
installed-ingress-test:
	$(CXX) $(CPPFLAGS) @Z_CPPFLAGS@ $(AM_CXXFLAGS) $(CXXFLAGS) \
	  -I$(ZFIX_PREFIX)/include $(srcdir)/ingress_io_test.cc \
	  -L$(ZFIX_PREFIX)/lib -Wl,-rpath-link,$(ZFIX_PREFIX)/lib \
	  -lZfix -lZfixFmt -lZtls -lZtcp -lZi -lZf -lZe -lZt -lZm -lZu \
	  @PTLS_LIBS@ @Z_IO_LIBS@ @Z_ZT_LIBS@ @Z_MT_LIBS@ @Z_LIBS@ \
	  -o inst_ingress_tcp_test
	$(CXX) $(CPPFLAGS) @Z_CPPFLAGS@ $(AM_CXXFLAGS) $(CXXFLAGS) -DZFIX_TEST_TLS \
	  -I$(ZFIX_PREFIX)/include $(srcdir)/ingress_io_test.cc \
	  -L$(ZFIX_PREFIX)/lib -Wl,-rpath-link,$(ZFIX_PREFIX)/lib \
	  -lZfix -lZfixFmt -lZtls -lZtcp -lZi -lZf -lZe -lZt -lZm -lZu \
	  @PTLS_LIBS@ @Z_IO_LIBS@ @Z_ZT_LIBS@ @Z_MT_LIBS@ @Z_LIBS@ \
	  -o inst_ingress_tls_test
	LD_LIBRARY_PATH="$(ZFIX_PREFIX)/lib:$${LD_LIBRARY_PATH}" prove ./inst_ingress_tcp_test
	LD_LIBRARY_PATH="$(ZFIX_PREFIX)/lib:$${LD_LIBRARY_PATH}" \
	  $(SHELL) $(srcdir)/tls-test.sh ./inst_ingress_tls_test
