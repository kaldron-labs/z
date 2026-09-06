# Zdb

Zdb is an in-process, typed database layer for stateful C++ services. It combines
row and replication-buffer caches, asynchronous backing stores, DB-wide sharding,
leader election and replication, and recoverable sagas. Tables and their indices
are defined in C++/FlatBuffers metadata before startup.

Include `<zlib/Zdb.hh>`. The complete runnable application example is
[zdbsaga.cc](example/zdbsaga.cc), with its schemas and build rules in
[example/](example/). Public implementation contracts are in
[Zdb.hh](src/Zdb.hh), [ZdbSaga.hh](src/ZdbSaga.hh), and
[ZdbStore.hh](src/ZdbStore.hh).

## Lifecycle: init, start, stop, final

The usual owner sequence is:

1. Configure and start a `ZiMultiplex`, including the DB, store, and optional
   shard worker threads.
2. Allocate the DB and call `init(ZdbCf{...}, &mx, handler, store)`.
3. Register every application table with `initTable<T>("tableID")`.
4. For a saga DB, populate its context and call `sagas(ZuMv(context))` once.
5. Call `start()` and wait for application activation through `handler.upFn`.
6. Serve requests while active. Stop admitting work when deactivated.
7. Quiesce application producers and finish outstanding application callbacks;
   call `stop()` and wait for completion.
8. Release application table/row references, call the typed DB's `final()`, and
   release the DB/store. Stop the multiplex last.

`init()` and `final()` are synchronous and may throw `ZeException`. Register
tables and sagas after initialization and before startup. The table configuration
does not replace `initTable<T>()`; registration supplies the actual C++ type.

`start()` and `stop()` have blocking boolean-returning forms and asynchronous
forms taking a boolean completion callback, inherited from `ZmEngine`. Use the
asynchronous forms from worker callbacks; do not block a thread needed to finish
the operation. Successful engine startup and application activation are distinct:
a running host can be a follower. Gate application service on `upFn`, not merely
on a successful `start()` return.

```c++
ZdbHandler handler{
  .upFn = [](Zdb *db, ZdbHost *oldMaster) {
    // Application may activate here; saga recovery has completed.
  },
  .downFn = [](Zdb *db, bool failed) {
    // Stop admitting application mutations and release/cancel dependent work.
  }
};
```

These handlers are function pointers. Recover application state through an owning
DB subclass or another established owner; they are not per-request closures.
Handlers run on the DB thread and must return promptly. Activation can recur
after failover. `downFn` distinguishes failure from orderly deactivation.

Graceful stop drains the framework's pending work, closes tables/stops the store,
and drains resulting shard callbacks. It cannot finish a step whose application
has retained its completion indefinitely. Keep the multiplex, DB, tables, and
context alive through teardown. A stopped DB can be started again; after
`final()`, initialize and register it again before reuse. For a saga DB, call
`final()` through its typed `ZdbSagaDB<Context, Sagas>` owner: that wrapper releases
the configured context after base finalization.

### Configuration

This DB configuration uses two logical shards and two shard workers. The referenced
thread names must also exist in the multiplex configuration; the example supplies
a complete matching multiplex setup.

```text
zdb: {
  thread: zdb,
  shards: 2,
  threads: [shard0, shard1],
  store: {thread: store},
  hostID: self,
  hosts: {self: {standalone: true}},
  tables: {
    account: {cacheMode: All},
    transfer: {cacheMode: All}
  }
}
```

`shards` must be a power of two in `[1, 64]`. If supplied, `threads` must have a
power-of-two length no greater than the shard count. Without it, all shards use
the DB worker. DB and shard workers must not be the multiplex Rx or Tx thread.
Sharding belongs to the DB; table-level `shards` and `threads` are rejected.
`cacheMode: All` retains cached rows instead of normal LRU eviction; the default
is `Normal`. Cache policy does not replace backing storage or change ownership.

## Tables, rows, and shard ownership

