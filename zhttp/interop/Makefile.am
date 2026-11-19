METASOURCES = AUTO
.cc.s:
	$(CXXCOMPILE) -c -S $<
%.i.cc: %.cc
	$(CXXCOMPILE) -C -E $< -o $@
AM_CPPFLAGS = -I$(top_srcdir)/zu/src -I$(top_srcdir)/zm/src \
	-I$(top_srcdir)/zt/src -I$(top_srcdir)/ze/src -I$(top_srcdir)/zf/src \
	-I$(top_srcdir)/zi/src \
	-I$(top_srcdir)/ztls/src \
	-I$(top_srcdir)/ztcp/src \
	-I$(top_srcdir)/zquic/src \
	-I$(top_srcdir)/zhttp/src \
	-I$(top_srcdir)/zhttp/util \
	@Z_CPPFLAGS@
AM_CXXFLAGS = @Z_CXXFLAGS@
AM_LDFLAGS = @Z_LDFLAGS@
LDADD = $(top_builddir)/zhttp/util/libzhttputil.la \
	$(top_builddir)/zhttp/src/libZhttp.la \
	$(top_builddir)/zquic/src/libZquic.la \
	$(top_builddir)/ztls/src/libZtls.la \
	$(top_builddir)/zi/src/libZi.la \
	$(top_builddir)/zf/src/libZf.la $(top_builddir)/ze/src/libZe.la \
	$(top_builddir)/zt/src/libZt.la \
	$(top_builddir)/zm/src/libZm.la \
	$(top_builddir)/zu/src/libZu.la \
	@PTLS_LIBS@ @Z_IO_LIBS@ @Z_ZT_LIBS@ @Z_MT_LIBS@ @Z_LIBS@
noinst_LTLIBRARIES = libzhttpqir.la
noinst_PROGRAMS = zhttpqir
noinst_HEADERS = zhttpqir.hh
libzhttpqir_la_SOURCES = zhttpqirimpl.cc
zhttpqir_SOURCES = zhttpqir.cc
zhttpqir_LDADD = libzhttpqir.la $(LDADD)
dist_noinst_SCRIPTS = qir_endpoint.sh
EXTRA_DIST = Dockerfile.interop README.md qir-run.py

.PHONY: qir-stage
qir-stage: zhttpqir
	test -n "$$ZHTTP_QIR_ROOT"
	$(MKDIR_P) "$$ZHTTP_QIR_ROOT$(prefix)/bin"
	$(LIBTOOL) --mode=install $(INSTALL_PROGRAM) zhttpqir \
	  "$$ZHTTP_QIR_ROOT$(prefix)/bin/zhttpqir"
	$(INSTALL_SCRIPT) "$(srcdir)/qir_endpoint.sh" \
	  "$$ZHTTP_QIR_ROOT$(prefix)/bin/qir_endpoint.sh"
