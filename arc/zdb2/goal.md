# `ZdbSaga` design goal

## Purpose and scope

`ZdbSaga` adds durable, at-least-once saga execution to `Zdb` for
latency-sensitive, write-intensive applications whose ordinary transactions
are single inserts, updates, or deletes and which do not require cross-row or
cross-table isolation.

A saga is application code which coordinates a sequence of idempotent Zdb
mutations.  It may perform arbitrary work between mutations, including
`find*`, `select*`, `count`, external calls, branches, and asynchronous work.
Recovery reloads the saved saga and re-executes its application code from step
zero, so the application must make all such work replayable and idempotent.
Zdb makes mutations invoked through the saga API replay-safe; it does not add
isolation, rollback, compensation, or exactly-once external effects.

A saga belongs to exactly one `Zdb_::DB`.  It may use any table and shard in
that DB, but it cannot span DBs.  Applications needing different shard
topologies use separate DBs and coordinate across them outside this facility.

The application is responsible for:

- defining each saga's saved FlatBuffer intent and `ZfbStruct` mapping;
- assigning each saga type an immutable string discriminator;
- declaring the saga's ordered compile-time table/operation sequence;
- implementing its replayable numbered `ZdbSagaStep` overloads;
- providing and retaining the opaque run-time context used by that body;
- obeying the ordinary `Table` callback and commit contracts; and
- treating payload evolution or an unloadable saved payload as an application
  problem.

The facility deliberately excludes compensation, a durable completion audit,
permanent completed-ID deduplication, cross-DB work, run-time saga-type
introspection, background retry, polling, and a separate saga manager.

## DB-owned architecture

Saga management is an intrinsic part of `Zdb_::DB`; there is no `SagaMgr` or
parallel lifecycle.  Each DB owns:

- private front-end `Table<SagaData>` and `Table<SagaStep>` instances;
- a startup-only direct back-end handle for `saga_type`;
- the optional typed saga workflow continuations configured before `start()`;
- a scalar graceful-drain count and activation-only recovery state; and
- the activation FIFO and lifecycle epoch.

The saga lifecycle has only `Inactive`, `Rebuilding`, and `Active` states.
Normal DB shutdown state already closes admission; do not add a parallel
`Draining` state.

A saga-enabled application uses `SagaDB<Sagas> : DB` (exported as
`ZdbSagaDB<Sagas>`) and calls `sagas(handler)` once before inherited
`DB::start()`. The bundle is fixed by the DB's C++ type.  If it does not register sagas, the internal tables remain
empty and the DB performs no saga data-path work.

`DB::start()` retains its normal lifecycle signature.  It opens the store and
tables, reconciles the registered compile-time saga definitions with
`saga_type`, closes that direct catalog handle, and performs initial leader
recovery before the DB becomes `up`.  No saga object is passed to `start()`.
Later leader activation performs the same pending-saga recovery before calling
the application `upFn`.

## DB-wide sharding

This design depends on the DB-wide topology implemented by
[`sharding.md`](arc/zdb1/sharding.md).  Every front-end table in one DB uses the
same `DB::nShards()` and numeric-shard-to-SID mapping.  Saga implementation code
dispatches with `DB::shardRun()`, `DB::shardInvoke()`, and
`DB::shardInvoked()`; it does not use table forwarding methods or maintain a
second shard map.

`DB::saga(shard, ...)` stores the main `SagaData` row on the caller-supplied
shard.  That shard is persisted solely so recovery can later delete the row.
The application may choose a shard convenient for its workload; the framework
does not derive it from the first saga step or require it to match any target
shard.

An effect's `SagaStep` row is stored on the same numeric DB shard as the target
insert, update, or delete. The two tables therefore execute on the same scheduler SID. Their UN
streams remain independent because a UN belongs to one `(table, shard)`
stream.

The same-SID property permits the step-intent commit and target mutation to run
synchronously in one non-intervenable shard turn.  It does not imply that
distinct saga steps are adjacent: application work may yield, other work may
intervene, and successive steps may target different shards.

Changing `nShards` remains an offline migration boundary.  Pending saga rows
must be recovered and drained under the old topology before such a migration.

## Commit and durability contract

The saga facility composes the existing Zdb commit behavior:

- a successful `ZdbObject::commit()` updates local memory;
- it enqueues replication and back-end store work before returning; and
- code after the commit may rely on that local/enqueued boundary.