Define a row with `ZfbStruct` metadata and a matching generated FlatBuffers schema.
Declare primary key fields with `Keys<0>`, secondary indices with other key IDs,
and updateable fields with `Mutable`. Use `Group<KeyID>` for grouped queries.
The primary key is immutable. Zdb supports unique primary and secondary indices.
See the account/transfer types in the example and
[ZdbTest.hh](test/ZdbTest.hh) for composite keys and grouped indices.

The main application types are `ZdbTable<T>`, `ZdbTblRef<T>`, `ZdbRow<T>`, and
`ZdbRowRef<T>`. Configure row and buffer heaps through the ADL hooks
`ZdbHeapID(T *)`, `ZdbBufSize(T *)`, and `ZdbBufHeapID(T *)` as in the example.

The application chooses the logical shard for each row and must use that same
shard for subsequent operations, including lookups through secondary indices.
Keep that mapping consistent across hosts and restarts. A logical shard maps to
`threads[shard & (threads.length() - 1)]`; several shards may share a worker.

Use `table->run(shard, fn)` or `db->shardRun(shard, fn)` to post row work.
`invoke`/`shardInvoke` may execute inline when already on the destination worker;
`run` posts work. `db->run(fn)` targets the DB control thread. Do not assume the
DB control thread also owns a row when dedicated shard workers are configured.

Perform row/cache access and mutations on the owning shard. A strong row reference
preserves lifetime, not permission to access it from another worker. Cross-shard
posts should carry identifiers, small value snapshots, or owned handles. Re-derive
cheap values on the destination thread and keep raw DB/table back-pointers valid
by draining their work before finalization.

### Ordinary operations

| Operation | Callback / purpose |
| --- | --- |
| `find<KeyID>(shard, key, fn)` | `fn(ZdbRowRef<T>)`; empty when no row is available; searches caches then backing store |
| `insert(row, fn)` | `fn(ZdbRow<T> *)`; construct the new payload and commit |
| `update<KeyIDs>(row, fn)` | Modify an existing row and commit; `KeyIDs` lists affected secondary indices |
| `findUpd<KeyID, KeyIDs>(shard, key, fn)` | Find and enter an update callback on the row's shard |
| `del(row, fn)` | Enter a delete callback and commit |
| `findDel<KeyID>(shard, key, fn)` | Find and enter a delete callback |

Ordinary mutation callbacks receive a nullable raw row pointer; check it before
use. These differ from the non-null saga-operation callbacks described below.
`ZuFwdTuple(...)` is convenient for constructing typed key arguments.

```c++
accounts->run(shard, [accounts, shard, id, balance]() {
  ZdbRowRef<Account> row = new ZdbRow<Account>{accounts, shard};
  accounts->insert(ZuMv(row), [id, balance](ZdbRow<Account> *row) {
    if (!row) return; // Report failure through the application's own callback.
    new (row->ptr()) Account{id, balance};
    bool ok = bool(row->commit());
    // Report ok to the caller.
  });
});
```

Allocation reserves payload storage; insertion code must placement-construct the
payload. Within an update callback, change `row->data()`. Call `row->commit()`
inside the mutation callback to accept the mutation, or `row->abort()` to abandon
it. Returning without committing causes the operation wrapper to abort it.
Do not retain an open mutation/raw callback row and commit later: perform any
asynchronous preparation before entering the mutation callback.

`commit()` returns a replication I/O buffer reference. Its boolean value indicates
local commit acceptance. Persistence and replication proceed asynchronously;
this return value is not a disk-flush acknowledgment or a replica quorum result.
Backend write failures are reported asynchronously and can deactivate the DB.
An insert is not an application-level “create only if absent” transaction:
backend primary-key collisions are handled idempotently. Design application
identity and duplicate-request handling explicitly.

## Queries and barriers

`count()` without arguments is the table's maintained count, not a backing-store
query. For an indexed count use `count<KeyID>(groupKey, fn)`, whose callback
receives `ZuUnion<void, uint64_t>`: a count on success, `void` on query error
(with diagnostics logged).

