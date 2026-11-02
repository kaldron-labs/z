METASOURCES = AUTO
EXTRA_DIST = README.md quickfix.lock build-quickfix.sh codec-test.sh \
	quickfixapp.hh quickfixwire.hh quickfix.patch dependency-test.py security-test.py duplicate-test.py matrix.py fault-proxy.py lifecycle-test.py probe-test.py

.PHONY: test codec-test
test: codec-test load-test dependency-test

AM_CPPFLAGS = -I$(top_srcdir)/zu/src -I$(top_srcdir)/zm/src -I$(top_srcdir)/zt/src -I$(top_srcdir)/ze/src -I$(top_srcdir)/zf/src -I$(top_srcdir)/zi/src @Z_CPPFLAGS@
AM_CXXFLAGS = @Z_CXXFLAGS@
AM_LDFLAGS = @Z_LDFLAGS@
noinst_PROGRAMS = nativecodec
nativecodec_SOURCES = nativecodec.cc
nativecodec_CPPFLAGS = $(AM_CPPFLAGS) -I$(top_srcdir)/zfix/src
nativecodec_LDADD = $(top_builddir)/zfix/src/libZfix.la $(top_builddir)/zfix/util/libZfixLoadFIX.la $(top_builddir)/zfix/util/libZfixLoad.la \
	$(top_builddir)/zfix/src/libZfixFmt.la $(top_builddir)/zf/src/libZf.la \
	$(top_builddir)/ze/src/libZe.la $(top_builddir)/zt/src/libZt.la \
	$(top_builddir)/zm/src/libZm.la $(top_builddir)/zu/src/libZu.la \
	@Z_ZT_LIBS@ @Z_MT_LIBS@ @Z_LIBS@
if HAVE_QUICKFIX
noinst_PROGRAMS += codec_test quickfix quickfixd quickfixloadtest quickfixprobe
noinst_LTLIBRARIES = libquickfixload.la
QF_CPPFLAGS = $(AM_CPPFLAGS) @QUICKFIX_CFLAGS@ \
	-DQUICKFIX_DICT_PATH='"@QUICKFIX_DICT@"'
QF_LDADD = $(top_builddir)/zfix/util/libZfixLoad.la $(top_builddir)/zi/src/libZi.la \
	$(top_builddir)/zf/src/libZf.la $(top_builddir)/ze/src/libZe.la \
	$(top_builddir)/zt/src/libZt.la $(top_builddir)/zm/src/libZm.la $(top_builddir)/zu/src/libZu.la \
	@QUICKFIX_LIBS@ @Z_IO_LIBS@ @Z_ZT_LIBS@ @Z_MT_LIBS@ @Z_LIBS@
libquickfixload_la_SOURCES = quickfixapp.cc quickfixwire.cc
libquickfixload_la_CPPFLAGS = $(QF_CPPFLAGS)
quickfix_SOURCES = quickfix.cc
quickfix_CPPFLAGS = $(QF_CPPFLAGS)
quickfix_LDADD = libquickfixload.la $(QF_LDADD)
quickfixd_SOURCES = quickfixd.cc
quickfixd_CPPFLAGS = $(QF_CPPFLAGS)
quickfixd_LDADD = libquickfixload.la $(QF_LDADD)
quickfixloadtest_SOURCES = quickfixloadtest.cc
quickfixloadtest_CPPFLAGS = $(QF_CPPFLAGS)
quickfixloadtest_LDADD = libquickfixload.la $(top_builddir)/zfix/itest/libZfixITest.la $(QF_LDADD)
quickfixprobe_SOURCES = quickfixprobe.cc
quickfixprobe_CPPFLAGS = $(AM_CPPFLAGS) @QUICKFIX_CFLAGS@
quickfixprobe_LDADD = $(top_builddir)/zf/src/libZf.la $(top_builddir)/ze/src/libZe.la \
	$(top_builddir)/zt/src/libZt.la $(top_builddir)/zm/src/libZm.la $(top_builddir)/zu/src/libZu.la \
	@QUICKFIX_LIBS@ -lssl -lcrypto @Z_ZT_LIBS@ @Z_MT_LIBS@ @Z_LIBS@
codec_test_SOURCES = codec_test.cc
codec_test_CPPFLAGS = $(AM_CPPFLAGS) @QUICKFIX_CFLAGS@
codec_test_LDADD = $(top_builddir)/zfix/util/libZfixLoad.la \
	$(top_builddir)/zf/src/libZf.la $(top_builddir)/ze/src/libZe.la \
	$(top_builddir)/zt/src/libZt.la $(top_builddir)/zm/src/libZm.la $(top_builddir)/zu/src/libZu.la \
	@QUICKFIX_LIBS@ @Z_ZT_LIBS@ @Z_MT_LIBS@ @Z_LIBS@
endif

codec-test: all
if HAVE_QUICKFIX
	$(SHELL) $(srcdir)/codec-test.sh \
	  ./nativecodec ./codec_test "@QUICKFIX_DICT@"
else
	@echo "configure --with-quickfix=PREFIX to run independent FIX codec tests" >&2
	@exit 1
endif

.PHONY: load-test
load-test: all
if HAVE_QUICKFIX
	prove ./quickfixloadtest
else
	@echo "configure --with-quickfix=PREFIX to run independent FIX load tests" >&2
	@exit 1
endif

.PHONY: dependency-test
dependency-test: all
if HAVE_QUICKFIX
	python3 $(srcdir)/dependency-test.py ./quickfixprobe "@QUICKFIX_DICT@"
else
	@echo "configure --with-quickfix=PREFIX to run independent FIX dependency tests" >&2
	@exit 1
endif

