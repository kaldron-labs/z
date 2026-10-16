# `ZdbSaga` implementation plan

## Journaling rule

All saga mutations are journaled. A new insert, update, or effective delete
records its intended target UN and immediately performs the ordinary Table
operation in the same shard turn. Recovery uses the existing exact-UN
overload, skipping previously performed effects without consuming a new UN.
Non-saga callers use ordinary `Table::insert` with the idempotent collision
policy prescribed below; they do not journal saga steps.

An already-absent delete records a `SagaStep` with `un == nullUN()`.
This is an explicit no-op marker, valid only for a declared Delete; it does
not reserve or consume a target UN. Replay advances from that marker without
looking up or deleting a replacement row. Its journal row is cleaned normally.

## Review result

Implement [`goal.md`](goal.md) without introducing a second subsystem beside
`Zdb_::DB`.  The smallest design that satisfies the goal is:

- `SagaDB<Sagas> : DB`, with the saga bundle fixed by the DB's C++ type;
- two private, ordinary front-end tables owned by a saga-enabled `DB`: `saga`
  and `saga_step`;
- one startup-only back-end table, `saga_type`, opened directly by `DB`,
  reconciled, and closed;
- one activation-only recovered-attempt hash/FIFO plus two recovered-step
  indexes, all destroyed before steady state;
- a monomorphic, type-erased `MSaga<Sagas>` carrying the application intent;
- unrestricted application orchestration whose reads and other work are
  replayed, plus saga mutation variants which journal every mutation; and
- the existing exact-UN CRUD APIs and explicit absent-delete markers.

Do not add `SagaMgr`, a service object, observers, a virtual mutation layer, a
run-time saga-type registry, a retained copy of `saga_type`, per-table saga
state, a durability acknowledgement protocol, compensation, background
polling, or recovery spill files.  These elements add state or indirection
without satisfying another requirement in the goal.

This audit removes four implementation artifacts which do not serve the goal:

- successfully loaded attempts enter the recovery hash during the authoritative
  main scan, and that same hash answers orphan-step membership after end-of-
  results; no second main-key index is needed;
- no saga work or private-table open when `SagaDB<Sagas>::sagas()` was not called;
- no public scan-size macro: a private named page constant is sufficient for
  the existing `selectRows()` API; and
- no extra state/result object for admission, mutation, replay, or completion
  beyond the types expressly listed below.

The implementation now includes DB-wide shard dispatch, the three-parameter
exact-UN `OpResult` API, the `internal` store-open argument, and distinct
selection error/EOR callbacks. The phases below describe the implementation
requirements; their acceptance evidence is recorded at the end of this plan.