`selectKeys<KeyID>(groupKey, limit, fn)` returns key tuples;
`selectRows<KeyID>(groupKey, limit, fn)` returns complete row tuples. Continue
from a key using `nextKeys` or `nextRows` with `(key, inclusive, limit, fn)`.
The callback receives `(ZuUnion<void, Tuple>, unsigned count)`, with a typed
tuple for each result and a `void`/zero-count end marker. `count` is the number
of results emitted so far for that query. A select error also delivers the empty
marker, logs diagnostics, and triggers DB failure; that marker alone does not
distinguish successful exhaustion from failure.

These queries access the backing store, bypassing the row cache. They are ordered
on its work queue after writes already enqueued when the query is issued. This
provides an ordering point for observing those writes. It is not a snapshot across
concurrent application work: later mutations can run while results are in flight.
Use bounded pages and a continuation key for large scans. Treat returned tuples
as read-only results, not mutable `ZdbRow` objects. Process borrowed string/span
data during the callback or copy it into owned storage before retaining it.

Query callbacks arrive through the store callback path; do not assume they execute
on the row's shard or the DB thread. Post any resulting row work to its owner.

There is no public `Zdb::barrier()`/`flush()` method. Choose the completion point
that matches the work:

- For a shard operation, continue from its callback. A marker posted to a worker
  orders preceding queue work but does not wait for an earlier asynchronous lookup
  that has yielded and whose result has not yet arrived.
- For several independent operations, collect their completion callbacks on one
  owner thread and continue after all have arrived. This avoids sharing a mutable
  counter across shard threads.
- To observe writes through a backend query, issue that query only after every
  relevant producer has enqueued its writes. Merely posting to all shards does
  not establish a database-wide backend barrier.
- From an external orchestration thread, `ZmBlock` or a semaphore can wait for an
  explicit callback. Never wait on a worker whose progress is required to produce
  that callback. See `accountBalance()` in the example.

None of these queue barriers establishes synchronous replication durability.
The supported teardown boundary is completed `stop()` followed by `final()`.

## Sagas

A saga is a serialized application intent executed as a fixed sequence of steps.
Each forward step describes one table mutation. If a step fails, the runner
compensates the previously completed steps in reverse order. Sagas provide
recovery and compensation across shards; they do not isolate intermediate
effects from other requests or provide an atomic multi-row transaction.

### Definition and context

```c++
struct Context : public ZmPolymorph {
  ZdbTable<Account> *accounts = nullptr;
  ZdbTable<Transfer> *transfers = nullptr;
};

struct BalanceTransfer : public ZdbSagaBase<Context> {
  using Base = ZdbSagaBase<Context>;
  using Base::context;
  using Base::saga;
  using Type = ZuStringT<"balanceTransfer">;
  enum { NSteps = 4 };
  // Serializable request fields and ZdbSagaStep definitions.
};

using Sagas = ZuTypeList<BalanceTransfer>;
using DB = ZdbSagaDB<Context, Sagas>;
using Saga = ZdbMSaga<Sagas>;
```

Supply `Context` once as a DB template argument. Every definition in `Sagas` must
derive from `ZdbSagaBase<Context>` and declare its `Base` alias. Definitions have
unique stable `Type` strings, positive `NSteps`, and contiguous step numbers
starting at zero. Serialize the request payload with `ZfbStruct`; the base
pointers are runtime data and must not be serialized. Aggregate construction
includes an empty base, for example `BalanceTransfer{{}, ...}`.

After table registration, populate a `ZmRef<Context>` and pass it to
`db->sagas(ZuMv(context))`. The DB retains that reference until typed finalization.
The same context instance serves all live and recovered sagas. Context need not
be aliased inside each definition. The runner populates `context` and `saga` as
raw pointers once, before that definition's first invocation. Do not replace
them. Context may be null only if the application never dereferences it.
Shared context does not make its mutable members safe for concurrent shard access.

### Steps and operation callbacks

`ZdbSagaStep(step, tableID, Insert|Update|Delete)` declares an operator template
with `unsigned Step`, defaulted `bool Fwd = true`, and `typename Complete`.
Its only argument is `Complete &&complete`; return `{}` for the macro's metadata
return type. The runner calls `operator()<Step>()` for forward execution and
`operator()<Step, false>()` for rollback. Select application behavior with
`if constexpr (Fwd)`. The last step is never reversed and can use `ZuAssert(Fwd)`.

