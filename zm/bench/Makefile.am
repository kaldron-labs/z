METASOURCES = AUTO
.cc.s:
	$(CXXCOMPILE) -c -S $<
%.i.cc: %.cc
	$(CXXCOMPILE) -C -E $< -o $@
AM_CPPFLAGS = -I$(top_srcdir)/zu/src -I$(top_srcdir)/zm/src \
	@Z_CPPFLAGS@
AM_CXXFLAGS = @Z_CXXFLAGS@
AM_LDFLAGS = @Z_LDFLAGS@
LDADD = $(top_builddir)/zm/src/libZm.la $(top_builddir)/zu/src/libZu.la \
	@Z_MT_LIBS@ @Z_LIBS@
noinst_PROGRAMS = \
	zmbench zmringbench zmfnbench \
	zmcachebench zmhashbench zmheapbench zmheapbench2 zmlockbench zmlhbench \
	zmrwbench zmrwlockbench zmhashthreadbench zmlockbench2
zmbench_SOURCES = zmbench.cc
zmringbench_SOURCES = zmringbench.cc
zmfnbench_SOURCES = zmfnbench.cc
zmcachebench_SOURCES = zmcachebench.cc
zmhashbench_SOURCES = zmhashbench.cc
zmheapbench_SOURCES = zmheapbench.cc
zmheapbench2_SOURCES = zmheapbench2.cc
zmlockbench_SOURCES = zmlockbench.cc
zmlhbench_SOURCES = zmlhbench.cc
zmrwbench_SOURCES = zmrwbench.cc
zmrwlockbench_SOURCES = zmrwlockbench.cc
zmhashthreadbench_SOURCES = zmhashthreadbench.cc
zmlockbench2_SOURCES = zmlockbench2.cc
