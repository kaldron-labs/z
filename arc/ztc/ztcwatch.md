# Ztc Lifecycle Watcher Development Plan

## Purpose and dependency

This plan is the first precursor in
`ztcwatch.md -> zdbtel.md -> plan.new.md`. It adds synchronous lifecycle
publication to the existing plain `Ztc` telemetry object hierarchy without
adding the telemetry service, wire protocol, FlatBuffers schema,
subscriptions, alerts, or an `App` secondary index.

`zdbtel.md` may begin only after this plan's acceptance criteria pass. It
applies the DB watcher contract while migrating the database telemetry values
and metadata. `plan.new.md` then treats both precursors as established.

The objective is deliberately narrow:

- retain each existing `all` traversal;
- add one synchronous watcher to each root manager and containing child
  collection needed by telemetry discovery;
- publish fully initialized objects before add callbacks and keep objects valid
  through delete callbacks;
- make every published queue pointer stable for its entire published lifetime;
- provide the ordering needed by a later consumer to maintain unique secondary
  indices; and
- preserve all originating telemetry values, counters, sampling,
  synchronization, transport behavior, and accuracy.

`GUIDELINES.md` is authoritative throughout implementation and review.

## Fixed contracts

### Single synchronous watcher

Every root `Ztc::*Mgr` registry gains the same surface. For example:

```cpp
using AddFn =
  ZmFn<void(Heap *), ZmFnHeapID<"Ztc.WatchFn">>;
using DelFn =
  ZmFn<void(Heap *), ZmFnHeapID<"Ztc.WatchFn">>;

static void watch(AddFn, DelFn);
static void unwatch();
```

There is one callback slot per event, not a subscriber list. A null callback is
the unwatched sentinel; no separate watched `bool` is maintained. `watch`
requires null slots and installs both callbacks while holding the manager lock.
A second registration without `unwatch` is an invariant failure, not a silent
replacement.

Each root manager's registry container uses `ZmNoLock` behind one external
lifecycle lock. A newly introduced `m_watchLock` is a `ZmRWLock`, with local
`Lock`/`Guard` aliases where `ZmGuard<ZmRWLock>` would otherwise repeat.
Latency-sensitive incumbent `ZmPLock` paths remain `ZmPLock`; their existing
lock also guards the registry and watcher slots instead of adding or replacing
it with `ZmRWLock`. `add`, `del`, `watch`, and `unwatch` take the write lock;
`all` explicitly uses `ZmReadGuard` for the traversal. Registry mutation and
synchronous callback delivery are one coarse-lock transaction, not split
between an internally locked container and a second callback lock.

`add` inserts the fully initialized object into the manager collection before
calling `AddFn` synchronously under the manager's lifecycle serialization.
`del` calls `DelFn` synchronously while the object is still valid, then removes
it. `unwatch` takes the same manager lock and nulls the slots; returning from
`unwatch` therefore also proves that an in-flight callback serialized by that
lock has completed.

Callbacks are never issued by a telemetry base constructor or destructor while
the most-derived object is only partially constructed or destroyed.

The watcher callback may run on any producer thread. It must be brief, but its
synchronous lifetime is intentional: a later consumer may take its own small
index lock in the callback, and object destruction cannot pass the delete
callback until that consumer has removed the pointer.

### Root managers

The root registries are:

- `HeapMgr`
- `HashMgr`
- `ThreadMgr`
- `MxMgr`
- `HubMgr`
- `DBMgr`

Each preserves its established `AllFn` and `all` names. `QueueMgr` remains a
traversal interface for queues owned by another object; queues are published by
the corresponding Mx or Hub child watcher rather than through a second global
registry.

`DBMgr` is consistently `DBMgr::all`, not `allDBs`. Object-owned traversals
retain their qualified names, including `DB::allDBHosts` and
`DB::allDBTables`.

### Child collections

Mx exposes the same one-watcher pattern for its live Connection collection.
Scheduler queues are fixed children and need no lifecycle callback slots.

`Mx` supplies Connection callbacks:

