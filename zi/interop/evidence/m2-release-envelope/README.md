# Local telemetry envelope repair — 2026-10-09

The only product change after m2-release-frozen is direct local
ZtBuiltin scratch for the Msg and Payload envelopes in ZtcMsg.hh.
Constructor/default semantics, general codec scratch selection, buffer
pooling, growth, ownership, alignment and heap IDs are unchanged.

Sequential incremental root/dashboard/interop rebuilds use -j4 and the
existing Clang 23 release flags. The functional and memory runners passed
before the release matrix started. Raw seven-round alternating CPU-6
measurements and medians of sample means are retained here. They are not
per-message tail percentiles. See plan.md for numerical interpretation,
remaining differences and acceptance; runner exit zero only establishes
successful measurement/preflights.

The scripts retain actual host-specific fixture paths as execution records.
Existing owning-module test/interop targets remain the test entry points.

Final assessment: material performance regressions are resolved. Nested
reuse is 156.239 ns versus canonical 155.027 ns; fresh is 204.270 versus
206.263 ns. Narrow saves are 46.992/85.477 ns reuse/fresh versus
56.399/100.772 ns. The remaining approximately 0.05–0.06 ns narrow-load
construction difference and BFBS-layout-dependent verifier differences
are retained in perf-summary.md and explained in plan.md. No constructor
or metadata contract is weakened to obtain these results.

The initial current-binary check stopped because the root traversal builds
Zum before telemetry. check-runner.log preserves that stop. A complete
post-Ztc `make -C zum -j4` refresh passed; check-continue.py and its runner
log preserve the successful continuation. This is a dependent-build order
issue, not a failed functional test. The final functional tally remains
85 files / 800 tests. All 15 memory reports are clean. Text across the ten
captured DSOs is 130193 bytes smaller than the pre-refresh baseline;
libZtcApp grows 1084 bytes relative to the preceding frozen batch.

Same-configuration `z.config -c -L /usr` regeneration passes without
diagnostics; flags and config.h are unchanged. Final root plus separate
dashboard distribution staging passes: 1882 files, 405 byte-identical
evidence files, exactly ZfFB.hh and ZfFBReflect.hh, and no obsolete zfb,
Zfb*, codegen or production schema artifacts. This closes packaging.