The following is the insert/compensating-delete pattern used in the example:

```c++
ZdbSagaStep(0, transfer, Insert) {
  context->transfers->run(transferShard(transferID),
    [this, complete = ZuMv(complete)]() mutable {
      auto shard = transferShard(transferID);
      if constexpr (Fwd) {
        ZdbRowRef<Transfer> row =
          new ZdbRow<Transfer>{context->transfers, shard};
        saga->insert(context->transfers, ZuMv(row), ZuMv(complete),
          [this](ZdbRow<Transfer> *row, auto &&complete) {
            new (row->ptr()) Transfer{
              transferID, fromID, toID, amount, TransferStatus::Pending};
            complete(bool(row->commit()));
          });
      } else {
        saga->findDel<0>(context->transfers, shard, ZuFwdTuple(transferID),
          ZuMv(complete), [](ZdbRow<Transfer> *row, auto &&complete) {
            complete(bool(row->commit()));
          });
      }
    });
  return {};
}
```

Saga `insert`, `update`, `findUpd`, `del`, and `findDel` accept the completion
before the application lambda. The wrapper owns it and calls the lambda as
`fn(nonNullRow, ZuMv(complete))`. Use `auto &&complete` to bind it without another
move construction. Capture only `this` when that supplies the application data.
Outer asynchronous posts retain it with `[this, complete = ZuMv(complete)]`.
Do not capture it by reference across a post; explicitly move it into any work
that outlives the callback.

The wrapper suppresses the application lambda on null: null insert/update
callbacks complete false; null delete callbacks complete true, silently. This
includes absent deletes in either direction. Saved forward replay may also skip
the application mutation lambda entirely. Consequently, required external side
effects must not depend on that lambda executing on every recovery attempt.
Use ordinary table APIs only for work intentionally outside the saga journal.

There is a current distinction between a null table-operation callback and a
missing row detected earlier by forward saga preparation/replay: the latter
uses the framework's `OpResult::Missing` failure path and deactivates the DB.
The non-null callback guarantee does not imply every lookup/storage failure is
converted to orderly application rollback. In particular, validate application
preconditions explicitly when they must produce an ordinary business failure.

### The completion contract

Treat each supplied completion as a single-use continuation. Complete or transfer
ownership of it on every application path; dropping it during a live run can
strand the saga and prevent graceful stop. Never invoke copies twice. Do not
throw across the callback boundary.

| Direction / result | Application obligation and runner action |
| --- | --- |
| Forward `complete(true)` | Call only after the step's intended effect has committed, or a legitimate no-op has succeeded. Runner advances to the next step. |
| Forward `complete(false)` | The failed step must leave no effect requiring compensation. Runner deletes any failed-step intent, then reverses steps `Step - 1` down to zero. The failed step itself is not reversed. |
| Reverse `complete(true)` | Compensation, including an already-compensated no-op, has succeeded. Runner deletes the original forward step intent before moving to the preceding step. |
| Reverse `complete(false)` | Intentional abandonment after the application's best cleanup effort. Runner logs the saga type/ID/step at `Error`, stops processing that saga without calling its terminal completion, retains its journal records, and continues other work. The application owns any remaining cleanup. |

Call `complete(bool(row->commit()))` after the mutation, as the final use of the
definition/continuation in that callback. Completion can advance execution and
release ownership; do not access `this`, `saga`, or the moved completion afterward.
Returning from the step operator only returns metadata; it does not finish a step.

Do not report forward false after a committed effect that still needs undoing.
Abort uncommitted mutations or finish your own cleanup first. Keep compensation
silently idempotent. An absent reverse delete is handled automatically, but
inverse updates such as adding back a debit need application reasoning about
concurrent changes and crash/retry behavior. Persist any data needed to restore
state in the original saga payload; mutating the in-memory definition does not
automatically resave that payload. The framework does not journal arbitrary
network calls or provide exactly-once external effects.

The declared table and operation describe the forward mutation. Use that table,
the correct shard, and one journaled mutation per forward step. Do not yield
between entering its mutation callback and commit. Asynchronous reads/preparation
may precede the operation; completion must remain owned throughout.

