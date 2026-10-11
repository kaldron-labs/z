METASOURCES = AUTO
AM_CPPFLAGS = -I$(top_srcdir)/zu/src -I$(top_srcdir)/zm/src \
	-I$(top_srcdir)/zt/src -I$(top_srcdir)/ze/src -I$(top_srcdir)/zf/src \
	-I$(top_srcdir)/zi/src @Z_CPPFLAGS@ @FB_INTEROP_CFLAGS@
AM_CXXFLAGS = @Z_CXXFLAGS@
AM_LDFLAGS = @Z_LDFLAGS@
LDADD = $(top_builddir)/zi/src/libZi.la $(top_builddir)/zf/src/libZf.la \
	$(top_builddir)/ze/src/libZe.la $(top_builddir)/zt/src/libZt.la \
	$(top_builddir)/zm/src/libZm.la $(top_builddir)/zu/src/libZu.la \
	@Z_IO_LIBS@ @Z_ZT_LIBS@ @Z_MT_LIBS@ @Z_LIBS@ @FB_INTEROP_LIBS@
noinst_PROGRAMS = zifbiobench zifbinterop
zifbiobench_SOURCES = zifbiobench.cc iobaseline.hh
.PHONY: fb-deps
fb-deps:
	pkg-config --exists 'flatbuffers >= 24'
$(am_zifbiobench_OBJECTS): | fb-deps
$(am_zifbiobench_OBJECTS): bench_generated.h
bench_generated.h: $(srcdir)/bench.fbs
	flatc --cpp --cpp-std c++17 -o . $<
CLEANFILES = bench_generated.h
EXTRA_DIST = README.md bench.fbs trace-growth.py

zifbinterop_SOURCES = zifbinterop.cc
$(am_zifbinterop_OBJECTS): | fb-deps

EXTRA_DIST += contracts.md codec-history.md \
	evidence/m2-core-deps.txt \
	evidence/m1-tests.txt evidence/m1-emitter.txt \
	evidence/pooled-64.csv evidence/pooled-1024.csv evidence/pooled-65536.csv
EXTRA_DIST += evidence/operations-64.csv evidence/operations-1024.csv \
	evidence/operations-65536.csv

EXTRA_DIST += evidence/operations-scalar-64.csv evidence/operations-scalar-1024.csv \
	evidence/operations-scalar-65536.csv evidence/growth-64.txt \
	evidence/growth-1024.txt evidence/growth-65536.txt

EXTRA_DIST += evidence/sameflags-build.txt evidence/operations-sameflags-64.csv \
	evidence/operations-sameflags-1024.csv evidence/operations-sameflags-65536.csv \
	evidence/wide-save-64.csv evidence/wide-save-1024.csv evidence/wide-save-65536.csv \
	evidence/wide-operations-64.csv evidence/wide-operations-1024.csv \
	evidence/wide-operations-65536.csv evidence/wide-save-65536-before.csv \
	evidence/growth-wide-65536.txt

EXTRA_DIST += evidence/alloc-64.txt evidence/alloc-1024.txt \
	evidence/alloc-65536.txt evidence/alloc-wide-65536.txt

EXTRA_DIST += evidence/accessor-build.json evidence/core-size.txt \
	evidence/accessor-relink.txt evidence/accessor-ops-64.csv \
	evidence/accessor-ops-1024.csv evidence/accessor-ops-65536.csv \
	evidence/accessor-wideops-64.csv evidence/accessor-wideops-1024.csv \
	evidence/accessor-wideops-65536.csv

.PHONY: test
test: $(noinst_PROGRAMS)
	prove zifbinterop

EXTRA_DIST += evidence/m2-save-64.csv \
	evidence/m2-save-1024.csv \
	evidence/m2-save-65536.csv \
	evidence/m2-wide-64.csv \
	evidence/m2-wide-1024.csv \
	evidence/m2-wide-65536.csv \
	evidence/m2-ops-64.csv \
	evidence/m2-ops-1024.csv \
	evidence/m2-ops-65536.csv \
	evidence/m2-wideops-64.csv \
	evidence/m2-wideops-1024.csv \
	evidence/m2-wideops-65536.csv

