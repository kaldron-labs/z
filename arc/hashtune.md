# Hash table tuning implementation plan

## Objective

Add `Ztc::hashTuneCSV(headroom = 0.05)` and an installed `ZiHashTune.hh`,
following the printable API and explicit utility lifecycle established by
`ZiHeapTune.hh`:

```cpp
// First executable statement in main, before constructing workload tables.
ZiHashTune::load();

// Run the canonical workload, then stop mutations and join workers.
// Export while the workload tables are still registered and alive.
ZiHashTune::save();
```

Callers include `ZiHashTune.hh` directly and use `save(headroom)` for an
explicit value. The utility owns environment lookup, CSV loading, file/stream
lifetime, flushing, and file-error reporting.

Start with a zero-byte configuration file and stock table defaults. Unlike
heap caches, hash tables require storage even on the first run: `bits = 0`
does not mean disabled storage. The next process loads the exported starting
sizes before table construction. Prove that replay avoids resizing for the
same deterministic workload and that further exports stabilize.

The exporter, consolidated utility, and integration test sources described
below are implemented and validated in the Clang/debug configuration. Read alongside
`heaptune.md`, `zi/src/ZiHeapTune.hh`, and the existing
`zi/itest/zheaptune.*` and `zheapwork.cc` implementation.

## Existing behavior

- `ZiHeapTune.hh` contains the heap configuration schema and CSV reader.
  `init(path)` loads an explicit path; `load()` calls it using `Z_HEAPTUNE`.
  `save(double headroom = 0.05)` delegates to the heap tuning exporter through
  `ZiFileTxStream`, flushes, and throws `ZeError` for open/write failure.
  Mirror this utility contract using `Z_HASHTUNE` independently.
- `ZiHashTune.hh` now loads `id,bits,loadFactor,cBits` through
  `ZmHashMgr::init(id, params)`. Configuration is keyed solely by ID.
  `ZmHashParams{ID}` retrieves that configuration during construction;
  explicit-parameter constructors use their supplied parameters instead.
  Loading configuration later affects subsequently constructed tables, not
  existing tables.
- `ZmHashParams` defaults to bits 8, load factor 1.0 and cBits 3.
  `ZmHash` clamps initial bits to [2, 28], clamps cBits to at most 12 and
  then to bits, and rounds its effective load factor down to sixteenths
  after enforcing a minimum of 1.0. `ZmNoLock` reports cBits 0.
- Dynamic `ZmLHash` rounds its effective load factor down to sixteenths
  after clamping to [0.5, 1.0], has no striped locks, and currently accepts
  initial bits without the `ZmHash` clamp. Local and statically allocated
  `ZmLHash` variants do not participate in manager telemetry.
- Registered `ZmHash`, including shadow hashes, and registered dynamic
  `ZmLHash` expose telemetry through `Ztc::HashMgr`. Identity is `(id, addr)`;
  multiple independent tables can share one configuration ID.
  Manager iteration is in address order, not ID order.
- `HashTelemetry` now contains `maxCount`, alongside current count, bits,
  load factor, cBits and resize count (`resized`). Both dynamic implementations
  track the lifetime peak. Peak updates are intentionally approximate under
  concurrency; exact sizing tests must serialize insertion stages.
- `ztc/src/ZtcFB.hh` already declares `maxCount` as
  `Ctor<5>, Mutable, Series` with type `UInt64`, and shifts the subsequent
  constructor indices accordingly. `ztc/src/fbs/ztc_telemetry.fbs` now includes
  `max_count:uint64` immediately after `count`, matching that metadata.
- Both dynamic implementations test the count before insertion, resizing
  when `((count << 4) >> bits) >= loadFactor_()` for count below `1U << 28`.
  An insertion that first reaches the threshold does not itself resize;
  the next insertion does. Internal linear-table rehash uses `add__()` and
  must not count entries as new workload insertions.