```cpp
virtual void watch(AddCxnFn, DelCxnFn) = 0;
virtual void unwatch() = 0;
```

Hub and DB child notification is centralized through their root managers.
`HubMgr` retains the ordinary root Hub add/delete portion and adds the required
child portion:

```cpp
static void watch(
  AddLinkFn, DelLinkFn,
  AddPoolFn, DelPoolFn,
  AddQueueFn, DelQueueFn);

private:
  friend struct Hub;
  static void linkAdded_(Link *);
  static void linkDeleted_(Link *);
  static void poolAdded_(Pool *);
  static void poolDeleted_(Pool *);
```

The root and child slots are the two portions of the one Hub-manager watcher
record, not independent observer lists. Its single `unwatch()` nulls and drains
both portions. `HubMgr::add(Hub *)`/`del(Hub *)` manage only Hub registry
membership. The private `linkAdded_`/`linkDeleted_` and
`poolAdded_`/`poolDeleted_` functions are lifecycle notifications, do not add
those objects to a HubMgr collection, and are accessible only to the friended
`Hub`. `Hub` exposes protected forwarding functions of the same names so only
an owning concrete Hub can report its child lifecycle. Link notification also
emits the corresponding queue callbacks in the ordering below.

Child registration occurs only after the child is fully initialized.
Deregistration occurs before child destruction. Parent publication precedes
publication or seeding of its existing children. Parent removal follows
deletion of all published children.

### Hub child ordering

A Hub link addition calls:

1. link added;
2. Rx queue added;
3. Tx queue added.

Link deletion is the exact reverse:

1. Tx queue deleted;
2. Rx queue deleted;
3. link deleted.

A pool addition calls:

1. pool added;
2. pool Tx queue added.

Pool deletion calls:

1. pool Tx queue deleted;
2. pool deleted.

These callbacks are lifecycle notifications only. They add no counter,
resampling, telemetry-accuracy, transport, flow-control, or backpressure
behavior.

### Stable queue publication

Every queue pointer delivered by `allQueues` or a child watcher must remain at
a stable address with an immutable queue type until its delete callback
returns.

The current stack-local mutable `TelQueue` pattern in link `allQueues`
implementations is replaced by two persistent member adapters, one Rx and one
Tx. Pool adapters are persistent members for their published lifetime.
Scheduler queues already backed by persistent scheduler objects retain that
identity; any transient wrapper is removed.

Queue keys remain structured rather than flattening their components into one
string:

```cpp
virtual ZuTuple<const ZuID &, const ZuID &, QueueType::T>
  telKey() const = 0;
```

The components are generically `ownerID`, `id`, and `type`, and
`QueueTelemetry` exposes the same three fields. Link queues use
`{hubID, linkID, Rx}` and `{hubID, linkID, Tx}`; QueueType discriminates the
two persistent adapters. Pool queues use `{hubID, poolID, Tx}`. Scheduler
queues use `{schedulerID, threadID, Thread}`. The key is never flattened,
suffixed, concatenated, or truncated.

### Traversal and watcher allocation identities

This precursor retains the incumbent traversal heap IDs while establishing the
watcher behavior:

| callback alias | exact `ZmFnHeapID` |
| --- | --- |
| `HeapMgr::AllFn` | `"Ztc.Heap.AllFn"` |
| `HashMgr::AllFn` | `"Ztc.Hash.AllFn"` |
| `ThreadMgr::AllFn` | `"Ztc.Thread.AllFn"` |
| `MxMgr::AllFn` | `"Ztc.Mx.AllFn"` |
| `QueueMgr::AllFn` | `"Ztc.Queue.AllFn"` |
| `HubMgr::AllFn` | `"Ztc.Hub.AllFn"` |
| `DBMgr::AllFn` | `"Ztc.DB.AllFn"` |
| `Mx::AllCxnsFn` | `"Ztc.Mx.AllCxnsFn"` |
| `Hub::AllLinksFn` | `"Ztc.Hub.AllLinksFn"` |
| `Hub::AllPoolsFn` | `"Ztc.Hub.AllPoolsFn"` |
| `DB::AllDBHostsFn` | `"Ztc.DB.AllDBHostsFn"` |
| `DB::AllDBTablesFn` | `"Ztc.DB.AllDBTablesFn"` |