EXTRA_DIST += evidence/m2-sameflags-build.txt \
	evidence/m2-sameflags-ops-64.csv evidence/m2-sameflags-ops-1024.csv \
	evidence/m2-sameflags-ops-65536.csv evidence/m2-sameflags-wideops-64.csv \
	evidence/m2-sameflags-wideops-1024.csv evidence/m2-sameflags-wideops-65536.csv \
	evidence/m2-alloc-64.txt evidence/m2-alloc-1024.txt \
	evidence/m2-alloc-65536.txt evidence/m2-alloc-wide-65536.txt \
	evidence/m2-dependencies.txt evidence/m2-install-deps.txt \
	evidence/m2-dashboard-memory.txt

EXTRA_DIST += evidence/m2-direct-load-64.csv evidence/m2-direct-load-relink.txt \
	evidence/m2-inline-load-64.csv evidence/m2-inline-load-relink.txt \
	evidence/m2-inline-ops-1024.csv evidence/m2-inline-ops-65536.csv \
	evidence/m2-inline-wideops-64.csv evidence/m2-inline-wideops-1024.csv \
	evidence/m2-inline-wideops-65536.csv

EXTRA_DIST += evidence/m2-shared-filter-build.txt \
	evidence/m2-shared-ops-64.csv evidence/m2-shared-ops-1024.csv \
	evidence/m2-shared-ops-65536.csv evidence/m2-shared-wideops-64.csv \
	evidence/m2-shared-wideops-1024.csv evidence/m2-shared-wideops-65536.csv

