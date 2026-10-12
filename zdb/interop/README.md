For the ZiFB adapter reorganization in `refactor.md`, retain the current build
configuration. `ZIFB_BENCH_CURRENT=1` enables the unchanged benchmark workloads
for matching before/after comparisons in the retained configuration. Compare native
ZiFB against its frozen pre-refactor sources; historical release comparisons
below retain their original scope. All builds for this refactor use `-j4`.

# Database message interoperability

## Current repair acceptance

`repair2.md` governs the current repair. Retain the existing Clang release
configuration and incremental artifacts; historical debug configuration commands
below are not instructions to change configuration.
[The final requirement and artifact audit](evidence/repair2-20261010/audit115/README.md)
records completion of the consolidated repair within the authorized current-build
scope: supported type/operation contracts, canonical interoperability, dependent
coverage, and nine matching clean memory gates. Historical sections below refer
to their original generations and do not override this completion disposition.

The approved corrected canonical baseline is `6e9a1252` plus only the scheduler
queueCount ordering guard. Long saga comparisons improve:
[corrected101](evidence/repair2-20261010/corrected101/README.md) for PostgreSQL and
[sqlite109](evidence/repair2-20261010/sqlite109/README.md) for current SQLite.
Original unmodified-baseline failures remain separate evidence.

Performance acceptance retains the small SQLite insert p50 cost of +1.24%
(316 ns paired median increment), with p95/p99 −0.43/+0.26%. It does not assert
that every operation is faster or statistically equivalent. The final audit
records the assessment, other residuals, justified copy boundaries and measurement
limits. Diagnostic timing controls do not replace the release measurements.
zhttp remains excluded and uncertified under the user's stop-building instruction;
other compilers, flags and platforms are outside this task's scope.

## Post-consolidation release checks

The rebuilt database unit suite passes 3 binaries / 84 top-level tests,
including the restored `IOBufAlloc<SagaData>` serialization path.
The release memory-store integration suite passes 4 binaries / 6 tests;
SQLite passes its unit test (5 groups) and all 7 integration targets /
52 tests, including schema/type mappings, sagas and replication.
PostgreSQL passes its unit test (9 groups) and all 3 integration targets /
16 tests against the retained local fixture, including continuation and saga
recovery paths. No backend cases were skipped.
Focused release PostgreSQL and SQLite saga Valgrind runs both report zero
errors and zero definite/indirect/possible lost blocks, without suppressions
(`/tmp/zffb-two-headers-{pq,sqlite}-saga.valgrind`). The PostgreSQL fixture
was stopped after these checks.
Zum's post-consolidation release unit suite passes 5 binaries / 35 tests.
The full `ZumTest` Valgrind run, covering the restored `SagaImage::Buf`
allocation path, reports zero errors and zero definite/indirect/possible
lost blocks without suppressions (`/tmp/zffb-two-headers-zum.valgrind`).
The first focused ZdbSagaTest Valgrind run reported 32 bytes definitely lost
and 864 bytes possibly lost in scheduler stop-callback/control-thread storage.
An unchanged rerun and two debugger-assisted exit inspections each reported
zero errors and zero definite/indirect/possible lost blocks. The pre-exit
inspection found only the main thread. The first report is consistent with
detached scheduler control-thread cleanup racing process exit, but that
specific failing schedule has not yet been captured; it is not being hidden
with a suppression or attributed to saga buffers without evidence.

Logs are `/tmp/zffb-two-headers-zdb-tests.log` and
`/tmp/zffb-two-headers-saga{,-rerun,-exit,-preexit}.valgrind`; debugger traces
are `/tmp/zffb-two-headers-saga-{exit,preexit}-stacks.txt`. Canonical interop,
remaining consumers, and release performance acceptance remain pending.

Explicitly build and run with `make -C zdb/interop -j4` then
`make -C zdb/interop test`. This target uses the owning message metadata
and links only the already-built core/I/O libraries. It does not rebuild
database/store/application binaries before their coordinated migration.

`dbmsg.fbs` is an independent canonical test fixture adapted from the captured
message contract, with the helper UInt128 declaration kept in the fixture.
Generated code and the canonical library are isolated interop dependencies.

## Initial migration evidence

The isolated target passes four top-level TAP groups (schema, records,
heartbeat, commit), including:

- canonical BFBS verification, union IDs and repeated Record alternatives;
- canonical compilation of the metadata-derived FBS span;
- native replication, heartbeat and commit read by generated canonical code;
- canonical recovery, heartbeat and commit read by native views;
- custom four-byte framing, owner handoff and frame-length overflow rejection;
- nested-row verification at ingress, trusted reads, and rejection of corrupt
  nested data even when the enclosing message remains structurally valid;
- absent optional heartbeat state and an empty union root.

Build: retained Clang debug configuration, canonical FlatBuffers 25.12.19.
No warnings were emitted. See [test output](results.txt).

`ZdbMsg.hh` and the buffer accessors have switched to native views. Existing
store/database/application callers are still being migrated as a coordinated
source batch; their builds remain deferred to M2. This target deliberately
links core/I/O libraries rather than the stale database library. No production
schema/codegen removal or full database acceptance is claimed yet.

Build regeneration/reconfiguration must use `z.config` and preserve the current
configuration. The original debug invocation is historical; do not introduce
`-d` into the retained release configuration.

## Queued consumer batch

The records case now uses `startNested`/`endNested`, `saveRecord` and `saveMsg`
in one pooled arena, matching the migrated database writer. This revision has
not yet been rebuilt or run; `results.txt` records the earlier object-save
case. Queue it with the next owning-core and interop build milestone.

The coordinated source batch also migrates heartbeat state iteration, message
reads, row save/update/delete filters, placement loading, buffer-cache views,
key serialization, selected tuple loading and saga catalog message writes.
Table and saga catalog schema ownership now use pre-sized BFBS buffers; store
signatures use borrowed native schema views. Backend field associations and
conversions, and application metadata migration, remain pending. Saga
metadata and payload load/save now use the native codec. These source changes
have not yet been built or tested.
Erased database row writers use storage alignment `Wire::MaxAlign`; root
padding follows emitted fields. Include that storage choice in the final
consumer allocation/capacity measurements.

The memory store source now uses native BFBS field views, shared native value
mappings, pooled nested writers and indexed `ZtScratch` offsets. Its store
schema handle includes a compiled native-ID resolver, used only during field
association; payload access still consumes canonical BFBS. This revision and
its explicit-ID/alternate-name associations await M2 tests. SQL backend
conversion remains pending, including adoption of the same resolver while
preserving SQL column names.

SQLite source now uses native reflected reads and result writers, including
borrowed text/bytes, mapped scalar/vector values, bitmap and IP unions,
forced alignment, and nested recovery envelopes. Existing ordered SQL blob
encoders and statement/scratch lifetimes remain in place. All SQLite mapping
and empty-value/SQL-NULL tests are queued for M2; source searches and whitespace
checks do not establish runtime acceptance. PostgreSQL conversion remains
pending. The final ownership audit must also cover backend table reuse after
close so retained field views cannot outlive their table-owned BFBS buffers.

PostgreSQL field association and input conversion now consume native views:
scalar text/bytes remain borrowed; numeric/array/bitmap/IP parameters retain
the existing PostgreSQL binary encoders, OIDs and send-scratch ownership.
Mapped inline omissions use the native field's existing default callback.
The result writer still needs conversion, and none of this PostgreSQL source
batch has been compiled or tested yet.

PostgreSQL result emission now uses native pooled builders, shared scalar and
inline layouts, and direct array conversion from PostgreSQL result spans.
The source batch removes its canonical serializer calls and changes offset
scratch to `ZtScratch`. M2 must exercise every supported result mapping before
this is accepted.

Close/reopen source review found retained store objects in all three backends.
Their reopen paths now rebind existing field records to the new caller-owned
BFBS span before reopening, including the short-lived saga catalog owner.
No schema copies or extra ownership graph were added. Queue reopen with the
previous owner destroyed for the final lifetime and memory-check gates.

Database telemetry headers and the telemetry service's payload metadata now
use ordinary `ZfStruct` declarations with native FlatBuffers table/enum tags.
Their owning libraries contain the metadata definitions. Borrowed telemetry
key assertions now target `ZiFB::View`; semantic-version transformation is a
native scalar mapping. Envelope/routing/dashboard and test migrations remain
pending, and these declarations are not yet compiled in the consumer batch.