All lifecycle add/delete aliases use the one shared watcher callback heap ID:

| callback alias | exact `ZmFnHeapID` |
| --- | --- |
| `HeapMgr::AddFn`, `HeapMgr::DelFn` | `"Ztc.WatchFn"` |
| `HashMgr::AddFn`, `HashMgr::DelFn` | `"Ztc.WatchFn"` |
| `ThreadMgr::AddFn`, `ThreadMgr::DelFn` | `"Ztc.WatchFn"` |
| `MxMgr::AddFn`, `MxMgr::DelFn` | `"Ztc.WatchFn"` |
| `HubMgr::AddFn`, `HubMgr::DelFn` | `"Ztc.WatchFn"` |
| `DBMgr::AddFn`, `DBMgr::DelFn` | `"Ztc.WatchFn"` |
| `Mx::AddCxnFn`, `Mx::DelCxnFn` | `"Ztc.WatchFn"` |
| `HubMgr::AddLinkFn`, `HubMgr::DelLinkFn` | `"Ztc.WatchFn"` |
| `HubMgr::AddPoolFn`, `HubMgr::DelPoolFn` | `"Ztc.WatchFn"` |
| `HubMgr::AddQueueFn`, `HubMgr::DelQueueFn` | `"Ztc.WatchFn"` |
| `DBMgr::AddHostFn`, `DBMgr::DelHostFn` | `"Ztc.WatchFn"` |
| `DBMgr::AddTableFn`, `DBMgr::DelTableFn` | `"Ztc.WatchFn"` |

`zdbtel.md` intentionally replaces these incumbent IDs across all `Ztc*`
headers with `Ztc::AllFnHeapID` and `Ztc::WatchFnHeapID` from
`ZtcTypes.hh`. This precursor's tests are updated as part of that breaking
migration. Any additional callback introduced here must first be entered in
this inventory; default `ZmFn` heaps are not acceptable.

## Consumer contract for `zdbtel.md` and `plan.new.md`

This precursor does not implement `Ztc::App`, but its tests must prove the
contract that App will consume.

A consumer registers `watch` before calling `all`. That closes the creation
gap but permits the same object to be observed once by the callback and once by
the initial traversal. The consumer must therefore accept the same key and
same pointer idempotently. The same key with a different simultaneously live
pointer is an invariant failure.

For an Mx observed through a root callback or root traversal, the consumer
installs the object's child watcher before traversing its existing children.
Hub and DB child watchers are manager-owned, so the consumer installs both
watcher overloads before the corresponding manager `all`. Hub children are
then seeded through `Hub::allLinks`, `Hub::allPools`, and each published
link/pool queue traversal; DB children are seeded through
`DB::allDBHosts` and `DB::allDBTables`.

Deletion ordering lets the consumer remove all child pointers before removing
the parent pointer. A synchronous delete callback may block briefly on the
consumer's index lock, so an object cannot be destroyed while that consumer is
sampling it under the same lock.

The producer watcher never owns or mutates a future subscriber watch list. A
future App may synchronously update only its locked object index and then post
an owned key event to its worker after releasing that lock.

## Generic DB watcher surface

The precursor establishes the generic DB lifecycle surface in `zv` without
introducing a `zv` dependency on `Zdb`:

```cpp
struct DBTable {
  virtual DBTableKey telKey() const = 0;
  virtual void telemetry(DBTableTelemetry &) const = 0;
};

struct DBHost {
  virtual DBHostKey telKey() const = 0;
  virtual void telemetry(DBHostTelemetry &) const = 0;
};

struct DB {
  using AllDBTablesFn =
    ZmFn<void(DBTable *), ZmFnHeapID<"Ztc.DB.AllDBTablesFn">>;
  using AllDBHostsFn =
    ZmFn<void(DBHost *), ZmFnHeapID<"Ztc.DB.AllDBHostsFn">>;

  virtual DBKey telKey() const = 0;
  virtual void telemetry(DBTelemetry &) const = 0;
  virtual bool start() = 0;
  virtual bool stop() = 0;
  virtual unsigned allDBHosts(AllDBHostsFn) const = 0;
  virtual unsigned allDBTables(AllDBTablesFn) const = 0;

protected:
  static void hostAdded_(DBHost *);
  static void hostDeleted_(DBHost *);
  static void tableAdded_(DBTable *);
  static void tableDeleted_(DBTable *);
};

struct DBMgr {
  using AllFn = ZmFn<void(DB *), ZmFnHeapID<"Ztc.DB.AllFn">>;
  using AddFn = ZmFn<void(DB *), ZmFnHeapID<"Ztc.WatchFn">>;
  using DelFn = ZmFn<void(DB *), ZmFnHeapID<"Ztc.WatchFn">>;
  using AddHostFn =
    ZmFn<void(DBHost *), ZmFnHeapID<"Ztc.WatchFn">>;
  using DelHostFn =
    ZmFn<void(DBHost *), ZmFnHeapID<"Ztc.WatchFn">>;
  using AddTableFn =
    ZmFn<void(DBTable *), ZmFnHeapID<"Ztc.WatchFn">>;
  using DelTableFn =
    ZmFn<void(DBTable *), ZmFnHeapID<"Ztc.WatchFn">>;

  static void add(DB *);
  static void del(DB *);
  static unsigned all(AllFn);
  static void watch(AddFn, DelFn);
  static void watch(
    AddHostFn, DelHostFn, AddTableFn, DelTableFn);
  static void unwatch();

private:
  friend struct DB;
  static void hostAdded_(DBHost *);
  static void hostDeleted_(DBHost *);
  static void tableAdded_(DBTable *);
  static void tableDeleted_(DBTable *);
};
```

The DB/host/table key and plain telemetry types are completed by `zdbtel.md`;
this precursor establishes their generic C++ interface shape so the lifecycle
can be wired, but adds no schema, metadata, serialization, or new sampling.
`DBMgr` follows the existing Hub-manager pattern: a
library-lifetime singleton, pointer-unique `ZmRBTree`, `ZmPLock`, and named heap
ID. A DB registers once after successful initialization and deregisters only
after it is stopped. Hosts and tables remain children, never global roots.
`DBMgr::add`/`del` therefore accept only DB roots. The private child
notification functions are accessible only to the friended `DB`; protected
`DB` forwarding functions let each concrete owning DB report host/table
lifecycle without exposing those notification entry points to unrelated
callers.

`Zdb_::DB`, `Host`, and `AnyTable` wire these generic root/child lifecycle
contracts to their existing collection ownership without changing telemetry
aggregation, traversal, synchronization, or counters.

## Implementation plan

### Phase 1: Root manager watchers

1. Audit each root manager's collection, lock, add/delete order, traversal
   iterator lifetime, and object construction/destruction call sites.
2. Add the exact `AddFn`/`DelFn` aliases, the two callback slots, `watch`, and
   `unwatch` to HeapMgr, HashMgr, ThreadMgr, MxMgr, and the root portion of
   HubMgr.
3. Invoke callbacks synchronously in the specified publication order while
   preserving the manager's existing uniqueness and locking model.
4. Add focused manager tests with one or two mock objects per case. Test
   callback identity, duplicate registration rejection, add/delete order,
   object validity during delete, and `unwatch` drainage.
5. Run the affected `zm` and `zi` tests and audit the changes against every
   applicable `GUIDELINES.md` red or amber flag.

### Phase 2: Mx, Hub, and queue children

1. Add Mx Connection child aliases and its single two-callback watcher.
   Scheduler queues remain fixed children traversed through `allQueues`; do
   not add dead queue callback slots or synthetic queue lifecycle events.