# Release measurements and acceptance evidence; historical runs remain identified.
EXTRA_DIST += \
	evidence/m2-release-dependencies.txt \
	evidence/m2-release-executables.txt \
	evidence/m2-release-headers.txt \
	evidence/m2-release-hotpath/affinity-save-1.csv \
	evidence/m2-release-hotpath/affinity-save-2.csv \
	evidence/m2-release-hotpath/affinity-wide-1.csv \
	evidence/m2-release-hotpath/affinity-wide-2.csv \
	evidence/m2-release-hotpath/arena-growth-save.csv \
	evidence/m2-release-hotpath/arena-growth-wide.csv \
	evidence/m2-release-hotpath/arena-growth-widewrite.csv \
	evidence/m2-release-hotpath/arena-growth-write.csv \
	evidence/m2-release-hotpath/current-save-long.csv \
	evidence/m2-release-hotpath/current-save-short.csv \
	evidence/m2-release-hotpath/current-wide.csv \
	evidence/m2-release-hotpath/fresh-profile.txt \
	evidence/m2-release-hotpath/initial-vector-alloc.txt \
	evidence/m2-release-hotpath/offset-vector-interop.txt \
	evidence/m2-release-hotpath/offset-vector-ops.csv \
	evidence/m2-release-hotpath/offset-vector-preflight.txt \
	evidence/m2-release-hotpath/offset-vector-save.csv \
	evidence/m2-release-hotpath/offset-vector-tests.txt \
	evidence/m2-release-hotpath/offset-vector-wide.csv \
	evidence/m2-release-hotpath/offset-vector-wideops.csv \
	evidence/m2-release-hotpath/ops-64.csv \
	evidence/m2-release-hotpath/reflect-dispatch-interop.txt \
	evidence/m2-release-hotpath/reflect-dispatch-ops.csv \
	evidence/m2-release-hotpath/reflect-dispatch-tests.txt \
	evidence/m2-release-hotpath/reflect-dispatch-wideops.csv \
	evidence/m2-release-hotpath/reflect-scalar-interop-tests.txt \
	evidence/m2-release-hotpath/reflect-scalar-ops.csv \
	evidence/m2-release-hotpath/reflect-scalar-regression-tests.txt \
	evidence/m2-release-hotpath/reflect-scalar-tests.txt \
	evidence/m2-release-hotpath/reflect-scalar-wideops.csv \
	evidence/m2-release-hotpath/save-64.csv \
	evidence/m2-release-hotpath/tests.txt \
	evidence/m2-release-hotpath/vector-padding-interop.txt \
	evidence/m2-release-hotpath/vector-padding-save.csv \
	evidence/m2-release-hotpath/vector-padding-snapshot.txt \
	evidence/m2-release-hotpath/vector-padding-tests.txt \
	evidence/m2-release-hotpath/vector-padding-vectors.csv \
	evidence/m2-release-hotpath/vector-padding-wide.csv \
	evidence/m2-release-hotpath/vector-product-interop.txt \
	evidence/m2-release-hotpath/vector-product-ops.csv \
	evidence/m2-release-hotpath/vector-product-tests.txt \
	evidence/m2-release-hotpath/vector-product-wideops.csv \
	evidence/m2-release-hotpath/vectors-65536.csv \
	evidence/m2-release-hotpath/verify-once-ops.csv \
	evidence/m2-release-hotpath/verify-once-tests.txt \
	evidence/m2-release-hotpath/verify-once-wideops.csv \
	evidence/m2-release-hotpath/verify-reduce-ops.csv \
	evidence/m2-release-hotpath/verify-reduce-tests.txt \
	evidence/m2-release-hotpath/verify-reduce-wideops.csv \
	evidence/m2-release-hotpath/wide-64.csv \
	evidence/m2-release-hotpath/wide-65536.csv \
	evidence/m2-release-hotpath/wideops-64.csv \
	evidence/m2-release-initial/commands.txt \
	evidence/m2-release-initial/link.txt \
	evidence/m2-release-initial/ops-0.csv \
	evidence/m2-release-initial/ops-1024.csv \
	evidence/m2-release-initial/ops-64.csv \
	evidence/m2-release-initial/ops-65536.csv \
	evidence/m2-release-initial/reflection-build.txt \
	evidence/m2-release-initial/replication.csv \
	evidence/m2-release-initial/save-0.csv \
	evidence/m2-release-initial/save-1024.csv \
	evidence/m2-release-initial/save-64.csv \
	evidence/m2-release-initial/save-65536.csv \
	evidence/m2-release-initial/summary.csv \
	evidence/m2-release-initial/telemetry.csv \
	evidence/m2-release-initial/vectors-0.csv \
	evidence/m2-release-initial/vectors-1024.csv \
	evidence/m2-release-initial/vectors-64.csv \
	evidence/m2-release-initial/vectors-65536.csv \
	evidence/m2-release-initial/wide-0.csv \
	evidence/m2-release-initial/wide-1024.csv \
	evidence/m2-release-initial/wide-64.csv \
	evidence/m2-release-initial/wide-65536.csv \
	evidence/m2-release-initial/wideops-0.csv \
	evidence/m2-release-initial/wideops-1024.csv \
	evidence/m2-release-initial/wideops-64.csv \
	evidence/m2-release-initial/wideops-65536.csv \
	evidence/m2-release-initial/widewrite-0.csv \
	evidence/m2-release-initial/widewrite-1024.csv \
	evidence/m2-release-initial/widewrite-64.csv \
	evidence/m2-release-initial/widewrite-65536.csv \
	evidence/m2-release-initial/write-0.csv \
	evidence/m2-release-initial/write-1024.csv \
	evidence/m2-release-initial/write-64.csv \
	evidence/m2-release-initial/write-65536.csv

EXTRA_DIST += evidence/m2-release-emitter.txt

EXTRA_DIST += evidence/m2-release-hotpath/reflect-align-rejected.txt \
	evidence/m2-release-hotpath/reflect-align-ops.csv \
	evidence/m2-release-hotpath/reflect-align-wideops.csv \
	evidence/m2-release-hotpath/reflect-align-tests.txt \
	evidence/m2-release-hotpath/reflect-align-interop.txt