Telemetry service producers, agent/hub routing, and filter enums have now been
migrated in source. `Msg.data` carries a complete nested `Payload` root with
its union tag; ingress verifies both spans, while trusted handoffs use views.
Hub routing uses `copyRoot` (one payload memcpy) and composes routing fields
outside the nested span. Alert replay explicitly emits its mutable sequence
slot at zero and updates only that slot in copied bytes. Dashboard, telemetry
tests and canonical envelope fixtures still need migration; none of this
consumer batch has been rebuilt or accepted. M2 must cover nested extraction,
corruption, byte-preserving forwarding, sequence mutation and alert replay.

Dashboard production request/receive code and its GTK exercise module now use
native nested envelopes, borrowed keys, and `ZiFB::handler` construction/update.
Group parsing uses the existing Z enum lookup. Data-frame table declarations
also use native metadata. These remain source-only changes queued for M2;
the obsolete dashboard schema-existence test and schemas must be removed in
the build/dependency cleanup slice.

The Zum database/saga declarations and administrative cursor/image paths now
use native metadata and serialization. Maintenance arrays use the core UDT
vector support; their five bespoke canonical-array transformers were deleted.
Saga images save through generic streaming with core scratch staging. External
image and cursor loads retain verification. The database saga and hub client
examples have also been migrated in source. These edits await the coordinated
M2 rebuild, replay/cursor tests and canonical schema checks.

Database test fixtures, saga round trips/direct-store writes, and database
telemetry checks now target native metadata, views and envelopes. The memory
store collision test allocates a replacement BFBS span before releasing the
first owner, then reopens the retained table against it. This queues an actual
schema-owner replacement case for M2; these tests have not yet been run.

The telemetry ring test client, alert/replay tests, filter tests, and Zum record
round trips have been migrated. Alert request construction now shares the
native test-client writer, and the persisted-identity corruption fixture edits
the native sequence slot directly. Existing behavioral assertions remain;
execution and canonical envelope checks are still deferred to M2.

Telemetry application tests, the Zum telemetry probe, and the hub load peer
now use native envelopes/views. Tag-dependent reads preserve the old nullable
accessor behavior with explicit alternative checks. Their integration runs
remain queued for M2, alongside the remaining hub/schema/wire and SQL-store
fixture migrations.

PostgreSQL/SQLite saga fixtures and the SQLite all-types declaration now use
native schemas and record envelopes. Direct namespace-test handles retain
caller-owned BFBS buffers through close and reconnect. The PostgreSQL IP-offset
unit check now targets the native offset/tag result. Their existing behavioral
assertions remain queued for M2; no SQL integration run has occurred yet.

Hub unit fixtures now use native envelopes, including explicit empty-device
presence and invalid group/status cases. A new payload-copy case checks exact
nested-byte preservation, independent extraction after source destruction,
retention of an explicitly emitted zero sequence slot, and rejection of a
corrupt nested root despite a structurally valid outer envelope. These checks
are prepared but have not been executed; they join the M2 gate.

The hub wire integration peer now constructs native nested envelopes and reads
native views; its request correlation, inventory, publisher, shutdown and heap
change assertions remain. Enum selection uses Z enum lookup. This peer has not
been rebuilt/run; final telemetry schema tests and build cleanup remain before
M2 execution.

Telemetry schema unit tests now exercise native defaults, empty/present fields,
framing, every telemetry alternative and value mapping, forced zero sequence
slots and corrupted union tags. The retired-envelope golden capture was replaced
by equivalent request-field assertions; it is not evidence of canonical byte
interop. New telemetry envelope/schema canonical fixtures remain required at
M2. Repository C++ consumer searches now find no legacy API uses outside zfb
and isolated interop sources; build inputs and old schemas still need cleanup.

Slice 10 build-input progress (not yet rebuilt): removed legacy zfb include/link
flags and schema-codegen rules from consumer Makefile.am files, along with 48
obsolete consumer schema sources and the dashboard schema-file existence test.
Top-level traversal/configuration no longer includes zfb. Canonical pkg-config
checks are confined to explicitly built zf/zdb interop makefiles; ordinary
configure no longer requires FlatBuffers or injects its global include flags.
The empty telemetry headers-stage target was removed; lib-stage still orders
Zum/telemetry library construction. Generated Makefiles have not been edited.
Reconfiguration remains deferred to M2 and must use `./z.config -c -L -d /usr`.

