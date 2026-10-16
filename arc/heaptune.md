# Heap tuning integration test plan

## Objective

Exercise the profile-guided heap configuration lifecycle in separate processes:
start with an empty configuration, run a deterministic canonical workload,
overwrite that configuration with `ZiHeapTune::save()`, and reload it with
`ZiHeapTune::load()` on the next invocation. Prove both that the CSV round-trips
and that the loaded
capacities change allocation behavior. The harness and workload fixture are
implemented in `zi/itest`; this document records their design and validation.

## Current implementation

- `zi/src/ZiHeapTune.hh` provides explicit environment-based lifecycle calls.
  It contains the CSV schema and reader; `init(path)` loads an explicit path,
  and `load()` passes `Z_HEAPTUNE` to `init`. An unset path is a no-op.
  `save(double headroom = 0.05)` requires a configured output path, opens it
  with `ZiFile::Write | ZiFile::GC`, and writes `Ztc::heapTuneCSV(headroom)`
  through `ZiFileTxStream`. It flushes before checking write errors and throws
  the file's `ZeError` on open or write failure. Scoped ownership destroys
  the stream before the file and closes the file before returning.
- `zm/src/ZtcHeap.hh` exposes printable `heapCSV()` and `heapTuneCSV()` objects.
  Their implementations are in `zm/src/ZtcHeap.cc`, using `ZuVStream`.
  Tuning emits `ID,partition,vshift,cacheSize,cpuset` and groups by
  `(ID, partition, vshift)`. All instantiated size classes participate.
  Arena identity remains `(ID, partition, size, alignment, sharded)`;
  multiple arenas can contribute to one configuration row. Fixed-size
  `ZmHeap` arenas use vshift 0. `ZmHeapCacheT` shares an arena between threads
  using the same arena key; count distinct arenas, not thread-local wrappers.
- `ZiHeapTune::init` loads those rows through
  `ZmHeapMgr::init(id, partition, vshift, config)`. In the main lifecycle test,
  configuration precedes workload allocation and worker creation. Arena
  resizing is not supported after a nonzero cache has been configured, so
  reinitializing in one process cannot substitute for independent runs.
- Configuration lookup and initialization of pre-existing zero-capacity arenas
  match ID, partition, and vshift. Each row supplies an actual block count for
  that class; the loader no longer right-shifts capacity. A missing class row
  defaults to zero, independently of other rows for the same ID/partition.
- `globalHeapAllocs`, `globalHeapFrees`, and `globalHeapMax` are per-ID values
  repeated in every arena row. Tests must distinguish those shared values
  from per-arena counters and verify the per-arena averaging described below.
  These globals count fallback allocations only: `globalHeapMax` is the peak
  outstanding fallback count, not the peak of cache plus fallback usage.
  Peak updates are intentionally approximate under concurrent allocation;
  deterministic tests must serialize allocations and frees for each ID.
- The exporter guards a zero `globalHeapAllocs` denominator by using
  weight 1.0. With no fallback allocations for that ID, its peak is also zero,
  so the result is the mean existing capacity within that configuration key.
  Consistent capacities are preserved on a fully cached replay.
- `vshift` is the size-class index relative to `ZmVHeap`'s minimum block class.
  `Min` is clamped to `ZmHeapAllocSize<1>`, `MinBits` is `ceil(log2(Min))`,
  and `blockSize(i)` is `1 << (MinBits + i)`. Class zero is `Min` rounded up
  to a power of two. `valloc` adds `Align` prefix bytes before class selection.
  Indices below `NCaches` use arenas; larger requests use the jumbo path and
  do not contribute to arena tuning. Verify distinct allocated class sizes
  and adequate payload space; do not add vshift to arena identity to mask
  duplicate size classes.
  Cached indices are `[0, NCaches)`: `blockSize(NCaches)` is the first block
  boundary beyond the cached classes, not an additional arena. Derive the
  effective limits from `MinBits`, `MaxBits`, and `NCaches`, including the
  17-class cap and the minimum of one class, rather than treating the supplied
  `Max` as an inclusive cached block size.