CLEANFILES = QfAsanApp.o QfAsanWire.o QfAsanLoad.o QfAsanRun.o \
	quickfixasan quickfixdasan quickfixloadasan
.PHONY: asan-load-test asan-tools
asan-tools:
if HAVE_QUICKFIX
	$(CXX) $(DEFS) $(QF_CPPFLAGS) $(CPPFLAGS) $(AM_CXXFLAGS) $(CXXFLAGS) \
	  -fsanitize=address -fno-omit-frame-pointer -c $(srcdir)/quickfixapp.cc -o QfAsanApp.o
	$(CXX) $(DEFS) $(QF_CPPFLAGS) $(CPPFLAGS) $(AM_CXXFLAGS) $(CXXFLAGS) \
	  -fsanitize=address -fno-omit-frame-pointer -c $(srcdir)/quickfixwire.cc -o QfAsanWire.o
	$(CXX) $(DEFS) $(QF_CPPFLAGS) $(CPPFLAGS) $(AM_CXXFLAGS) $(CXXFLAGS) \
	  -fsanitize=address -fno-omit-frame-pointer -c $(srcdir)/../util/load.cc -o QfAsanLoad.o
	$(CXX) $(DEFS) $(QF_CPPFLAGS) $(CPPFLAGS) $(AM_CXXFLAGS) $(CXXFLAGS) \
	  -fsanitize=address -fno-omit-frame-pointer -c $(srcdir)/../util/load_run.cc -o QfAsanRun.o
	$(LIBTOOL) --tag=CXX --mode=link $(CXX) $(DEFS) $(QF_CPPFLAGS) $(CPPFLAGS) \
	  $(AM_CXXFLAGS) $(CXXFLAGS) -fsanitize=address -fno-omit-frame-pointer \
	  $(srcdir)/quickfix.cc QfAsanApp.o QfAsanWire.o QfAsanLoad.o QfAsanRun.o $(QF_LDADD) -o quickfixasan
	$(LIBTOOL) --tag=CXX --mode=link $(CXX) $(DEFS) $(QF_CPPFLAGS) $(CPPFLAGS) \
	  $(AM_CXXFLAGS) $(CXXFLAGS) -fsanitize=address -fno-omit-frame-pointer \
	  $(srcdir)/quickfixd.cc QfAsanApp.o QfAsanWire.o QfAsanLoad.o QfAsanRun.o $(QF_LDADD) -o quickfixdasan
	$(LIBTOOL) --tag=CXX --mode=link $(CXX) $(DEFS) $(QF_CPPFLAGS) $(CPPFLAGS) \
	  $(AM_CXXFLAGS) $(CXXFLAGS) -fsanitize=address -fno-omit-frame-pointer \
	  $(srcdir)/quickfixloadtest.cc QfAsanApp.o QfAsanWire.o QfAsanLoad.o QfAsanRun.o \
	  $(top_builddir)/zfix/itest/libZfixITest.la $(QF_LDADD) -o quickfixloadasan
else
	@echo "configure --with-quickfix=PREFIX to build independent FIX sanitizer tools" >&2
	@exit 1
endif
asan-load-test: asan-tools
	ASAN_OPTIONS=detect_leaks=1:exitcode=99 LSAN_OPTIONS=exitcode=99 prove ./quickfixloadasan

.PHONY: process-test fault-test security-test duplicate-test
process-test: all
if HAVE_QUICKFIX
	python3 $(srcdir)/matrix.py --profile canonical
else
	@echo "configure --with-quickfix=PREFIX to run independent FIX processes" >&2
	@exit 1
endif
fault-test: all
if HAVE_QUICKFIX
	python3 $(srcdir)/matrix.py --profile faults
else
	@echo "configure --with-quickfix=PREFIX to run independent FIX faults" >&2
	@exit 1
endif
security-test: all
if HAVE_QUICKFIX
	python3 $(srcdir)/security-test.py --native-client $(top_builddir)/zfix/util/zfix \
	  --native-server $(top_builddir)/zfix/util/zfixd --quickfix-client ./quickfix \
	  --quickfix-server ./quickfixd --dictionary "@QUICKFIX_DICT@"
else
	@echo "configure --with-quickfix=PREFIX to run independent FIX security tests" >&2
	@exit 1
endif
duplicate-test: all
if HAVE_QUICKFIX
	python3 $(srcdir)/duplicate-test.py --native-client $(top_builddir)/zfix/util/zfix \
	  --quickfix-client ./quickfix \
	  --quickfix-server ./quickfixd --dictionary "@QUICKFIX_DICT@"
else
	@echo "configure --with-quickfix=PREFIX to run independent FIX duplicate tests" >&2
	@exit 1
endif

.PHONY: lifecycle-test probe-test
lifecycle-test: all
if HAVE_QUICKFIX
	python3 $(srcdir)/lifecycle-test.py --native-client $(top_builddir)/zfix/util/zfix \
	  --native-server $(top_builddir)/zfix/util/zfixd --quickfix-client ./quickfix \
	  --quickfix-server ./quickfixd --dictionary "@QUICKFIX_DICT@"
else
	@echo "configure --with-quickfix=PREFIX to run independent FIX lifecycle tests" >&2
	@exit 1
endif
probe-test: all
if HAVE_QUICKFIX
	python3 $(srcdir)/probe-test.py --native-client $(top_builddir)/zfix/util/zfix \
	  --native-server $(top_builddir)/zfix/util/zfixd --quickfix-client ./quickfix \
	  --quickfix-server ./quickfixd --dictionary "@QUICKFIX_DICT@"
else
	@echo "configure --with-quickfix=PREFIX to run independent FIX probe tests" >&2
	@exit 1
endif