Cleanup is still incomplete: the old zfb module and fbscodegen script remain
until the benchmark comparison is detached from ZfbStruct. The benchmark must
preserve the custom IOBuilder baseline; replacing it with the default canonical
builder would invalidate the performance comparison. Telemetry canonical
fixtures, documentation/reference cleanup, regeneration, and M2 execution are
also pending. Source diff whitespace audit passed; these edits have no new
compile or runtime evidence.

Further Slice 10 progress: the benchmark now owns a frozen, isolated copy of
only the old custom IOBuilder and two schema-specific canonical adapters.
The entire obsolete zfb module, fbscodegen script and 132 ignored generated
schema headers were removed. Current source/build-input searches find no
production canonical includes/API, generated-schema include, zfb link/include,
or schema-codegen rules. README, CODEBASE, AGENTS and GUIDELINES now describe
ZiFB/ZiFB and metadata-generated in-memory schemas. Remaining legacy names in
arc/, Zum's chronological IMPLEMENTATION.md, and interop evidence are history.
Generated Autotools outputs still await M2 regeneration; no compile/link or
runtime claim follows from the source audit. Telemetry canonical fixtures and
all pending M2 gates remain outstanding.

M2 has started. `./z.config -c -L -d /usr` initially rejected inline
pkg-config --libs expressions in Automake LDADD. Replaced those expressions
with optional FB_INTEROP configure substitutions, without global flags or a
mandatory package dependency, and reran the same z.config command successfully.
Retained clang++, -march=x86-64-v2 -g -DZDEBUG, /usr, and enabled backends/GTK.
An owning-src build batch now runs in dependency order with make -j8, including
the dashboard explicitly. This is a build in progress, not an acceptance result.
The new ztc/interop fixture covers canonical request exchange, nested shutdown
copy/default-presence/mutation and full Payload BFBS/FBS export comparison.
Its source is complete but it has not yet been built or executed.

M2 source build passed zu/zm/zt/ze/zf/zi/zv/zrl/zdbus/ztcp/ztls/zquic/zhttp/
zws/zrest/zjrpc/zmcp before reaching zdb. No warnings appeared in those logs.
The first zdb build found SchemaBuf brace initialization selecting the byte
initializer-list constructor, and accidental calls to the compile-time
ZuMax trait as a runtime function. Fixed schema capacity construction across
production and tests, and used natural-versus-forced alignment comparisons
in memory/PQ/SQLite writers. The focused source batch resumes at zdb; final
consumer and runtime results remain pending.