- Telemetry exposes vshift, 16-bit alignment, and boolean sharding.
  `heapCSV()` includes vshift after alignment, while the tuning CSV contains
  only the five configuration columns above.
- A zero-byte file currently reaches EOF successfully in `ZiCSV::Reader`.
  Test that exact initial condition as requested, as well as header-only
  and explicit zero-capacity configurations. `ZiHeapTune::CSV::read` discards
  the reader's error result, so successful process exit alone is not evidence
  of a successful load.

Resolve exporter defects exposed by the tests in focused follow-up changes.
Check current arithmetic against independently calculated examples, and check
the resulting behavior against the tuning lifecycle. Document any policy
decision about rounding, aggregation, or variable-size coverage alongside
the tests.

Existing allocator tests and heap benchmarks exercise the migrated API,
but do not replace the fresh-process tuning/export/reload harness implemented
here. Rerun relevant checks against the implementation used by the harness;
previous build results are not acceptance evidence for subsequent changes.

### Current sizing arithmetic

For a quiescent `(ID, partition, vshift)` group, let n be its arena count,
C the sum of its arena capacities, A the sum of its fallback allocations,
G the per-ID fallback allocation count, P the per-ID peak, and h the headroom.
G and P span all partitions and size classes for the ID. For n > 0 the current
exporter produces:

```text
G > 0:  uint64_t(C/n + P * (1+h) * A/(n*G))
G == 0: uint64_t(C/n)  [P is zero]
```

It sums the repeated global counters across n rows: averaging the peak
recovers P, while the allocation denominator remains n*G. The additional
division by n apportions the configuration group's peak share across its
arenas. The same output capacity is loaded into every arena for that key,
without further scaling. Capacities are consistent within a configuration
key, so C/n equals that key's configured capacity. With nonnegative headroom,
the added term is nonnegative: tuning preserves or increases capacity; it
does not shrink it. Assert this for each instantiated key across generations.
Positive fractional results truncate. The exporter includes instantiated
unused arenas and emits their groups; it does not filter them by activity
like `heapCSV()`. It retains the first arena's cpuset.
Cpuset is likewise consistent within a configuration key. Configuration rows
with no instantiated arena are omitted; omission is not capacity shrinkage
of an instantiated key.

Use these hand-derived examples to make aggregation coverage concrete:

- Two fixed-size arenas in one partition, both with vshift 0, each holding
  100 live objects at the same time: P=200, A=G, n=2, so cold output is 105
  per arena.
- One active arena holding 100 objects and one instantiated unused arena of
  the same ID/partition/vshift: P=100, A=G, n=2, so cold output is 52. Assert
  that arithmetic and observe residual fallback on replay; equal apportionment
  cannot meet this uneven demand in one pass.
- Two partitions with one arena each, holding 100 and 300 objects
  simultaneously, with equal burst repetition counts: P=400 and allocation
  shares 1/4 and 3/4 produce 105 and 315. Serialize allocation operations but
  retain both sets until the common peak is reached.
- A fully cached `ZmVHeap` with separate shift-0 and shift-1 configuration
  rows of 128 and 64 blocks reports those exact capacities. Export emits two
  rows with 128 and 64; repeated reloads preserve both independently. Choose
  sufficiently small demands to keep successive replays cached.
- A cold mixed-class workload holding 100 shift-0 and 100 shift-1 allocations
  simultaneously has P=200, G=200, A=100 and n=1 for each group. It emits two
  rows of 105; a fresh replay should serve both classes entirely from cache.
- Seed shift 0 with 128 blocks and shift 1 with 40. Hold 100 allocations in
  each class: only shift 1 falls back, so P=60 and A=G=60 for that group.
  Output is 128 for shift 0 and 103 for shift 1. This proves growth is applied
  independently despite shared global counters.
- A workload that instantiates only shift 1 must emit and reload its own row
  without instantiating shift 0. A standalone peak of 100 gives capacity 105.

## Files and build integration

- `zi/src/ZiHeapTune.hh`: installed utility header, listed in
  `zi/src/Makefile.am`'s `pkginclude_HEADERS`, with direct dependencies.
  Existing startup callers in `zhttp/util/zhttp{,d}.cc` and
  `zrest/example/zrest{,d}.cc` use `ZiHeapTune::load()`. This migration adds
  no new application shutdown hooks.
