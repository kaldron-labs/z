METASOURCES = AUTO
.cc.s:
	$(CXXCOMPILE) -c -S $<
%.i.cc: %.cc
	$(CXXCOMPILE) -C -E $< -o $@
AM_CPPFLAGS = -I$(top_srcdir)/zu/src -I$(top_srcdir)/zm/src \
	-I$(top_srcdir)/zt/src -I$(top_srcdir)/ze/src -I$(top_srcdir)/zf/src \
	-I$(top_srcdir)/zi/src -I$(top_srcdir)/zi/test \
	-I$(top_srcdir)/zv/src -I$(top_srcdir)/zfb/src \
	-I$(top_srcdir)/zdb/src -I$(top_srcdir)/ztc/src -I$(top_srcdir)/zum/src \
	-I$(top_srcdir)/zhttp/src \
	@Z_CPPFLAGS@
AM_CXXFLAGS = @Z_CXXFLAGS@
AM_LDFLAGS = @Z_LDFLAGS@
LDADD = $(top_builddir)/zi/test/libZiTest.la \
	$(top_builddir)/ztc/src/libZtc.la \
	$(top_builddir)/zum/src/libZum.la \
	$(top_builddir)/zdb/src/libZdb.la \
	$(top_builddir)/zfb/src/libZfb.la $(top_builddir)/zv/src/libZv.la \
	$(top_builddir)/zi/src/libZi.la \
	$(top_builddir)/zf/src/libZf.la $(top_builddir)/ze/src/libZe.la \
	$(top_builddir)/zt/src/libZt.la $(top_builddir)/zm/src/libZm.la \
	$(top_builddir)/zu/src/libZu.la \
	@FBS_LIBS@ @Z_IO_LIBS@ @Z_ZT_LIBS@ @Z_MT_LIBS@ @Z_LIBS@
noinst_HEADERS = ZtcTestClient.hh
noinst_PROGRAMS = ZtcMgrTest ZtcSchemaTest \
	ZtcFilterTest ZtcAppTest ZtcAlertTest ZtcAgentTest ZtcHubTest

.PHONY: test
test: export ZI_LOGDIR = $(abs_top_builddir)/logs
test: $(noinst_PROGRAMS)
	prove $(noinst_PROGRAMS)

ZtcMgrTest_SOURCES = ZtcMgrTest.cc
ZtcSchemaTest_SOURCES = ZtcSchemaTest.cc
ZtcFilterTest_SOURCES = ZtcFilterTest.cc
ZtcAppTest_SOURCES = ZtcAppTest.cc
ZtcAppTest_LDADD = $(top_builddir)/ztc/src/libZtcApp.la $(LDADD)
ZtcAlertTest_SOURCES = ZtcAlertTest.cc
ZtcAlertTest_LDADD = $(top_builddir)/ztc/src/libZtcApp.la $(LDADD)
ZtcAgentTest_CPPFLAGS = $(AM_CPPFLAGS) -I$(top_srcdir)/ztcp/src \
	-I$(top_srcdir)/ztls/src -I$(top_srcdir)/zquic/src \
	-I$(top_srcdir)/zhttp/src -I$(top_srcdir)/zws/src
ZtcAgentTest_SOURCES = ZtcAgentTest.cc ../src/ztcagent_daemon.cc
ZtcHubTest_SOURCES = ZtcHubTest.cc ../src/ztchub_daemon.cc
ZtcHubTest_LDADD = $(LDADD) $(top_builddir)/zum/src/libZum.la
ZtcAgentTest_LDADD = $(LDADD) \
	$(top_builddir)/zws/src/libZws.la \
	$(top_builddir)/zhttp/src/libZhttp.la \
	$(top_builddir)/ztls/src/libZtls.la