2. Add the six Hub child callback aliases and slots to the same HubMgr watcher
   record as the root slots.
3. Centralize link, pool, and queue notifications through the HubMgr callback
   record and implement the exact ordering specified above. Keep
   `HubMgr::add`/`del` exclusive to Hubs; owning Hubs use the protected
   `linkAdded_`/`linkDeleted_` and `poolAdded_`/`poolDeleted_` forwarders into
   HubMgr's private friended notification entry points.
4. Replace transient link/pool queue wrappers with persistent immutable member
   adapters. Ensure `allQueues` and watcher callbacks return the same addresses
   and keys for the same published queues.
5. Use structured queue keys and telemetry identity fields. Link Rx/Tx queues
   are `{hubID, linkID, Rx/Tx}`, pool queues are `{hubID, poolID, Tx}`, and
   scheduler queues are `{schedulerID, threadID, Thread}`.
6. Add low-volume mock tests for order and pointer stability, plus focused TCP,
   TLS, QUIC, multiplexer, scheduler, and pool tests.
7. Run a top-level rebuild because the affected `Ztc` headers are public, then
   perform the complete `GUIDELINES.md` review for the touched modules.

### Phase 3: Generic DB root and children

1. Add the generic DB root manager and its host/table child watcher overload
   with the incumbent traversal IDs and shared watcher ID inventoried above.
2. Wire Zdb DB/host/table owners to register only fully initialized objects and
   report child additions/deletions through `DB`'s protected forwarding
   functions, deleting children before their parent.
3. Preserve `DBMgr::all`, `DB::allDBHosts`, and `DB::allDBTables`; do not add
   aliases, forwarding APIs, another dispatcher, or a global host/table
   registry.
4. Test duplicate root registration, root/child traversal counts,
   watch-before-all overlap, child churn, callback allocation identity,
   unwatch drainage, and delete-time object validity with minimal mock data.
5. Run focused `zv` generic-interface and `zdb` tests, followed by a top-level
   rebuild and `GUIDELINES.md` audit.

### Phase 4: Cross-hierarchy lifetime audit

1. Exercise root and child add/delete from non-consumer threads while callbacks
   take a mock `ZmPLock`-protected pointer index.
2. Hold the mock index iterator during `telemetry`, start deletion on another
   thread, and prove deletion cannot pass its synchronous callback until the
   iterator releases the lock.
3. Race `watch` installation with the initial `all` traversal and prove the
   same-key/same-pointer idempotence rule yields one consumer entry without a
   creation gap.
4. Repeatedly watch, seed, churn, unwatch, and destroy each hierarchy using
   callbacks, barriers, `ZmBlock`, or `ZmSemaphore`; do not use sleeps or
   polling as completion conditions.
5. Run the full affected build/test matrix and a final line-by-line
   `GUIDELINES.md` audit. Repair every unaccepted red or amber finding.

## Code references

- `zm/src/ZtcHeap.hh`, `ZmHeap.cc`, `ZtcHash.hh`, `ZmHashMgr.cc`,
  `ZtcThread.hh`, `ZmThread.cc` - root manager watcher contracts and hooks.
- `zm/src/ZtcQueue.hh`, `ZmScheduler.hh`, `ZmScheduler.cc` - persistent
  scheduler queue traversal.
- `zi/src/ZtcMx.hh`, `ZiMultiplex.hh`, `ZiMultiplex.cc` - Mx root and
  Connection child lifecycle plus fixed queue traversal.
- `zi/src/ZtcHub.hh`, `ZtcHub.cc`, `ZtcLink.hh`, `ZtcPool.hh` - Hub root and
  six-callback child lifecycle and ordering.
- `ztcp/src/Ztcp.hh`, `ztls/src/Ztls.hh`, `zquic/src/ZquicLink.hh` -
  persistent link Rx/Tx queue adapters and structured queue keys.
- concrete pool implementations - persistent Tx queue adapters and ordered
  queue notification.
