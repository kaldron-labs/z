METASOURCES = AUTO
EXTRA_DIST = README.md codec.hh heaptrace.cc audittrace.cc load.py results.py
EXTRA_DIST += RESULTS.md results/README.md results/2026-10-28.tar.xz
EXTRA_DIST += REPAIR3.md results/repair3/manifest.json results/repair3/inspect.cc \
	results/repair3/inspect-1.json results/repair3/inspect-100000.json \
	results/repair3/before-0.jsonl results/repair3/before-1.jsonl results/repair3/before-2.jsonl \
	results/repair3/after-0.jsonl results/repair3/after-1.jsonl results/repair3/after-2.jsonl
AM_CPPFLAGS = -I$(top_srcdir)/zu/src -I$(top_srcdir)/zm/src -I$(top_srcdir)/zt/src -I$(top_srcdir)/ze/src -I$(top_srcdir)/zf/src -I$(top_srcdir)/zfix/src @Z_CPPFLAGS@
AM_CXXFLAGS = @Z_CXXFLAGS@
AM_LDFLAGS = @Z_LDFLAGS@
noinst_PROGRAMS = nativecodec dispatchcodec
dispatchcodec_SOURCES = dispatchcodec.cc
dispatchcodec_LDADD = $(nativecodec_LDADD)
nativecodec_SOURCES = nativecodec.cc
nativecodec_LDADD = $(top_builddir)/zfix/util/libZfixLoad.la \
	$(top_builddir)/zfix/src/libZfixFmt.la $(top_builddir)/zf/src/libZf.la \
	$(top_builddir)/ze/src/libZe.la $(top_builddir)/zt/src/libZt.la \
	$(top_builddir)/zm/src/libZm.la $(top_builddir)/zu/src/libZu.la \
	@Z_ZT_LIBS@ @Z_MT_LIBS@ @Z_LIBS@
if HAVE_QUICKFIX
noinst_PROGRAMS += quickfixcodec
quickfixcodec_SOURCES = quickfixcodec.cc
quickfixcodec_CPPFLAGS = $(AM_CPPFLAGS) @QUICKFIX_CFLAGS@ \
	-DQUICKFIX_DICT_PATH='"@QUICKFIX_DICT@"'
quickfixcodec_LDADD = $(top_builddir)/zfix/interop/libquickfixload.la \
	$(top_builddir)/zfix/util/libZfixLoad.la $(top_builddir)/zi/src/libZi.la \
	$(top_builddir)/zf/src/libZf.la $(top_builddir)/ze/src/libZe.la \
	$(top_builddir)/zt/src/libZt.la $(top_builddir)/zm/src/libZm.la \
	$(top_builddir)/zu/src/libZu.la @QUICKFIX_LIBS@ \
	@Z_IO_LIBS@ @Z_ZT_LIBS@ @Z_MT_LIBS@ @Z_LIBS@
endif
.PHONY: test codec-bench heaptrace
test:
codec-bench: all
	./nativecodec
if HAVE_QUICKFIX
	./quickfixcodec
endif
# Diagnostic observer is deliberately Linux/glibc-only, outside the timing run.
heaptrace:
	$(CXX) -std=gnu++2b -O2 -fPIC -shared -fno-builtin -Wall -Wextra \
	  $(CPPFLAGS) -isystem $(top_srcdir)/zu/src \
	  $(srcdir)/heaptrace.cc -ldl -o heaptrace.so
CLEANFILES = heaptrace.so

.PHONY: audittrace
audittrace:
	$(CXX) -std=gnu++2b -O2 -fPIC -shared -fno-builtin -Wall -Wextra \
	  $(CPPFLAGS) -isystem $(top_srcdir)/zu/src \
	  $(srcdir)/audittrace.cc -o audittrace.so
CLEANFILES += audittrace.so
