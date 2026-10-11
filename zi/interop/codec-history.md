Historical codec execution records, moved from `zf/interop`. Paths and
commands below identify the recorded runs; current targets are documented in
[README.md](README.md).

# ZfFB interoperability and execution evidence

## Unit-test consolidation — 2026-10-09

The nine incremental `ZfFB...Test` programs are now `ZfFBTest` (payload codec)
and `ZfFBReflectTest` (reflection/BFBS/FBS), with shared fixture metadata in
`zf/test/ZfFBTest.hh`. All cases and seven `ZFFB_BAD_*` selectors are retained;
the selectors now apply to `ZfFBTest.cc`. See [unit-test layout](../test/README.md).
Earlier entries and archived evidence retain their original executable names.
This consolidation has not been built or run.

The current status is summarized first, followed by historical execution
records. Historical commands are evidence, not instructions to repeat them. Current build commands must use `z.config`
as prescribed in [plan.md](../../plan.md).
The pooled benchmark now lives in [zi/interop](README.md)
as `zifbiobench`; earlier `zffbiobench` commands below identify historical runs.

## Current release snapshot — 2026-10-09

The final repaired implementation passes 85 TAP files / 800 tests, the
telemetry lifecycle fixtures and saga example, all 15 final Valgrind runs,
and the production dependency audit. The final 32-case release matrix is in
[evidence/m2-release-envelope](evidence/m2-release-envelope); the preceding
batch is in [evidence/m2-release-frozen](evidence/m2-release-frozen).