### Submission and terminal outcome

```c++
ZmRef<Saga> request = new Saga{};
request->init(BalanceTransfer{{}, transferID, fromID, toID, amount});
bool queued = db->saga(shard, sagaID, ZuMv(request),
  [](bool admitted) { /* admission result */ },
  [](bool success) { /* terminal result for this live submission */ });
```

`sagaID` identifies the intent within its saga type; it is separate from payload
identifiers. The submission `shard` locates the main intent and need not be the
shard of each target mutation. Choose IDs and duplicate-request policy at the
application level.

- Immediate `false`: the call could not queue; neither callback runs.
- `submit(false)`: asynchronous admission rejection; terminal completion does not
  run. Examples include an inactive/unconfigured DB, invalid shard, or invalid
  saga object.
- `submit(true)`: the main intent was locally committed and accepted for
  execution. Like row commit, this is not a synchronous backend durability
  acknowledgment.
- Application `complete(true)`: final forward success followed by normal journal
  cleanup.
- Application `complete(false)`: forward failure followed by all necessary
  successful compensation and terminal cleanup. The DB remains available.

Submission and terminal callbacks run on the DB thread after the associated
internal state changes. They must return promptly and must not throw. Their types
are templated, decay-owned at the asynchronous boundary, and moved through the
continuation chain; move-only callables are supported. Keep captures small. The
callbacks are not stored in DB-wide handlers or persisted.

If activation changes while admission is in flight, `submit(false)` can arrive
after the intent has already committed; that intent is left for recovery. Thus
an admission failure during failover is not proof that no durable request exists.
Reconcile by application identity before blindly retrying. Likewise, a state
change triggered by `submit(true)` can defer execution to recovery.

The live run has at most one terminal notification. A crash, deactivation, or
internal storage/invariant failure can abandon its continuation without delivering
one. Reverse `complete(false)` explicitly guarantees no terminal notification;
recovered execution uses a no-op application completion. Applications must
reconcile interrupted requests from durable application state. Internal failure
is not reported as orderly `complete(false)`.

### Recovery and rollback persistence

The internal `saga` table holds serialized intents, `saga_step` holds forward
step intents and target update numbers (UNs), and `saga_type` records step
metadata. Their namespace is distinct from application tables. Catalog validation
checks compatible definitions at activation; keep type IDs, step ordering,
operation/table metadata, and payload decoding compatible with outstanding
intents. Changing the bundle is not a migration for already-persisted work.

Forward recovery uses recorded UNs to execute or skip mutations without blindly
repeating an effect. Recovery finishes before application `upFn`; malformed
intents, incompatible metadata, or replay that cannot make progress fail
activation. Reads may run again even when a mutation body is skipped.

Rollback direction and progress are entirely in memory. Reverse operations use
ordinary table mutation paths, write no reverse intents, and reserve no saga UNs.
After compensation succeeds, its original forward intent is deleted; then the
preceding reverse step runs. After all reversals, the main intent is deleted.
Forward success deletes the main intent and cleans remaining step intents.

A crash or leader loss during rollback abandons that in-memory decision. Recovery
starts in forward mode at step zero using whatever original intents remain. It
may succeed this time; if it fails, compensation begins again. There is no durable
rollback cursor or callback outbox. The window between compensation and deletion
of its original intent is part of the application recovery model, not an atomic
pair. Test interruption points and business invariants accordingly.

Reverse `complete(false)` has the same persistence outcome without terminating
the process: the saga and its remaining step intents stay in the journal, while
the in-memory run and recovery reservations are released so unrelated work can
continue. A later service run replays the saga forward from step zero. This gives
operators an opportunity to repair the compensation failure before restarting;
after repair, replay can complete and remove the retained journal records.

## Replication and activation

Zdb organizes configured hosts into a chain from leader to followers. Election
considers recovered database state, active status, and configured priority.
Recovery loads backing-store state and, where needed, catches up from the leader.
Replication is asynchronous and includes mutation ordering metadata and commit
messages. Do not infer quorum acknowledgment or synchronous consistency from
the RAFT-like election mechanism.