- `zi/itest/zheaptune.cc`: C++ harness using `ZuTestUtil`/`ZuTest`, emitting TAP.
- `zi/itest/zheapwork.cc`: separate workload fixture executable. Keep it here
  because it serves only this integration test; a new `zi/util` directory is
  unnecessary. It should use `ZuTestUtil` for its own checks, with its output
  captured in a child log rather than interleaved with the parent's TAP.
- `zi/itest/Makefile.am`: build both as `noinst_PROGRAMS`; introduce an explicit
  list of TAP drivers for `prove` so the fixture is not invoked without its
  required arguments. Preserve the existing drivers and Python/shell/Go tests.
- `zi/itest/README.md`: document the lifecycle, focused invocation, workload
  controls, timeouts, and retained artifacts. Existing `zi/itest` configure
  registration and module traversal already cover these additions.

Use existing Z allocation, CSV, file, thread, and test facilities. Follow
`GUIDELINES.md`, including lowercase integration-test filenames, hard tabs,
and direct includes. No external dependencies or timing benchmark is needed.

## Workload fixture

Make the first executable statement in `main`:

```cpp
ZiHeapTune::load();
```

Only then initialize test support, parse workload arguments, and create
workers. Avoid global objects that instantiate the fixture's heaps before
`main`. Use dedicated short heap IDs so library/test-support allocations can
be ignored when checking workload expectations.

Provide a small scenario selector, a live-object count, repetition count,
report path, and optional headroom. Omitted headroom must actually call
`ZiHeapTune::save()` to cover the utility's default argument; explicit headroom
calls `ZiHeapTune::save(headroom)`. Keep controls small and scenario-driven.
The late-initialization case uses `ZiHeapTune::init(options.late)` because
it loads a separately named input file rather than the tuning output path.

The basic scenario allocates N objects simultaneously, verifies their payloads,
then frees all N. Repeat the burst R times with complete drainage between
bursts. This gives known peak live demand N and allocation volume N*R, allowing
the harness to distinguish peak sizing from cumulative allocation sizing.
Use workload-sized Z pointer storage under a separate heap ID. Observe
allocations through actual heap operations; do not inject synthetic telemetry.

Capture structured telemetry after heap instantiation but before the workload,
and after the drained workload. Record full arena keys, vshift, cacheSize,
cpuset, cache/fallback allocation and free counters, global counters, and peak.
Include scenario parameters and allocation balance. Use `HeapMgr::capture`
or enumeration and framework CSV facilities; do not extend the tuning CSV
with diagnostic columns. Verify `heapCSV()`'s new column against captured
telemetry for active arenas; use direct capture for unused arenas that
`heapCSV()` omits. Take snapshots while all workers are quiescent. Instantiate
each intended class explicitly without allocating when a zero-counter initial
snapshot is required, using `Cache<I>::warmup()` for a variable-size class.
Warmup instantiates/configures its arena without incrementing allocation
counters. Diagnostics must not instantiate additional classes.

For variable-size workloads, derive requests from `blockSize(i)` and subtract
the alignment prefix when testing an exact block boundary. Write and verify
the full requested payload, including boundary bytes. Keep one consistent
Min/Max/alignment setup per fixture heap ID. Select classes through real
`valloc` requests, then verify
the captured vshift and size instead of assuming them from payload size alone.
For boundary `B = blockSize(i)`, test payload lengths `B-Align-1`, `B-Align`,
and `B-Align+1` where positive. The first two select class i and the last
selects the next class or jumbo. Separately test `valloc(0)`/`vfree(nullptr)`:
they must leave allocation counters and instantiated workload arenas unchanged.

After the workload and snapshots, overwrite the same environment-selected
file using the intended API:

```cpp
ZiHeapTune::save();
```

Include `ZiHeapTune.hh` directly. The utility owns file/stream lifetime and
error checks; the fixture catches failures and reports a failed TAP check.
Diagnostic `heapCSV()` output still uses its own `ZiFileTxStream` and file.
The parent must additionally validate the complete resulting file. Export
before singleton teardown; retain arena visibility until collection finishes.