- `ZtcHash.hh` exposes diagnostic `hashCSV()` and tuning `hashTuneCSV()`;
  their printers are now implemented in `ZtcHash.cc`. Diagnostic CSV emits `count,maxCount,effLoadFactor,resized` after the table attributes.
  The consolidated loader metadata uses lowercase `id`, and existing
  configuration fixtures use that spelling. Keep it for the tuning CSV,
  matching the heap exporter's lowercase `id` column.

## Sizing and policy

Tune only `bits`, to avoid resizing on the next canonical run and allow
tables smaller than the stock default. For each live registered table:

```text
if resized > 0:
    tableBits = terminating bits
else:
    target = ceil(maxCount * (1 + headroom))
    tableBits = smallest supported bits accommodating target
outputBits(ID) = max(tableBits for every table with that ID)
```

The resize branch uses the size already established by the workload, without
adding headroom or attempting shrinkage. The no-resize branch uses the table's
effective load factor and existing pre-insertion resize threshold to choose
bits for the headroom-adjusted peak. It has no floor at the initial or current
bits: an oversized or unused table can shrink to the supported minimum.
Headroom applies to demand, so it does not compound between runs.

Keep loadFactor and cBits unchanged in the existing four-column configuration
format. Copy these from `ZmHashParams{id}`; replace only bits. No new
construction-policy telemetry or policy-conflict machinery is needed.
Tests of configuration replay use tables constructed from their ID's manager
parameters. Explicit-parameter tables still contribute observed sizing, but
their constructors must use manager configuration for tuned bits to apply.

Multiple live tables sharing an ID contribute their maximum candidate bits,
never a sum or average of occupancy. Evaluate each table's resize branch
independently, using its own effective load factor in the no-resize branch.

Examples with load factor 1.0 and headroom 0.05:

| Initial bits | maxCount | Resized? | Terminating bits | Exported bits |
| ---: | ---: | --- | ---: | ---: |
| 2 | 5 | Yes | 3 | 3 |
| 2 | 125 | Yes | 7 | 7 |
| 8 | 100 | No | 8 | 7 |
| 8 | 3 | No | 8 | 2 |
| 8 | 0 | No | 8 | 2 |
| 7 | 125 | No | 7 | 8 |

A grown table can need another export to apply headroom: the 125-entry case
first exports 7 after growth, then exports 8 on its no-resize replay. Subsequent
runs stabilize. With zero headroom, exactly reaching the pre-insertion
threshold must permit replay without resizing. Check the supported bit bounds
and headroom arithmetic without allocating enormous tables.

## Implementation steps

1. Use the existing `maxCount` tracking in registered `ZmHash` and dynamic
   non-local `ZmLHash`. Verify that deletion and `clean()` retain the peak
   and internal rehash does not inflate it. Keep the existing approximate
   concurrent update behavior; serialize exact peak checks.
2. Refresh generated telemetry output through the existing rules. The
   telemetry member, producers, mutable-series metadata,
   FlatBuffers schema, and diagnostic CSV column are already present.
   Check dependent readers and a serialization round-trip, keeping current
   count distinct from peak.
3. Add `zm/src/ZtcHash.cc` to `libZm_la_SOURCES`. Move diagnostic CSV printing
   behind `HashCSV_print(ZuVStream)` and expose `HashTuneCSV_print` and the
   lightweight printable `hashTuneCSV(double headroom = 0.05)` in the header,
   matching `ZtcHeap.hh/.cc`. Implement the two sizing branches above using
   existing framework arithmetic and supported bit bounds.
4. Snapshot quiescent telemetry with `HashMgr::capture`, then aggregate in a
   Z container keyed by ID. Do not depend on manager address order, retain
   raw table pointers after capture, or hold the manager lock during file I/O.
   Use an untuned scratch/grouping heap ID to avoid introducing exporter
   tables into the sample. Emit deterministic ID order and one row per ID,
   including instantiated unused tables. Proposed header:
   `id,bits,loadFactor,cBits`. Quote identifiers using the framework's CSV
   conventions. Preserve the manager's loadFactor and cBits columns; tune
   only bits.