EXTRA_DIST += \
	evidence/m2-release-hotpath/shape-before-ops.csv \
	evidence/m2-release-hotpath/shape-before-wideops.csv \
	evidence/m2-release-hotpath/shape-after-ops.csv \
	evidence/m2-release-hotpath/shape-after-wideops.csv \
	evidence/m2-release-hotpath/shape-inline-tests.txt \
	evidence/m2-release-hotpath/shape-inline-interop.txt \
	evidence/m2-release-hotpath/shape-inline-review.txt

EXTRA_DIST += evidence/m2-release-hotpath/schema-cross-review.txt \
	evidence/m2-release-hotpath/schema-cross-ops.csv \
	evidence/m2-release-hotpath/schema-cross-wideops.csv

EXTRA_DIST += evidence/m2-release-hotpath/empty-attrs-ops.csv \
	evidence/m2-release-hotpath/empty-attrs-wideops.csv \
	evidence/m2-release-hotpath/empty-attrs-tests.txt \
	evidence/m2-release-hotpath/empty-attrs-interop.txt \
	evidence/m2-release-hotpath/empty-attrs-review.txt

EXTRA_DIST += evidence/m2-release-hotpath/allocation-row.txt \
	evidence/m2-release-hotpath/allocation-wide.txt \
	evidence/m2-release-hotpath/allocation-vectors.txt \
	evidence/m2-release-hotpath/allocation-review.txt

EXTRA_DIST += evidence/m2-release-hotpath/copy-row.txt \
	evidence/m2-release-hotpath/copy-wide.txt \
	evidence/m2-release-hotpath/copy-vectors.txt \
	evidence/m2-release-hotpath/copy-review.txt

EXTRA_DIST += evidence/m2-release-hotpath/pool-row-off.csv \
	evidence/m2-release-hotpath/pool-row-on.csv \
	evidence/m2-release-hotpath/pool-wide-off.csv \
	evidence/m2-release-hotpath/pool-wide-on.csv \
	evidence/m2-release-hotpath/pool-configured-trace.txt \
	evidence/m2-release-hotpath/pool-fallback-trace.txt \
	evidence/m2-release-hotpath/pool-build.txt \
	evidence/m2-release-hotpath/pool-review.txt

EXTRA_DIST += evidence/m2-release-refresh/build.txt \
	evidence/m2-release-refresh/commands.txt \
	evidence/m2-release-refresh/measure.py \
	evidence/m2-release-refresh/ops-0.csv \
	evidence/m2-release-refresh/ops-1024.csv \
	evidence/m2-release-refresh/ops-64.csv \
	evidence/m2-release-refresh/ops-65536.csv \
	evidence/m2-release-refresh/replication.csv \
	evidence/m2-release-refresh/save-0.csv \
	evidence/m2-release-refresh/save-1024.csv \
	evidence/m2-release-refresh/save-64.csv \
	evidence/m2-release-refresh/save-65536.csv \
	evidence/m2-release-refresh/snapshot.json \
	evidence/m2-release-refresh/summary.csv \
	evidence/m2-release-refresh/telemetry.csv \
	evidence/m2-release-refresh/vectors-0.csv \
	evidence/m2-release-refresh/vectors-1024.csv \
	evidence/m2-release-refresh/vectors-64.csv \
	evidence/m2-release-refresh/vectors-65536.csv \
	evidence/m2-release-refresh/wide-0.csv \
	evidence/m2-release-refresh/wide-1024.csv \
	evidence/m2-release-refresh/wide-64.csv \
	evidence/m2-release-refresh/wide-65536.csv \
	evidence/m2-release-refresh/wideops-0.csv \
	evidence/m2-release-refresh/wideops-1024.csv \
	evidence/m2-release-refresh/wideops-64.csv \
	evidence/m2-release-refresh/wideops-65536.csv \
	evidence/m2-release-refresh/widewrite-0.csv \
	evidence/m2-release-refresh/widewrite-1024.csv \
	evidence/m2-release-refresh/widewrite-64.csv \
	evidence/m2-release-refresh/widewrite-65536.csv \
	evidence/m2-release-refresh/write-0.csv \
	evidence/m2-release-refresh/write-1024.csv \
	evidence/m2-release-refresh/write-64.csv \
	evidence/m2-release-refresh/write-65536.csv