For partition scenarios, use `ZmThreadParams::partition` and distinct workers.
Coordinate stages with `ZmBlock` or `ZmSemaphore`, never sleeps or polling.
Serialize allocation stages where necessary to make shared peak expectations
exact and avoid depending on concurrent peak-counter update behavior. Keep
frees on their allocating worker unless a case explicitly tests cross-free
behavior; cross-thread free stress is outside this plan.

## Harness lifecycle and evidence

1. Create an owned `ZiTestResidue` directory per harness run, with separate
   case directories. Put every configuration, snapshot, and child log there.
   Never use or overwrite the caller's real `Z_HEAPTUNE` file.
2. Create a zero-byte configuration and set `Z_HEAPTUNE` only for the child.
   Launch the build-tree fixture through its normal libtool wrapper, using
   absolute paths and quoting consistent with existing `zi/itest` launchers.
   Support paths containing spaces and out-of-tree builds. Bound each child
   execution and reap it on failure or timeout.
3. Run the cold workload, require successful exit and child checks, and save
   both input and output configuration generations before further runs.
4. Parse the output independently using framework CSV support. Check the
   exact five-column header, valid integer capacities, unique
   `(ID, partition, vshift)` keys, expected workload rows, cpuset, and complete
   file consumption. Compare
   keyed records, not whole-file golden output: incidental framework heaps
   may vary by platform and build type.
5. Launch a fresh child against the generated file. Compare its initial
   telemetry to the saved configuration, then compare its workload counters
   to the cold run. This proves configuration was applied, beyond parsing.
6. Launch a third child against the second output. Check stable capacities
   and cached behavior for sufficiently provisioned cases, including nonzero
   shifts. Check omission/default-zero behavior separately for configuration
   keys with no instantiated arena. This detects regressions in zero-fallback
   handling and capacity drift on repeated
   export/reload.
7. Emit named TAP checks with case, generation, heap key, expected/actual
   values, and artifact paths on failure. Stop children before residue
   finalization; remove artifacts on success and retain them on failure.

## Test matrix

| Case | Workload and required observations |
| --- | --- |
| Cold/default/replay | One fixed-size arena, N=100 and R=3. Cold cacheSize=0, heapAllocs=300, matching heapFrees, globalHeapMax=100, allocated=0. Default export should yield 105; replay should load 105 and serve all 300 allocations from cache with zero fallback. Third run remains stable. |
| Peak versus volume | Repeat the same peak with different R. Cold tuned capacity must be independent of cumulative allocation count. Free everything before export so final outstanding allocation count is zero despite a nonzero peak. |
| Empty input variants | Repeat the baseline with a zero-byte file, header-only file, and explicit zero row. Verify equivalent workload telemetry and capacities. |
| Headroom | Cover omitted default, explicit 0, and explicit 0.25 (N=100 gives 100 and 125). Include N=3 with default headroom: current truncation yields 3. |
| Warm growth | Seed capacity 40, peak 100 in a single arena. Expect cache use plus fallback peak 60; default tuning should add 63 for capacity 103. A fresh replay should eliminate fallback. Also seed an oversized capacity and verify a zero-fallback run preserves it. |
| Unequal partitions | Cover the overlapping 100/300 example above and a sequential-lifetime variant. Require separate rows, independently calculated allocation shares, preserved partition identities, and correct loaded capacities. Require zero fallback for the overlapping example; do not assume it for every partition schedule. |
| Multiple arenas per key | Use the same ID, partition, and vshift with distinguishable sizes/alignments and sharding variants, all with the same configured capacity and cpuset. Require one tuning row and reload application to every contributing arena. Cover equal simultaneous demand (105 per arena in the example above) and an active/unused pair (52), distinguishing average provisioning from each arena's actual demand. Also use two threads with the same arena key and confirm they contribute only one arena. |
| Variable-size heap | Verify captured shifts and the `heapCSV()` column. Require separate rows for classes 0 and 1 under the same ID/partition. Cover independent seeded capacities 128/64, cold outputs 105/105, and selective growth to 128/103 as derived above. Replay must load capacities directly, without a shift or cross-class overwrite. |
| Nonzero shifts only | Instantiate only class 1. Require its row to be emitted and loaded in a fresh process, with no dependency on a class-0 row. Test both cold tuning and stable seeded capacity. |
| Key isolation | Seed distinct capacities for the same ID across two partitions and two classes, then vary CSV row order. Verify all four loaded capacities. Also omit one row: only that class/partition should default to zero, with no fallback to another class's configuration. |
| Pre-existing arenas | In a separate scenario, load an empty file first, instantiate zero-capacity classes in two partitions, then load a prepared CSV once before allocation. Verify each row configures only its matching partition/vshift and matches the result of configuring before arena creation. This covers supported late initialization, not resizing nonzero arenas. |
| Size-class boundaries | Cover default, clamped-small, and non-power-of-two minima, the minimum-one-class rule, and the 17-class cap. Check class-zero rounding, distinct physical class sizes, and requests immediately below/at/above block boundaries after prefix accounting. Verify payload integrity and the transition at `NCaches` to jumbo allocation, which changes neither arena counters nor per-ID fallback counters and emits no arena row of its own. Cover zero allocation and null free separately. |
| Zero fallback / unused | Instantiate an unused dedicated ID alongside a used ID, then an unused arena sharing a configuration key. Require zero-capacity rows for unused unconfigured groups, including nonzero shifts. Fully cached groups must retain capacity. Also cover a group with A=0 while another partition or class of the same ID has G>0: its weight is zero and its capacity remains unchanged. |
| cpuset | Seed a valid cpuset derived from available topology; verify semantic preservation per configuration key through output and reload, including nonzero shifts. Also cover the default empty cpuset. Avoid requiring specific CPU numbers or NUMA layout. |
| File replacement | Start from valid CSV longer than the eventual output, for example with unused configured IDs. Confirm overwrite truncates it and leaves no stale rows or trailing bytes; reload the replacement successfully. |

