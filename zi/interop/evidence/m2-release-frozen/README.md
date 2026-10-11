# Frozen release batch — 2026-10-09

This records the completed batch before the subsequent local-envelope
scratch repair. It is not complete performance acceptance.

- Clang 23.1.1, x86-64-v2, O3, g, NDEBUG; configured prefix /usr.
- Sequential incremental builds: root, dashboard, zf/zi/zdb/ztc interop,
  all at -j4; no diagnostics.
- 85 TAP files / 800 tests passed, plus three telemetry Python lifecycle
  fixtures and the self-checking saga example. See functional-summary.txt.
- All 15 final Valgrind binaries: zero errors and definite, indirect or
  possible losses; no suppressions. See memory-summary.txt.
- Production isolation audit: 25 modules, 27 DSOs, 583 declared-object
  depfiles; zero canonical/Zfb findings. See check-dependencies.log.
- Ten dependent DSOs shrink 131277 text bytes in aggregate; no increases.
  This is not a whole-tree baseline. See check-dependent-size.log.
- Canonical FlatBuffers 25.12.19 headers and matching reflection object.
  CPU 6, default pool except explicitly named pool-4 cases; no overlapping
  build, tests, debugger or memory activity during timing.
- 32 cases with seven alternating rounds. CSV times are sample means;
  perf-summary.md gives their medians, not per-message tail percentiles.
- Small nested construction and narrow-load differences remain visible.
  Three-process repeats are included. No regression is silently waived.

sources.json identifies the product inputs for this batch. The scripts
retain actual host-specific fixture paths and commands as execution records;
they are not portable test entry points. Existing module test and interop
Makefiles remain the test entry points. Original full logs use
/tmp/zffb-final-parity-*; this directory retains compact results and raw
performance samples. Later source changes require their own acceptance.