The rebuilt libZf ELF dependencies are retained in
../../zi/interop/evidence/m2-core-deps.txt. Generated zf/src/.deps/*.Plo contains no
canonical, zfb or Zi header dependency; zi/src/.deps/*.Plo contains no canonical
or zfb dependency. This is current owning-library evidence, not a completed
whole-consumer dependency gate.

After those fixes, zdb/src built cleanly with no warnings. PostgreSQL compilation
then exposed the same initializer-list ambiguity for a ZuSpan of endian bitmap
words; its pointer/length construction now uses parentheses. Refreshed Automake
metadata through the same z.config command after registering the new ELF evidence
file for distribution, and resumed the incremental source batch at zdb_pq.
The batch is still in progress; no new runtime tests have run.

Both PostgreSQL and SQLite source libraries built successfully after the span
fix. Zum then instantiated paths missing from earlier core coverage: boolean
constructor/print fields returned wire bytes rather than bool, projected key
saving selected the underlying bare tuple's metadata instead of its projection,
and ClientGrant flags lacked an export name. Fixed boolean decoding in the
shared value trait, passed the actual Meta specialization through field-writing
helpers, and tagged ClientGrant. Added focused bool-construction and non-leading
key-projection regressions to existing zf test binaries. The affected source
batch restarted at zf; test execution is still pending.

Old unused libZdb_la-*.Plo files remain from the prior object naming scheme.
They reference deleted canonical headers but are not inputs to the current
Zdb.lo/ZdbSaga.lo build. Final dependency auditing must use current target
object lists, not treat every historical .deps file as a live dependency.

Zum and telemetry source targets now build after making the existing SemVer
mapping constexpr. Zdf then exposed ZuCmp<ZuBox>::null() returning a static
constant through a non-constexpr function. Made that shared boxed-default path
constexpr (including its local constant), preserving the same default and
reference-return contract; added compile-time assertions for boxed integer,
floating zero, and unsigned sentinel defaults in ZuBoxTest. This avoids a
separate default implementation or ad hoc Deflt overrides in ZiFB/Zdf. The
owning/dependent source batch restarts in dependency order because ZuBox is a
widely included framework header. No test or final acceptance pass is claimed.

M2 owning-source milestone: the complete incremental source batch passed
zu/zm/zt/ze/zf/zi/zv/zrl/zdbus/ztcp/ztls/zquic/zhttp/zws/zrest/zjrpc/zmcp/
zdb/zdb_pq/zdb_sqlite/zum/ztc/zdf/zgtk/zdash with no warnings or errors in the
final per-module logs. Test/example/interop targets are now building, including
the dashboard module and the separate canonical harnesses. Runtime acceptance
is still pending.

A disposable PostgreSQL 16.3 cluster at /tmp/zffb-pg.r84cgV/data is running
with TCP disabled and a private socket in /tmp/zffb-pg.r84cgV/socket. Database
zffb_m2 has uint and libz enabled, using /home/count0/postgres. The sandbox
rejected Unix socket creation/connection; approved escalated calls started and
initialized this fixture. Integration-test connection is
host=/tmp/zffb-pg.r84cgV/socket dbname=zffb_m2. Stop this owned server after its
tests and memory gate; this fixture must not be confused with an existing
user database. No PostgreSQL test has run yet.

M2 runtime progress: after zf/test completed its build, `make -C zf/test test`
passed all 21 binaries / 413 top-level TAP tests, including the new boolean
constructor and key-projection regressions. Consumer test builds continue.
ZuAxorTest's previously write-only assignment counters now assert that neither
assignment occurs, addressing the warnings reported by its rebuild; that
changed test must be rebuilt and rerun before final acceptance.

Further M2 runtime results: ZiFBTest passed all four top-level groups;
ZuBoxTest passed 192 checks; the complete Zu target passed 42 binaries / 973
checks, including the assignment-counter assertion; the complete zdb unit
target passed 3 binaries / 84 top-level tests. ZtcDBTest's migrated IP checks
now use the mapped ZiIP value/type and field presence. Replication-test
compilation found one remaining pointer dereference of a value view in JSON
logging; removed it and resumed the consumer build at zdb/itest. No backend
integration, telemetry process, canonical, performance or Valgrind pass is
implied by these unit results.

M2 database integration results (all PASS, no skipped cases):

| Target | Binaries/fixtures | Top-level tests |
| --- | ---: | ---: |
| zdb/itest | 4 | 6 |
| zdb_pq/test | 1 | 9 |
| zdb_pq/itest | 3 | 16 |
| zdb_sqlite/test | 1 | 5 |
| zdb_sqlite/itest | 7 | 52 |

The replication suite and backend suites ran outside the sandbox because local
socket operations are blocked inside it. PostgreSQL used the disposable
zffb_m2 database documented above; SQLite used its existing residue-managed
fixture setup. Updated remaining test/benchmark XField empty-view initializers
and PostgreSQL saveOffset arguments for the new reflection interface. The
consumer-build batch continues through Zum/telemetry. These are functional
results; performance, canonical consumer interop and the final Valgrind gate
remain outstanding. Retain the owned PostgreSQL server for its memory gate.


### Further M2 acceptance progress

Zum unit tests passed 5 binaries / 35 tests. Its integration suite passed
through federation; the cluster fixture then exposed a stale route-count
assertion (60 versus the current 61). After correcting that fixture assertion,
the focused cluster rerun passed, including standby rejection of all 61 routes.

Additional passing targets: telemetry unit tests (7 binaries / 28 tests),
telemetry agent/hub integration units (2 / 4), all three telemetry process
scenarios (agent provisioning, full routing/restart, cluster), data-frame unit
tests (5 / 6), data-frame SQLite persistence (1 / 1), GTK (1 / 1), and dashboard
(1 / 1). The hub initialization fixture now supplies its required SSF delivery
URL. Canonical harnesses passed zf (41 groups), zdb (4), and telemetry (4).
See telemetry's interop README for the canonical unsigned-default exporter
limitation and its parsed-value comparison.

Final memory checks completed so far use Valgrind 3.25.1 through `libtool exec`,
`--leak-check=full`, definite/indirect leak errors and `--error-exitcode=97`.
No suppressions were used. All of these have zero reported errors and zero
definite, indirect or possible leaks:

- ZiFBBuilderTest, ZiFBSchemaTest, ZiFBViewTest, ZiFBTest.
- ZdbSagaTest and ZtcDBTest.
- PostgreSQL zdbpqtest, zdbsagatest and zdbpqcontinuationtest.
- SQLite zdbsltest, zdbsagatest and zdbslrepltest.

The first saga memory run exposed a fixture use-after-free: a completion
callback could destroy the saga before the fixture accessed its semaphore
through `this`. The fixture now snapshots the semaphore pointer before
completion; its memory rerun passed. No production lifetime change or
suppression was needed. The sandboxed Valgrind startup could not acquire
loader symbols; the successful runs used escalated access.

Transport/dashboard memory gates, performance acceptance and the final
complete dependency/style audit remain outstanding. The private PostgreSQL
fixture remains running for possible consumer performance measurements and
must be stopped when those checks finish.


The first full telemetry transport Memcheck attempt did not finish. Its first
hub lifetime reported zero memory errors/leaks, but the unsanitized collector
aborted during early shutdown with `Ztls::Link::~Link` asserting `!m_txTLS`.
The fixture stops the collector immediately after the instrumented hub prints
agent acceptance. Agent::stop posts WebSocket close, then stops/finalizes the
client and multiplex without waiting for the application's disconnected
callback; investigate this lifetime boundary before changing transport code.
The failure is not a completed transport memory gate. Evidence is retained in
`/tmp/zffb-m2-ztc-mem-run.log` and the residue directory named there.

Latest build-metadata regeneration was `./z.config -c -L -d /usr`, after
registering the M2 raw benchmark evidence and renaming the isolated baseline
header to lowercase `iobaseline.hh`. It completed successfully and left
`config.h` unchanged. No alternate build configuration or clean was used.


The agent shutdown repair is now exercised: Agent::stop retains its owner
and scheduler until the Rx-owned link completes TLS/codec disconnection.
A stack semaphore is signaled through an Rx-owned pointer, then cleared;
no additional ownership cycle, allocation or lock was added. Direct transport
disconnect also handles a link whose WebSocket upgrade has not completed.
The source and telemetry tests rebuilt incrementally without diagnostics;
all 7 telemetry unit binaries / 28 tests passed again.

The full telemetry routing/restart fixture then passed with both hub lifetimes
under Memcheck: each reported zero errors and zero definite/indirect/possible
leaks, without suppressions. Additional passing Memcheck targets are
ztcmsginterop, zdbmsginterop, ZdfStoreTest and ZdfSeriesTest with the same zero
error/leak results. Dashboard memory and final performance/audit remain open.

## Release replication comparison

`./zdb/interop/zdbmsginterop --bench 100000 11` compares native nested
replication with the frozen custom canonical pooled IOBuilder. Both use
`Zdb_::IOBufAlloc<>`, construct a row root directly inside the record's byte
vector, emit the Record/Msg union, prepend the four-byte application header,
and detach/rebind the buffer. Fresh and reuse samples alternate codec order.
Native code retains the erased-row builder's supported alignment ceiling but
supplies the row/envelope's actual metadata-derived alignment; the baseline
retains its original eight-byte downward arena policy. Encoded
bytes and retained capacity are reported alongside elapsed time and checksum.
Both outer and inner roots and the independent field expectations are checked
before timing. Debug execution is rejected. The mode has been built and its
[initial release measurements](../../zi/interop/evidence/m2-release-initial/replication.csv)
showed a regression, recorded in [plan.md](../../plan.md). Refresh it against
the final rebuilt state after the subsequent codec fixes; performance
acceptance remains open. The initial measurements used the former blanket
maximum alignment; they do not measure the repair2 alignment changes.

## Verification-boundary review — 2026-10-08

The saga catalog scan now loads `TupleData` directly. All repository store
implementations create that FlatBuffer locally from decoded tuple values:
`ZdbMemStore.cc::StoreTbl::select` uses `saveTuple`, PostgreSQL's `select_save`
builds a new buffer, and SQLite's `tuple_` does the same. The extra
`verify<SagaTypeStep>` in `Table_::sagaCatalogScan` therefore repeated
verification of same-process generated data. `TupleData` now documents that
contract. Application-level catalog checks in `SagaCatalog__::load` remain.

This does not remove verification of nested opaque persisted payloads:
`SagaCatalog::load` still verifies the selected saga definition, and
`SagaImage::load` verifies replayed images. Replication ingress verifies the
message envelope; `rowRecover` verifies its typed nested row. Locally generated
BFBS in table/catalog constructors goes directly to borrowed schema views.
Diagnostic printing retains its verification boundary because its public
input may be corrupt. Telemetry verifies its envelope and nested payload at
network/shared-ring ingress; the ring is a cross-process boundary.

The changed database header rebuilt successfully in the current incremental
Clang release top-level batch (`/tmp/zffb-current-refresh-build.log`), without
compiler diagnostics. Refreshed memory-store unit/integration checks pass
3/84 and 4/6; PostgreSQL passes 1/9 and 3/16; SQLite passes 1/5 and 7/52,
including all three stores' saga coverage. Logs are
`/tmp/zffb-current-{zdb,pq,sqlite}-{tests,itests}.log`. The private PostgreSQL
fixture was stopped after the checks. Performance and final memory acceptance
remain open.

## Direct record reads and owning-consumer control

`./zdb/interop/zdbmsginterop --reads 1000000 7 32` (and payload4096)
compares canonical generated accessors with `ZiFB` borrowed views over the
same canonical-produced Record buffer. Both read the same metadata, payload
length and final payload byte. A third mode constructs an owning C++ Record
consumer, using the saga's retained `SagaPayload` type, before reading those
fields. This mode deliberately measures full materialization; it does not claim
that production envelope readers currently materialize that object.

Input construction and both envelope verifiers run before timing. Allocation
snapshots surround each round outside timing; mode order reverses on alternate
rounds. CSV reports elapsed time, observed Z heap allocation requests, checksum
and logical payload bytes materialized. The latter excludes string copies,
growth and allocator movement and is not a measurement of all memory traffic.
Compiler barriers prevent reuse of constant field reads across iterations.
The serialized owner remains alive through every reader. These are diagnostic
reader controls, not complete-saga acceptance or a replacement HEAD workload.
Release configuration is required. Build and execution are recorded in
`evidence/repair2-20261010/read24/`. The corrected noncapturing heap observer
reports zero Z heap requests for both borrowed readers. Z heap telemetry
does not include `ZmVHeap` jumbo allocations; zero in the 4096-byte owning
control therefore does not mean allocation-free materialization. Original CSV
files retain the capturing observer's one extra allocation per round; files
ending in `-observer.csv` use the corrected observer. Timing varies between
these builds, so the small accessor difference is not regression attribution.

## Saga row buffer growth

`./zdb/interop/zdbmsginterop --saga-buffers 100000 7 32` also accepts payload
sizes24 and4096. This diagnostic compares the actual native `SagaData` row
layout with an independent canonical schema, nested in Record/Msg using pooled
owners. It reports growth calls, backing allocations, front/back bytes moved
and in-place moves, for fresh and reused owners. Initial owner allocations are
separate from growth allocations; input preparation and verifiers are outside
timing. Local observers include jumbo backing allocation paths. They add work
on growth, so their timing is diagnostic.

`evidence/repair2-20261010/growth25/` records the measurements, slow-path
outlining comparison, owning builds, TAP acceptance and matched complete-saga
results. This row does not show excess native backing allocations. The codec
improvement does not close complete-saga performance acceptance.

Latest postgres runtime control: [restart99](evidence/repair2-20261010/restart99/README.md)
retains the existing original/current workload and artifacts, restarting the owned
server before every process. Large select median paired p50/p95/p99 differences
are+0.15/+0.42/+0.28%; the earlier shared-uptime discrepancy did not reproduce.
Both scopes remain recorded. Long saga baseline and broad performance acceptance
remain open; no production rebuild or extra memory gate was required.

[Consolidated performance evidence](evidence/repair2-20261010/report100/README.md)
reports codec/store/saga latency, tracked requests, payload sizes, failures and
remaining limits with reproducible input hashes. It does not claim completion.

Approved corrected canonical long saga comparison: [corrected101](evidence/repair2-20261010/corrected101/README.md). Both stores improve saga latency; SQLite ordinary update tails remain reported separately.

Current completion audit: [audit104](evidence/repair2-20261010/audit104/README.md). Remaining performance attribution: [residual103](evidence/repair2-20261010/residual103/README.md).