Performance meets `goal.md`'s material-regression criterion: 64-byte saves
are about 15–17% faster and large-vector saves about 29–41% faster. Nested
telemetry reuse is within 0.8% of canonical and fresh construction is
slightly faster. The measured roughly 0.05–0.06 ns narrow-load difference
predates the constructor-metadata correction documented in the
[constructor audit](README.md#constructor-metadata-audit--2026-10-09).
The extra initialization was a fixture omission, not an unavoidable adapter
contract. A [subsequent release rerun](evidence/ctor-metadata-release/README.md)
shows native narrow loads at 1.974–2.015 ns versus canonical 2.005–2.042 ns,
and wide loads 14–15% faster across three fresh processes. Only the benchmark
was rebuilt and rerun; the other corrected fixtures have not been tested. Small own-schema
verification differences depend on BFBS layout. All differences, code-size tradeoffs and
raw sample-mean distributions remain explicit in [plan.md](../../plan.md).
Historical network overload failures remain failures; these codec results
do not establish a hard 200-microsecond network latency maximum.
Final evidence distribution staging passes: 1,882 files, including 405
byte-identical evidence files, exactly the two codec headers and no obsolete
`zfb` artifacts or production schemas. Dashboard is staged separately.

## Historical release snapshot — 2026-10-08

Performance acceptance remains open. The current build is Clang 23 release
(`-march=x86-64-v2 -O3 -g -DNDEBUG`); the canonical reflection comparison uses
matching release flags. The initial full measurement batch is retained in
[evidence/m2-release-initial](evidence/m2-release-initial), and subsequent
focused fixes and measurements in
[evidence/m2-release-hotpath](evidence/m2-release-hotpath).
The complete post-rebuild matrix is in
[evidence/m2-release-refresh](evidence/m2-release-refresh): 30 cases, 110
comparisons, 11 alternating samples on CPU 6 with the default pool
configuration. `summary.csv` reports min/median/max per-operation sample means;
the raw CSVs, commands, runner, release link log and source/binary hashes are
retained alongside it. All benchmark correctness prechecks passed.

Remaining median regressions include narrow reflected verification (about
20%), small fresh saves (6–10%), empty-vector fresh saves (15%), narrow
empty JSON printing (13%), and nested telemetry construction (14% reuse,
21% fresh). Replication is 9–10% faster and telemetry copies 8–16% faster;
large vectors and reflected writes also improve. These results do not
close performance acceptance or the separate end-to-end telemetry latency gate.

The vector-padding batch passes Zf 21 binaries / 415 TAP tests,
ZfFB 1/4, and canonical interoperability 1/41. Source/binary hashes and
measurement commands for that batch are in
[the snapshot record](evidence/m2-release-hotpath/vector-padding-snapshot.txt).
Subsequent consumer refreshes and their individual limits are recorded in
[plan.md](../../plan.md). These include a passing live dashboard check after
the bounded pre-authentication receive-queue fix. A later scheduler teardown
fix passes its focused lifetime regressions and Valgrind check. Its dependent
rebuild, dashboard build and explicit interop builds now pass without
diagnostics. Refreshed consumer checks include all three stores, Zum,
telemetry, Zdf, GTK and dashboard; canonical interop passes 49 tests.
Focused rebuilt scheduler/store/saga/transport memory checks pass with zero
errors or lost allocations. Individual coverage and limits remain in the plan.

Small fresh-buffer saves and reflected verification still have measured
regressions. Repeated measurements also vary with process state; pinning to
one CPU did not eliminate that variation. See [plan.md](../../plan.md) for
individual results, limits, and remaining gates. Sample-average timing distributions
are not per-message tail-latency distributions.

### BFBS generator review

[Current release disassembly](evidence/m2-release-emitter.txt) records the
recursive Root/Child emitter with source/binary hashes. Schema offsets and
counts are literals, and object emission uses direct specialized calls.
The visible branches belong to destination capacity/storage handling and the
stack canary, which Q2 permits. This representative check complements the
source review; it does not claim all streaming helpers are branch-free.

BFBS metadata choices use `consteval`, `if constexpr`, and `ZuUnroll`.
Both exporters retain the generic `S << ...` destination contract. Attribute
numbers use `ZuBox` with `ZtScratch`; FBS subnormal defaults use the existing
ZtString hexadecimal-float formatter. The scalar-byte pointer casts are real
object-representation conversions, not casts between equivalent span types.

## Earlier consolidation snapshot

The debug timings in the historical sections below are diagnostic history,
not performance acceptance. Earlier load/handler forced-inlining experiments
were reverted. Later release evidence above supersedes their pending-work
statements, without turning historical results into current acceptance.

The codec is now organized as two headers: `ZfFB.hh` for load/save, views,
verification and builder support; `ZfFBReflect.hh` for reflection and BFBS/FBS
schema output. Earlier helper-header names below identify historical source
layouts. The main header follows the other adapters' field-property,
input/output, field-handler and public-entry-point sections. Its consolidated
release library build passed without diagnostics. The post-consolidation
release Zf suite passes 21 binaries / 413 tests; ZfFB/ZiMxLoop pass 10 groups,
and the release Zu test-harness self-test passes 6 groups. ZfFBTest's local
`save` helper now uses qualified calls to avoid ADL selecting the public
stream-save overload newly visible through the combined header. At that snapshot, dependent and canonical interop rebuilds/checks were still
pending; later runs and their limits are recorded above and in plan.md.

The post-consolidation release dependency audit covers 25 modules' configured
production object dependency files and shared libraries, plus 8 configured
production executables: no canonical FlatBuffers or `zfb` dependency remains.
All Zf production dependency files are free of Zi headers, and libZf has no
Zi library dependency. The configured installation lists contain 369 headers;
the FlatBuffers adapter installs exactly `ZfFB.hh` and `ZfFBReflect.hh`.
Evidence: [object/library dependencies](evidence/m2-release-dependencies.txt),
[executables](evidence/m2-release-executables.txt), and
[header declarations](evidence/m2-release-headers.txt). This audits existing
artifacts and installation rules; no installation or extra build configuration
was performed.

## M1 execution status — 2026-10-07

M1 is underway; it is not signed off. No ASan switch or clean was used.
`autoreconf -i`, `config.status --recheck`, and `config.status` retained the
configured flags. Owning libraries were built incrementally with `make -j8`
in `zu`, `zm`, `zt`, `ze`, `zf`, `zi` order, followed by the isolated targets.
Compiler: Clang 23.1.1; canonical library and `flatc`: 25.12.19.

Current execution evidence:

- All 21 `zf/test` binaries pass (411 top-level TAP tests).
- `ZfFBTest`, `ZuArrayTest`, `ZuCmpTest`, `ZuSearchTest`, and `zffbinterop`
  pass (206 top-level TAP tests). Nested checks add further assertions.
  See [test summary](evidence/m1-tests.txt).
- The subsequent expanded `zffbinterop` run passes 40 top-level TAP tests.
  Independent canonical declarations now cover the six mapped inline numeric/
  time types, inline vectors and byte-vector wrappers in both BFBS/FBS and
  payload exchange. Nested-root extraction verifies against canonical schemas;
  copies preserve forced-present defaults after the original storage is
  overwritten. Evolution cases cover explicit IDs in reordered declarations,
  live data in a skipped slot, unknown extra fields, scalar mutation through
  both implementations, and update/delete filters with original field IDs.
- [Representative emitter disassembly](evidence/m1-emitter.txt) records the
  compiled `Values` BFBS root emitter and source hashes. It uses literal
  offsets/counts and direct specialized calls. Its only conditional jump
  checks the stack canary; no runtime metadata dispatch occurs in that
  function. This complements the template-source review and is not a claim
  that every transitive streaming/allocator function is branch-free.
- The seven `ZFFB_BAD_*` branches in `ZfFBMetaTest.cc` were each compiled
  with the configured `CXXCOMPILE`, `-D` selector and `-fsyntax-only`.
  MIXED fails `Explicit || Implicit`; GAP/UNION fail `valid()`;
  NAME/TAG_NAME/ALT_NAME fail `namesUnique()`; ALIGN fails the power-of-two
  assertion. None passed accidentally or failed because of missing includes.
- Latest core/I/O/test/interop build logs contain no compiler warnings.
- Compiler/runtime failures exposed two shared framework defects: constexpr
  `ZuString::cmp` shadowed its index, and `ZuSearch` missed matching interior
  pivots. Both are fixed with owning-module regression coverage. Borrowed
  metadata now retains facet properties when adapting field types.

The migration-only pooled benchmark compares the unchanged old custom
`Zfb::IOBuilder` against `ZfFB::IOBuilder`, including save, detach, borrowed
scalar read and reattach. Each codec uses the same two-field row, defaults,
pool and payload, with seven samples and alternating codec order. Fresh mode
allocates an owner each iteration; reuse mode retains its grown owner.
The initial run exposed per-byte serialization overhead, removed with
`memcpy` for contiguous native byte scalars. Wider/mapped values retain their
conversion path. The final samples are retained below; microseconds are
medians per iteration, measured in the retained debug configuration.

| Payload bytes | Mode | Canonical µs | Native µs | Encoded bytes |
| --- | --- | ---: | ---: | ---: |
| 64 | Fresh | 1.236 | 1.095 | 104 |
| 64 | Reuse | 0.968 | 0.820 | 104 |
| 1024 | Fresh | 1.605 | 1.439 | 1064 |
| 1024 | Reuse | 1.000 | 0.854 | 1064 |
| 65536 | Fresh | 3.571 | 2.380 | 65576 |
| 65536 | Reuse | 1.921 | 1.792 | 65576 |

Raw samples: [64 bytes](evidence/pooled-64.csv),
[1024 bytes](evidence/pooled-1024.csv),
[65536 bytes](evidence/pooled-65536.csv). Commands were
`zffbiobench 10000 64 7`, `zffbiobench 1000 1024 7`, and
`zffbiobench 1000 65536 7`. These are narrow workload measurements, not a
general performance claim. Allocation/copy counts, broader field/verification/
reflection/printing workloads, remaining interop
coverage and migrated consumer measurements are still pending. M2 and the
final Valgrind gate have not run; `zfb` remains until the consumer migration.

### Expanded operation baseline

`zffbiobench COUNT BYTES ROUNDS ops` measures borrowed scalar access, typed
verification, native construction, reflected scalar access, reflected payload
verification and JSON printing. The canonical side uses the current
`ZfbStruct` binding with a generated `bench.fbs` fixture. This fixture is
isolated to the explicitly requested migration benchmark; it is not a
production schema/codegen dependency. The native side uses the matching
metadata and `ZfFB`. Before timing, operations must agree and both borrowed
JSON outputs must match JSON generated from the native input object.
Schemas, field associations and output-capacity warmup are outside timing.

Seven samples alternate codec order. Commands use `(COUNT, BYTES)` pairs
`(20000, 64)`, `(1000, 1024)`, `(100, 65536)`. Raw samples:
[64 bytes](evidence/operations-64.csv),
[1024 bytes](evidence/operations-1024.csv),
[65536 bytes](evidence/operations-65536.csv).
For the 64-byte payload, median microseconds per operation are:

| Operation | Canonical | Native |
| --- | ---: | ---: |
| Borrowed scalar read | 0.0285 | 0.0456 |
| Typed verification | 0.2573 | 0.1645 |
| Native construction | 0.1151 | 0.1467 |
| Reflected scalar read | 0.0986 | 0.0751 |
| Reflected payload verification | 0.0586 | 0.9178 |
| Borrowed JSON printing | 0.9677 | 0.9982 |

These initial measurements **did not pass the full performance gate**. Borrowed read,
construction and printing need further work/measurement, and schema-driven
verification is substantially slower. The canonical reflected verifier is in
the installed external library; its build is separate from the retained Z
debug configuration. Source audit found current database ingress calls typed
verification (`ZdbMsg.hh`, `ZdbBuf.hh`, `Zdb.hh`, `ZdbSaga.hh`), not canonical
reflected verification. This distinguishes the existing consumer path from
the additional benchmark; it does not remove the measured result or establish
acceptance. A `ZuInline` accessor experiment failed to improve these timings
and was removed. Expanded-field workloads and allocation/copy counts remain
pending. The 40-case canonical interop harness still passes after this work.

### Scalar access refinement and growth trace

Packed little-endian scalar access removes address-taking temporary copies;
wire input entry points take span references, and unmapped values bypass the
identity adapter. Tests cover unaligned bases and 64-bit integer/double reads.
The complete 21-binary core suite passes 410 top-level TAP tests; the I/O and
canonical interop runs pass another 44. No build configuration changed.

Seven-sample operation runs after these changes are retained separately:
[64 bytes](evidence/operations-scalar-64.csv),
[1024 bytes](evidence/operations-scalar-1024.csv),
[65536 bytes](evidence/operations-scalar-65536.csv).
For 64 bytes, median microseconds are:

| Operation | Canonical | Native |
| --- | ---: | ---: |
| Borrowed scalar read | 0.0302 | 0.0337 |
| Typed verification | 0.2750 | 0.1451 |
| Native construction | 0.1085 | 0.1121 |
| Reflected scalar read | 0.0921 | 0.0553 |
| Reflected payload verification | 0.0519 | 0.6315 |
| Borrowed JSON printing | 0.9174 | 0.9315 |

This reduces several deficits without establishing full performance acceptance.
The reflected verifier remains substantially slower in this comparison.

`trace-growth.py` observes actual `ZiIOBuf::realloc` calls and their completed
allocation/copy branches under GDB. Commands use `zffbiobench 3 BYTES 1`,
with results recorded for [64](evidence/growth-64.txt),
[1024](evidence/growth-1024.txt), and [65536](evidence/growth-65536.txt) bytes.
Three messages are emitted per codec/mode. Both codecs make zero growth calls
for 64 bytes, one allocation in reuse mode and three in fresh mode for each
larger payload. No growth failures occurred.

| Payload | Mode | Canonical growth-copy bytes | Native growth-copy bytes |
| --- | --- | ---: | ---: |
| 1024 | Reuse | 1040 | 1040 |
| 1024 | Fresh | 3120 | 3120 |
| 65536 | Reuse | 65536 | 0 |
| 65536 | Fresh | 196608 | 0 |

These are total bytes copied during buffer growth, excluding serialization
copies, scalar stores, scratch writes and owner-pool allocations. They are
not complete allocation/copy counts. At 65536 bytes the canonical capacity is
98304 and native capacity 66576; both encode 65576-byte messages. The trace
runs exited normally. GDB emitted missing-RTTI warnings for the allocator
specialization but resolved the frames and counters. Debugger timings in
these logs must not be used as performance measurements.

### Matching canonical reflection flags and broader rows

The installed canonical reflected verifier runs in an independently built
shared library. To isolate that difference, the unchanged upstream
[25.12.19 reflection.cpp](https://raw.githubusercontent.com/google/flatbuffers/v25.12.19/src/reflection.cpp)
was compiled with this benchmark's configured Clang flags and linked into a
separate `/tmp/zffbrefbench` launcher. The original installed-library benchmark
and its results remain. No Z build configuration changed. The canonical
source hash, exact compilation/link commands and benchmark source hashes are
in [build evidence](evidence/sameflags-build.txt). Symbol inspection confirms
the comparison executable defines its own `flatbuffers::Verify`.

For the narrow 64-byte row, reflected verification now measures 0.7657 µs
canonical versus 0.6709 µs native (seven-sample medians). Raw operation samples:
[64](evidence/operations-sameflags-64.csv),
[1024](evidence/operations-sameflags-1024.csv),
[65536](evidence/operations-sameflags-65536.csv).
Commands use the previous narrow `(COUNT, BYTES)` pairs with seven rounds and
`ops`. This controls the reflection implementation's compilation flags;
it does not erase the earlier installed-library result or waive other gates.

`wide` and `wideops` add an owning string, an owning signed 64-bit vector,
a double, an int32 and a boolean alongside sequence/data fields. The vector
has `BYTES / 8` elements, including negative values. Both codecs use identical
input and ownership. Before timing, native output is verified/read by the
independent canonical declaration; both constructed objects and borrowed JSON
outputs are compared in full against input JSON. Setup/input construction and
these correctness checks are outside timing.

Contiguous unmapped scalar vectors whose native representation equals the
little-endian wire representation now use one `memcpy`. Differing widths,
mapped values, noncontiguous values and byte swapping keep element conversion.
The regression batch passes 411 core plus 44 I/O/interop top-level TAP tests,
including float/double vectors and explicit int64-to-int32 narrowing.

For the 65536-byte data field plus 8192 numeric elements, both writers emit
131208 bytes. Median microseconds per complete save/detach/read/reattach:

| Mode | Canonical | Native before bulk copy | Native after bulk copy |
| --- | ---: | ---: | ---: |
| Reuse | 106.1251 | 30.0969 | 3.8504 |
| Fresh | 181.4613 | 32.6761 | 5.7738 |

The before column comes from an earlier run, retained separately; it is not a
simultaneous sample. Current wide save samples: [64](evidence/wide-save-64.csv),
[1024](evidence/wide-save-1024.csv), [65536](evidence/wide-save-65536.csv);
[pre-change 65536](evidence/wide-save-65536-before.csv).
Wide operation samples, using canonical reflection compiled with matching flags:
[64](evidence/wide-operations-64.csv),
[1024](evidence/wide-operations-1024.csv),
[65536](evidence/wide-operations-65536.csv).
Commands use `(COUNT, BYTES)` of `(2000, 64)`, `(1000, 1024)`, `(100, 65536)`,
seven rounds, and the corresponding `wide`/`wideops` mode.

For the largest wide row, native construction is 127.9465 µs versus canonical
242.0091 µs; JSON printing is 1111.2405 versus 1117.0129 µs. Tiny scalar-read
operations still show overhead, and the narrow construction/printing results
remain slightly slower. These observations do not constitute a full M1 signoff.

The [wide growth trace](evidence/growth-wide-65536.txt) exercises growth with
live data: both codecs allocate twice for three messages with owner reuse,
and six times with fresh owners. Canonical growth copies total 131128/393384
bytes (reuse/fresh), native 65592/196776. No failures occurred. These counters
still exclude owner-pool allocations and nongrowth copies. Remaining gates
include complete allocation accounting, code-size/compile-time assessment,
remaining interop coverage, and migrated consumer measurements.

### Allocator request accounting

The extended `trace-growth.py` also observes `ZmHeapBase::operator new` and
`ZmVHeap::valloc` inside `Bench::run`. Records include requested variable
sizes and totals, including setup/teardown. They do not count a pool's backing
malloc again or claim to measure arbitrary libc allocations. Both codecs use
the Z allocators on the audited writer/owner paths. Raw traces:
[64](evidence/alloc-64.txt), [1024](evidence/alloc-1024.txt),
[65536](evidence/alloc-65536.txt),
[wide 65536](evidence/alloc-wide-65536.txt).
All commands emit three messages per codec/mode; all exited normally without
Python exceptions or growth failures.

Both codecs request one fixed-pool owner in reuse mode and four in fresh
mode. The latter includes the initial owner and the replacement after each
of three messages, including the final replacement. The wide input's owning
string/vector copy is prepared once per run outside the timed loop and is
included in the variable counts below.

| Workload | Mode | Canonical variable requests / bytes | Native variable requests / bytes |
| --- | --- | ---: | ---: |
| 64 | Reuse | 0 / 0 | 0 / 0 |
| 64 | Fresh | 0 / 0 | 0 / 0 |
| 1024 | Reuse | 1 / 1560 | 1 / 1560 |
| 1024 | Fresh | 3 / 4680 | 3 / 4680 |
| 65536 | Reuse | 2 / 163840 | 1 / 66576 |
| 65536 | Fresh | 6 / 491520 | 3 / 199728 |
| Wide 65536 | Reuse | 5 / 393257 | 4 / 264265 |
| Wide 65536 | Fresh | 11 / 1048617 | 8 / 661641 |

Source review attributes the extra canonical 65536-byte request to initial
arena allocation through `Zfb::IOBuilder::allocate`/`ZiIOBuf::alloc`, before
`reallocate_downward`. Native construction grows from its attached owner.
The variable-request trace captures both paths; the earlier growth-only
trace did not capture the canonical initial allocation. There is no measured
native allocation-count or requested-capacity regression in these workloads.
Nongrowth payload-copy accounting and broader consumer measurements remain
separate pending work.

### Wire accessor refinement and floating defaults

The scalar accessor and vtable-address helper now use `ZuInline`, keeping
these primitive reads in the caller. A separate field-adapter inlining trial
had no measured benefit and was removed. Eleven-sample runs against canonical
reflection compiled with the same flags use `200000 64 11`, `1000 1024 11`,
and `100 65536 11`, each with `ops` and `wideops`. For the narrow 64-byte row,
median microseconds per operation are:

| Operation | Canonical | Native |
| --- | ---: | ---: |
| Borrowed scalar read | 0.0298 | 0.0175 |
| Typed verification | 0.2589 | 0.1389 |
| Native construction | 0.1069 | 0.1040 |
| Reflected scalar read | 0.1059 | 0.0417 |
| Reflected payload verification | 0.7529 | 0.5805 |
| Borrowed JSON printing | 0.8480 | 0.8315 |

Raw narrow samples: [64](evidence/accessor-ops-64.csv),
[1024](evidence/accessor-ops-1024.csv), [65536](evidence/accessor-ops-65536.csv).
Raw wide samples: [64](evidence/accessor-wideops-64.csv),
[1024](evidence/accessor-wideops-1024.csv), [65536](evidence/accessor-wideops-65536.csv).
[Relink commands and source hashes](evidence/accessor-relink.txt) identify the
comparison executable. These results supersede the earlier accessor timings;
all previous samples remain available. Small construction/printing timings
varied between runs; consumer measurements remain required at M2.

The owning-library/core/I/O/interop incremental batch was also timed. Its
[per-directory build record](evidence/accessor-build.json) totals about 30.6
seconds elapsed with `make -j8`, reusing existing artifacts. The recorded
maximum RSS is the cumulative child-process high-water mark, reaching about
772 MiB; it is not simultaneous aggregate memory usage. This is the observed
cost of this header-change batch, not a clean-build comparison.
[Core object sizes](evidence/core-size.txt) record the retained debug footprint:
`ZfFB.o` about 374 KB of text/read-only data, `ZfFBReflect.o` about 35 KB, and
`ZfFB.o` below 1 KB. Ordinary Z field/enum matchers dominate the first object.
These object figures are neither a release estimate nor a whole-program delta;
final consumer link sizes and build effects still need assessment.

An independent canonical floating schema now covers float/double NaN defaults,
positive/negative infinity, the smallest positive double subnormal, omitted
scalar defaults, and floating vectors. Canonical BFBS/FBS comparisons treat
NaN defaults semantically (NaN versus NaN), and both payload directions pass.
The latest regression run passes 411 core tests and 45 I/O/interop top-level
TAP tests. All current build logs are warning-free. Full M1 signoff remains
pending the remaining audit items above and below.

## Baseline

The migration starts at commit
`a114cf416ca8b070069b927d92ed5b289d61fefd`. At the start the only untracked
files were `goal.md` and `plan.md`; tracked source was clean.

- Compiler/configuration: `clang`/`clang++`, x86-64 Linux,
  `-march=x86-64-v2 -g -DZDEBUG`, empty configured `LDFLAGS`.
- Canonical headers/library and `flatc`: 25.12.19; the existing dependency
  floor in `configure.ac` is 24.
- Existing `zfb/test/ZfbTest{,2,3}` libtool launchers and objects are present.
  Their existence is not performance evidence. Comparable pooled-builder
  initial pooled measurements are now recorded above; broader acceptance
  remains pending before deleting the old source/library.
- [contracts.md](contracts.md) captures the original live schema declarations,
  including defaults, field order, scalar widths, inline members/arrays,
  enums and duplicate union payload alternatives. Orphaned dashboard schemas
  are excluded. This record supplies independent migration expectations;
  changed application schemas do not have to read historical data.

## Arena reference

`zfb/src/Zfb.hh::IOBuilder` uses 8-byte alignment and initial usable capacity
`(buf->size & ~7) - buf->skip`; aligned `skip` reserves the front. It resets
length at attachment, and destroys/resets the arena before releasing its
owning buffer. Allocation adds `skip`, while deallocation subtracts it from
the data pointer. Detach transfers the same `ZmRef`, adds the returned arena
offset to `skip`, sets `length = size - offset`, and clears arena state.

Canonical `vector_downward::reallocate` adds
`max(requestedLength, oldCapacity ? oldCapacity / 2 : initialCapacity)` and
rounds to the arena alignment. `IOBuilder::grow` passes that request through
without another growth factor. `ZiIOBuf::realloc` retains front scratch and
back payload, moves the back region in existing storage when possible, or
copies only those live regions on allocation. Offsets survive growth;
pointers do not. New forced alignment must cover the allocation base as well
as offsets. No baseline latency/allocation result has yet been claimed.

## Consumer coverage

| Consumer | Native replacement and required evidence |
| --- | --- |
| Memory store | BFBS borrowed field views; retain existing tuple/key mapping, scalar/default/span/vector reads, dynamic partial writes, group/order semantics, recovery envelopes. Exercise all dispatched Z field codes. |
| PostgreSQL | Reuse `XField` associations made at setup. Preserve scalar integer widths, bool/double/string, inline 128-bit/fixed/decimal/time/date, bitmap/IP UDTs, bytes and supported vectors. Preserve OIDs, endian conversions, borrowed text/bytes until send, and serialization before `PGresult` release. Verify with the backend's actual conversion dispatch and integration tests. |
| SQLite | Reuse `XFields`/`XFieldRefs`; `xField` resolves strings, bool, integers through 64 bits, double, inline value types, bitmap/IP, bytes, strings/bytes vectors and supported scalar/inline vectors. Preserve ordered encodings, SQL NULL versus empty, `SQLITE_STATIC` and statement/scratch lifetimes. `zdbsltypes.fbs` is captured separately in the contracts record. |
| Database core/sagas | Each `Table` constructs/owns BFBS; stores borrow through asynchronous close. Direct cache-key tuples, erased row construction, nested replication/recovery, extra saga constructor arguments and owned snapshots; no row materialization for printing/keys. |
| Telemetry/hub/dashboard | Synthetic `All` fields, enum names, packed version, default/presence and sequence mutation; independently copyable nested body with union tag, separately rewritten routing envelope; raw stream sends, alert persistence/replay and retained Rx/Tx buffers. |
| `zum` / `zdf` | Version/defaults, JSON facets, maintenance/saga images, deletion vectors; grouped/descending keys and block byte spans. |
| Build/link-only consumers | Remove `zfb`/canonical production inputs throughout configure, module traversal, installation/distribution and includes/links, including `zgtk` and explicitly built dashboard targets. |

The full source-role inventory in `goal.md` remains the consumer checklist.
Backend support is determined by each existing dispatch, not by assuming that
the three stores accept identical types. Fixture execution, the new-code
consumer coverage, and runtime performance evidence remain pending.

## Preparation history

The entries below record the implementation before M1 execution. Their
queued/unrun labels describe that earlier state; current results are above.

The following entries record source preparation. Their original queued/
uncompiled status is superseded only by the specific execution evidence above.

- `zffbiobench` is an explicitly built migration target comparing the actual
  old `Zfb::IOBuilder` and new pooled builder with identical default policy,
  payloads and buffer tuning. Fresh and detached-owner reuse modes alternate
  codec order across samples and report elapsed time, encoded bytes, final
  capacity and readback checksum. Invoke as `zffbiobench COUNT BYTES ROUNDS`;
  sample sizes below, near and above the configured pooled capacity at M1.
  The target is uncompiled/unrun. It is an initial arena comparison, not full
  performance acceptance: allocation/copy instrumentation, verification,
  reflection, printing and consumer workload measurements remain required.
  Its old-header dependency is temporary and must be removed at Slice 10
  after preserving baseline source/results. Timing uses existing `Zm::now`;
  discard samples affected by clock adjustments or competing machine load.

- Bitmap mappings use one `Zf.Bitmap` declaration for fixed, Zt and hwloc
  representations. hwloc limbs stream into the wire vector directly, with
  compile-time 32/64-bit native-limb packing. Finite bit zero and rounded
  fixed capacities are retained; infinite hwloc bitmaps report a value error.
  Native UDT reflection IDs moved into their new owning headers. Root filters
  select mapped fields without pruning their helper representation. Bitmap
  and filtered byte-wrapper cases plus canonical bitmap exchange are queued
  for M1; no build or execution result is claimed.

- Borrowed reflection handles now provide name/ID lookup, scalar defaults,
  direct string/vector/inline access, union tags, and dynamic field writing
  over the existing builder. Name lookup uses `ZuSearch`; no owning schema
  graph or field registry is introduced. Erased payload verification shares
  `Verifier` bounds and traversal budgets with typed verification. It takes
  a trusted schema. `Reflect::verifySchema` now checks external BFBS structure
  and then the referenced indexes, supported field kinds, inline bounds and
  alignment, union tag relationships and keyed lookup ordering. Generated
  schemas still bypass this ingress-only path. Mutation cases distinguish
  a structurally valid BFBS buffer from invalid indexes/layout metadata.
  Reflected writer/reader and both canonical-schema payload directions are
  covered by queued source tests, with no execution result claimed.

- `ZfFB.hh` exposes generic stream saving and trusted native loading with
  compile-time size-prefix selection. Stream staging uses caller-scoped
  `ZtScratch`, the existing downward writer, and schema-derived alignment;
  the direct pooled path remains available. Payload and BFBS reachability
  share one compile-time traversal. `ZfFBStreamTest` covers both framing
  modes, operator-only streams, required-field failures and borrowed spans.
- BFBS/FBS exports accept a facet and resolve alternative names from its
  effective metadata while retaining original numeric field IDs. Facet
  checks and size-prefixed canonical interop cases are queued for M1.

- Shared `Skip`: `ZuFields` retains declarations; `ZuActiveFields` excludes
  them from tuples, keys, matchers, runtime field factories and format object
  paths. Direct dependent object traversals use the same filter.
- `ZfSkipTest` prepares metadata, no-member, retained-property, key, printing,
  JSON, CSV, ASN.1, CBOR, D-Bus, URI, CLI and configuration regression cases.
  It is queued for M1 and has not been compiled or executed.
- `ZfFBMeta.hh` starts the wire metadata: field properties, ADL type naming,
  union alternatives, full-declaration IDs, and facet/projection mapping.
  `ZtCase::camelSnake` now shares its transform with compile-time names.
- `ZfFBWire.hh`/`ZfFB.cc` provide trusted borrowed wire views and bounded
  verification primitives. Typed visitors in `ZfFBView.hh` now cover
  scalar/string/vector/table/inline shapes; unions remain pending.
- `ZfFBArena.hh`/`ZfFBBuilder.hh` implement downward construction, arena
  scratch, exact vtable reuse, nested-root isolation and raw span copying.
  `ZfFB.hh` connects this directly to pooled `ZiIOBuf` growth and detach.
- `ZfFBMetaTest`, `ZfFBWireTest`, `ZfFBBuilderTest`, and `ZfFBTest` are queued
  for M1. The metadata test also contains separately selected compile-failure
  cases. These additions have not yet been compiled or executed.

- `ZfFBTraits.hh`, `ZfFBView.hh`, and `ZfFBSave.hh` add shared wire shapes,
  scalar defaults from `Field::deflt()`, inline layout, borrowed typed access,
  recursive verification and native child-before-parent writing. Fixed arrays
  in inline structs retain their protocol widths and element layout.
- `ZfFBLoad.hh` adds native construction, extra constructor arguments,
  allocation, placement construction and updates. Borrowed views expose
  readonly field metadata, keys and facets without constructing native rows.
- `ZfFBValue.hh` maps 128-bit integers, fixed/decimal values, time/date and
  byte-vector vectors through ordinary wire metadata. Tagged unions support
  repeated table alternatives, empty tables, inline values and prebuilt
  payload composition. `ZfFB.hh` adds null/IPv4/IPv6 mapping in the I/O layer.
  `ZfFBValueTest`, `ZfFBLoadTest`, `ZfFBUnionTest` and extended `ZfFBTest`
  cases are queued for M1; none has been compiled or executed yet.
- `ZfFBReflect.hh` declares canonical BFBS types in ordinary `ZfStruct`
  metadata, including the truly deprecated `EnumVal.object` slot. These are
  declarations for the wire layout, not a second owning runtime schema graph.
- `ZfFBSchemaOut.hh` and `ZfFBSchema.hh` emit BFBS directly into generic
  `operator <<` streams. Reachability, name ordering, object indexes,
  table layouts and relative offsets are compile-time computations. Recursive
  table descriptions use indexes, without runtime registration. Scalar
  defaults are constant-evaluated through the existing Zf resolution.
- `ZfFBFBS.hh` provides `saveFBS<O>(S&)`; `saveSchema<O>(S&)` emits BFBS.
  Neither needs files, command-line tools, seeking or a runtime descriptor
  graph.
  Floating defaults use ZtString formatting into heap-identified ZtScratch;
  canonical hexadecimal floating literals preserve their value.
- Union schemas now emit canonical tag/payload slots, a name-sorted enums
  vector, discriminator-sorted alternatives including NONE, and object
  indexes shared by repeated alternatives. Textual export emits inline
  dependencies before their parents and tables. Canonical comparison cases
  cover deprecated/required unions, alternate field names and fixed arrays;
  these additions remain uncompiled and unrun.
- Scalar and vector enum metadata now derives names/ordinals from the existing
  map and widths from the field. Maps acquire only a qualified-name ADL tag.
  Flags emit canonical bit values and the `bit_flags` attribute. If a native
  enum's scalar default is outside its named values, Q8 drops the enum
  annotation while retaining the exact scalar default; no synthetic enum
  value or substitute default is introduced. Interop cases cover these
  translations and remain queued for M1.
- Forced vector alignment and enclosing inline-struct alignment now emit
  canonical `force_align` attributes in BFBS and FBS. Inline members retain
  natural offsets; enclosing alignment and trailing padding determine the
  struct stride. Schema attribute numbers use Z formatting and heap-identified
  scratch. Canonical cases check vector data alignment, nested aligned
  structs, metadata attributes and padding; they have not been run.
- `ZfFBViewTest` and `ZfFBSchemaTest` cover these initial paths, including
  required fields, skipped metadata, typed inline/vector access, recursive
  schema references, keyed name ordering and operator-only destinations.
- `zffbinterop` is prepared as an explicitly built interop target. It uses
  an independent canonical schema, checks both BFBS directions, imports the
  textual export, and exchanges payloads with the canonical runtime. Build
  inputs keep its canonical dependency within `zf/interop`. These tests have
  not yet been compiled or run; no interoperability result is claimed.
- Source review corrected the stale blanket Skip-property rejection in
  `goal.md` to match the settled decision already recorded in `plan.md`:
  incidental constructor/key/Mutable metadata is retained and skipped;
  incompatible canonical wire properties are diagnosed.

Canonical cross-checks used the installed 25.12.19 reflection headers and
upstream [reflection.fbs](https://raw.githubusercontent.com/google/flatbuffers/v25.12.19/reflection/reflection.fbs)
and [IDL parser](https://raw.githubusercontent.com/google/flatbuffers/v25.12.19/src/idl_parser.cpp).
The parser defines non-required reference fields as optional, serializes
scalar defaults and inline padding, and accepts hexadecimal float literals.
These source checks supplement, and do not replace, the queued interop runs.

M1 remains open for the remaining coverage and performance requirements.
M2 is the final dependent build/consumer gate, including Valgrind. Incremental
builds have run; the configuration is unchanged and no clean has been used.

## Slice 10: isolated baseline after module deletion

The production `zfb` module and `scripts/fbscodegen` have been deleted.
`iobaseline.hh` freezes only the old custom pooled IOBuilder, with unchanged
allocation, downward growth, alignment, detach/rebind and destruction logic.
Its namespace is now `Bench`; it has no production installation or linkage.
The benchmark uses ordinary `ZfStruct` declarations and two explicit canonical
adapters for Row/Wide, instead of retaining the removed ZfbStruct framework.
Those adapters preserve declaration-order field emission, canonical scalar
vector element conversion and mapped borrowed-vector access. The old full
adapter measurements above remain historical evidence; they do not prove
performance of the revised benchmark, which must run again at M2.

Configure probes canonical pkg-config optionally and exposes its flags only
to explicit interop makefiles. Configuration and ordinary production builds
do not require canonical FlatBuffers. Explicit interop targets check the
version prerequisite before compiling. `flatc` still compiles the independent benchmark
schema here. Regeneration uses `./z.config -c -L -d /usr`, deferred to M2.
These cleanup changes have been source-reviewed but not rebuilt or tested.


### M2 current benchmark run

The deleted production adapter is no longer a benchmark dependency.
`iobaseline.hh` freezes its pooled IOBuilder behavior; the two independent
canonical schemas use local adapters. Canonical borrowed JSON printing uses
`ZfFieldFilter::All` because those local adapters expose read-only getters.
The premeasurement native/canonical verification, full JSON and load checks
pass for both row shapes.

Current raw seven-sample results are `evidence/m2-{save,wide,ops,wideops}-SIZE.csv`
for SIZE 64, 1024 and 65536. Save/wide counts are 10000, 1000 and 100; operation
counts are 1000, 1000 and 100. The installed canonical reflection library is
still independently optimized, so that comparison is recorded separately
from the outstanding matching-flags rerun. No performance run used Valgrind.

For 64-byte payloads, median microseconds per pooled save/detach/read/reattach:

| Shape | Mode | Canonical | Native |
| --- | --- | ---: | ---: |
| Row | Reuse | 1.0633 | 0.7914 |
| Row | Fresh | 1.3456 | 1.0925 |
| Wide | Reuse | 1.7091 | 1.2696 |
| Wide | Fresh | 1.9717 | 1.5477 |

Native saves were faster at every measured size/mode. Read, typed verification,
reflected scalar access and wide construction/printing also improved. Narrow
construction/printing has small deficits in some samples; installed canonical
reflected verification remains much faster (Row 64: 0.0590 versus 0.6570 us).
These results do not sign off the complete performance gate. Matching-flags
reflection, current allocator traces and migrated consumer measurements remain.

### M2 matching-flags comparison and current allocation traces

The current benchmark was relinked with the canonical reflection translation
unit compiled under the retained Clang debug flags; see
[the relink record](evidence/m2-sameflags-build.txt). Seven alternating samples
were pinned to CPU 4, with 100000, 10000 and 1000 operations per sample at
64, 1024 and 65536 bytes respectively. Both codecs ran in the same process,
without Valgrind or concurrent builds. The raw results are
`evidence/m2-sameflags-{ops,wideops}-SIZE.csv`.

Median microseconds for the narrow 64-byte row:

| Operation | Canonical | Native |
| --- | ---: | ---: |
| Borrowed scalar read | 0.02867 | 0.01726 |
| Typed verification | 0.23349 | 0.13972 |
| Native construction | 0.10028 | 0.12273 |
| Reflected scalar read | 0.10024 | 0.04094 |
| Reflected payload verification | 0.71867 | 0.63507 |
| Borrowed JSON printing | 0.83344 | 0.81867 |

Matching-flags reflected verification improves at every measured size/shape.
Narrow construction remains 14–26% slower than the local canonical adapter;
that adapter directly assigns the fields, whereas native construction uses
the metadata handler. This difference needs investigation before performance
acceptance. Wide construction improves by 15–46%; printing ranges from a 2.2%
narrow-row deficit to an 11.1% wide-row improvement. These measurements do not
close the performance gate or replace migrated consumer measurements.

Current GDB allocation traces are `evidence/m2-alloc-{64,1024,65536}.txt`
and [wide 65536](evidence/m2-alloc-wide-65536.txt). They use three messages,
separate fresh/reused builders, and the unchanged custom pooled baseline.
Both codecs request one owner in reuse mode and four in fresh mode. At 64
bytes neither grows; at 1024 both grow once/reuse or three times/fresh and
copy 1040/3120 bytes during growth. For the wide 65536-byte row both grow
twice/reuse or six times/fresh; native growth copies 65592/196776 bytes versus
canonical 131128/393384. No growth failures occurred. GDB traces provide
allocation/copy evidence only, not latency measurements.

[Current production dependencies](evidence/m2-dependencies.txt) cover the
configured objects in 25 modules and their shared libraries, with zero
canonical/zfb dependency findings. The
[install/executable audit](evidence/m2-install-deps.txt) checks their 381
installed headers and eight source executables, plus the installed backtrace
test executable/libraries in `zm/test`. All declared headers exist; no
installed target or inspected ELF dependency names canonical FlatBuffers or
`zfb`. These are current build-input/artifact checks, not a second install or
clean-build matrix.

### M2 load-path correction

Disassembly of the narrow load path showed a moved temporary span in
`Handler::field`, plus out-of-line forwarding through `load` and `handler`.
The field adapter now returns unchanged values directly; the three small
public forwarding functions use `ZuInline`. Object construction, defaults,
field selection and verification policy are unchanged. This follows
`GUIDELINES.md`'s data-movement and algorithm/control-flow audit rules.

The direct-return change alone did not remove the measured deficit
(`evidence/m2-direct-load-64.csv`). With the forwarding functions inlined,
native narrow construction measures 0.10549 versus 0.11229 us at 64 bytes,
0.10086 versus 0.11400 at 1024, and 0.11014 versus 0.11643 at 65536. Wide
construction also remains faster. The 64-byte check uses eleven samples of
200000 operations; the other checks retain the seven-sample counts above.
Raw results are `evidence/m2-inline-load-64.csv` and
`evidence/m2-inline-{ops,wideops}-SIZE.csv`; relink commands are in
`evidence/m2-inline-load-relink.txt`. Correctness prechecks passed in each run.

The full performance gate remains open: these latest samples show 1–3%
printing deficits at larger sizes, and migrated consumer measurements are
unfinished. The full 2048-agent telemetry fixture failed before publishing
data with TLS `bad_record_mac` errors. Its failure cleanup released links
before draining pending TLS work; the fixture now drains started connections
and records TLS diagnostics for the full-scale rerun. This is not a codec
throughput result or a passing workload gate.

The subsequent moved-target comparison uses the same explicit JSON filter on
both codecs and is recorded as `evidence/m2-shared-{ops,wideops}-SIZE.csv`, with
`evidence/m2-shared-filter-build.txt`. Its narrow-load medians regress again:
canonical/native are 0.09998/0.12819, 0.09909/0.13190 and 0.10819/0.13186 us
at 64/1024/65536 bytes. The earlier inlining result is therefore not stable
evidence of acceptance. Narrow printing ranges from 2.7% faster to 5.3% slower;
wide loading/printing improve. All premeasurement correctness checks passed.
Further profiling remains necessary; neither dataset is discarded.