5. Verify the chosen bits are supported by each participating implementation.
   Test the minimum size, below-default shrinkage, and exact resize thresholds.
   Keep this work scoped to sizing export and reload.
6. Consolidate the hash CSV schema, reader, and `init(path)` into
   `ZiHashTune.hh` under namespace `ZiHashTune`, matching the consolidated
   heap utility. Delete `ZiHashCSV.hh` and its namespace, propagate the
   intentional breaking change through all callers, and provide no aliases
   or forwarding header. Continue using `ZiCSV`/`ZfCSV` rather than manual
   parsing. Verify zero-byte and header-only inputs and round-trip the exact
   header. The current reader discards the returned read error, so fixture
   success alone is insufficient evidence of a successful load.
7. Add `zi/src/ZiHashTune.hh` to `pkginclude_HEADERS` in
   `zi/src/Makefile.am`, removing the old CSV header entry. Follow the
   repository header skeleton and include direct dependencies of the moved
   schema and reader, plus `ZtcHash.hh`, `ZtPlatform.hh`, `ZiFile.hh`, and
   `ZiFileTxStream.hh`. Implement these utilities alongside `init(path)`:

   ```cpp
   namespace ZiHashTune {
   inline void load() {
     init(Zt::getpath("Z_HASHTUNE"));
   }
   inline void save(double headroom = 0.05) {
     ZiFile file{Zt::getpath("Z_HASHTUNE"), ZiFile::Write | ZiFile::GC};
     if (!file) throw file.error();
     ZiFileTxStream<> stream{file};
     stream << Ztc::hashTuneCSV(headroom);
     stream.flush();
     if (file.error()) throw file.error();
   }
   }
   ```

   An unset load path is a no-op; saving requires a valid configured path.
   Destroy the stream before its file owner and close the file before
   returning. Migrate existing environment-based startup loads in
   `zhttp/util/zhttp{,d}.cc` and `zrest/example/zrest{,d}.cc` to
   `ZiHashTune::load()`, with direct utility includes. Migrate explicit-path
   loads to `ZiHashTune::init(path)`, retaining their selected input files.
   Use the utility in the new fixture; do not add implicit saves.

The initial feature samples live registered tables, just as the current
manager API does. Export after draining work but before destroying tables.
Destroyed tables, uninstantiated configuration IDs, static linear tables and
local linear tables contribute no rows. Do not add persistent process-wide
history, automatic file writes, or application shutdown hooks in this change.
Applications explicitly choose the workload and export point.

## Integration harness in zi/itest

Add `zhashtune.cc` (TAP driver), `zhashwork.cc` (child fixture), and a shared
`zhashtune.hh` for report metadata and constants. Follow the existing heap
harness: fresh processes, child-only `Z_HASHTUNE`, `ZiTestResidue`, bounded
child execution, process-group/job cleanup, and libtool wrapper paths.
Reuse existing helpers where practical; avoid refactoring working heap tests
unless both harnesses need a concrete shared facility.

The fixture's first executable statement in `main` is `ZiHashTune::load()`.
Then initialize test support with `ZuTestUtil::parse`, so `HARNESS_ACTIVE`
controls verbosity, and construct strongly identified test tables. Capture
initial telemetry, run insertion/find/deletion workloads, then capture peak
and drained telemetry while tables remain alive. Verify
every key/value and successful insertion, and distinguish current count zero
from a retained peak. Export diagnostic CSV and overwrite the same tuning
path with `ZiHashTune::save()` or `save(headroom)` before releasing table
owners. Catch file errors and fail the fixture's checks. Both C++ TAP emitters
use `ZuTestUtil`, with diagnostics through `log`; intentional whole-program
skips may emit `1..0 # SKIP` manually. Use `ZmSemaphore`/`ZmBlock` for worker
coordination, with serialized deterministic allocation stages and explicit
joins. No sleeps or polling to infer workload completion.

