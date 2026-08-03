METASOURCES = AUTO
AM_CPPFLAGS = -I$(top_srcdir)/zu/src @Z_CPPFLAGS@ \
	-DZDASH_SRCDIR='"$(abs_top_srcdir)/zdash/src"'
AM_CXXFLAGS = @Z_CXXFLAGS@
AM_LDFLAGS = @Z_LDFLAGS@
LDADD = $(top_builddir)/zu/src/libZu.la @Z_MT_LIBS@ @Z_LIBS@
noinst_PROGRAMS = ZdashSchemaTest
ZdashSchemaTest_SOURCES = ZdashSchemaTest.cc

.PHONY: test
test: $(noinst_PROGRAMS)
	prove $(noinst_PROGRAMS)