This is not an acknowledgement of physical PostgreSQL or other back-end
durability.  The queue barrier demonstrated near
[`zdbreptest2.cc:180`](zdb/itest/zdbreptest2.cc#L180) is useful when a later
operation must observe preceding writes, but saga completion performs no such
read and requires no additional durability barrier.

For saga type `T`, instance ID `I`, and effects `E0..EN`, the required causal
order is:

```text
commit SagaData(T, I, capture-pack)
  -> E0
  -> E1
  -> ...
  -> EN
  -> commit deletion of SagaData(T, I)
  -> delete all SagaStep rows
```

The main row is committed before application saga code first runs.  Its
deletion is committed only after the final effect, recorded no-op, or exact-UN
skip completes. New inserts advance after local commit, including when the
backend ignores a collision; already-applied inserts skip without a fresh UN.
The main row has no progress or completion field.  If it survives a hard
crash, recovery reloads it and begins the application body at step zero.

## Persistent vocabulary

The facility has three persistent row types and three FlatBuffers root
schemas.  `SagaData` and `SagaStep` are private ordinary Zdb tables;
`SagaTypeStep` is accessed only through a temporary direct store handle during
startup.

Place the payload default beside the other header defaults:

```c++
#ifndef ZdbSaga_BuiltinSize
#define ZdbSaga_BuiltinSize 256
#endif
```

Use an unparameterized builtin payload type:

```c++
using ZdbSagaID = uint128_t;

ZuDerive(ZdbSagaType,
  (ZtString<ZtStringHeapID<"Zdb.Saga.Type">>));

ZuDerive(SagaPayload,
  (ZtBuiltin<
    ZtArray<uint8_t, ZtArrayHeapID<"Zdb.Saga.Data">>,
    ZdbSaga_BuiltinSize>));
```

The 256-byte inline size is a compile-time tunable, not a template parameter
on the public type.  Larger values spill through the named heap.  Changing the
inline size does not change the persistent schema.

### Main saga row

```c++
namespace Zdb_ {

struct SagaData {
  ZdbSagaType type;
  ZdbSagaID id;
  ZdbShard shard;
  SagaPayload data;

  friend ZuStringT<"Zdb.Saga"> ZdbHeapID(SagaData *);
  friend ZuStringT<"Zdb.Saga.Buf"> ZdbBufHeapID(SagaData *);
};

ZfbStruct(SagaData,
  (((type),  (Ctor<0>, Keys<0>)), (String)),
  (((id),    (Ctor<1>, Keys<0>)), (UInt128)),
  (((shard), (Ctor<2>)),          (UInt8)),
  (((data),  (Ctor<3>)),          (Bytes)));

ZfbRoot(SagaData);

} // Zdb_
```

`zdb_saga.fbs`:

```fbs
include "zfb_types.fbs";

namespace Zdb_.fbs;

table SagaData {
  type:string;
  id:Zfb.UInt128;
  shard:uint8;
  data:[ubyte];
}

root_type SagaData;
```

The primary key is `(type, id)`.  The string names the saga type; the 128-bit
integer identifies a particular instance of that type.  Equal IDs under
different type strings do not collide.  Completion deletes the row, after
which the same key may be reused.

`data` is the opaque application FlatBuffer.  It is conceptually the capture
pack of a replayable lambda.  Zdb saves it once at admission and does not
automatically re-save later mutations to the in-memory saga object.  Payload
compatibility and sufficiency are application concerns.

### Update-intent row

```c++
namespace Zdb_ {

struct SagaStep {
  ZdbSagaType type;
  ZdbSagaID id;
  uint32_t step;
  ZdbShard shard;
  ZdbUN un;

  friend ZuStringT<"Zdb.Saga.Step"> ZdbHeapID(SagaStep *);
  friend ZuStringT<"Zdb.Saga.Step.Buf"> ZdbBufHeapID(SagaStep *);
};

ZfbStruct(SagaStep,
  (((type),  (Ctor<0>, Keys<0>)), (String)),
  (((id),    (Ctor<1>, Keys<0>)), (UInt128)),
  (((step),  (Ctor<2>, Keys<0>)), (UInt32)),
  (((shard), (Ctor<3>)),          (UInt8)),
  (((un),    (Ctor<4>)),          (UInt64)));

ZfbRoot(SagaStep);

} // Zdb_
```

`zdb_saga_step.fbs`:

```fbs
include "zfb_types.fbs";

namespace Zdb_.fbs;

table SagaStep {
  type:string;
  id:Zfb.UInt128;
  step:uint32;
  shard:uint8;
  un:uint64;
}

root_type SagaStep;
```

The primary key is `(type, id, step)`. Every declared Insert, Update, and
Delete has a SagaStep. Its non-null UN identifies the intended effect in the
target table/shard stream. A Delete which finds no row instead stores
`nullUN()`: an explicit no-op marker which never reserves a target UN.
Null UN is invalid for Insert and Update.

The persisted shard locates the journal row for scanning, reservations, and
orphan cleanup. Recovered step rows may be any sparse subset; independent
shard queues do not imply a contiguous durable prefix. Validate every row
against the compile-time definition, without imposing prefix validation.

### Saga-type row

```c++
namespace Zdb_ {

ZtEnumNS(ZdbAPI, SagaOp, int8_t,
  Invalid,
  Insert,
  Update,
  Delete);

struct SagaTypeStep {
  ZdbSagaType type;
  IDString table;
  uint32_t step;
  SagaOp::T op;
};

ZfbStruct(SagaTypeStep,
  (((type),  (Ctor<0>, Keys<0>)), (String)),
  (((step),  (Ctor<2>, Keys<0>)), (UInt32)),
  (((table), (Ctor<1>)),          (String)),
  (((op),    (Ctor<3>, Enum<SagaOp::Map>)), (Int8)));

ZfbRoot(SagaTypeStep);

} // Zdb_

namespace ZdbSagaOp = Zdb_::SagaOp;
```

`zdb_saga_type.fbs`:

```fbs
namespace Zdb_.fbs;

enum SagaOp:int8 {
  Invalid = 0,
  Insert,
  Update,
  Delete
}

table SagaTypeStep {
  type:string;
  step:uint32;
  table:string;
  op:SagaOp;
}

root_type SagaTypeStep;
```

The primary key is `(type, step)` and the value is `(table, op)`.  `table` is
the existing owning `IDString` used by Zdb; do not add another table-ID type.
The catalog describes every ordered saga mutation; each has a `saga_step` row.

The persisted numeric `SagaOp` values are stable.  `Invalid` is the rejected
zero sentinel.  Registered catalog steps are zero-based and contiguous.

## Internal physical namespace

The PostgreSQL relations are:

```text
"zdb"."saga_type"
"zdb"."saga"
"zdb"."saga_step"
```

Application tables retain their normal physical namespace.  Add a required
boolean `internal` parameter to `Store::open()` so a back end can distinguish
the same logical ID in the Zdb namespace:

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

Propagate this intentional breaking change through every store and caller.
Application tables pass `false`; all three internal relations pass `true`.
For PostgreSQL, every generated table and index reference must qualify an
internal name through schema `zdb`.  Do not encode `zdb.` into `IDString`.
Stores without namespaces may ignore the placement distinction while keeping
it in table identity where needed.

The flag changes only physical identity.  It does not change row encoding,
query behavior, sharding, or the returned `StoreTbl` API.  This is the only
back-end store surface change required for sagas.

Reserve the logical IDs `saga_type`, `saga`, and `saga_step`; application table
registration must reject them.  `saga` and `saga_step` use DB-private ordinary
`TableCf` values with `CacheMode::Normal`.  `saga_type` has no `TableCf`, front-
end `Table`, cache, sharding, or replication machinery.

## Compile-time taxonomy and type erasure

Each application saga declares a persistent string discriminator and a
compile-time mutation sequence through numbered overloads:

```c++
template <typename TableID_, ZdbSagaOp::T Op_>
struct ZdbSagaStep_ {
  using TableID = TableID_;
  enum { Op = Op_ };
};

struct OrderNew {
  using Type = ZuStringT<"orderNew">;
  enum { NSteps = 2 };

  ZdbSagaStep(0, orders, Insert) { /* post work, complete after commit */ return {}; }
  ZdbSagaStep(1, accounts, Update) { /* ... */ return {}; }
};
```

`SagaSteps<OrderNew>` derives the descriptor list from the overload return
types over `[0, NSteps)`. Missing numbered overloads and invalid operation
descriptors fail at compile time. The descriptor value is empty and ignored at
run time; there is no separate step list, registry, or compatibility API.

Changing a saga's step count, order, table, operation, or replay meaning under
the same type string is unsafe.  An evolved saga must use a new `Type` name.
The persisted table catches structural collisions; payload semantics and
whether an application can load old bytes remain application concerns.

Saga rows are type-erased by the string discriminator.  Follow the
`ZrestClient.hh` `MReqBuilder` pattern:

```text
Saga
  <- MSaga<Sagas>
       using Union = GetUnion<Sagas>;
       Union u;
```

`GetUnion<Sagas>` produces `ZuUnion<void, ...>`.  On load,
`ZuMatcher<Types>` matches the stored string, `ZuSwitch` narrows it to the
concrete saga type, and `u.new_<S>()` constructs the correct alternative from
that type's verified FlatBuffer.  Do not copy the matcher index into the union
discriminator because the leading `void` changes the numeric alternatives.

On save, `u.cdispatch()` obtains the concrete `S::Type` and `ZfbStruct`.
On run, `u.dispatch()` narrows the concrete definition and `ZuSwitch` invokes
its current numbered overload. Do not infer types by probing payload schemas. Do not use
RTTI, virtual per-type callbacks, `std::variant`, or a run-time factory hash.

`MSaga<Sagas>` derives from the catalog-independent `Saga` base and uses
the named `Zdb.Saga` heap. `SagaDB<Sagas>` retains the compile-time bundle
for catalog reconciliation, recovery loading, admission, and execution.
Save uses `u.cdispatch()`; mutable application execution uses `u.dispatch()`.
There is no `SagaOps` descriptor, manual vtable, or runtime factory registry.

Non-template DB machinery owns the tables, indexes, queues, and lifecycle.
Typed continuation lambdas connect startup, activation scans, and execution
to that machinery without adding a function pointer for every saga accessor.
Ordinary applications continue to use `DB` with no saga callbacks installed.

### Startup catalog reconciliation

`SagaDB<Sagas>::sagas(SagaHandler)` installs the handler and typed workflow
continuations after init and before start. A second call or wrong lifecycle
state is a configuration error. Use the existing nullable callbacks/container
as the setup sentinel; do not add a parallel boolean.

During `DB::start()`:

1. Resolve every registered table ID in the current DB.
2. Open `saga_type` directly with `Store::open(true, ...)`.
3. Stream stored rows in `(type, step)` order.
4. Compare each stored registered type as one group with the compile-time
   definition.
5. Insert an entirely absent current type sequentially through the ordinary
   Zdb record envelope.
6. Reject a partial definition, step-count mismatch, gap, or any table/op
   mismatch.
7. Allow historical stored types absent from the current build.
8. Drain all callbacks, close the table on every path, and retain neither the
   rows nor its `StoreTbl *`.

A conflicting type is a fatal startup error; the application must use a new
name for an evolved saga.  A hard crash while first inserting a new type can
leave a partial group.  The next startup rejects it rather than guessing,
repairing, or silently completing a possibly conflicting definition.

Catalog reconciliation is startup-only.  Steady-state mutation code performs
no catalog lookup or per-step validation. The taxonomy exists only in the
compile-time `Sagas` definitions; no runtime descriptor is retained.

## Application execution model

The application runner is:

```c++
ZdbSagaStep(N, table, Op) { /* body */ return {}; }
```

Each overload receives `context_`, a raw `saga` pointer, and a movable
`complete` continuation. The continuation owns the enclosing `MSaga`, so
capturing `this`, `saga`, and `complete` together keeps the definition and base
alive across asynchronous work. The opaque context is not persisted and must
remain alive until DB shutdown drains saga continuations.

An overload is not a mutation-only DSL. It may issue reads or external work,
return, and later resume through the current Zdb continuation-lambda pattern.
The application moves `complete` through that chain and invokes
`complete(true)` only after the declared mutation callback commits, or
`complete(false)` to fail the step.
There is no coroutine, future, blocking wait, polling loop, timeout, or
framework `Waiting` state.

Each invocation eventually performs exactly one of these actions:

- call one saga mutation variant and complete after its callback commits;
- abandon the step with `complete(false)`; or
- call `fail()` with a specific exception.

The returned descriptor never advances the saga. `complete(true)` is the sole
executed-step advancement path; a recovered `Skipped` operation invokes the
same internal completion path without calling the application mutation body.
After step `NSteps - 1`, the framework dispatch observes `NSteps` and completes
the saga automatically. Application code cannot call `done()`.

The application may yield between steps and may cause arbitrary other work to
intervene.  Recovery restarts at step zero and repeats every read, branch, and
external action leading to the replayed mutations.  Making those actions safe
is an application responsibility.

Calling a mutating `Table` API directly from saga code bypasses saga replay
protection and step accounting and is a contract error.  Non-saga application
code continues to call `Table` mutations directly.

## Saga mutation API

Each saga mutation has exactly the corresponding `Table` signature with one
leading, deduced `Table *` parameter:

```text
table->insert(object, fn)                 saga->insert(table, object, fn)
table->update<KeyIDs>(object, fn)         saga->update<KeyIDs>(table, object, fn)
table->findUpd<KeyID, KeyIDs>(s, key, fn) saga->findUpd<KeyID, KeyIDs>(table, s, key, fn)
table->del(object, fn)                    saga->del(table, object, fn)
table->findDel<KeyID>(s, key, fn)         saga->findDel<KeyID>(table, s, key, fn)
```

The table type is deduced.  Preserve every remaining parameter, explicit
template argument, return type, and callback signature.  Passing a table owned
by another DB is a contract failure.

The callback retains the ordinary `Table` responsibility: initialize or
mutate the supplied object and commit synchronously before returning.  It must
not retain the mutable object, yield, abort, schedule another mutation from
inside the callback, or throw. The saga wrapper never advances an executed
step. The callback invokes its one-shot `complete(true)` only after commit;
that completion posts step advancement.

The application shares its compile-time step declarations with its saga body.
Choosing the wrong table or operation during live execution is therefore an
application contract error; do not add steady-state catalog validation to the
hot path.  During `Rebuilding` only, compare the invoked table, operation, and
shard with the compile-time definition and any recovered step row.  A
mismatch is a fatal recovery compatibility error.

Saga mutation entry points require the target shard and run inline through the
intent/effect pair. The application moves the epoch-bearing completion to that
shard; only its result is posted back to DB control. No locks or default lambda
captures are introduced.

### Journaled mutations

All saga mutation variants journal their operation before performing it.
Ordinary non-saga Table APIs are unchanged. In particular, ordinary insert
commits locally and its backend ignores a primary-key collision; the app
ensures repeated payloads agree with durable contents.

For a new operation on target table `A`, shard `s`:

1. Complete preparation and any yielding findUpd/findDel lookup.
2. Prepare the callback and intent object, choosing `U = A->nextUN(s)`.
3. Commit `SagaStep(type, id, step, s, U)`.
4. Immediately call the corresponding ordinary object-bearing Table operation.
5. Require the application callback to commit synchronously before returning.
6. After the operation returns, post its result to the DB control thread.

No yield, post, saga allocation, logging, or unrelated application work may
intervene between intent commit and target invocation. Both are ordinary
local-cache/enqueue commits. Their tables share a DB shard/SID but have
independent UN streams. Once intent commits, epoch changes cannot cancel its
paired effect. A post-intent in-memory invariant failure is process-fatal;
later backend errors follow the ordinary DB failure path.

An insert's journal records UN, not key or row contents. The existing
application callback may construct its key and data using on-shard finds.
There is no insert presence preflight or upfront row-construction requirement.

The saga wrappers resolve findUpd/findDel before a new intent, then use
object-bearing update/delete. This requires no change to the public Table
findUpd/findDel APIs.

### Already-absent delete

If the lookup finds no row, commit a `SagaStep` with `un == nullUN()`.
On that initial execution invoke the ordinary null callback; its application
body calls `complete(true)` after accepting the no-op marker.
There is no target mutation or target UN consumption. On replay the marker
enters the internal successful-completion path without lookup or callback,
preserving the original no-op even if another saga subsequently created a row at that key. Markers take part
in logical step indexing and cleanup but never in UN reservations.

## Exact-UN recovery

Every saved effect uses the existing UN-bearing Table API. Its callback has
three parameters: `OpResult::T`, the borrowed object pointer, and observed
`nextUN`.

```text
nextUN < intended UN  -> NotReady
nextUN = intended UN  -> Executed
nextUN > intended UN  -> Skipped
```

Compare UN before looking up a recovered update/delete key. Skipped effects
do not run the mutation callback or consume a new UN, even if the original row
is now absent or replaced. A null-UN Delete marker is recognized separately,
never passed to the exact-UN API.

This is essential for insert as well: replaying an ordinary insert would
consume a fresh UN even if its backend ignores a duplicate, potentially making
a later unapplied update appear already applied. Delete replay could similarly
delete a replacement row and consume an unintended UN.

If no saved step exists, resolve preparation and check the current target UN
against recovered reservations. Defer a conflicting effect with NotReady;
otherwise journal and execute through the live critical path. An absent
delete needs no target reservation. Executed waits for the application's
post-commit completion; Skipped invokes the same internal completion path;
NotReady rotates the activation FIFO, and Missing/Invalid fail activation. Missing means that
an equality-path update/delete found no target, not an intentional null marker.

Activation uses one intrusive node per recovered step with two indexes:

```text
(type, id, step)   -> recovered step or no-op marker
(table, shard, UN) -> recovered effect (non-null UN only)
```

The logical index owns the node; its UN index is shadow. Remove the shadow
before the owner on success. No-op markers have only the logical entry.
All index mutation belongs to the DB control thread and none survives into
steady state. Keep one cleanup shard per declared step; array position is
the step number, so update-count/ordinal mappings are unnecessary.

Different sagas can interleave because each live intent/effect pair occupies
one non-intervenable shard turn. Reservations and FIFO rotation recover their
effects in stream order without DB-wide or per-row saga slots.

## Admission API

Submission success needs no enum:

```c++
namespace Zdb_ {

using SagaSubmitResult = ZuUnion<void, ZeException>;
using SagaSubmitFn = ZmFn<
  void(SagaSubmitResult), ZmFnHeapID<"Zdb.Saga.SubmitFn">>;

typedef void (*SagaDoneFn)(void *, ZuCSpan, ZdbSagaID);
typedef void (*SagaErrorFn)(
  void *, ZuCSpan, ZdbSagaID, ZeException);

struct SagaHandler {
  void *context = nullptr;
  SagaDoneFn doneFn = [](void *, ZuCSpan, ZdbSagaID) { };
  SagaErrorFn errorFn =
    [](void *, ZuCSpan, ZdbSagaID, ZeException) { };
};

} // Zdb_

using ZdbSagaSubmitResult = Zdb_::SagaSubmitResult;
using ZdbSagaHandler = Zdb_::SagaHandler;
```

The void alternative means only that the main `SagaData` row committed to
local memory and its replication/store work was enqueued.  It does not mean
step zero or the whole saga completed.

The public shape is:

```c++
class DB {
public:
  template <typename Sagas>
  void sagas(SagaHandler handler);

  template <typename Sagas, typename L>
  bool saga(
      ZdbShard shard, ZdbSagaID id,
      ZmRef<MSaga<Sagas>> saga, L &&submitFn);
};
```

`SagaDB<Sagas>::saga()` posts admission to the DB control thread.  `false` means that post
was not queued and the callback will not run.  The passed saga and callback are
consumed in either case.  If it returns `true`, the callback runs exactly once
unless the process terminates.

Admission:

1. Requires a configured saga DB, an active DB, a valid main-row shard, and a
   non-empty union.
2. Relies on the application to allocate non-colliding live IDs.
3. Saves the active concrete saga directly into `SagaPayload`.
4. Allocates its exactly-sized step-shard cleanup array.
5. Commits `SagaData(type, id, shard, data)` on the supplied shard.
6. Calls `submitFn` with the void alternative.
7. Dispatches numbered application step zero with its one-shot completion.

Failures before the main commit return a
descriptive `ZeException`; they do not call `SagaHandler::errorFn`.  Once the
main row commits, later failures belong to the persisted saga and retain it
for recovery.

Because the main row is saved before any application saga code runs, admission
does not arm or prepare step zero, derive a target shard, or implement a
main-shard/first-step inline path.

## Activation and replay

Incomplete sagas at activation are exceptional hard-crash residue.  Normal
graceful shutdown drains all admitted work.  It is therefore acceptable to
materialize the recovered pending set in memory; replay is not divided into
batches or windows.

Leader activation enters `Rebuilding`, rejects live submissions, and:

1. Scans every `SagaData` row, validating its shard and type, narrowing its
   payload, and adding a recovery-only owner node to the hash and FIFO.
2. Accepts the main scan as authoritative only after successful EOR. A failed
   scan drains its current page and does not begin the step scan.
3. Scans every `SagaStep` row.  Resolve `(type, step)` from the compile-time
   definitions and require a valid step/shard; null UN is permitted only for
   a Delete no-op marker.
4. Insert one recovered-step node in the logical index and, for non-null UN,
   the reservation index. Duplicate keys or reservations are fatal.
5. Accept any subset of declared steps; there is no prefix validation.
6. Delete orphan step rows whose `(type, id)` has no main row.
7. After orphan cleanup, replay the pending FIFO from run-time step zero.

The scanner reuses ordinary typed `selectRows<0>()` and `nextRows<0>()`.  It
may page store reads to bound transient buffers, but paging does not batch
replay.  The entire pending FIFO is driven as one activation workload.

`Table::select_()` must not conflate a store error with normal end-of-results.
Align its error branch with `retrieve_()`: report the error through the owning
DB failure path before delivering the terminal empty callback.  This changes
no public query result type.

Replay drives only the FIFO head.  Application code may yield for a read or an
external continuation; while it does, the head remains active and no second
saga starts. After an executed callback calls `complete(true)`, or a skipped
operation enters the equivalent internal completion, continue the same saga.
On completion, remove it and drive the next head.

Only an exact-UN update returning `NotReady` rotates the current saga to the
FIFO tail.  The next saga is driven immediately.  A full FIFO rotation
containing only `NotReady` is fatal; do not poll, sleep, back off, fill a gap,
or wait for ordinary ingress. A successful completion, a newly committed
intent/effect pair, or saga completion resets the no-progress marker.

`Missing`, `Invalid`, malformed rows, unknown pending types, load failures, and
full no-progress rotations prevent activation.  They are not delivered to
application saga code and do not invoke `SagaHandler::errorFn`.

The DB enters `Active` and calls application `upFn` only after the scans,
orphan cleanup, and recovery FIFO all complete successfully.

## Completion, failure, and lifecycle

After the final mutation's successful completion, typed dispatch observes
`step == NSteps` and automatically enters the protected `done()` path:

1. Deletes and commits the main `SagaData` row on its persisted shard.
2. Serially deletes the saga's `SagaStep` rows on their stored
   shards.
3. Invokes `SagaHandler::doneFn` on the DB control thread.
4. During recovery, removes the activation hash/FIFO node; during steady state,
   decrements the scalar graceful-drain count and releases the final framework
   reference.

Do not wait for a back-end durability acknowledgement before deleting the main
row.  The final mutation has already committed locally and enqueued its work;
the causally subsequent main-row deletion is sufficient.

Main-first deletion is deliberate.  A crash after it may leave orphan step
rows, which the next activation deletes after its authoritative main scan.  A
crash before it leaves the main row and permits replay.  Step rows are never
deleted while the main row remains pending.

Each attempt owns an exactly-sized `ZtArray<ZdbShard>` indexed by step number.
Live work records the shard when it creates a step intent or no-op marker.
Activation fills recovered entries and later fills missing entries as replay
creates them. At terminal cleanup, every declared position has a step row
and stored cleanup shard. Do not keep
live copies of intended UNs; recovery obtains them from `SagaStep`.

Zdb does not reserve steady-state IDs. The application owns ID allocation and
must not submit colliding live IDs; reuse after completion is permitted. There
is no permanent completed-key table.

`fail()` on an admitted saga invokes `errorFn` at most once for the current
epoch, retains the main row, and enters the existing DB failure path.  A
back-end write failure also follows that path asynchronously; it is not a
transient saga result.

A fatal error during initial startup logs its context, drains outstanding
catalog/scan callbacks, closes the temporary catalog handle, and completes
startup with `started(false)`.  A later leader-activation failure does not call
`upFn`; it uses `DB::fail()`/`deactivate(true)`.  Recovery failures do not call
the per-attempt application `errorFn`.

Deactivation increments the epoch and stops admission and new application
turns.  An update pair already past its epoch check and step-intent commit must
finish the target invocation before quiescing.  Late callbacks cannot advance
or complete a stale attempt.  Pending rows remain for the next activation.

Graceful stop rejects new submissions and drains all admitted sagas,
completion cleanup, and application-held continuations before closing tables
or destroying the registered context.  There is no background recovery
thread, timer, retry loop, or periodic journal garbage collection.

## Allocation and ownership

Use the existing Z containers and named heaps:

- `Zdb.Saga` and `Zdb.Saga.Buf` for main rows/buffers and saga attempts;
- `Zdb.Saga.Step` and `Zdb.Saga.Step.Buf` for update rows/buffers;
- `Zdb.Saga.Rec` for the one intrusive node per recovered step;
- `Zdb.Saga.Node` for one hash/FIFO owner per recovered saga;
- `Zdb.Saga.Locs` for exact update-shard arrays;
- `Zdb.Saga.Scan` for activation scanning; and
- `Zdb.Saga.SubmitFn` for submission callbacks.

The DB-control thread owns all saga containers and lifecycle mutation.  Target
objects are touched only on their DB shard.  Cross-thread callbacks hold
`ZmRef<Saga>`; subordinate contexts use raw owner pointers under DB drain
discipline.  Do not add locks or atomics to permit non-owner mutation.

The activation-only hash stores one `SagaNode` per recovered saga; that node
owns `ZmRef<Saga>` and embeds both hash and FIFO intrusions. `Saga` itself has
neither intrusion. Each recovered step uses one allocation with intrusive nodes
for both logical and UN indexes. The crash-residue working set is proportional to sagas which were
already live before the crash; do not add spill files or a recovery-size
configuration without measured need.

Use a named-heap `ZmFn` for variable-sized prepared application state only when
the typed call cannot be captured compactly.  Do not allocate, format, log, or
perform type dispatch inside the update intent/target critical pair.

## Application example

The following abbreviated application supports two saga types.  No lambda uses
a default capture.

```c++
struct OrderNew {
  using Type = ZuStringT<"orderNew">;
  enum { NSteps = 2 };

  uint64_t orderID;
  uint64_t accountID;
  int targetState;

  ZdbSagaStep(0, orders, Insert) { /* shown below */ return {}; }
  ZdbSagaStep(1, accounts, Update) { /* shown below */ return {}; }
};

ZfbStruct((OrderNew, ...),
  (((orderID),     (Ctor<0>)), (UInt64)),
  (((accountID),   (Ctor<1>)), (UInt64)),
  (((targetState), (Ctor<2>)), (Int32)));

struct AccountMove {
  using Type = ZuStringT<"accountMove">;
  enum { NSteps = 1 };

  // saved fields, one ZdbSagaStep overload, and ZfbStruct metadata
};

using AppSagas = ZuTypeList<OrderNew, AccountMove>;
using AppSaga = ZdbMSaga<AppSagas>;
using AppDB = ZdbSagaDB<AppSagas>; // type of App::m_db
```

The application configures its typed saga DB before start:

```c++
void App::initSagas()
{
  ZdbSagaHandler handler;
  handler.context = this;
  handler.doneFn = [](
      void *context, ZuCSpan type, ZdbSagaID id) {
    static_cast<App *>(context)->sagaDone(type, id);
  };
  handler.errorFn = [](
      void *context, ZuCSpan type, ZdbSagaID id, ZeException e) {
    static_cast<App *>(context)->sagaError(type, id, ZuMv(e));
  };
  m_db.sagas(ZuMv(handler));
}

void App::start()
{
  m_db.init(m_dbCf, m_mx, zdbHandler(), m_store);
  m_orders = m_db.initTable<Order>("orders");
  m_accounts = m_db.initTable<Account>("accounts");
  initSagas();
  m_db.start();
}
```

The saga obtains current services through the opaque context.  The insert
initializes and commits exactly as an ordinary table insert; the framework does
not inspect its captured primary key.

```c++
ZdbSagaStep(0, orders, Insert) {
  auto app = static_cast<App *>(context_);
  auto table = app->orders();
  auto shard = app->orderShard(orderID);
  table->run(shard,
    [this, app, table, saga, complete = ZuMv(complete), shard]() mutable {
      ZdbObjRef<Order> row = new ZdbObject<Order>{table, shard};
      saga->insert(table, ZuMv(row),
        [this, complete = ZuMv(complete)](ZdbObject<Order> *row) mutable {
          if (!row) { complete(false); return; }
          new (row->ptr()) Order{.orderID = orderID, .accountID = accountID};
          row->commit();
          complete(true);
        });
    });
  return {};
}

ZdbSagaStep(1, accounts, Update) {
  auto app = static_cast<App *>(context_);
  auto table = app->accounts();
  auto shard = app->accountShard(accountID);
  table->run(shard,
    [this, table, saga, complete = ZuMv(complete), shard]() mutable {
      saga->findUpd<0>(table, shard, accountKey(*this),
        [this, complete = ZuMv(complete)](ZdbObject<Account> *row) mutable {
          if (!row) { complete(false); return; }
          row->data().orderState = targetState;
          row->commit();
          complete(true);
        });
    });
  return {};
}
```

The application names and submits a concrete instance:

```c++
void App::submitOrderNew(OrderRequest request)
{
  ZdbSagaID id = newSagaID();
  ZmRef<AppSaga> saga = new AppSaga{};
  saga->init(OrderNew{
    .orderID = request.orderID,
    .accountID = request.accountID,
    .targetState = request.targetState
  });

  if (!m_db.saga(orderShard(request.orderID), id, ZuMv(saga), [
        app = this, id
      ](ZdbSagaSubmitResult result) {
        app->sagaSubmitted(id, ZuMv(result));
      }))
    sagaSubmitUnavailable(id);
}
```

The callback's void alternative means the main intent was accepted at the
local/enqueued boundary.  `doneFn` reports eventual process-local completion.
Both notifications may be lost across process termination and are not durable
audit records.

## Required verification

Use deterministic `ZdbMockStore` callback control and live PostgreSQL tests.
Do not use sleeps or elapsed-time assertions.  Verify at least:

- all three schemas and `ZfbStruct` round trips, including payloads below, at,
  and above `ZdbSaga_BuiltinSize`;
- exact `ZuMatcher`/`ZuSwitch` load narrowing, `u.cdispatch()` save, and
  `u.dispatch()` run;
- startup catalog insertion, exact matching, historical types, and fatal
  partial/count/order/table/op conflicts;
- `Store::open(false, ...)` for application tables and `true` for internal
  tables, with PostgreSQL objects only in schema `zdb`;
- catalog closure and absence of retained catalog rows after startup;
- main-row admission before the first application turn and one submit callback
  with either void or `ZeException`;
- live journaling and exact-UN replay of all mutation kinds, without insert
  presence preflight or requiring upfront row construction;
- absent-delete markers consume no target UN, reserve no target position,
  and prevent replay from deleting a replacement row;
- mutation ordering `SagaStep commit -> target invocation -> target commit`, with
  no intervening same-shard work;
- all-effect recovery for `Executed`, `Skipped`, and `NotReady`, with `Missing` and
  `Invalid` fatal;
- interleaved updates, reversed recovery order, FIFO tail rotation, and fatal
  full no-progress rotations;
- sparse recovered step rows without prefix validation;
- multi-shard and multi-table sagas, including a main-row shard unrelated to
  any target shard;
- application reads and asynchronous continuations between mutations, with
  replay from step zero;
- crash cuts before and after main, step-intent, target, main-delete, and
  step-row-cleanup commits;
- authoritative multi-page scans, scan-error propagation, duplicate indexes,
  unknown types, invalid payloads, and orphan step cleanup;
- no back-end durability barrier before main deletion;
- application-owned live ID allocation, completed-key reuse, stale epochs,
  later leader activation, and graceful drain; and
- Clang debug builds and Valgrind runs through `libtool exec` in addition to
  the normal test suite; the user's verification override requires no further
  ASAN/LSAN runs.

## Decision summary

1. `DB` implements saga management monolithically and owns two private
   front-end tables plus one startup-only direct catalog table.
2. The application registers one compile-time `Sagas` bundle before start.
   Startup reconciles and closes `saga_type`; there is no retained run-time
   registry.
3. A saga is keyed by `(string type, uint128 id)`, saves an opaque FlatBuffer
   capture pack, and receives an opaque live application context when run.
4. Application saga code may perform arbitrary work and yield between
   mutations.  Recovery reloads it and begins at step zero.
5. Saga mutation APIs mirror `Table` APIs with one leading deduced `Table *`.
   The application callback retains the ordinary commit contract.
6. Every effective mutation journals its target UN, then performs ordinary CRUD
   in one non-yielding same-shard turn; replay uses the exact-UN overload.
7. An absent Delete journals a null-UN no-op marker, which replay skips without
   looking up a row or consuming a target UN.
8. Recovered rows may be sparse. Activation indexes all logical steps and only
   non-null target reservations; there is no step-prefix validation.
9. Recovery drives one FIFO.  Only `NotReady` rotates the head; missing,
   invalid, and a full no-progress rotation are fatal activation errors.
10. Submission succeeds after the main row commits, before application step
    zero.  Completion deletes the main row without a durability barrier, then
    cleans all step rows.
11. Completed IDs may be reused.  Graceful shutdown drains all work; crash
    residue is recovered before the next leader becomes active.

## Open questions

None.