For aggregation cases, first write down the expected per-ID peak and
per-group share from the deterministic allocation schedule. Existing
allocation-share weighting is a heuristic: unequal lifetimes and multiple
arenas need not be provisioned perfectly by one cold run. Keep exact arithmetic
and reload checks distinct from the zero-fallback requirement, which is
mandatory for the controlled single-arena baseline and simultaneous mixed-class
case. Retain sequential-lifetime and unequal-demand cases to characterize the
heuristic without asserting that it independently measures every group's peak.
Repeated cached exports must preserve each class's configured block count.
For every instantiated configuration key, assert that the exported capacity
is at least the consistent initial capacity. Do not construct inconsistent
capacities within a key to introduce an artificial shrinkage scenario.

## Implementation order and validation

The current utility migration is reviewed through source inspection and
`git diff --check`; no rebuild or runtime validation is authorized while the
instruction to stop rebuilding remains in effect. The build steps below
apply only when rebuilding is explicitly resumed. At that point, rerun the
default/explicit-headroom lifecycle and late-input cases, and check utility
open/write failure reporting and the resulting file's reload telemetry.

1. Add the fixture and harness build targets; compile and link the consumer
   against the existing `ZtcHeap.cc` exporters and five-column CSV loader.
   Keep `ZtcHeap.cc` in the `zm` library build and check dependent calls use
   `ZmHeapMgr::init(id, partition, vshift, config)` with per-class block counts.
2. Implement the single-arena cold/replay/third-run test, including residue,
   bounded child execution, snapshots, and independent CSV verification.
3. Add headroom, peak-versus-volume, zero-allocation, and warm-growth cases;
   fix demonstrated exporter defects without changing unrelated heap code.
4. Add partition, multi-arena, independent-class, late-initialization,
   class-boundary, cpuset, and replacement cases.
   Record any required sizing-policy correction with its reproducing case.
5. Regenerate build files through `z.config` if necessary, preserving the
   active configuration, then build the configured modules sequentially in
   dependency order. Use `-j8` for `zu zm zt ze zf zi zv zrl zdbus ztcp ztls
   zquic zhttp zws zrest zmcp zfb zdb ztc`, and `-j4` beyond `ztc`. Use `-j4`
   for enabled modules outside that explicit list as well. Preserve top-level
   traversal with a recursive make wrapper that switches the job count at
   module boundaries. Run the focused harness, `make -C zi/itest test`,
   and `make -C zi test` after the full build passes.