The driver runs enough fresh-process generations to verify growth or
shrinkage, headroom adjustment, and a stable replay. Independently compute
expected bits from observed resize status and known peak demand.
Read reports/configuration through framework CSV metadata and readers; use
a `constexpr auto header = "id,bits,loadFactor,cBits"_Zu` if exact header
validation is useful. Check
loaded bits/policy directly before workload execution, then verify resize
behavior. Do not use exporter output as its own expected-value oracle.
Keep fixture TAP in child logs. Preserve caller-owned files/environment.

| Case | Required evidence |
| --- | --- |
| Empty, header-only, explicit defaults | Stock defaults load and export/reload correctly |
| Chained cold/replay | A resized run exports its terminating bits exactly; replay has resized 0; further output stabilizes after any headroom adjustment |
| Linear cold/replay | Equivalent lifecycle for registered dynamic `ZmLHash` |
| Threshold boundaries | Below, exactly at, and above the pre-insertion resize threshold |
| Default/zero/explicit headroom | Headroom affects only the no-resize branch; include a case crossing a power-of-two boundary |
| Utility lifecycle/errors | Unset load is a no-op; default/explicit saves overwrite and reload; invalid save paths and write failures are reported; heap/hash environment paths remain independent |
| Drain and clean | Count becomes zero; peak and tuned capacity remain correct |
| Peak telemetry | Captured telemetry, diagnostic CSV, and FlatBuffers round-trip retain maxCount separately from drained count |
| Repeated bursts | Peak depends on simultaneous occupancy, not total insertion volume |
| Warm and oversized | Insufficient seed exports grown bits; oversized seed shrinks below the default when peak demand permits |
| Multiple IDs | Independent rows and no telemetry-address leakage into configuration |
| Same ID, unequal instances | Maximum requirement, independent of overlap and address order |
| Mixed chained/linear | Per-table resize branch and effective load factor determine candidates; maximum bits wins |
| Configuration policy | Manager loadFactor and cBits are unchanged, including when tuned bits clamp effective locks |
| Concurrent stripes | Resize/replay behavior under coordinated stages; exact peak checks use serialized insertion |
| Shadow hash | Registered shadow table participates without transferring node ownership |
| Unused and excluded tables | Live unused table tunes to minimum bits; destroyed/static/local tables absent |
| Late init | Existing instance unchanged; new instance sees updated manager configuration |
| File replacement | Longer seed replaced without stale rows or trailing bytes |
| Arithmetic limits | Supported bit bounds and headroom-adjusted target boundaries without huge allocations |
| Launch controls | Timeout kills the child process group and reaps the child; paths with spaces/apostrophes work |

Expose `Z_HASHTUNE_CASE`, `Z_HASHTUNE_TIMEOUT` and `Z_HASHTUNE_CHILD`, following
the heap harness's semantics. Retain generation inputs, outputs, telemetry
reports, diagnostic CSV and child logs on failure; remove owned residue on
success. Concurrent harness invocations use separate `ZI_LOGDIR` roots.

Add the driver to `tap_drivers` and the fixture to `noinst_PROGRAMS` in
`zi/itest/Makefile.am`, preserving all existing tests. Document focused runs
and controls in `zi/itest/README.md`; both `make -C zi/itest test` and
`make -C zi test` must include the new driver.

## Validation and completion

Rebuilding was explicitly resumed. Apply the following validation to the
completed implementation.

Use only the Clang/debug build configuration. Build changed src layers before
dependents, regenerate telemetry/build inputs through existing rules, then
build the full configured module tree. Run one build at a time, using `-j8`
through `ztc` and `-j4` thereafter. Retain the memory headroom guard used for
heap validation. No ASan/LSan, GCC, or MinGW builds. Use Valgrind if needed to
investigate observed memory corruption or crashes, via `libtool exec` and
normal wrapper paths.

Run focused peak/telemetry regressions, applicable existing hash tests, the
new driver, the existing heap driver, and the complete zi suite. Check a
telemetry serialization round-trip through ztc metadata/schema. Inspect
generated/build diffs and `git diff --check`. Record what was actually run.