EXTRA_DIST += \
	evidence/m2-e2e-latency/agents128-262144.log \
	evidence/m2-e2e-latency/baseline.log \
	evidence/m2-e2e-latency/clock.c \
	evidence/m2-e2e-latency/clock.csv \
	evidence/m2-e2e-latency/clocksource.log \
	evidence/m2-e2e-latency/final-isolated.log \
	evidence/m2-e2e-latency/final-load.log \
	evidence/m2-e2e-latency/final-snapshot.json \
	evidence/m2-e2e-latency/load-131072.log \
	evidence/m2-e2e-latency/load-262144.log \
	evidence/m2-e2e-latency/load-32768.log \
	evidence/m2-e2e-latency/load-65536.log \
	evidence/m2-e2e-latency/peer-callers.txt \
	evidence/m2-e2e-latency/peer-functions.txt \
	evidence/m2-e2e-latency/profile.log \
	evidence/m2-e2e-latency/profile.py \
	evidence/m2-e2e-latency/rate-1000.log \
	evidence/m2-e2e-latency/rebuild.log \
	evidence/m2-e2e-latency/snapshot.json \
	evidence/m2-e2e-latency/turn1-32768.log

EXTRA_DIST += \
	evidence/m2-loopback/build.log \
	evidence/m2-loopback/compare.log \
	evidence/m2-loopback/compare.py \
	evidence/m2-loopback/connections.log \
	evidence/m2-loopback/connections.py \
	evidence/m2-loopback/direct-131072-1.log \
	evidence/m2-loopback/direct-262144-1.log \
	evidence/m2-loopback/direct-262144-2.log \
	evidence/m2-loopback/direct-32768-1.log \
	evidence/m2-loopback/direct2-131072.log \
	evidence/m2-loopback/direct2-262144.log \
	evidence/m2-loopback/direct32-131072-repeat.log \
	evidence/m2-loopback/hub-131072-1.log \
	evidence/m2-loopback/hub-262144-1.log \
	evidence/m2-loopback/hub-262144-2.log \
	evidence/m2-loopback/hub-32768-1.log \
	evidence/m2-loopback/hub32-131072.log \
	evidence/m2-loopback/preflight.log \
	evidence/m2-loopback/snapshot.json

EXTRA_DIST += evidence/m2-loopback/summary.csv