This is an intentional new and breaking API.  Propagate changes to dependents;
do not add default arguments, deprecated overloads, forwarding functions, or
migration code.  This follows the repository's integrity, latency, simplicity,
and no-compatibility priorities in
[`GUIDELINES.md:9-46`](GUIDELINES.md#L9).

## Invariants to preserve in every phase

1. A saga belongs to one `DB`; it may mutate any tables and shards in that DB,
   but cannot span DBs.
2. The saved FlatBuffer is conceptually the capture pack of a replayable
   lambda.  The application saga may call `select*`, `count`, `find*`, external
   services, or any other code; recovery reloads the capture pack and
   re-executes that work.  The framework saves it once at admission and does
   not persist later in-memory changes to the application saga object.  Zdb
   neither constrains that object's mutability nor determines whether the saved
   fields are sufficient; the application must save everything required to
   replay the saga according to its own semantics.
3. The application must make the saga body replayable and idempotent from its
   saved capture pack, including reads, branches, and external effects.
   Every saga mutation is journaled; recovery uses exact-UN CRUD or a recorded
   absent-delete no-op. The app owns payload compatibility, its opaque context,
   and its lifetime. Zdb provides no snapshot or isolation for replayed reads.
4. Application code may yield between steps and during preparation. Resolve
   yielding finds before journaling. No post, allocation, or unrelated work
   intervenes between a step-intent commit and its protected Table operation.
5. Both commits are local cache/enqueue commits, not physical-store barriers.
   A new insert still commits locally when its backend later ignores a key
   collision. An absent delete commits its marker but no target operation.
6. New effects use ordinary CRUD immediately after their step intent. Saved
   effects use exact-UN CRUD during recovery; only recovery rotates NotReady.
7. `Missing`, `Invalid`, a full FIFO rotation with only `NotReady`, an unknown
   recovered type, or a catalog conflict is fatal to the current startup or
   activation and follows the existing DB lifecycle failure path; it is not a
   new saga result state.
8. Completion commits deletion of the main saga row before deleting its step
   rows.  Re-use of `(type, id)` after completion is permitted.

The critical pair implements thread ownership and handoff directly; it must not
be made safe with a lock.  See
[`GUIDELINES.md:266-305`](GUIDELINES.md#L266),
[`GUIDELINES.md:440-489`](GUIDELINES.md#L440), and
[`GUIDELINES.md:598-677`](GUIDELINES.md#L598).

## Names and files

Use the names below consistently.  Do not introduce synonymous `Journal`,
`Manager`, `Registry`, `RecoveryContext`, or `ActivationScanner` types.

| Purpose | Prescribed name |
|---|---|
| 128-bit attempt identifier | `ZdbSagaID` |
| persisted type discriminator | `ZdbSagaType` |
| opaque saved bytes | `SagaPayload` |
| `saga` row | `SagaData` |
| `saga_step` step-intent row | `SagaStep` |
| `saga_type` row | `SagaTypeStep` |
| persisted operation enum | `SagaOp` |
| compile-time step descriptor | `ZdbSagaStep_` |
| numbered step declaration macro | `ZdbSagaStep` |
| derived descriptor list | `SagaSteps<S>` |
| one-shot step completion | `SagaStepComplete` |
| type-erased base/context | `Saga`, exported as `ZdbSaga` and held by `ZmRef` |
| monomorphic union wrapper | `MSaga<Sagas>`, exported as `ZdbMSaga<Sagas>` |
| typed saga-enabled DB | `SagaDB<Sagas>`, exported as `ZdbSagaDB<Sagas>` |
| DB-local saga lifecycle enum | `SagaState` |
| submission result/callback | `SagaSubmitResult`, `SagaSubmitFn` |
| application event callback | `SagaHandler` |
| recovered-attempt key | `SagaKey` |
| activation attempt index/node | `SagaHash`, `SagaNode` |
| activation step key | `SagaStepKey` |
| activation target-UN reservation key | `SagaUNKey` |
| activation step indexes | `SagaStepHash`, `SagaUNHash` |
| one recovered-step/index node | `SagaRec` |
| activation scan context | `SagaScan` |
| private scan-page limit | `SagaScanSize` |
| public handler setup/submission | `SagaDB<Sagas>::sagas`, `SagaDB<Sagas>::saga` |

All are at most 28 bytes.  Public names use the existing `Zdb` prefix only
where they are exported vocabulary.  Private class members use `m_`; public
aggregate fields do not.  Private control-thread and shard-thread entry points
use a trailing underscore.  Prefer existing verbs such as `open`, `load`,
`save`, `run`, `start`, `stop`, `insert`, `update`, and `del`.
See [`GUIDELINES.md:534-596`](GUIDELINES.md#L534) and
[`GUIDELINES.md:679-781`](GUIDELINES.md#L679).

Add only these saga-specific files:

- `zdb/src/ZdbSaga.hh`: public row types, compile-time union dispatch,
  `Saga`/`MSaga`, and dependent operation templates;
- `zdb/src/ZdbSaga.cc`: non-template saga definitions;
- `zdb/src/fbs/zdb_saga.fbs`;
- `zdb/src/fbs/zdb_saga_step.fbs`;
- `zdb/src/fbs/zdb_saga_type.fbs`; and
- `zdb/test/ZdbSagaTest.cc`, `zdb/itest/zdbsagareptest.cc`, and
  `zdb_pq/itest/zdbsagatest.cc`, with integration-test documentation.

`ZdbSaga.hh` must be self-contained and included by `Zdb.hh`; it must not
include `Zdb.hh` back.  Forward-declare `DB`, `Table<T>`, and `Object<T>` and
keep table-dependent templates dependent until instantiation.  Put non-template
definitions in `.cc`; do not make the module header-only.  Update `Makefile.am`
and generated/install header lists in the same change.
See [`GUIDELINES.md:62-99`](GUIDELINES.md#L62) and
[`GUIDELINES.md:503-532`](GUIDELINES.md#L503).

## 0. Finish prerequisite work

### DB-only shard dispatch

Delete `AnyTable::{run,invoke,invoked}` and propagate the breaking change
through `zdb`, its tests, and `zdb_pq`.  A caller that has a table obtains its
`DB *` once and calls `DB::{shardRun,shardInvoke,shardInvoked}`.  Assertions in
table mutation code use `db()->shardInvoked(shard)`.  Do not preserve a table
forwarder.

This closes the gap between the accepted DB-wide sharding design and the
current tree, and prevents saga code from implying that tables own shard
schedulers.  It follows the rules against compatibility shims, duplicated
surfaces, and redundant forwarding in
[`GUIDELINES.md:116-130`](GUIDELINES.md#L116) and
[`GUIDELINES.md:266-305`](GUIDELINES.md#L266).

### Ordinary idempotent insert

`Table::insert` means idempotent insert. Preserve its existing signature,
on-shard callback, and synchronous local `commit()` contract. The application
may construct the row inside the callback using data obtained on that shard;
do not require the key or row to be initialized before calling `insert`.

An insert commit accepts the supplied row into local state and enqueues
replication and backend writing. The backend silently ignores a primary-key
collision, leaving the durable row unchanged. The saga advances after the local
commit returns; it does not wait for collision detection or backend durability.
There is no duplicate-specific null callback, presence lookup, comparison of
non-key values, rollback of cached data, or saga-layer `Skipped` result.

The application must ensure that repeated inserts supply data consistent with
the existing durable row. The cache may contain the newly supplied data even
when that particular insert is never written durably. Zdb does not validate or
repair an application violation of that contract. Other backend errors retain
the existing failure path.

Implement primary-key collision suppression in both memory and PostgreSQL
stores. PostgreSQL uses `ON CONFLICT` on the primary-key fields with
`DO NOTHING`; create logical key 0 as a unique index and validate its uniqueness
on open. Existing non-unique key-0 indexes require an explicit schema change;
do not add automatic migration. Do not suppress unrelated constraint errors.
Preserve the accepted
UN/SN position even when no durable row carries the ignored insert's UN, using
the existing MRD bookkeeping in PostgreSQL. Test that collisions preserve the
durable payload and row cardinality, acknowledge success, and preserve the stream
position across reopen. Also test saga replay through an already-durable insert.

### Exact-UN behavior

Lock down the existing exact-UN overloads before depending on them.
Saga recovery uses all three exact-UN mutation overloads. When an insert's
initializer is not invoked, the saga wrapper default-constructs its contained
T before releasing the unused object. This exceptional recovery path is not
latency sensitive. Preserve Object_'s unconditional destructor and unchanged
ptr()/data() accessors; add no construction flags or accessor side effects.
Ordinary exact-UN callers retain responsibility for their object storage.
Test that:

- `nextUN < intendedUN` returns `NotReady` without lookup or mutation;
- equality executes exactly at `intendedUN` and returns `Executed`;
- `nextUN > intendedUN` returns `Skipped` without invoking the body;
- equality with no update/delete target returns `Missing`; and
- a null UN or invalid object transition returns `Invalid`.

Each callback also reports the observed `nextUN`.  Do not add waiter, parking,
or result-wrapper types; the existing three callback parameters are sufficient.
The tests must run on actual shard threads with the mock store.

### Authoritative scan errors

Do not treat a store `Event` as successful end-of-results in `Table::select_()`.
That could make activation accept a partial scan. Log the event, schedule the
existing `DB::fail()` path, then
deliver the terminal empty callback only to drain the request.  The activation
scanner accepts EOR only when its epoch is current and activation remains
healthy.  This is a front-end correctness repair; do not change
`StoreTbl::select()` or any back-end API.  Add the raw `Table *` owner to the
existing `Select` context, as `Find` already does, rather than capturing a
reference-counted owner.

Likewise, `Table::retrieve_()` must not abandon its lookup continuation on a
store error. Run `DB::fail()` on the control thread before posting the terminal
null-object callback to the lookup's shard. This invalidates recovery before
the saga wrapper resumes and lets it retire counted lookup work without
journaling a new effect. Keep existing callback signatures. Test an injected
backend lookup error during replay: startup fails and drains, then a retry
without the injected error recovers the unchanged saga to completion.

These changes use the existing result sentinel and failure path instead of new
state.  See [`GUIDELINES.md:307-319`](GUIDELINES.md#L307),
[`GUIDELINES.md:338-369`](GUIDELINES.md#L338), and
[`GUIDELINES.md:598-677`](GUIDELINES.md#L598).

Acceptance: all existing `zdb`/`zdb_pq` tests pass; `rg` finds no table shard
dispatch surface; the new exact-UN tests cover all five results; a forced select
error cannot be mistaken for EOR.

## 1. Define the persistent vocabulary

At the top of `ZdbSaga.hh`, adjacent to and below comparable defaults, add
exactly:

```c++
#ifndef ZdbSaga_BuiltinSize
#define ZdbSaga_BuiltinSize 256
#endif
```

`SagaPayload` is an unparameterized `ZtBuiltin` using this compile-time size.
Admission saves directly into the constructed journal object's payload and
posts only object/attempt handles to its shard. Recovery decodes the scan row
into the application's union alternative, without retaining the serialized
payload in the attempt. The builtin size is the one sanctioned
fixed common-case capacity; payloads above it spill to the named variable-size
heap.  It is the admission-time recovery capture pack, not a framework-managed
progress snapshot.  Do not make the loaded application alternative `const` or
add automatic re-save/version tracking; transient mutations and their loss on
recovery are application concerns.

Define the three row types exactly as required by `goal.md`:

- `SagaData`: key `(type, id)`, value `(shard, data)`;
- `SagaStep`: key `(type, id, step)`, value `(shard, un)`; and
- `SagaTypeStep`: key `(type, step)`, value `(table, op)`.

`SagaStep` records every declared mutation. Its `un` is the intended
non-null target UN for Insert, Update, and effective Delete. Only Delete may
use `nullUN()`, recording a no-op observed after resolving the lookup.
Activation indexes all logical steps, but excludes no-op markers from the UN
reservation index. It rejects null UNs for Insert/Update.

Also define the small process-local API types:

- `SagaSubmitResult = ZuUnion<void, ZeException>`;
- `SagaSubmitFn` on heap `Zdb.Saga.SubmitFn`; and
- aggregate `SagaHandler` containing the raw application context plus
  `doneFn` and `errorFn`, both with side-effect-free no-op defaults.

Do not add a `SagaSubmit` enum or an `Accepted` attempt state.  The void result
means only that the main `SagaData` row committed locally and its
replication/store work was enqueued.  `Inactive`, duplicate key, invalid shard
or uninitialized union, and save failure are distinct `ZeException` diagnostics returned
before any main row commits.  The public `bool SagaDB<Sagas>::saga(...)` separately retains
the normal scheduling meaning: false means the DB-thread post was not queued
and the callback will not run.

Use `IDString` for `table`; do not add another owning table-ID type.  Use
`uint128_t`, fixed-width persistent integers, and a signed `int8_t` `SagaOp`
with stable `Invalid`, `Insert`, `Update`, and `Delete` values.  Use `ZtEnum`
only because `SagaOp` is persisted and needs external-name conversion; do not
use `enum class`.

Arrange C++ members after measuring alignment and `sizeof`, without changing
the FlatBuffers field numbers or composite-key order.  Aggregates are structs
with public fields and no `m_`.  Supply `ZfbStruct` and `ZfbRoot` metadata for
all three; use generated FlatBuffers code only for the existing Zdb record
envelope, not for row save/load.

Use the goal's named heaps:

- `Zdb.Saga` and `Zdb.Saga.Buf`;
- `Zdb.Saga.Data` for payload spill storage;
- `Zdb.Saga.Step` and `Zdb.Saga.Step.Buf`; and
- `Zdb.Saga.Type.Buf`.

Ancillary state uses descriptive suffixes in the same hierarchy, including
`Zdb.Saga.Hash`, `Zdb.Saga.StepHash`, `Zdb.Saga.UNHash`, `Zdb.Saga.Rec`,
`Zdb.Saga.Node`, `Zdb.Saga.Locs`, `Zdb.Saga.Scan`, and
`Zdb.Saga.SubmitFn`. Give every
hash/list/function allocation its own
`Zdb.Saga.*` heap ID at the declaration.  Do not create a heap per saga type.
Heap allocation and payload spill remain observable and tunable under these
fixed IDs.

This phase follows the integer/enum rules in
[`GUIDELINES.md:132-185`](GUIDELINES.md#L132), capacity and member-layout rules
in [`GUIDELINES.md:211-250`](GUIDELINES.md#L211), byte-container rules in
[`GUIDELINES.md:371-426`](GUIDELINES.md#L371), and FlatBuffers rules in
[`GUIDELINES.md:432-438`](GUIDELINES.md#L432).

Acceptance: `ZdbSagaTest` round-trips all fields and keys, all enum values,
128-bit IDs, and payloads just below, at, and above the builtin size.  The spill
case uses the named heap and serializes identically after reload.

## 2. Namespace Zdb-internal relations

Make the sole back-end interface change:

```c++
virtual void open(
    bool internal,
    IDString id,
    ZfVFieldArray fields,
    ZfVKeyFieldArray keyFields,
    const reflection::Schema *schema,
    IOBufAllocFn,
    OpenFn) = 0;
```

The argument is non-defaulted.  Propagate it through the memory, mock, and
PostgreSQL stores and every call site.  Add an immutable `internal` bit to
`AnyTable`; ordinary `DB::initTable()` sets false and the two private saga-table
construction paths set true.  Do not expose this through `TableCf`.
Reject `saga`, `saga_step`, and `saga_type` as application table IDs even on a
store whose physical namespaces would prevent collision.

For PostgreSQL, one private function emits the fully qualified relation name:
`"zdb"."name"` for internal tables and the current application relation for
ordinary tables.  Use it at every table, index, discovery, count, select, find,
recovery, CRUD, and MRD SQL site.  Namespace is also part of:

- the `StoreTbls` key;
- prepared-statement identity, within the existing `IDSize_` suffix budget;
  and
- the `zdb.mrd` table identity.

Retain immutable `m_internal` in `StoreTbl` and precompute the qualified
relation name once for SQL emission; do not allocate or reformat it per query.
Include one compact application/internal marker in the existing prepared-
statement and `zdb.mrd.tbl` identities.  Count that marker as part of the
existing `IDSize_` suffix budget and add boundary tests for a maximum-length
logical ID.  The `StoreTbls` key axor compares `(internal, logical id)`
directly; another owning ID class is unnecessary.

Keep the logical `IDString` and replication table ID unchanged.  The existing
`zdb` schema, `zdb.meta`, and `zdb.mrd` are already created by store startup;
the saga relations are created on demand, so `StartState` needs no saga-
specific phase.  A store without physical schemas still includes `internal`
in its open-table identity so two namespaces cannot alias accidentally.

Do not change the `StoreTbl` virtual API, add a saga store result, or make the
backend aware of saga semantics.  Its PostgreSQL implementation necessarily
retains the namespace bit and derived SQL identities, but exposes the same
table operations.  The internal bit is relation placement, not a new storage
protocol.  Centralized SQL generation avoids repeated conditional formatting
and follows [`GUIDELINES.md:266-305`](GUIDELINES.md#L266); the intentional
breaking propagation follows [`GUIDELINES.md:116-130`](GUIDELINES.md#L116).

Acceptance: a PostgreSQL integration test opens the same test row type in both
namespaces and proves independent DDL, discovery, prepared statements, MRD,
CRUD, close, and reopen behavior.  Existing application tables retain their
current location.  Run this test against the live server with
`PATH=/home/count0/postgres/bin:$PATH` and
`PGDATA=/home/count0/postgres/data`.

PostgreSQL table close retains prepared statements on the connection. Track
this with the existing open-state machine: a successful close permits reopen
at the count/UN discovery phase, without preparing duplicate statement names.
Connection close resets table open states so the next connection performs
normal discovery and preparation. No new store API or per-query flag is needed.
Reset discovered sequence positions on open and merge MRD values with explicit
null-sentinel handling, including when the main table is empty. Newly created
MRD entries use null UN/SN, not zero, because zero is a valid operation position.
Verify MRD after finalizing and recreating the store, not just handle reopen.

## 3. Build static saga dispatch and reconcile `saga_type`

### Compile-time erasure

Follow the `ZrestClient.hh` `MReqBuilder` pattern:

```text
Saga
  <- MSaga<Sagas>
       using Union = GetUnion<Sagas>;
       Union u;
```

`GetUnion<Sagas>` produces `ZuUnion<void, ...>`. On load, `ZuMatcher` selects
the compile-time type and `ZuSwitch` constructs that concrete type with
`u.new_<S>()`; never copy the matcher index into the union discriminator because
the leading `void` changes its numbering. On save, `u.cdispatch()` obtains the
concrete `S::Type` and `ZfbStruct`. Mutable `u.dispatch()` narrows the definition
and a second `ZuSwitch` selects its current numbered step overload.

Each application saga declares `using Type = ZuStringT<"orderNew">`,
`enum { NSteps = N };`, one `ZdbSagaStep(i, table, Op)` overload for every
number in `[0, N)`, and `ZfbStruct` load/save support. A step is application
orchestration, not a restricted mutation DSL: reads and other work do not
consume a step and replay repeats them.

The macro expands to a SFINAE-selected `operator()<i>` returning the empty
descriptor `ZdbSagaStep_<ZuStringT<"table">, ZdbSagaOp::Op>`. Spell its
operation constant as `enum { Op = Op_ };`. `SagaSteps<S>` maps the overload
return types over `ZuSeqTL<ZuMkSeq<S::NSteps>>`; missing numbered overloads and
invalid descriptors therefore fail at compile time. The returned value is
ignored during execution. Do not retain `U::Steps`, a shorthand typelist,
compatibility alias, counter, registry, manual vtable, virtual dispatch,
`std::variant`, RTTI, or runtime factory.

Use `SagaDB<Sagas> : public DB` for saga-enabled applications; ordinary
applications continue to use `DB`. The derived DB knows `MSaga<Sagas>`
throughout startup, admission, and replay. Keep bundle-dependent catalog
enumeration, matching, loading, saving, and execution in typed code. There is
no runtime descriptor identity check: `SagaDB<Sagas>::saga` accepts only
`ZmRef<MSaga<Sagas>>`.

The non-template `DB` retains scheduling, private tables, journal mutation,
queue/index ownership, and lifecycle machinery. Cross that boundary with
typed continuation lambdas using existing `ZmFn` patterns. Keep the typed
receiver/capture alive logically through the DB lifetime, with a raw owner
pointer and normal drain discipline. In particular, completion of a generic
mutation must resume the typed union runner; do not recreate a per-operation
function table, erase the bundle prematurely, or add virtual forwarding
methods. A continuation is a workflow boundary, not a separate callback for
each union accessor. The ordinary DB fast path has no saga callbacks installed.

Define `SagaDB<Sagas>` after `DB` in `Zdb.hh`; `ZdbSaga.hh` contains
the union/row vocabulary without including `Zdb.hh` back. Keep genuinely
non-dependent work in `.cc`; only bundle-dependent control belongs in the
template. Use narrow friendship/access declarations, not public internal APIs.

Use SFINAE/detector traits for diagnostics.  Do not use `concept` or
`requires`.  Keep non-dependent declarations and code outside templates, and
instantiate one dispatch body per `Sagas`, not per submission.

See [`GUIDELINES.md:116-160`](GUIDELINES.md#L116),
[`GUIDELINES.md:199-209`](GUIDELINES.md#L199), and
[`GUIDELINES.md:400-426`](GUIDELINES.md#L400).

### Registration and direct catalog access

`SagaDB<Sagas>::sagas(SagaHandler)` is a once-only setup call after inherited
`DB::init()` and before inherited `DB::start()`. The bundle is already fixed
at compile time; this call installs the application context/handlers and typed
lifecycle continuations. A second call or wrong lifecycle state is a
configuration error. Use the existing nullable callback/container sentinel,
not an extra initialized flag. Unconfigured/ordinary DBs construct no saga
tables or recovery state. Preserve the regular `start()`/application `up`
sequence; add no separate saga-start operation.

During `DB::start()`:

1. validate every registered step's table ID resolves in this DB;
2. open `saga_type` directly with `Store::open(true, ...)`;
3. stream rows in `(type, step)` order and compare each registered type as one
   group against the compile-time definitions;
4. derive the shard-zero next UN from the direct open result's last-UN array,
   feed its max SN into `DB::recoveredSN()`, and insert an entirely absent
   registered type sequentially using the ordinary Zdb record envelope,
   `ZfbStruct::save`, `saveHdr`, shard zero, monotonically allocated UNs, and
   `DB::allocSN()`;
5. reject a partial type, different step count, or any table/op mismatch;
6. allow historical database types absent from this build; and
7. close the back-end table on every success or failure path and retain none of
   its rows.

At most the current type/step cursor and one `ZtArray<uint8_t>` seen vector are
retained.  Put the cold vector on a named heap.  Stream the stored rows and
write missing definitions through the existing asynchronous store callbacks;
do not build a complete database or compile-time catalog in memory.

A crash cannot expose a valid partial new type as success: the next startup
sees the partial group and fails.  The operator deploys a build using a new
type name or removes the bad rows before retrying.  Do not add rollback or
repair logic.

This uses compact sentinels and framework containers per
[`GUIDELINES.md:307-336`](GUIDELINES.md#L307) and direct asynchronous
continuations per [`GUIDELINES.md:598-677`](GUIDELINES.md#L598).

Acceptance: compile-time tests reject duplicate type names, empty/invalid step
definitions, and unsupported signatures.  Mock-store startup tests cover an
empty catalog, exact match, historical types, whole-type insertion, partial
groups, and every mismatch.  They also prove the direct table is closed and no
catalog container survives startup.

## 4. Add DB-owned state and activation scans

### Direct ownership

Add direct private members to `DB`; do not hide them behind an allocated state
object:

- private `TableCf` values and one `ZmRef<Table<SagaData>>` plus one
  `ZmRef<Table<SagaStep>>`;
- `SagaHandler` and the minimal typed workflow continuations installed by
  `SagaDB<Sagas>`;
- `SagaState::T`, following the `ZmEngineState` namespaced-enum pattern, with
  only `Inactive`, `Rebuilding`, and `Active`;
- an activation epoch, one pending-continuation count, and one steady-state
  live-saga count used only to drain graceful shutdown;
- an activation-only `SagaHash`, keyed by `SagaKey`, which is the
  authoritative recovered main-key set after a successful activation scan;
- lazily allocated, activation-only `SagaStepHash` and `SagaUNHash` indexes
  for recovered steps; and
- one activation FIFO whose link is embedded in each recovery-only `SagaNode`,
  plus one raw `Saga *m_sagaBlocked` no-progress marker.

Do not add `Draining`; DB shutdown state already rejects admission.  Keep saga
state active until admitted work drains, then set it inactive.  Do not add
locks or atomics: all container and lifecycle mutation belongs to the DB
control thread.  Shard continuations carry compact values and return results
to that thread.  Hold the allocating hash containers by nullable `ZmRef` and
construct all three only on entry to `Rebuilding`. Destroy the recovery indexes before
`Active`.  This preserves the no-saga fast path without inventing an allocated
manager/state aggregate.  Wrap each container object in the established
`T_<Heap>`/`ZmHeap` pattern as well; use `Zdb.Saga.Hash`,
`Zdb.Saga.StepHash`, and `Zdb.Saga.UNHash` rather than allocating the container
objects with global `operator new`.

Allocate each concrete `MSaga<Sagas>` through the `Zdb.Saga` heap using the
established `T_<Heap>`/`ZmHeap` pattern; do not rely on global `operator new`.
Keep `Saga` free of hash/list intrusions in steady state. During exceptional
recovery allocate one `SagaNode` from `Zdb.Saga.Node`; it owns the recovered
`ZmRef<Saga>` and stacks the owning hash node with the shadow FIFO link. The
additional recovery allocation removes both links from every normally admitted
saga and every live hot-path hash operation. Allocate one
`SagaRec` per recovered step from
`Zdb.Saga.Rec`; stack its two hash intrusions so the logical-step index is the
sole owner and the UN-reservation index is shadow/`HeapID<"">`.  Always remove
the shadow reservation before deleting the owning logical node.  Make the FIFO
link shadow as well: unlink it before deleting the owning recovery-hash node.
`SagaRec` holds only a raw back-pointer to its owned saga.  Its logical-key
axor reads type/ID through that pointer and adds the stored step; its
reservation key stores the resolved `AnyTable *`, shard, and UN, plus the resolved operation for recovery validation. Do not copy
type/table strings into the node or add a second main-row node or hash.

Use `ZmHash`/`ZmList`, named heaps, and built-in key hashes.  `Saga` is
`ZmPolymorph`/`ZmRef` because references cross scheduled callbacks;
subordinate contexts hold raw `DB *`.  DB drains its internal contexts before
destruction, and the application drains saga handles held by its own
continuations under the registration lifetime contract.  Measure the concrete
node layout and heap sizes; do not replace it with a non-intrusive
`ZmHash<ZmRef<Saga>>` whose separately allocated hash node could not also hold
the FIFO intrusion.

Keep one copy of attempt state in `Saga`: raw `DB *`, `SagaKey`, main-row shard,
activation epoch, current step, and step-shard array. `SagaKey` stores a
non-owning span to the concrete definition's compile-time type string; its
lifetime is static. Keep the separately owning `SagaCursor` only in the
asynchronous activation scanner, where a page-row string cannot safely be
borrowed.
The DB's derived type fixes the bundle; keep no descriptor pointer per attempt.
`MSaga<Sagas>` adds only its `ZuUnion`; do not retain the serialized
`SagaData`, payload bytes, journal `ZdbObject` references, a per-step state
vector, or a second copy of type/ID.  Completion resolves its private rows by
key and recorded shard.  Order the concrete members by measured alignment
while keeping immutable fields before mutable control-thread-owned fields.

For each attempt, allocate one contiguous `ZtArray<ZdbShard>` sized to the
declared step count from `Zdb.Saga.Locs`. Array position is the step number;
no compact update ordinal or reverse mapping remains. Leave slots uninitialized
until their intents or no-op markers exist. Admission allocates this array
before the main row commits. Recovery fills scanned positions, and execution
fills missing ones as it journals them. Successful completion proves all slots
are filled before cleanup. Live state retains only each step's journal shard,
not target UNs or row references.

Measure member order and node size. No DB-owned container grows with admitted
live sagas during normal steady state. Each application-owned saga retains only
its exactly-sized step-shard array. Recovery structures are bounded by
crash-residual rows, which were live before the crash, and are destroyed before
`Active`.

See [`GUIDELINES.md:211-250`](GUIDELINES.md#L211),
[`GUIDELINES.md:338-357`](GUIDELINES.md#L338),
[`GUIDELINES.md:408-484`](GUIDELINES.md#L408), and
[`GUIDELINES.md:645-677`](GUIDELINES.md#L645).

### Private tables and scanner

When the installed saga continuation is non-null, construct the two private configurations before
`start()` and add the two table objects to the existing `m_tables` machinery.
That container is required for open/close, DB state, replication lookup, and
recovery; a second internal-table container would duplicate those paths.
“Private” means the DB constructs/configures them and their IDs are reserved,
not that core Zdb iteration must hide them.  Begin catalog reconciliation and
scanning only after the private and application tables are open, so every
compile-time table ID can be resolved.  The private tables use
`CacheMode::Normal`, the DB shard/SID map, and the `internal` store flag; there
is no second lifecycle or scheduler.  If the installed saga continuation is null, construct and
open none of the three saga relations.

Use the existing `selectRows<0>()`/`nextRows<0>()` APIs internally.  Add one
scanner context holding the epoch, cursor, current page, and pending count.
Typed continuation lambdas capture the raw `SagaDB<Sagas> *`; do not duplicate
that pointer or the continuation in the context. The current page is owned by the store callback until
end-of-page, then moved to the DB thread; activation containers are mutated
only there.  Request the next page only after the DB thread has consumed the
current page.  Ignore stale-epoch results except for retiring their pending
callback.

Use one non-template internal constant in `Zdb.hh` for the required `selectRows()` limit:

```c++
enum { SagaScanSize = 256 }; // cold recovery store page, not a replay batch
```

The maintenance comment records the capacity's purpose.  It bounds only a
store result page; it is not a scheduler-work limit or public tuning contract.
Reuse page storage from `Zdb.Saga.Scan`; do not add a public macro, run-time
configuration, scheduler repost policy, or spill mechanism.

Activation rebuild order is:

1. scan all `SagaData` pages; on the DB thread validate each shard and type,
   narrow/load its payload directly into one saga attempt, insert it into the
   recovery hash, and link it into the FIFO;
2. treat end-of-results as authoritative only if the scan epoch is current and
   no load/scan error occurred; otherwise drain the request, `clean()` the
   owning recovery containers, and fail activation without scanning steps;
3. scan all `SagaStep` rows; first look for `(type, id)` in the now-
   authoritative recovery hash, and asynchronously delete an absent main's orphan
   row using its persisted shard;
4. for a non-orphan, resolve table/op from `MSaga<Sagas>::stepDef`, validate step/shard,
   and permit null UN only for a Delete no-op marker;
5. insert one `SagaRec` in the logical index and, for non-null UN only, the
   reservation index; duplicate logical keys or target UNs are fatal corruption;
6. accept any subset of declared steps without a contiguous-prefix rule; and
7. wait for the ordinary asynchronous orphan deletes to complete before
   replaying the FIFO.

Sparse recovered step rows are expected, not corruption.  Different DB
shards enqueue back-end work independently, so a hard crash may leave a later
different-shard step intent durable while an earlier one is absent.  The
same-shard ordering of each individual step intent and its target effect is
the required recovery invariant; no cross-shard durable-prefix invariant is
assumed.

Scan failures use the existing DB failure path and epoch invalidation; do not
retain a second, unused error sentinel in the scanner. Catalog reconciliation
still retains its first `ZeException` while draining its direct store callback.
A page already in flight is drained, but a failed main scan
does not start a step scan whose result cannot make activation succeed.
Orphan cleanup uses ordinary asynchronous table operations and the pending
count; do not add a cleanup collection, batching policy, or repair state.
The step scan itself holds one pending completion until authoritative EOR;
orphan deletes each hold another. Release the scan's completion only after
consuming its final page. This prevents early replay when all current orphan
deletes finish while a later scan page is still in flight, without a separate
scan-complete flag. Clear each consumed page while retaining capacity for the
next query, and release its storage at the end of that scan.
Each posted orphan deletion must own its typed table key; a forwarding tuple
of references into the scan page would dangle after page reuse or release.

The scanner follows the owner-thread rules in
[`GUIDELINES.md:338-357`](GUIDELINES.md#L338) and
[`GUIDELINES.md:598-677`](GUIDELINES.md#L598).

Acceptance: deterministic mock scans cover multiple pages, empty relations,
each corrupt field, unknown/unloadable types, sparse valid step rows,
duplicate logical/target keys, orphan cleanup, store errors, and deactivation
during every callback boundary.  A DB with no registration performs no saga
open and allocates no saga container.  No stale callback can publish `Active`.

## 5. Implement the critical mutation path

`Saga` exposes saga variants of every table mutation path used by applications:
`insert`, object-bearing `update`, key-bearing `findUpd`, object-bearing `del`,
and `findDel` if that table overload is used.  Non-saga callers continue to use
`Table` directly.  Saga code may call any ordinary `Table` read/query API
directly.  Calling a mutating `Table` function directly from saga code bypasses
saga replay protection and runtime step accounting and is an application
contract violation.

Each saga mutation has exactly the corresponding `Table` signature with one
leading, deduced table parameter added:

```text
table->insert(object, fn)                 saga.insert(table, object, fn)
table->update<KeyIDs>(object, fn)         saga.update<KeyIDs>(table, object, fn)
table->findUpd<KeyID, KeyIDs>(s, key, fn) saga.findUpd<KeyID, KeyIDs>(table, s, key, fn)
table->del(object, fn)                    saga.del(table, object, fn)
table->findDel<KeyID>(s, key, fn)         saga.findDel<KeyID>(table, s, key, fn)
```

Implement each as a function template with a deduced `Table *table` first
argument.  Preserve the corresponding `Table` function's explicit template
arguments, remaining parameter order, return type, and application callback
signature.  The added table type remains deduced, so calls such as
`saga.findUpd<0>(table, shard, key, fn)` retain the normal `KeyID` syntax.
Recovery-only `OpResult` values are consumed inside the wrapper and are not a
second application-visible mutation result API.

The DB invokes the selected numbered application overload on its control
thread. Before selecting the saga mutation, application code may perform
arbitrary synchronous or asynchronous work and may yield. Startup recovery
begins at step zero, so all work leading to every replayed mutation runs again.
The application must therefore make work outside the protected mutations
idempotent.

Each overload receives `context_`, a raw `ZdbSaga *saga`, and a movable
concrete `SagaStepComplete`. It moves `complete` together with captured `this`
and `saga` through asynchronous continuations. The completion owns the
enclosing `MSaga`, so both borrowed pointers remain live until it is invoked.
There is no `Waiting` state, coroutine, future, condition variable, timeout, or
polling path.

The typed union runner and application callbacks must not throw. This is the
same callback discipline as the settled Zdb APIs; an escaping exception is an
application error, not a saga result. Do not add exception translation,
rollback, or retry machinery around the runner or mutation callbacks. The
persisted row remains the recovery description after a process failure.

The callback passed to a saga mutation remains the synchronous body of the
corresponding `Table` function. It must commit and then invoke `complete(true)`
as its final action. The returned descriptor and mutation result never advance
an executed step. `complete(false)` enters the failure path. A chain which
calls two terminal
actions for one turn is an application contract violation, just like violating
an existing one-shot Zdb callback contract.  The activation epoch still rejects
callbacks which arrive after deactivation; do not add a second per-turn state
machine to defend against application-created duplicate continuations.

The saga contains a raw `DB *`, not a DB reference; the application must drain
every continuation holding a completion before finalizing the DB
and its registered context.  Calls after terminal completion are a contract
failure.  Do not add an atomic DB pointer or make DB reference-counted to excuse
a violated application lifetime.  A hung external callback can therefore hold
graceful shutdown or recovery, which is an application failure rather than a
condition Zdb times out.

Application code selects the target shard with `Table::run` and invokes the
saga mutation wrapper there. The wrapper asserts shard ownership, commits the
intent, and immediately calls the ordinary `Table` mutation inline. There is
no DB-control bounce before the intent/effect pair and no yield between them.
Any recovery record and reserved-UN state required by the wrapper is staged by
`sagaRun` before application dispatch. The one-shot completion posts the
captured epoch, step, and staged target shard to the DB control thread.

`complete(true)` and the wrapper-created equivalent for recovered `Skipped`
operations are the only advancement paths. `sagaResult` handles only
`NotReady`, `Missing`, and `Invalid`; it cannot advance or directly dispatch
the next step. Duplicate or concurrent completions violate the one-shot
contract; do not add a callback registry or deduplication subsystem.

Factor common dispatch, intent/effect execution, and result posting into typed
helpers shared by insert, update, and delete; retain static Table calls.

For each mutation:
1. take the implicit `Saga::step()`; do not validate live table/op against the
   catalog on every call;
2. consume recovery preparation staged before application execution;
3. on the target shard choose its next UN, commit the prepared `SagaStep`,
   and immediately invoke ordinary object-bearing CRUD;
4. require the application callback to commit synchronously and invoke its
   completion afterward; the mutation result itself does not advance the step.

Insert needs no key extraction or presence preflight: its existing callback
constructs the row on-shard after the intent commits. Backend duplicate
suppression remains the ordinary insert contract, with application-owned
payload consistency. A recovered saved insert instead uses exact UN and can
skip without executing that callback or consuming a fresh UN.

Resolve `findUpd` and `findDel` inside the saga wrapper before a new intent,
then call object-bearing update/delete. If delete finds nothing, commit a
null-UN no-op marker, invoke the ordinary null callback on initial execution,
and advance without a target commit. Replay of that marker skips without a
lookup or callback. No public Table lookup API changes are required.

The application shares its compile-time step aliases with its body, so a body
that chooses the wrong live table or operation is an application defect, not a
reason to tax every steady-state mutation.  During `Rebuilding` only, compare
the armed table, operation, and shard with the compile-time definition and any
existing `SagaRec`; this is recovery integrity validation, not catalog
reconciliation.

All effective mutations have an intent/effect critical pair. Give `Saga`
narrow friend access to the existing `Table::findUpd_()` lookup primitive;
do not alter `Table::findUpd` or `Table::findDel`, or add forwarding hooks.
For saved update/delete intents, compare UN before lookup, so an already-
applied operation can skip even when its row has disappeared or been replaced.

The framework owns the step-intent commit. The application callback initializes
or mutates the supplied object and commits synchronously. It must not retain
the mutable object, abort, yield, start another mutation, or throw. No saga
allocation, logging, lookup, or dispatch occurs between the intent commit and
the ordinary Table invocation. An in-memory failure after intent is a process-
fatal invariant violation. Later backend failures use the existing DB failure
path, not a transient saga result. Recovery OpResult remains framework-internal.

Use one named-heap `ZmFn` for variable-size prepared application state only
when the typed operation cannot be captured compactly.  The scheduler lambda
captures that function by move plus fixed identifiers; it never captures a
logger reference, an application context reference, or mutable saga `this`.
Use explicit captures only; `[&]` and `[=]` are forbidden.  Do not introduce a
base class hierarchy for prepared CRUD.

These rules apply the no-copy/allocation audit in
[`GUIDELINES.md:238-264`](GUIDELINES.md#L238), DRY/control-flow rules in
[`GUIDELINES.md:266-305`](GUIDELINES.md#L266), and handoff/capture rules in
[`GUIDELINES.md:598-677`](GUIDELINES.md#L598).

Acceptance: instrumented tests prove intent commit, target invocation, target
commit, result post with no intervening same-shard work. Cover all CRUD kinds,
cache hits and yielding findUpd/findDel, multiple shards/tables, pre-intent
preparation failure, process-fatal invalid post-intent mutations, and
asynchronous store failure. Prove skipped insert/delete consume no fresh UN,
deleted-key replacement survives replay, and absent-delete markers perform
no lookup or target commit and reserve no UN.

## 6. Implement admission, replay, and completion

### Live admission

`SagaDB<Sagas>::saga(shard, id, ZmRef<MSaga<Sagas>>, SagaSubmitFn)` dispatches to the DB
control thread through the existing private `DB::spawn()` pattern, preserving
its `bool` queued/not-queued contract.  The argument's bundle matches at compile time. Reject unless
DB is active, saga state is `Active`, and the shard is valid. Saga ID allocation
and avoidance of concurrent duplicate `(type, id)` values are application
concerns; the steady-state path performs no hash lookup or insertion.

Save into the row-owned `SagaPayload`, allocate the live step-shard array, and commit
`SagaData` on the caller-supplied shard before invoking application saga code.
This row is the durable capture pack from which the same code can be restarted.
Invoke `SagaSubmitFn` with its void success alternative after that main-intent
commit/enqueue boundary, then dispatch numbered step zero. Admission
failure before the main commit returns a descriptive
`ZeException`.  Once the main row commits, later application, read, or mutation
failure belongs to the persisted saga and leaves the row for recovery.

The supplied shard is only main-row placement.  The framework does not derive
it from the first step.  Each target step independently supplies its shard.
Because the intent is persisted before application code discovers or prepares
step zero, there is no special main-shard/first-step inline path.  Always return
to the DB control thread between completed mutation steps; do not recursively
enter the next saga step on a target shard.

### Activation replay

After both scans, orphan cleanup, and index construction complete, replay the
FIFO already populated by the main scan.  Do not divide the recovered workload
into batches, windows, or independently scheduled groups.  Replay its head
from step zero through the numbered application overloads using the saved capture pack
and current opaque application context.  Reads, finds, branches, and other
application work are executed again; their repeatability is an application
concern.

For every mutation, find `(type, id, step)` in the logical index. A saved
null-UN Delete marker enters the internal successful-completion path without
lookup, callback, or target UN change.
For a saved non-null UN require its reservation and use exact-UN CRUD. Compare
UN before resolving a saved update/delete key. If no intent exists, complete
preparation, then check that the target next UN is not reserved by another
recovered effect. If reserved, return NotReady without committing. Otherwise
journal and execute through the live critical path. New absent deletes write
only a no-op marker and do not compete for a target UN.

Any subset of steps may be recovered. Executed advances only when the
application invokes its post-commit completion; Skipped invokes the same
internal successful-completion path. NotReady rotates the FIFO head and
immediately runs the next head. Missing/Invalid
are fatal. On success remove the reservation shadow before its logical owner;
no-op markers have only the logical node. Keep the scanned cleanup shard.
A new intent/effect pair or no-op marker needs no recovered index allocation.

If application code yields for a read or unrelated asynchronous action, keep
that FIFO head active and wait for its continuation; do not start a second saga
merely because application code yielded. Only a replayed mutation returning
`NotReady` moves the attempt to the tail.  Consequently an application read or
external dependency that never completes prevents activation, as it would
prevent the live saga from completing.

Likewise, after the successful completion of an executed/skipped mutation or
no-op marker,
keep driving the same head saga through its next step.  Remove it and select
the next head only when it completes.  If a mutation returns `NotReady`, move
the saga to the tail in O(1) and immediately drive the new head.  Queue
rotation—not workload batching—is the sole recovery fairness mechanism.

Track a raw `Saga *` marker for the first rotated head instead of snapshotting
the FIFO count. Clear it on successful step completion, a committed no-op
marker, or saga completion. If the
same marked head returns `NotReady` again without intervening progress, the
full rotation is fatal.  Clear the marker before removing its saga.  Do not
wait, poll, back off, add a timer, or add a no-progress counter.  Requeue logic
exists only in activation code; add no recovery batch size, workload limit, or
scheduler repost policy.

The lack of replay batching is an intentional exception to the general mixed-
workload liveness audit: `Rebuilding` completes before application `up`, so the
control thread is not servicing live application work.  Reads and mutations
still yield through their existing asynchronous callbacks.  Do not generalize
this exception to steady-state loops.  See
[`GUIDELINES.md:354-357`](GUIDELINES.md#L354).

### Completion and lifecycle

Once the final saga step has completed—after its target commit returns, or
after an already-absent delete—the saga may delete its main row. Do not
enqueue a back-end barrier or wait for physical store durability.  A target
commit has already updated local memory and enqueued replication/store work;
the causally subsequent main-row deletion is ordered behind it by the normal
Zdb queues.  The queued callback in
[`zdbreptest2.cc:180`](zdb/itest/zdbreptest2.cc#L180) exists to make a later
`select` observe preceding writes; saga completion performs no such read and
does not need that synchronization.

After the final `complete(true)`, typed dispatch observes `step == NSteps` and
automatically enters the protected `done()` path:

1. delete and commit the main `SagaData` row on its stored placement shard;
2. serially delete and commit all of its `SagaStep` rows on their
   stored shards;
3. invoke `SagaHandler::doneFn` on the DB control thread while a local
   `ZmRef<Saga>` keeps the callback's borrowed type span valid; and
4. after the callback returns, remove its recovery hash/FIFO node or decrement
   the steady-state live count.

Zdb does not reserve live IDs. The application owns ID allocation and may reuse
an ID after completion; it must not submit colliding live IDs. Do not add a
completed-key state or table.

Main-first deletion makes orphan cleanup sufficient after a crash.  Serial
cleanup minimizes bookkeeping and allocations; it is off the saga's logical
critical path.

The protected `done()` path is valid only at the declared step count. `fail()` after the main row
has committed invokes `errorFn` at most once for the current epoch, enters the
existing DB failure path, and retains the persistent intent.  A save,
allocation, or admission error before the main commit is instead returned as
the exception alternative of `SagaSubmitResult` and never calls `errorFn`.

Reconcile `saga_type` once during `DB::start()` and close it before the DB is
eligible for leadership.  Replay pending sagas before every leader `up`, both
the initial activation and later promotion, using the already reconciled
compile-time bundle.  On deactivation increment the epoch and stop admission.
A critical pair already past its epoch check completes.  If deactivation wins
after the main commit but before the next mutation intent, stop running
application code and leave the persisted main row for recovery.  Later callbacks
cannot publish success for the stale epoch.  After failure quiescence,
`clean()` reservation shadows before logical owners, then FIFO shadows before
the recovery hash, without deleting pending rows; a later activation rebuilds
them.  Graceful stop instead drains all admitted sagas and cleanup; hard-crash
residue is the only normal source of startup replay.  Add no background
recovery thread, timeout, or periodic GC.

In particular, the main-row admission completion checks the attempt's epoch
before publishing `SagaSubmitResult` success or entering application code.
An interrupted admission returns an exception but does not delete a committed
main row. Test this without sleeps: on the common control/shard SID, queue
admission then failure, so admission posts its commit behind failure; restart
and prove that the retained intent replays from step zero.

For graceful stop, reject admission using the existing engine lifecycle state.
Keep saga execution active while the scalar live count is nonzero, including through
main/step cleanup and `doneFn`. `stop_0()` defers table/connection closure
while activation or admitted work remains; the last completion posts its
resumption. Clear application-active and saga-active state only after draining,
so restarting the same DB performs activation normally. Add no separate
`Draining` state or periodic drain check.

Count framework continuations on the DB thread from dispatch until their
return: main admission, protected mutations, yielding wrapper lookups,
main/step deletion, each scan page, and each orphan deletion. `sagaRetire()`
retires even a stale result. A nullable `DrainFn` holds the next lifecycle
continuation; `sagaDrain()`/`sagaDrained()` schedule it only when no counted
work remains. Deactivation cleans state and resumes a waiting stop; a newer
activation replaces that continuation and rebuilds only after the old work
drains. No atomics, polling, separate drain state, or per-operation allocation
is needed. `stop_0()` and `final()` must respect both the count and the pending
drain continuation. Test failure during graceful stop with a mutation already
dispatched: its stale completion retires, stop finishes, and restart skips its
committed effect before completing the remaining saga.
The drain may resume `stop_0()` before the engine's queued stop wake arrives;
`stop_()` therefore treats `Stopping` (table closure) and `Initialized`
(store stop/shard drain) as an already-running stop, not a lifecycle error.

Store/table close completion must follow previously queued writes and their
callbacks. Fix the memory store's immediate `close()` callback by executing it
on its existing store queue. After store stop, drain each DB shard's queued
write-completion work before `stopped(true)` permits `final()` to release
tables. Use serial continuations and the existing shard map, not locks,
blocking, or an extra counter. This is normal shutdown lifetime management;
it adds no barrier or backend acknowledgement to live saga completion.

Surface startup and activation failures through the existing Zdb paths:

- while the engine is still starting, log the specific event/context at
  `Fatal` severity, drain the outstanding saga/catalog callbacks, close the
  temporary catalog handle, and complete the engine start with
  `started(false)`, matching backing-store and table-open failures in
  [`Zdb.cc:254-295`](zdb/src/Zdb.cc#L254);
- during a later leader activation, do not set application-active state or call
  `upFn`; log the failure and enter `DB::fail()`/`deactivate(true)`, matching
  the established running-DB failure path in
  [`Zdb.cc:544-589`](zdb/src/Zdb.cc#L544); and
- do not invoke `SagaHandler::errorFn` for catalog, scan, malformed-row,
  `Missing`, `Invalid`, or no-progress recovery failures.  That callback is for
  a process-local application failure of an already admitted live saga.

Here `Fatal` is the existing Zdb diagnostic severity and failure outcome.  It
does not introduce an unconditional process abort. An application abandons a
step with `complete(false)`; invoking `complete(true)` without a successful
commit is an application contract violation and is not detected by a second
framework state machine.

Share the non-template `startFailed()` close/stop/drain path between table-open,
catalog, and initial activation failures. Catalog code closes its temporary
handle before entering that path. Normal stop and startup failure share
`drainShards()` with an explicit final continuation; do not duplicate the
shard traversal or add a terminal-mode flag. Test conflicting step counts and
operations against a preserved catalog, then reopen with the original bundle
to prove failure released every table handle without rewriting the catalog.

This phase follows asynchronous lifecycle/teardown guidance in
[`GUIDELINES.md:338-357`](GUIDELINES.md#L338) and
[`GUIDELINES.md:645-677`](GUIDELINES.md#L645), and the rule against blocking,
polling, and timing-dependent state machines in
[`GUIDELINES.md:266-305`](GUIDELINES.md#L266).

Acceptance: deterministic cut-point tests restart after every main/step/effect
and cleanup commit boundary and after asynchronous read callbacks.  They prove
that reads preceding skipped, executed, and deferred mutations are replayed;
they also prove exact-once update effect under UN replay, exact-UN insert/delete replay and explicit absent-delete markers, FIFO NotReady
progress for all effects, fatal no-progress rotations, interleaved sagas on shared
target streams, application-owned ID reuse, later leader activation,
stale epochs, and graceful drain.  Include a recovery queue larger than one
scan page to prove page boundaries do not create replay batches.

## 7. Verification and delivery order

Land the work as independently buildable slices in this order:

1. prerequisite cleanup and exact-UN/select tests;
2. schemas, row types, and serialization tests;
3. internal store flag and PostgreSQL namespace tests;
4. compile-time dispatch and catalog reconciliation;
5. DB-owned tables, state, and activation scans;
6. critical mutation path and live admission;
7. replay, completion, and lifecycle; and
8. end-to-end mock and live PostgreSQL recovery tests.

Use `ZuTest` TAP assertions.  Coordinate asynchronous tests with `ZmBlock` or
`ZmSemaphore`; do not use sleeps or elapsed-time assertions.  Keep unit test
names CamelCase, integration source names lower-case, and FlatBuffers filenames
snake_case.  See [`GUIDELINES.md:82-89`](GUIDELINES.md#L82),
[`GUIDELINES.md:486-489`](GUIDELINES.md#L486), and
[`GUIDELINES.md:716-739`](GUIDELINES.md#L716).

For every slice:

1. build affected libraries and their prerequisites with `make -j8`, stopping
   at `zdb_pq`; do not rebuild unrelated downstream modules;
2. run the focused binaries and affected `zdb`/`zdb_pq` module tests;
3. complete verification within `zdb` and `zdb_pq`, as requested;
4. use Clang debug builds; and
5. run the new binaries under Valgrind via `libtool exec`.

The build and dynamic-analysis requirements come from
[`GUIDELINES.md:100-115`](GUIDELINES.md#L100) and
[`GUIDELINES.md:783-790`](GUIDELINES.md#L783).
The user's verification override is Clang/debug plus Valgrind; no further
ASAN/LSAN runs are required. Earlier sanitizer results below are historical
evidence, not an additional acceptance gate.

## Principal-engineer audit checklist

Before accepting implementation, review the diff against every row below.  A
failed row requires code removal or a measured justification in the review;
silence is not a justification.

| Audit area | Required evidence | Guideline |
|---|---|---|
| Scope | No manager/service, compatibility layer, store saga API, compensation, or cross-DB path | [`9-46`](GUIDELINES.md#L9), [`116-130`](GUIDELINES.md#L116) |
| Application code | The whole body is application-idempotent under replay; reads, queries, branches, and external work are permitted and replayed; all effects journal immediately before mutation; absent deletes record no-op markers | [`266-305`](GUIDELINES.md#L266), [`598-677`](GUIDELINES.md#L598) |
| Idempotence | Insert advances after local commit and the backend ignores primary-key collisions under the application consistency contract; delete permits an already-absent row; all effects reserve and replay exact UNs; no-op markers reserve none | [`266-305`](GUIDELINES.md#L266), [`440-489`](GUIDELINES.md#L440) |
| Header DAG | `ZdbSaga.hh` is self-contained; direct includes only; no `Zdb.hh` cycle; non-template code in `.cc` | [`62-99`](GUIDELINES.md#L62), [`503-532`](GUIDELINES.md#L503) |
| Language | GNU C++2b, no concepts/requires, `enum class`, `[&]`, `[=]`, unnecessary STL, or anonymous namespaces | [`116-185`](GUIDELINES.md#L116) |
| Code structure | `SagaDB<Sagas>` retains the compile-time bundle, with direct union dispatch and no manual vtable; non-dependent declarations live outside templates; loop-invariant lengths/lookups are cached; repeated CRUD and result-post blocks are factored without virtual dispatch | [`144-160`](GUIDELINES.md#L144), [`199-209`](GUIDELINES.md#L199), [`266-269`](GUIDELINES.md#L266) |
| Capacity | `ZdbSaga_BuiltinSize` is the sole public capacity knob; `SagaScanSize` is a documented private query-page limit; arrays are exact-sized, and queues grow only with admitted or crash-residual rows | [`211-236`](GUIDELINES.md#L211) |
| Storage/GC | C++ members are measured and ordered independently of schema order; control-thread ownership avoids cache-line contention; completed attempts and step rows are removed deterministically; activation orphan reconciliation is not periodic GC | [`211-228`](GUIDELINES.md#L211), [`338-357`](GUIDELINES.md#L338) |
| Allocation | Every allocation uses a named heap; recovery hash/FIFO intrusions share `SagaNode`, recovered-step intrusions share `SagaRec`, and steady-state `Saga` carries neither recovery intrusion; no wrapper allocation occurs between step-intent commit and ordinary Table invocation | [`238-250`](GUIDELINES.md#L238), [`466-484`](GUIDELINES.md#L466) |
| Data movement | Payload and scan pages move once; exact-sized shard arrays remain uninitialized until filled; no hidden contiguous conversion, default-init-before-overwrite, or logger/reference capture | [`252-264`](GUIDELINES.md#L252), [`371-393`](GUIDELINES.md#L371) |
| Control flow | Application work uses current continuation patterns; iterator scope ends before mutation/posting; member containers are iterated directly without redundant null tests; traversed counts are accumulated rather than snapshotted; discrete results use `switch`; nesting is flattened; built-in key hashes replace hand-rolled hashes | [`266-305`](GUIDELINES.md#L266), [`598-677`](GUIDELINES.md#L598) |
| Liveness | Unbatched replay is confined to pre-`up` `Rebuilding`; asynchronous Zdb/application work still yields, and no equivalent long loop is added to steady state | [`354-357`](GUIDELINES.md#L354) |
| Framework idioms | `ZuUnion`/`ZuMatcher`/`ZuSwitch`, `ZfbStruct`, `ZmHash`/`ZmList`, `ZmFn`, `ZtBuiltin`; no ad-hoc equivalents | [`307-336`](GUIDELINES.md#L307), [`400-489`](GUIDELINES.md#L400) |
| State representation | Nullable installed saga continuation, nullable first error, lifecycle enum, epoch, and raw no-progress pointer are the sentinels; no parallel initialized/error/blocked booleans | [`307-319`](GUIDELINES.md#L307) |
| Type boundaries | No avoidable casts or char/span/array reinterpretation; `IDString`, `SagaPayload`, and Z spans cross their existing conversion boundaries directly; no cast is used to force CRTP dispatch | [`321-336`](GUIDELINES.md#L321) |
| Lifetime | Raw owner back-pointers in subordinate work, `ZmRef<Saga>` across shards, epoch invalidation, and DB drains before destruction | [`338-352`](GUIDELINES.md#L338), [`408-426`](GUIDELINES.md#L408), [`666-677`](GUIDELINES.md#L666) |
| Threading | Live/recovery indexes, FIFO, and lifecycle counters mutate only on the DB thread; target mutations only on their DB shard. Store callbacks own scan-page construction and catalog results until their terminal continuation hands them back to the DB thread. No new locks or atomics excuse ownership violations | [`349-357`](GUIDELINES.md#L349), [`440-444`](GUIDELINES.md#L440), [`598-677`](GUIDELINES.md#L598) |
| Scope/cleanup | No dead compatibility code, retained startup catalog, stale scanner, orphan attempt, owner-reference cycle, or post-terminal callback remains | [`338-347`](GUIDELINES.md#L338) |
| Callbacks/logging | `ZmFn` is used only at erased boundaries; compact captures use `mvFn` where available; every lambda capture is explicit; deferred logs copy `ZeString`/scalars and never pointers or references | [`252-256`](GUIDELINES.md#L252), [`481-484`](GUIDELINES.md#L481) |
| Naming/layout | Names follow the table above, stay within 28 bytes, use `m_`/trailing `_` correctly, and preserve tabs/style in C++ | [`534-596`](GUIDELINES.md#L534), [`679-781`](GUIDELINES.md#L679) |
| Persistence | Three schemas and three `ZfbStruct`s; stable field numbers/enums; internal namespace affects only physical store identity | [`432-438`](GUIDELINES.md#L432) |
| Tests | Deterministic cut points, no sleeps, full build before tests, Clang debug, live PostgreSQL, and Valgrind via `libtool exec` (user override above) | [`82-115`](GUIDELINES.md#L82), [`486-489`](GUIDELINES.md#L486), [`783-790`](GUIDELINES.md#L783) |

The implementation is complete only when the goal's success criteria pass and
this audit contains no unexplained amber flag.

### Implementation audit evidence

Layout inspection with `libtool exec gdb` on the x64 debug build found:

- `SagaData`: 336 bytes, including its 280-byte builtin payload container;
  the scalar/member total is 329 bytes and 16-byte aggregate alignment requires
  336. Moving the one-byte shard does not reduce the object size.
- `SagaStep`: 64 bytes; its members total 61 bytes and 16-byte alignment
  requires 64. Reordering those fields does not reduce the object size.
- `SagaTypeStep`: originally 128 bytes, measured at 120 after placing `table`
  before `step`. `Ctor<>` positions follow C++ aggregate order, while
  the `ZfbStruct` field order, primary key, and FlatBuffers schema stay unchanged.
- `Saga_` and `Saga`: 112 bytes, with the borrowed compile-time type span,
  numeric ID, owner pointer, exact-sized step locations, epoch, and progress
  fields. The concrete application's union extends this base; this is not the
  full `MSaga` size. Before the recovery-only hash and borrowed-key refinements,
  `Saga` was 144 bytes. `SagaRec`: 72 bytes including both recovered-step
  indexes; `SagaCatalog`: 232 bytes, excluding
  the separately allocated startup `seen` array and string overflow storage.

These checks apply the storage-layout rule in
[`GUIDELINES.md:211-228`](GUIDELINES.md#L211). They do not justify adding
cache-line padding to control-thread-owned state.

The critical allocation boundary is intent commit to ordinary Table invocation,
not intent commit to the end of the application callback. `Table::commit`
retains its existing cache/UN bookkeeping and replication-buffer allocation;
the application still constructs and mutates its row in the ordinary operation
body. Prohibiting those allocations would contradict the intended Table API.
The wrapper prepares its intent object and callback before that boundary.

The cut-point matrix, instrumented critical-pair ordering, and per-flag audit
are covered below. The final verification record supersedes intermediate
test counts in this implementation history.

`MSaga::save` now uses `Zfb::IOBuilder` with `IOBufAlloc<SagaData>` on
`Zdb.Saga.Buf`, replacing FlatBuffers' default allocator. The builder writes
backwards into its pooled arena; the finished payload is copied once into the
row-owned `SagaPayload`, whose lifetime crosses the admission shard handoff.
This is an explicit retained-storage copy under
[`GUIDELINES.md:252-264`](GUIDELINES.md#L252): exactly the serialized byte count,
into uninitialized destination storage, with no intermediate string/array.
The builtin destination cannot adopt an interior pointer into the builder's
arena. Keep this existing Zfb integration rather than adding a saga-specific
FlatBuffers allocator or changing the required owning payload representation.
Tests exercise empty payloads, the builtin boundary, and arena growth.

Arena growth exposed a prerequisite `Zfb::IOBuilder` lifetime defect: without
`buf()` detachment, its `ZmRef<ZiIOBuf>` member was destroyed before the
FlatBuffers base deallocated the arena. Its destructor now calls `Reset()`
while the buffer owner is alive. `ZfbTest3` covers finished/unfinished builders
with and without exception unwinding, all forced beyond builtin storage.
This also protects admission when application serialization throws before
`Finish()`; normal buffer detachment is unchanged.

`zdb/itest/zdbsagareptest` covers later leader activation with two live peers.
The initial leader pauses after its journaled insert and update have replicated;
it is then failed and stopped. The promoted follower replays both steps without
re-entering their mutation callbacks, completes the saga before `up`, and
retains exactly one increment in the row. The test uses the existing network,
store, and lifecycle APIs, not a test-only activation hook. Its loopback ports
and invocation are documented in `zdb/itest/README.md`.
The four-program integration suite passes in debug and ASAN/LSAN builds;
Valgrind reports no errors or lost memory for the new promotion test.

The mutation-boundary test queues a same-shard probe from the mock store's
synchronous `SagaStep` write hook. It records intent commit, application
mutation entry, target commit, callback return, the queued probe, and entry
into the next saga step, checking target/journal UNs at each boundary. This
tests the critical pair without a production instrumentation API.

The lookup and mutation now remain in one application-selected shard turn.
`SagaFind<Key, L>` owns the lookup key and callback by value while `findUpd_`
resolves the row; its callback immediately prepares the intent/effect pair on
that same shard. There is no resolved-object handoff through the DB control
thread and therefore no stale-object retry bounce. Each actual Table lookup
takes one key copy while the continuation retains the original for a
possible application use. No lookup occurs on saved-UN skip paths. Saved-UN
replay and direct object-bearing APIs retain their existing behavior.

`live(1)` and `live(2)` inject replacement between lookup and update/delete,
respectively, while the non-contending `live(0)` retains the full
critical-pair trace assertions. The intent write hook proves that a queued
same-shard probe cannot enter between intent commit and the ordinary mutation.
Both replacement tests also run with a dedicated shard thread, separate from
the control thread. The original 43-check unit suite passed in debug and
ASAN/LSAN builds, with no Valgrind errors or lost memory; the affected debug and
sanitizer PostgreSQL suites also passed.

Four additional cases change the secondary key before deleting the resolved
object, then create the replacement under the original lookup key. Both update
and delete are tested on shared and dedicated shard threads. All four failed
when retries reconstructed keys from the deleted object; the retained-key
implementation passes all four. The affected debug and ASAN/LSAN suites pass:
47 unit checks, four replication/integration programs, and nine PostgreSQL
checks. Valgrind reports no errors or lost memory for the saga unit binary.
The extra key copy is the explicit ownership/correctness trade-off
under [`GUIDELINES.md:252-264`](GUIDELINES.md#L252); there is no new type erasure,
allocation of a retry object, or change to public Table lookup APIs.

The recovery fixture now also covers a committed main row before any mutation,
an insert intent without its effect, and a delete intent without its effect.
Together with the existing update-intent cut, each CRUD operation has an
intent-only replay case. The fixture preserves the mock store across a failed
activation and reconstructs the DB, checking which mutation bodies replay and
which skip. These are deterministic persisted-residue tests, not process-kill
tests; the live PostgreSQL crash tests remain separate. The expanded debug and
ASAN/LSAN suites pass 50 checks, and Valgrind reports no errors or lost memory.

`recoveryCleanup(0..3)` interrupts the real cleanup chain after the main-row
commit and after each of its three step-row deletion commits. The synchronous
mock write hook posts DB failure before the next cleanup continuation. After
draining, the fixture asserts the exact residue (no main row and three, two,
one, or zero step rows), then reconstructs the DB against the preserved store.
Startup removes the orphan rows without running application saga code. A new
saga using the same `(type, id)` then executes all three mutations normally.
The expanded debug and ASAN/LSAN unit suites pass 54 checks; Valgrind reports
zero errors and no lost memory. This covers actual cleanup continuation
interruption rather than only fabricating orphan rows.

`recovery(0)` and `recovery(7)` trace an uncached update and delete, respectively.
A shard probe posted when the store lookup begins runs before the new intent;
a second probe posted from the intent commit runs only after target callback
entry, target commit, and callback return. Both tests therefore exercise a
yielding preparation followed by a non-yielding intent/effect pair. The debug
and ASAN/LSAN suites pass 55 checks, and Valgrind reports no errors or lost
memory. These traces initially used the shared control/shard SID.

The header audit removed the unused `SagaDefSteps` alias, added direct
`string.h`, `ZuSeq.hh`, and `ZuString.hh` dependencies, and corrected lambda
indentation. The replication test's write-hook comment now identifies its
committing-shard owner. These apply the dead-code, direct-dependency, layout,
and ownership rules in [`GUIDELINES.md`](GUIDELINES.md#L338). The refreshed debug
build passes 55 unit checks, four integration programs, and nine PostgreSQL
checks without build warnings. A standalone include of `ZdbSaga.hh` compiles
with the project's GNU C++2b/version settings and no diagnostics. The refreshed
ASAN/LSAN unit, integration, and PostgreSQL suites also pass; Valgrind reports
no errors or lost memory.

The lifecycle/source review confirms that `SagaRec` indexes borrow their
attempt and table owners; the primary step hash owns each record and the UN
hash is a shadow index. Result processing removes the shadow entry before its
owning entry. Deactivation invalidates the epoch, drains pending work, then
clears the indexes before attempts. Scan callbacks build one store-owned page
and hand it to the control thread at terminal completion; they do not post one
control task per row. This phase ownership is intentional and requires no
locking, atomics, extra copies, or separate per-row allocations for handoff
([`GUIDELINES.md:238-264`](GUIDELINES.md#L238),
[`GUIDELINES.md:338-357`](GUIDELINES.md#L338)).

The live CRUD trace and both cold-lookup recovery traces also run with a
dedicated shard SID, separate from the DB control thread. Their mutable trace
state stays shard-owned: entry into the next application turn on the control
thread is not required to wait for an unrelated shard probe. Both shared and
dedicated configurations require the probe queued at intent commit to follow
target commit and callback return. The expanded unit suite passes all 63
top-level cases
in debug and ASAN/LSAN; Valgrind reports zero errors and no lost memory.

The PostgreSQL fixture now uses four shards, assigning the main row to shard
2 while mutations use shards 0 and 1 across two tables. Verification uses a
fresh database because DB shard topology is persisted; it does not alter the
existing two-shard fixture database. Shard 3 is unused: the attempted three-shard
configuration was rejected by Zdb's existing power-of-two topology check.
Both PostgreSQL test programs now use four shards because they share
`ZDB_CONNECT` and therefore the same persisted `nShards` metadata. The original
generic test's two-shard configuration correctly caused a fatal mismatch when
followed by the four-shard saga test. The aligned four-shard runs pass all nine
PostgreSQL checks in debug and ASAN/LSAN against the fresh
`zdb_saga_shards4_20260905` test database. Valgrind reports zero errors and no
lost memory for `zdbsagatest`; the persisted `zdb.meta.nShards` value is 4.

The application test body can issue an ordinary asynchronous `count<0>` before
each turn, retain its saga handle in the callback, and post the result back to
the DB control thread before selecting its mutation. No framework API or state
was added. Two dedicated-shard recovery cases cover reads before executed and
skipped mutations, including a fully skipped saga whose four application reads
still run. The interleaved FIFO test performs ten reads for ten application
turns, including both `NotReady` retries, while preserving the expected effect
counts and final UN. These cases pass in the 61-check debug and ASAN/LSAN unit
suites; Valgrind reports no errors or lost memory.

An additional application-read cut deactivates the DB after the first count
callback, before it calls the saga insert. The continuation must not mutate
under its stale epoch; restart must reload the retained main row and replay
the application read and all three mutations. The 63-case suite passes in
debug and ASAN/LSAN, but Valgrind exposed a deferred-log lifetime defect during
rapid stop/reinitialization: `DB::setMaster()` captured a host-ID `ZuCSpan`, and
the logger read it after its `HostCf` owner was freed. Host/peer log captures
in `Zdb.cc` now retain `ZeString` values, including incoming association IDs.
Existing owning table-ID and `ZuID` captures are unchanged. This applies the
explicit deferred-logging ownership rule in
[`GUIDELINES.md:252-256`](GUIDELINES.md#L252), without delaying shutdown or
retaining DB/host owners. After the owning captures, the debug unit/integration/
PostgreSQL suites pass and Valgrind reports zero errors or lost memory; the
ASAN/LSAN unit suite also passes.

Compile-only probes were rerun against the current standalone saga header:
the valid bundle compiles; duplicate type names, zero-step definitions, missing
numbered overloads, invalid operation descriptors, and empty bundles fail at
their intended template/static-assert boundary. No runtime descriptor or
signature-validation layer is needed.

The remaining shard-mismatch lookup diagnostic now formats its message while
the lookup context and row are alive, then moves that `ZeString` into the
deferred log. It no longer retains objects whose raw table back-pointers could
outlive DB finalization. This cold fatal-path formatting is an explicit owned
diagnostic snapshot, not a data-path copy.

Reserved table IDs now use `ZuMatcher::exact`, not its prefix matcher. Only
`saga`, `saga_step`, and `saga_type` are reserved; names such as `saga_app` and
`saga_step_log` remain valid application tables. `tableIDs` tests all three
rejections and four accepted names through ordinary DB registration/start/stop.
These audit changes pass the 63-check debug and ASAN/LSAN unit suites;
Valgrind reports zero errors or lost memory. The refreshed debug integration
and four-shard PostgreSQL suites also pass.

Two additional startup fixtures corrupt the persisted catalog through its
existing direct store handle: one deletes the final step, leaving a partial
type; the other changes an update's table name without changing its type or
step. Both require fatal startup on two successive attempts, proving that the
failure releases its handles and does not repair the incompatible definition.
These complement the decoder checks and existing persisted step-count/op
conflicts. Both pass in the final 65-check unit suite and under Valgrind.

## Open questions and ambiguities

None. Insert/delete journaling and explicit absent-delete markers are approved.

## Requirement-to-evidence map

This maps the goal's acceptance criteria to the current implementation and
passing verification, including the persisted-catalog corruption fixtures.

| Requirement | Implementation / evidence |
|---|---|
| Three schemas, `ZfbStruct`, opaque builtin payload | `zdb/src/fbs/zdb_saga*.fbs`, row mappings in `ZdbSaga.hh`; `rows`, `dispatch`, and payload boundary/growth tests |
| Compile-time taxonomy and union narrowing | `MSaga<Sagas>` uses `ZuMatcher::exact`, `ZuSwitch`, `cdispatch` save and `dispatch` run; valid/invalid compile probes and `dispatch` |
| Startup reconciliation, historical types, fatal conflicts, catalog closure | `SagaCatalog` and `SagaDB::sagaCatalog*`; `catalog`, `catalogStartup`, and persisted `catalogCorrupt` fixtures; successful starts reopen the direct handle to prove closure |
| DB-owned management, optional registration, normal start/up | `SagaDB<Sagas>` installs typed continuations in `DB`; `admission`, catalog startup tests, and the ordinary non-saga DB tests |
| Composite IDs, application-owned allocation, completion and reuse | `SagaKey`, recovery-only hash, scalar live count, `sagaDone`/`sagaCleanup`; `admission`, `gracefulStop`, `recoveryCleanup` |
| Internal store identity without a saga-specific backend API | `Store::open(bool internal, ...)`; PostgreSQL namespace/reopen tests cover application and internal relations, including maximum-length IDs |
| Only exact internal names reserved | `reservedTableID` uses exact matching; `tableIDs` tests reserved and similarly prefixed application names |
| Local commit before advance; intent immediately before CRUD | `Saga::mutate_` invokes ordinary Table CRUD synchronously; live and cold-lookup traces queue same-shard probes, on shared and dedicated SIDs |
| Exact-UN insert/update/delete replay and ordinary insert collision policy | Existing Table overloads plus saga wrappers; `ZdbTelTest`, `collisions`, `recovery`, and live PostgreSQL crash/reconnect tests |
| Absent-delete marker and replacement-row safety | Null-UN marker branch; `recovery(2)` and `recovery(3)` retain a replacement row rather than repeating deletion |
| Sparse intents, reservations, immediate FIFO rotation, fatal no progress | Logical/UN intrusive indexes and `sagaResult`; `indexes`, `recoveryQueue`, `recoveryError` |
| Arbitrary application work and asynchronous reads replay | Optional application `count<0>` continuation in `LiveSaga`; executed/skipped and interleaved deferred-read cases; `admissionDeactivated(true)` cuts after read completion |
| Main row before effects; all intent/effect/cleanup boundaries | `admissionDeactivated`, `recovery(0..7)`, `recoveryCleanup(0..3)` and PostgreSQL hard-crash children |
| DB-wide sharding across tables, unrelated main-row shard | PostgreSQL fixture uses four distinct shard SIDs, targets 0/1 in two tables, and main row 2 |
| Authoritative scans, corruption, orphan cleanup, no replay batches | `sagaScanData`/`sagaScanSteps` and activation FIFO; `scanError`, `scanFail`, `recoveryError`, and `recoveryPages` crossing the query-page boundary |
| Startup failure, stale epochs, graceful drain, later activation | `sagaDrain`/`sagaRetire`, `startFailed`, and epoch checks; `findFail`, pending-scan stop tests, `gracefulStop`, and two-peer `zdbsagareptest` promotion |
| No completion durability barrier or periodic retries/GC | Direct `sagaDone` deletion after final result; deterministic step cleanup; recovery-only FIFO; inspected paths contain no barrier, timer, or background sweep |
| Guideline audit | Checklist and evidence above cover header DAG, naming, static dispatch, measured layout, named heaps, intrusive indexes, justified retained copies, ownership, explicit captures, and deferred-log snapshots |
| Build and dynamic analysis | Affected `src` directories precede dependents; Clang debug unit/integration/PQ suites; Valgrind through `libtool exec`; standalone header and compile-negative probes |

The implementation does not add compensation, cross-DB sagas, permanent
completed-ID storage, payload version management, a runtime type registry,
`SagaOps`, a separate manager, or a second asynchronous programming model.

## Final verification record

Verified on 2026-09-05 with Clang, GNU C++2b, `-g -DZDEBUG`, on Linux x64.
Affected libraries were built before their dependents with `make -j8`, scoped
through `zdb_pq`; the final builds contain no compiler warnings.

| Check | Result | Local evidence |
|---|---|---|
| `make -C zdb/test test` | 3 programs, 65 TAP cases pass | `/tmp/zdb-catalog-replace-tests.log` |
| `make -C zdb/itest test` | 4 programs pass | `/tmp/zdb-final-audit-itest-tests.log` |
| Live PostgreSQL integration | 2 programs, 9 TAP cases pass | `/tmp/zdb-final-audit-pq-tests.log` |
| `ZdbSagaTest` under Valgrind | Zero errors; zero definitely, indirectly, or possibly lost bytes | `/tmp/zdb-catalog-replace-valgrind.log` |
| Saga leader-promotion test under Valgrind | Zero errors or lost memory | `/tmp/zdb-saga-repl-valgrind.log` |
| Four-shard PostgreSQL saga test under Valgrind | Zero errors or lost memory | `/tmp/zdb-four-valgrind.log` |
| Standalone `ZdbSaga.hh` include | Compiles without diagnostics | `/tmp/zdb-saga-header-check.log` |
| Compile-time bundle constraints | Valid bundle compiles; all five invalid cases fail as intended | `/tmp/zdb-compile-audit-{0..5}.log` |
| Prerequisite `Zfb` builder lifetime tests | 3 programs, 13 TAP cases pass; Valgrind clean | `/tmp/zdb-payload-zfb-tests.log`, `/tmp/zdb-payload-zfb-valgrind.log` |
| Whitespace/diff audit | `git diff --check` clean | Current worktree |

PostgreSQL tests used `PATH=/home/count0/postgres/bin:$PATH`,
`PGDATA=/home/count0/postgres/data`, and the task-owned four-shard database
`zdb_saga_shards4_20260905`. Fatal diagnostics and child-process failures in
the negative tests are expected and asserted. The tests did not migrate or delete
application databases. All source changes remain uncommitted.

The requirement map and audit above account for the original acceptance gates.
The subsequent diff audit and its verification are recorded below.

## Post-implementation diff audit

Scope: the saga implementation, prerequisite `zdb`/`zdb_pq` changes, `Zfb`
builder lifetime repair, and their new/changed tests and build manifests.
Unrelated worktree edits in `TODO.md`, `devlayer/planning.md`, and other
pre-existing files are outside this audit. The checklist above remains the
full guideline matrix; this pass additionally traces each new container and
each allocation/copy across its actual lifetime.

### Findings repaired

1. **Admission copied builtin payload storage through a large lambda capture.**
   A `ZtBuiltin` move must copy its inline bytes; spelling `ZuMv` did not make
   the row/payload handoffs free. Construct the final `Object<SagaData>` before
   serialization, save into its payload, and capture its `ZmRef` rather than
   the row. The shard callback performs the same ordinary insert/commit. This
   uses the already-required journal allocation, not a new envelope, and
   preserves cleanup when save throws. See
   [data movement](GUIDELINES.md#data-movement-and-initialization) and
   [cross-shard handoffs](GUIDELINES.md#cross-shard-handoffs).
2. **Cleanup copied the immutable saga key into each scheduled lambda.**
   The existing captured saga reference already owns that key. Read it on the
   destination shard and retain only the key copy required by the existing
   asynchronous Table lookup. Do not retain a span into an unowned temporary.
   See [data movement](GUIDELINES.md#data-movement-and-initialization).
3. **Membership lookups constructed owning string keys and took references.**
   Admission and recovered-step membership checks now use heterogeneous
   forwarding tuples and `findPtr`; logical-step lookup also uses `findPtr`.
   `ZmHash`/`ZuHash` support these forms directly. The owning hash remains
   control-thread-affined and outlives each borrowed result. See
   [framework fit](GUIDELINES.md#framework-fit) and
   [scope and cleanup](GUIDELINES.md#scope-and-cleanup).
4. **PostgreSQL stored two identical names per opened table.**
   `m_mrdID` duplicated `m_stmtID` byte-for-byte. Removed the former; the
   namespace-prefixed identity now serves both prepared statements and MRD.
   Logical IDs and qualified SQL relation names remain distinct. See
   [storage](GUIDELINES.md#storage-and-capacity) and
   [dead state](GUIDELINES.md#scope-and-cleanup).
5. **The activation scan carried an unused error sentinel and dead branches.**
   No path assigned `SagaScan::error`: Table scan errors already invalidate
   activation before terminal callback delivery, and decoder errors fail it
   directly. Removed that field and both impossible branches; retained the
   catalog's genuinely used error sentinel. See
   [state representation](GUIDELINES.md#framework-fit) and
   [dead code](GUIDELINES.md#scope-and-cleanup).

### Container and allocation decisions

| Storage | Decision and rejected alternative |
|---|---|
| Recovery `SagaHash` and activation FIFO | Retain only during `Rebuilding`: hash membership associates steps and detects orphans; FIFO provides O(1) rotation. One `SagaNode` allocation consolidates both intrusions and owns the recovered saga. `Saga` itself has neither intrusion, and steady state performs no hash work. |
| Live saga key | Retain a `ZuCSpan` into the selected compile-time saga definition plus the numeric ID. This avoids an owning string/copy in every live saga. Keep an owning key only for the asynchronous scan cursor. |
| Logical-step and target-UN hashes | Retain: distinct keys answer current-step lookup and cross-saga reservation lookup. Both intrusions share one recovered record; the UN index is a non-owning shadow. A tagged single hash needs two entries or scans, and a dense per-saga pointer array increases per-step storage or adds a recovery allocation. |
| `SagaLocs` | Retain the exact-sized byte array. It survives asynchronous steps through cleanup, so stack scratch would dangle. An inline maximum-step array inflates every saga according to the largest application definition; no measured common capacity justifies a new builtin knob. |
| Main/step scan pages | Retain the two typed arrays: only one phase has allocated page storage, and the first is released before the second starts. A union saves only the inactive array descriptor while adding lifetime/dispatch machinery; it does not consolidate live payload allocations. Pages survive store/control handoffs, excluding `ZtScratch`. |
| Catalog `seen` array | Retain one byte per compile-time type during asynchronous startup reconciliation. It is released on close; there is no retained registry. Removing it requires repeated scans or type-order assumptions, and stack storage cannot survive the callbacks. |
| Payload and builder arena | Retain the required builtin payload and pooled `IOBuilder`. Exactly one finished-arena-to-payload copy remains: the owning builtin cannot adopt the builder's interior buffer. Admission now writes that copy into final storage. Stack scratch is not a substitute for the pooled I/O arena. |
| `MSaga`, journal objects, recovered records, scan/catalog contexts | Retain named-heap intrusive allocations. They cross asynchronous callbacks; reference counts and index links are already consolidated with their objects. No extra operation-state allocation was added. |
| PostgreSQL physical names | One cached namespace-prefixed identity replaces two. Retain the qualified relation string because it avoids formatting and allocation on every query. Existing store-table indexing now includes the namespace in its key; no additional namespace container exists. |
| Framework/application continuation captures | Keep raw DB/table back-pointers and moved handles. A resolved `findUpd`/`findDel` must retain its original key across yielding lookup and possible stale-row retry; reconstructing it from a changed/deleted object is incorrect. That necessary lookup copy is covered by the secondary-key replacement tests. |

These decisions apply the storage, heap, data-movement, container-intrusion,
callback, and lifetime flags rather than using stack allocation indiscriminately.
None of the retained new heap buffers is synchronous scratch. Synchronous
key views, scalars, and formatting intermediates remain stack values; the
existing PostgreSQL scratch allocation mechanisms remain unchanged.

The remaining checklist areas were rechecked against the diff: explicit
captures, names and layout, header dependencies, GNU C++2b/static dispatch,
enums, direct `ZfbStruct` use, sentinel state, iterator scope, cached loop
bounds, no hand-written hashing, owner-thread mutation, and deterministic
drain/cleanup. The explicit unbatched pre-`up` recovery policy remains the
user-approved liveness exception. No locks, timers, polling, compatibility
forwarders, or generic safety state were added.

### Verification of the repairs

Clang debug builds of `zdb/src`, `zdb_pq/src`, and their unit/integration
dependents completed without warnings. `git diff --check` is clean.

- Unit tests: **68 TAP cases across three programs**, including new
  `payloadAdmission` cases with empty, builtin-sized, and overflowing input
  payloads. They use a dedicated shard, a type name exceeding builtin string
  storage, and compare the stored main-row payload with its expected serialized
  bytes. The suspended fixture is failed and drained deliberately. Evidence:
  `/tmp/zdb-lean-unit-tests.log`.
- Integration tests: **four programs pass**, including later leader promotion.
  Evidence: `/tmp/zdb-lean-itest-tests.log`.
- Live PostgreSQL: **nine TAP cases across two programs pass**, including
  namespace/MRD identity, reopen, four-shard replay, and hard-crash tests.
  Evidence: `/tmp/zdb-lean-pq-tests.log`.
- Valgrind: saga unit, leader-promotion, and PostgreSQL saga binaries each
  report **zero errors and zero definitely, indirectly, or possibly lost
  bytes**. Evidence: `/tmp/zdb-lean-valgrind.log`,
  `/tmp/zdb-lean-repl-valgrind.log`, `/tmp/zdb-lean-pq-valgrind.log`.
- Debugger layout inspection confirms `IDString` is 80 bytes and
  `ZeException` is 112 bytes on this build: those are the removed duplicate
  per-table member and unused scan member. `SagaScan` is now 160 bytes;
  `SagaData` and `SagaRec` remain 336 and 72 bytes respectively.
  Admission no longer captures a 336-byte `SagaData` value; it captures its
  object handle. These are storage/copy reductions, not a claimed latency
  benchmark.

All findings from this pass are repaired or have an explicit retained-design
justification above. No additional sanitizer run was performed, per the user's
Clang/debug and Valgrind instruction. Changes remain uncommitted.

### Recovery-only attempt index

A subsequent steady-state review removed `SagaHash` from live execution. Saga
IDs and collisions are application concerns; the DB needs only to know whether
graceful shutdown may close its tables while application saga code is yielding.
`m_sagaLive`, owned exclusively by the DB control thread, now increments after
the main row commits and decrements after cleanup and `doneFn`. It is distinct
from `m_sagaPending`, which may legitimately reach zero during an application
yield. No atomic is needed because all changes and shutdown checks occur on the
same thread.

`SagaHash` is now allocated at activation and destroyed before `Active`.
Recovered attempts use a 56-byte `SagaNode` which owns `ZmRef<Saga>` and
combines the hash and FIFO intrusions. This exceptional extra recovery
allocation removes both intrusions from every ordinary saga: measured `Saga`
size falls from 144 to 112 bytes after also replacing its owning type string
with a view of the selected compile-time discriminator. Normal
admission/completion removes one hash lookup, insertion, and deletion. Recovery
retains O(1) main-row membership for step association and orphan detection, and
O(1) FIFO rotation.

The existing deterministic graceful-stop tests pause application code between
steps, when `m_sagaPending` is zero, and prove that table closure waits until
the saga resumes and completes. Failure/deactivation tests prove epoch
invalidation resets the live count only after framework continuations drain.
ID allocation and prevention of simultaneous duplicate `(type, id)` submissions
are explicitly application responsibilities.

Final verification after this refinement uses
`/tmp/zdb-live-count-{unit-tests,itest-tests,pq-tests}.log` and
`/tmp/zdb-live-count-{valgrind,repl-valgrind,pq-valgrind}.log`. The Clang debug
builds are warning-free; all 68 unit cases, four integration programs, and nine
PostgreSQL checks pass. All three Valgrind runs report zero errors and zero
definitely, indirectly, or possibly lost bytes. Debugger inspection measures
`Saga` at 112 bytes (down from 144 before removing its recovery intrusions and
owning type string), `SagaNode` at 56, `SagaRec` at 72, the borrowed `SagaKey`
at 32, and the owning scan-only `SagaCursor` at 48 bytes.