6. Validate with Clang on Linux and under MSYS2/MinGW where available.
   GCC and ASan/LSan validation are out of scope. Cover x64 and ARM when
   runners exist. Use Valgrind only to investigate observed memory corruption
   or crashes, instrumenting the affected parent or child through `libtool exec`.
   Never execute `.libs` binaries directly. Reconfigure/clean/rebuild
   consistently when changing build type; report unavailable platforms.

Acceptance requires a demonstrable empty-file-to-tuned-file-to-cached-replay
transition, independently loaded capacities for every class and partition,
stable repeated export for satisfied workloads, verified omission only for
uninstantiated configuration keys, nondecreasing capacities for instantiated
keys, deterministic aggregation expectations, successful suite integration,
and useful retained evidence on failure. No wall-clock speed threshold is part of acceptance;
allocation telemetry provides the direct behavioral proof.

## Implementation and validation record

The driver implements the matrix above with 28 named cases, normally three
fresh-process generations per case. `Z_HEAPTUNE_CASE` selects one case;
`Z_HEAPTUNE_TIMEOUT` bounds each child and `Z_HEAPTUNE_CHILD` supplies an
instrumentation command prefix. Both programs use framework CSV metadata and
I/O. The driver checks complete CSV consumption, five configuration columns,
unique configuration keys, integer fields, and cpuset round-tripping; it does
not manually split file rows. The shared header is a constexpr `ZuCSpan`,
with its length supplied by the span.

The tests exposed and reproduced these prerequisite defects:

- The heap CSV loader's ID field needed the uppercase `ID` CSV column property to
  reload the exporter's rows.
- Both heap exporters needed CSV quoting around cpuset values containing
  commas.
- `ZfCSV::Reader::process` needed an explicit consumed-byte count because a
  fully consumed span resets its pointer. Its non-constructor field loading
  paths also used a stale `loadField` template signature. Regression coverage
  exercises construction, allocation, placement construction, load/update,
  and partial versus complete consumption.
- Formatted file output uses `ZiFileTxStream`; `ZiFile` no longer supplies
  `operator <<`. Exporters flush before inspecting the underlying file error
  and closing the file. `ZiFileTest` covers formatted output and exact bytes
  read back through this stream.

Earlier Linux x64 validation (Clang 22.1.8, debug), before the utility migration:

- The full configured Clang build passes with the module job counts above.
  A guard reserves 12 GiB of available host memory; it did not trigger.
  The build emits only make jobserver notices when switching to `-j4`.
- The focused matrix, CSV/file regressions, and migrated allocator/hash/queue
  tests pass: six programs, 76 top-level TAP tests, with 7,256 checks inside
  the tuning driver's case scope on this run.
- `make -C zi test` passes unrestricted, including its resolver tests:
  22 unit-test programs with 114 top-level TAP tests and three integration-test
  programs with 37 top-level TAP tests, plus Python, shell, and Go checks.
  An initial parallel suite run failed the existing socket-backpressure test;
  its standalone rerun and the serial suite both passed. No source change was
  made for that transient failure.
- Earlier Valgrind run: the complete parent/child matrix passed, with zero
  memory errors and zero lost allocations in the parent.
- Launch checks pass with spaces and apostrophes in artifact paths, preserve
  a caller-owned tuning file, reject an unknown case selector, and terminate
  a one-second timed-out workload with no live descendants while retaining
  failure artifacts.

GCC and ASan/LSan verification are out of scope. Further Valgrind runs are
conditional on observed memory corruption or crashes.

For subsequent build verification, use one build at a time, the module job
counts above, and memory protection for the entire build process group,
including its compiler children. Earlier concurrent builds exceeded the
host's memory capacity; a per-process limit did not bound their combined use.
Stop the build if available host memory falls below a reserved margin for
desktop applications. Do not treat interrupted builds as successful validation.
Windows MSYS2/MinGW and ARM runners are unavailable in this environment;
their validation remains an external platform check.