# Completed frozen batch; later envelope repair is separately accepted.
EXTRA_DIST += \
	evidence/m2-release-frozen/README.md \
	evidence/m2-release-frozen/check-dependencies.log \
	evidence/m2-release-frozen/check-dependent-size.log \
	evidence/m2-release-frozen/check.py \
	evidence/m2-release-frozen/deps.py \
	evidence/m2-release-frozen/functional-summary.txt \
	evidence/m2-release-frozen/manifest.json \
	evidence/m2-release-frozen/memory-summary.txt \
	evidence/m2-release-frozen/memory.py \
	evidence/m2-release-frozen/perf-ops-0.csv \
	evidence/m2-release-frozen/perf-ops-1024.csv \
	evidence/m2-release-frozen/perf-ops-64.csv \
	evidence/m2-release-frozen/perf-ops-65536.csv \
	evidence/m2-release-frozen/perf-pool-save-64.csv \
	evidence/m2-release-frozen/perf-pool-wide-64.csv \
	evidence/m2-release-frozen/perf-save-0.csv \
	evidence/m2-release-frozen/perf-save-1024.csv \
	evidence/m2-release-frozen/perf-save-64.csv \
	evidence/m2-release-frozen/perf-save-65536.csv \
	evidence/m2-release-frozen/perf-summary.md \
	evidence/m2-release-frozen/perf-vectors-0.csv \
	evidence/m2-release-frozen/perf-vectors-1024.csv \
	evidence/m2-release-frozen/perf-vectors-64.csv \
	evidence/m2-release-frozen/perf-vectors-65536.csv \
	evidence/m2-release-frozen/perf-wide-0.csv \
	evidence/m2-release-frozen/perf-wide-1024.csv \
	evidence/m2-release-frozen/perf-wide-64.csv \
	evidence/m2-release-frozen/perf-wide-65536.csv \
	evidence/m2-release-frozen/perf-wideops-0.csv \
	evidence/m2-release-frozen/perf-wideops-1024.csv \
	evidence/m2-release-frozen/perf-wideops-64.csv \
	evidence/m2-release-frozen/perf-wideops-65536.csv \
	evidence/m2-release-frozen/perf-widewrite-0.csv \
	evidence/m2-release-frozen/perf-widewrite-1024.csv \
	evidence/m2-release-frozen/perf-widewrite-64.csv \
	evidence/m2-release-frozen/perf-widewrite-65536.csv \
	evidence/m2-release-frozen/perf-write-0.csv \
	evidence/m2-release-frozen/perf-write-1024.csv \
	evidence/m2-release-frozen/perf-write-64.csv \
	evidence/m2-release-frozen/perf-write-65536.csv \
	evidence/m2-release-frozen/perf-zdb-messages.csv \
	evidence/m2-release-frozen/perf-ztc-messages.csv \
	evidence/m2-release-frozen/perf.py \
	evidence/m2-release-frozen/refresh.py \
	evidence/m2-release-frozen/size.py \
	evidence/m2-release-frozen/sources.json \
	evidence/m2-release-frozen/summarize.py \
	evidence/m2-release-frozen/zffb-parity-repeat-ops-0-0.csv \
	evidence/m2-release-frozen/zffb-parity-repeat-ops-0-1.csv \
	evidence/m2-release-frozen/zffb-parity-repeat-ops-0-2.csv \
	evidence/m2-release-frozen/zffb-parity-repeat-ops-64-0.csv \
	evidence/m2-release-frozen/zffb-parity-repeat-ops-64-1.csv \
	evidence/m2-release-frozen/zffb-parity-repeat-ops-64-2.csv \
	evidence/m2-release-frozen/zffb-parity-repeat-telemetry-0.csv \
	evidence/m2-release-frozen/zffb-parity-repeat-telemetry-1.csv \
	evidence/m2-release-frozen/zffb-parity-repeat-telemetry-2.csv