- `zv/src/ZtcDBTable.hh`, `ZtcDBHost.hh`, `ZtcDB.hh`, `ZtcDB.cc` - generic DB
  root/child lifecycle surface.
- `zv/test/ZtcMgrTest.cc` - generic manager and consumer-contract watcher
  tests.
- `zdb/src/Zdb.hh`, `Zdb.cc`, and focused DB tests - generic lifecycle
  implementation over existing ownership and traversal.
- affected module `Makefile.am` files - focused watcher tests and any new
  source/install entries.

## Detailed test plan

- Root manager tests prove a second watcher is rejected, null callbacks are the
  sole unwatched sentinel, `unwatch` drains an in-flight callback, and add/delete
  callbacks see fully valid objects.
- Watch-before-all tests deliberately overlap one add with initial traversal.
  The mock consumer accepts a repeated same-key/same-pointer observation and
  rejects a same-key/different-live-pointer collision.
- Hub tests prove link-add, Rx-add, Tx-add and Tx-delete, Rx-delete,
  link-delete order. Pool tests prove pool-add, Tx-add and Tx-delete,
  pool-delete order.
- Queue tests compare immutable types, exact keys, and telemetry IDs for TCP,
  TLS, QUIC, scheduler, and pool queues. Lifecycle callback addresses apply
  only to live Hub Link/Pool children, not fixed scheduler queues.
- Mx tests cover fixed scheduler-queue traversal, accepted Connection churn,
  parent publication, and Connection deletion before Mx deletion.
- DB tests cover root uniqueness, host/table seeding and churn, child deletion
  before DB deletion, and registration only across the initialized/stopped
  lifetime.
- Allocation tests inspect every traversal callback's incumbent heap identity
  and prove every lifecycle add/delete callback uses `"Ztc.WatchFn"`.
- Lifetime tests hold a mock consumer index lock during sampling and prove
  deletion remains blocked in the synchronous watcher until release.
- All concurrency tests use deterministic barriers/callbacks, not timing
  sleeps, polling, or excessive object/event volumes.

## Acceptance criteria

- Every specified root manager and child collection has exactly one watcher;
  null callback slots are the only watched-state sentinel.
- `watch`, `unwatch`, add, delete, and traversal use the existing collection
  lock coherently, and `unwatch` drains active callbacks.
- Add callbacks see fully initialized, already published objects. Delete
  callbacks complete while objects are still valid and before collection
  removal/destruction.
- Parent/child and Hub link/pool/queue ordering exactly matches this plan.
- Every published queue pointer is persistent and type-stable through its
  delete callback. Keys and telemetry expose `{ownerID, id, type}`; link
  Rx/Tx keys share `{hubID, linkID}` and are discriminated by QueueType.
- Every traversal callback uses its inventoried incumbent heap ID, and every
  lifecycle add/delete callback uses `"Ztc.WatchFn"`; `zdbtel.md` owns their
  later common-alias migration.
- Watch-before-all permits idempotent same-key/same-pointer overlap without a
  creation gap and exposes a same-key/different-live-pointer collision.
- A synchronous delete callback cannot pass a mock consumer's locked sample
  and permit object destruction until sampling ends.
- No watcher changes telemetry counters, sampling, accuracy, synchronization,
  transport behavior, queue semantics, or flow control.
- Focused manager, Mx, Hub, TCP, TLS, QUIC, scheduler, pool, generic DB, and
  Zdb tests pass after a top-level build.
- The final `GUIDELINES.md` audit has no unresolved red or amber finding.

## Non-goals

- `Ztc::App`, its object indices, filters, snapshots, subscriptions, protocol,
  FlatBuffers metadata, serialization, transport endpoint, or alert handling.
- Multiple watcher registrations, observer lists, watcher tokens, or dynamic
  watcher ownership.
- Asynchronous producer publication or subscriber watch-list mutation from a
  producer callback.
- New telemetry counters, resampling, accuracy improvements, flow control,
  authentication, or transport/backpressure accounting.
- Compatibility wrappers, duplicate APIs, or retained transient queue
  adapters.