Completion requires a reloadable deterministic exporter, stable repeated
canonical runs without resizing, demonstrated below-default shrinkage,
unchanged loadFactor/cBits and per-ID maximum sizing,
passing relevant suites, and documentation updated with implementation
decisions and actual validation evidence.

## Implementation record

Source implementation now includes `zm/src/ZtcHash.cc`, the printable tuning
API, the consolidated `zi/src/ZiHashTune.hh`, and all former hash CSV callers
migrated to its namespace. The old CSV header is removed from the source tree
and install manifest. Both exporters snapshot telemetry before writing; tuning
groups by ID in an untuned tree and copies the manager's loadFactor/cBits.
Identifiers are CSV-quoted, including embedded quotes.

`zi/itest/zhashtune.cc`, `zhashwork.cc`, and `zhashtune.hh` provide 35 named
lifecycle cases, normally four generations, with launch controls and retained
failure evidence. The maximum-bit case uses a registered synthetic telemetry
sample rather than allocating a huge table. `ZtcSchemaTest` includes a 64-bit
peak round-trip and a drained-count/retained-peak round-trip. The existing heap
harness was aligned with the current lowercase `id` exporter and loader so
its regression run uses the same schema.

Arithmetic coverage also includes a linear table needing bits 29 at load
factor 0.5. The exporter respects the chained constructor's 28-bit cap and
the linear table's unsigned slot-index range independently.

The hash launcher's Linux timeout uses a monotonic deadline so interruptions
cannot restart the timeout. It kills the process group and child on timeout
and reaps the child. The harness also checks deterministic ID ordering and
that a reported resize increases bits from the initial snapshot.
The concurrent case verifies parallel insertion/count integrity, then runs
a serialized burst after joining workers to establish an exact tuning peak.

Validation completed on Linux x64 with Clang 22.1.8 and the debug configuration:

- Ran `./z.config -c -L -d /usr`, then built the complete configured module
  tree and refreshed the final source edits. A recursive make wrapper used
  `-j8` through `ztc` and `-j4` thereafter. The 12 GiB available-memory guard
  did not trigger. The successful final pass had no compiler warnings;
  make reported its expected jobserver reset when switching to four jobs.
- The first build exposed an unresolved `log_` in
  `zum/interop/zumjwtinterop.cc`. Added its missing `ZuTestUtil` using
  declaration, then rebuilt and passed `make -C zum/interop test`.
- Passed a serial focused `prove` run of `zhashtune`, `zheaptune`,
  `ZmHashTest2`, `ZmLHTest`, `ZmHashCleanup`, `ZmPolyHashTest`, `ZmWatchTest`,
  `ZtcSchemaTest`, `ZfCSVTest`, and `ZiFileTest`: 10 programs, 65 top-level
  TAP tests. The hash driver passed 3,638 checks covering all 35 cases and
  137 fresh-process generations.
- Passed `make -C zi test`: 22 unit programs (114 top-level TAP tests),
  four integration drivers (38 top-level TAP tests), and the Python,
  shell, and Go residue tests.
- Independently exercised a log-directory path containing spaces and an
  apostrophe with a `libtool exec` child prefix. Caller-owned tuning bytes
  stayed unchanged, and successful generation residue was removed.
  An unknown selector failed as intended. An injected one-second timeout
  killed the child process group, including a sleeping descendant, and
  retained the generation input and child log. The validation process
  adopted and reaped orphaned descendants; the harness reaps its own child.
- Reviewed source/install manifests and generated telemetry, confirmed no
  remaining `ZiHashCSV` source callers, and passed `git diff --check` plus
  whitespace checks on new files.

Build and test logs are `/tmp/hashtune-configure.log`,
`/tmp/hashtune-all.log`, `/tmp/hashtune-refresh.log`,
`/tmp/hashtune-focused.log`, `/tmp/hashtune-jwt-interop.log`, and
`/tmp/hashtune-zi-suite.log`; launch results are recorded in
`/tmp/hashtune-launch-status.json`. No GCC, MinGW, sanitizer, or Valgrind
run was needed or performed. Windows launch code remains untested here.
