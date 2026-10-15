METASOURCES = AUTO
AM_CPPFLAGS = -I$(top_srcdir)/zu/src -I$(top_srcdir)/zm/src \
	-I$(top_srcdir)/zt/src -I$(top_srcdir)/ze/src \
	-I$(top_srcdir)/zf/src -I$(top_srcdir)/zi/src \
	-I$(top_srcdir)/zi/test \
	@Z_CPPFLAGS@
AM_CXXFLAGS = @Z_CXXFLAGS@
AM_LDFLAGS = @Z_LDFLAGS@
LDADD = $(top_builddir)/zi/test/libZiTest.la \
	$(top_builddir)/zi/src/libZi.la \
	$(top_builddir)/zf/src/libZf.la $(top_builddir)/ze/src/libZe.la \
	$(top_builddir)/zt/src/libZt.la $(top_builddir)/zm/src/libZm.la \
	$(top_builddir)/zu/src/libZu.la \
	@Z_IO_LIBS@ @Z_ZT_LIBS@ @Z_MT_LIBS@ @Z_LIBS@

tap_drivers = zringmatrix zitestresidue zheaptune zhashtune
noinst_PROGRAMS = $(tap_drivers) zheapwork zhashwork
zringmatrix_SOURCES = zringmatrix.cc
zitestresidue_SOURCES = zitestresidue.cc
zheaptune_SOURCES = zheaptune.cc zheaptune.hh
zheapwork_SOURCES = zheapwork.cc zheaptune.hh
zhashtune_SOURCES = zhashtune.cc zhashtune.hh
zhashwork_SOURCES = zhashwork.cc zhashtune.hh
EXTRA_DIST = README.md zi_test_residue.py zi_test_residue_test.py \
	zi-test-residue.sh zi-test-residue-test.sh \
	residue/go.mod residue/residue.go residue/residue_unix.go \
	residue/residue_windows.go residue/residue_test.go

.PHONY: ZiRingTest
ZiRingTest:
	$(MAKE) $(AM_MAKEFLAGS) -C $(top_builddir)/zi/test ZiRingTest

.PHONY: test
test: export ZI_LOGDIR = $(abs_top_builddir)/logs
test: $(noinst_PROGRAMS) ZiRingTest
	prove -j1 $(tap_drivers)
	PYTHONDONTWRITEBYTECODE=1 python3 $(srcdir)/zi_test_residue_test.py
	$(SHELL) $(srcdir)/zi-test-residue-test.sh
if HAVE_GO
	cd $(srcdir)/residue && GOCACHE=$(abs_top_builddir)/.go-cache $(GO) test ./...
endif