EXTRA_DIST += \
	evidence/m2-release-envelope/README.md \
	evidence/m2-release-envelope/check-continue-runner.log \
	evidence/m2-release-envelope/check-continue.py \
	evidence/m2-release-envelope/check-dependencies.log \
	evidence/m2-release-envelope/check-dependent-size.log \
	evidence/m2-release-envelope/check-runner.log \
	evidence/m2-release-envelope/check.py \
	evidence/m2-release-envelope/framing-prototype/baseline-0.csv \
	evidence/m2-release-envelope/framing-prototype/baseline-1.csv \
	evidence/m2-release-envelope/framing-prototype/baseline-2.csv \
	evidence/m2-release-envelope/framing-prototype/candidate-0.csv \
	evidence/m2-release-envelope/framing-prototype/candidate-1.csv \
	evidence/m2-release-envelope/framing-prototype/candidate-2.csv \
	evidence/m2-release-envelope/framing-prototype/commands.json \
	evidence/m2-release-envelope/framing-prototype/tests.log \
	evidence/m2-release-envelope/functional-summary.txt \
	evidence/m2-release-envelope/legacy-construction.txt \
	evidence/m2-release-envelope/manifest.json \
	evidence/m2-release-envelope/memory-summary.txt \
	evidence/m2-release-envelope/memory.py \
	evidence/m2-release-envelope/perf-ops-0.csv \
	evidence/m2-release-envelope/perf-ops-1024.csv \
	evidence/m2-release-envelope/perf-ops-64.csv \
	evidence/m2-release-envelope/perf-ops-65536.csv \
	evidence/m2-release-envelope/perf-pool-save-64.csv \
	evidence/m2-release-envelope/perf-pool-wide-64.csv \
	evidence/m2-release-envelope/perf-save-0.csv \
	evidence/m2-release-envelope/perf-save-1024.csv \
	evidence/m2-release-envelope/perf-save-64.csv \
	evidence/m2-release-envelope/perf-save-65536.csv \
	evidence/m2-release-envelope/perf-summary.md \
	evidence/m2-release-envelope/perf-vectors-0.csv \
	evidence/m2-release-envelope/perf-vectors-1024.csv \
	evidence/m2-release-envelope/perf-vectors-64.csv \
	evidence/m2-release-envelope/perf-vectors-65536.csv \
	evidence/m2-release-envelope/perf-wide-0.csv \
	evidence/m2-release-envelope/perf-wide-1024.csv \
	evidence/m2-release-envelope/perf-wide-64.csv \
	evidence/m2-release-envelope/perf-wide-65536.csv \
	evidence/m2-release-envelope/perf-wideops-0.csv \
	evidence/m2-release-envelope/perf-wideops-1024.csv \
	evidence/m2-release-envelope/perf-wideops-64.csv \
	evidence/m2-release-envelope/perf-wideops-65536.csv \
	evidence/m2-release-envelope/perf-widewrite-0.csv \
	evidence/m2-release-envelope/perf-widewrite-1024.csv \
	evidence/m2-release-envelope/perf-widewrite-64.csv \
	evidence/m2-release-envelope/perf-widewrite-65536.csv \
	evidence/m2-release-envelope/perf-write-0.csv \
	evidence/m2-release-envelope/perf-write-1024.csv \
	evidence/m2-release-envelope/perf-write-64.csv \
	evidence/m2-release-envelope/perf-write-65536.csv \
	evidence/m2-release-envelope/perf-zdb-messages.csv \
	evidence/m2-release-envelope/perf-ztc-messages.csv \
	evidence/m2-release-envelope/perf.py \
	evidence/m2-release-envelope/refresh.py \
	evidence/m2-release-envelope/rejected-offset-view/baseline-0.csv \
	evidence/m2-release-envelope/rejected-offset-view/baseline-1.csv \
	evidence/m2-release-envelope/rejected-offset-view/baseline-2.csv \
	evidence/m2-release-envelope/rejected-offset-view/candidate-0.csv \
	evidence/m2-release-envelope/rejected-offset-view/candidate-1.csv \
	evidence/m2-release-envelope/rejected-offset-view/candidate-2.csv \
	evidence/m2-release-envelope/rejected-offset-view/commands.json \
	evidence/m2-release-envelope/sources.json \
	evidence/m2-release-envelope/summarize.py \
	evidence/m2-release-envelope/zum-final-build.log \
	evidence/m2-release-envelope/zum-refresh-dry.log

# Preserve the constructor-metadata rerun with the other acceptance evidence.
EXTRA_DIST += \
	evidence/ctor-metadata-release/README.md \
	evidence/ctor-metadata-release/build.log \
	evidence/ctor-metadata-release/commands.txt \
	evidence/ctor-metadata-release/link-command.txt \
	evidence/ctor-metadata-release/link.log \
	evidence/ctor-metadata-release/ops-1.csv \
	evidence/ctor-metadata-release/ops-2.csv \
	evidence/ctor-metadata-release/ops-3.csv \
	evidence/ctor-metadata-release/save.csv \
	evidence/ctor-metadata-release/source-hashes.json \
	evidence/ctor-metadata-release/wide.csv \
	evidence/ctor-metadata-release/wideops-1.csv \
	evidence/ctor-metadata-release/wideops-2.csv \
	evidence/ctor-metadata-release/wideops-3.csv \
	evidence/ctor-metadata-release/zifbiobench.cc