For multiple hosts, configure a distinct `hostID` per process and the same host
set with `ip`, `port`, and `priority`; use consistent table definitions, saga
bundles, and shard mappings. `standalone: true` is only valid for a single-host
configuration. DB options include `heartbeatFreq`, `heartbeatTimeout`,
`reconnectFreq`, and `electionTimeout` (seconds), plus `nAccepts`.
See [zdbsagareptest.cc](itest/zdbsagareptest.cc) for a complete two-host setup.

The application serves leader-owned work only between activation and deactivation.
Stop accepting work promptly in `downFn`; references and queued callbacks alone
do not prove that the host remains leader. Saga epoch checks discard stale
continuations. Applications still own coordination with external services and
the visibility of intermediate saga effects.

## Backend stores

Pass an explicit `ZmRef<Zdb_::Store>` to `init()` or omit it and configure
`store.module` to load a module exporting the `ZdbStore` entry point. `store.preload`
controls module preloading. A `store` configuration object is required even when
passing an explicit store instance.

### In-memory

`ZdbMem::Store` from `<zlib/ZdbMemStore.hh>` is useful for tests and embedded
applications that do not require disk persistence. Supply `store.thread` and pass
`new ZdbMem::Store{}` via a `ZmRef`. Process loss loses that store's data; replication
can preserve copies only on surviving hosts. Do not confuse a test store that
preserves state across simulated restarts with persistence across process death.

### PostgreSQL

`ZdbPQ::Store` lives in [zdb_pq](../zdb_pq/). Its configuration requires `thread`
and a libpq `connection` string; `replicated` defaults to false. It uses prepared
statements and binary I/O. Install the PostgreSQL extensions required by the
types in use, including the project's custom types and `pguint` where applicable;
the type mapping is documented in [ZdbPQ.hh](../zdb_pq/src/ZdbPQ.hh).

```text
store: {
  module: "/path/to/libZdbPQ.so",
  thread: store,
  connection: "host=localhost dbname=app user=app",
  replicated: false
}
```

When `replicated: true`, the backend is declaring that storage replication is
provided externally; Zdb changes follower persistence behavior accordingly.
Enable this only when the backend deployment actually supplies that contract.
This flag does not configure PostgreSQL replication or make application commits
synchronous. PostgreSQL integration tests require `ZDB_MODULE` and `ZDB_CONNECT`;
see [zdbsagatest.cc](../zdb_pq/itest/zdbsagatest.cc).

### Implementing a store

Implement `Store` and `StoreTbl` in [ZdbStore.hh](src/ZdbStore.hh). The contract
includes synchronous `init`/`final`, asynchronous start/stop and table open/close,
count/select/find, recovery by shard/UN, and idempotent writes with completion.
Respect write/query queue ordering, internal table namespaces, schemas, and buffer
ownership. `InitData::replicated` advertises externally replicated storage.

On open, return row count, per-shard last UNs, and the table's last sequence number
(SN), including trailing deletes. Preserve deletion high-water marks even when
the table becomes empty; the interface describes the most-recent-deletes metadata
needed for this. Writes must retain the replication buffer until their commit
callback and report errors through the prescribed result/failure paths. Close and
stop must finish pending work and emit its callbacks before completing, so the
DB's final shard drain can safely release tables.

## Examples and verification

- [example/zdbsaga.cc](example/zdbsaga.cc): standalone two-shard balance transfer,
  full lifecycle, ordinary row operations, saga setup and submission.
- [test/ZdbSagaTest.cc](test/ZdbSagaTest.cc): admission, rollback, callback lifetime,
  catalog validation, replay, and interruption cases.
- [itest/zdbsagareptest.cc](itest/zdbsagareptest.cc): replica promotion and saga
  recovery; [integration-test notes](itest/README.md).
- [../zdb_pq/itest/zdbsagatest.cc](../zdb_pq/itest/zdbsagatest.cc): persistent backend
  restart/reconnect and saga recovery scenarios.

Run the relevant binaries from the configured build tree. For debugging or memory
checks, use `./libtool exec gdb ...` or `./libtool exec valgrind ...`; do not invoke
`.libs` binaries directly.
