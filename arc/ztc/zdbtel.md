# Ztc Database Telemetry Migration Plan

## Purpose and dependency

This plan follows `ztcwatch.md` and is a required precursor to
`plan.new.md`. `ztcwatch.md` first establishes the synchronous root/child
watcher contract, callback allocation identity, watch-before-`all` consumer
order, and delete-time pointer lifetime. This plan applies those contracts to
the generic database hierarchy and completes the migration of
`zdb/src/ZdbTelemetry.hh` into `zv`.

`plan.new.md` may begin only after both precursor plans meet their acceptance
criteria:

```text
ztcwatch.md -> zdbtel.md -> plan.new.md
```

The delivered state has:

- generic `Ztc::DB`, `Ztc::DBHost`, and `Ztc::DBTable` interfaces and plain
  telemetry values in `zv`;
- the DB-specific `ZfStruct`/`ZfbStruct` metadata formerly in
  `ZdbTelemetry.hh` colocated with those `ZtcDB*` values;
- `Ztc::DBMgr::all`, `watch`, and `unwatch` with the lifecycle behavior
  specified by `ztcwatch.md`;
- `Zdb_::DB`, `Host`, and `AnyTable` implementing the generic interfaces by
  filling plain values rather than serializing FlatBuffers; and
- no active `ZdbTelemetry.hh` or telemetry-schema ownership in `zdb`.

`GUIDELINES.md` is authoritative throughout implementation and review.

## Fixed module boundary

The public files are:

- `zm/src/ZtcTypes.hh`
- `zv/src/ZtcDBTable.hh`
- `zv/src/ZtcDBHost.hh`
- `zv/src/ZtcDB.hh`
- `zv/src/ZtcDB.cc`

Rename `zm/src/ZtcRAG.hh` to `zm/src/ZtcTypes.hh`. This is an intentional
breaking change: remove the old header and propagate the new include to every
dependent without a forwarding header or compatibility alias.

`ZtcTypes.hh` is the one common type/allocation header for the telemetry
hierarchy:

```cpp
namespace Ztc {
namespace RAG {
  using T = int8_t;
  enum { Off = 0, Red, Amber, Green };
}

using AllFnHeapID = ZmFnHeapID<"Ztc.AllFn">;
using WatchFnHeapID = ZmFnHeapID<"Ztc.WatchFn">;
}
```

Every `Ztc*` header in `zm`, `zi`, and `zv` includes `ZtcTypes.hh` directly
when it uses these declarations. Replace every incumbent per-manager or
per-child `all*` callback heap ID with `Ztc::AllFnHeapID`, and replace every
lifecycle add/delete callback heap literal with `Ztc::WatchFnHeapID`. Both
aliases are members of namespace `Ztc`, not global aliases or members of an
individual manager/interface. The two `ZmFnHeapID` instantiations occur only
in `ZtcTypes.hh`; callback declarations must not repeat them. This migration
covers Heap, Hash, Thread, Queue, Mx, Connection, Hub, Link, Pool, DB, DBHost,
and DBTable traversal/lifecycle surfaces and all active dependents and
allocation-identity tests.

Unlike the other `Ztc` producer headers, the three `ZtcDB*.hh` headers include
their own `ZfStruct`/`ZfbStruct` metadata. This is an intentional exception to
the general `ZtcFB.hh` enrichment rule: DB telemetry values already live in
`zv`, so colocating the migrated metadata does not introduce a lower-to-higher
layer dependency. `ZtcFB.hh` consumes these definitions and must not redeclare
their fields.

`zv` must build without including or linking `Zdb`. The generic headers
therefore must not include `ZdbLib.hh`, `ZdbTypes.hh`, or generated `zdb`
schema headers. `Zdb` remains a dependent of `zv`.

The telemetry-only FlatBuffers definitions required by the migrated
`ZfbStruct` declarations move from `zdb/src/fbs` to
`zv/src/fbs/ztc_db.fbs`, use the active `Ztc.fbs` namespace/vocabulary, and
are generated and installed by `zv/src/Makefile.am`. This one file contains
the DB table, host, and database tables and their cache-mode and host-state
enums. `plan.new.md` later includes these definitions in the service telemetry
union rather than copying them. Replication and persistence schemas such as
`zdb_.fbs` remain in `zdb`.

The migration is breaking. Do not retain aliases, forwarding telemetry
methods, compatibility schemas, or a wrapper `ZdbTelemetry.hh`.

## Generic DB types and metadata

### `ZtcDBTable.hh`

Define:

- the generic cache-mode vocabulary needed by table telemetry;
- a full-width stable table key;
- `DBTableTelemetry` with every value currently supplied by
  `Zdb_::Tel::DBTable`; and
- the abstract `DBTable` key and plain-value `telemetry(...)` functions.

The telemetry value retains table identity, shard count, configured threads,
record count, cache size, loads, misses, evictions, cache mode, and derived
RAG. Identifier storage must accept the complete implementing DB identifier
domain; do not preserve the legacy `ZuCArray<28>` truncation.

Move the corresponding `ZfbStruct` declaration from `ZdbTelemetry.hh` into
this header. Preserve key, constructor, mutable, series, delta, enum, and
synthetic-field metadata, correcting the existing duplicate constructor index
on `cacheEvictions`. The generated schema width and signedness must match the
plain value, including the full shard count and 64-bit counters.

### `ZtcDBHost.hh`

Define:

- the generic DB-host state vocabulary;
- a full-width stable host key;
- `DBHostTelemetry`; and
- the abstract `DBHost` key and plain-value `telemetry(...)` functions.

Retain IP, ID, priority, port, state, vote, and derived RAG. Move the
corresponding metadata and state-to-RAG helper from `ZdbTelemetry.hh` into this
header, using `Ztc::RAG` and the generic host-state vocabulary. State enum
ordinals must be checked against the generated schema.

### `ZtcDB.hh`

Define `DBTelemetry`, the abstract `DB` interface, and `DBMgr`. Retain DB
identity and topology, state/activity flags, table/host/peer/connection counts,
configured thread, heartbeat/reconnect/election settings, and derived RAG.
Move the corresponding metadata from `ZdbTelemetry.hh` into this header.

The abstract hierarchy exposes the traversal and watcher surface fixed by
`ztcwatch.md`:

```cpp
namespace Ztc {

struct DB {
  using AllDBHostsFn =
    ZmFn<void(DBHost *), AllFnHeapID>;
  using AllDBTablesFn =
    ZmFn<void(DBTable *), AllFnHeapID>;

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
  using AllFn = ZmFn<void(DB *), AllFnHeapID>;
  using AddFn = ZmFn<void(DB *), WatchFnHeapID>;
  using DelFn = ZmFn<void(DB *), WatchFnHeapID>;
  using AddHostFn = ZmFn<void(DBHost *), WatchFnHeapID>;
  using DelHostFn = ZmFn<void(DBHost *), WatchFnHeapID>;
  using AddTableFn = ZmFn<void(DBTable *), WatchFnHeapID>;
  using DelTableFn = ZmFn<void(DBTable *), WatchFnHeapID>;

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

} // Ztc
```

The `Ztc::AllFnHeapID` and `Ztc::WatchFnHeapID` aliases come from
`ZtcTypes.hh`; they are not redeclared in any `ZtcDB*` header. Within
`namespace Ztc`, callback declarations use the unqualified names shown above.
All root and child traversal callbacks share `Ztc::AllFnHeapID`; all lifecycle
callbacks share `Ztc::WatchFnHeapID`.

The function names are exactly `DBMgr::all`, `DB::allDBHosts`, and
`DB::allDBTables`. Do not add `allDBs`, shortened child traversal names, or a
second manager.

## `ZtcDBMgr_` implementation

Implement `ZtcDBMgr_::all` in the same way as `ZtcHubMgr_::all`: explicitly
read-lock `m_watchLock` for the complete traversal, initialize the result to
zero, increment it for each pointer actually visited, invoke the supplied
callback, and return that visited count. Do not snapshot `count_()` before
iteration.

The manager is a library-lifetime `ZmSingleton` containing one pointer-unique
`ZmRBTree` using `ZmNoLock`, one external `ZmRWLock m_watchLock`, and
`ZmRBTreeHeapID<"Ztc.DBMgr">`. It owns both watcher overloads: the root
two-slot watcher and the host/table four-slot watcher specified by
`ztcwatch.md`:

- `watch(AddFn, DelFn)` installs both null slots while holding the same
  lifecycle lock and rejects a second registration;
- `watch(AddHostFn, DelHostFn, AddTableFn, DelTableFn)` installs all four
  child slots under that same lock and rejects a second child registration;
- `add` write-locks, inserts a fully initialized DB, then invokes `AddFn`;
- `del` write-locks, invokes `DelFn` while the DB remains valid, then removes
  it; and
- `unwatch` clears all six slots under the same lock, so return drains any
  callback serialized by that lock.

Do not add a subscriber list, watched boolean, asynchronous publication, or a
second lock that weakens ordering with `all`. `DBMgr::add`/`del` accept DB
roots only. Its private, friended `hostAdded_`/`hostDeleted_` and
`tableAdded_`/`tableDeleted_` entry points receive notifications through
protected `DB` forwarders. They do not maintain host/table registries. Hosts
and tables are published after full initialization, deleted before
destruction, and every child is removed before its parent DB.

## Re-platform `Zdb` on the generic hierarchy

`Zdb_::DB`, `Zdb_::Host`, and `Zdb_::AnyTable` derive directly from
`Ztc::DB`, `Ztc::DBHost`, and `Ztc::DBTable`.

Replace the current `telemetry(Zfb::Builder &, bool)` functions and their
`ZuStructShim` serializers with functions that fill the corresponding plain
`Ztc` telemetry value. Reuse the existing reads and cache-stat aggregation
without introducing a dispatcher, lock, atomic, counter, or stable-snapshot
mechanism. Serialization no longer occurs in `zdb`.

Implement the generic key and traversal functions directly:

- DB registration occurs once after successful initialization and
  deregistration occurs after stop, child drainage, and before destruction;
- failed initialization leaves no manager entry;
- DBMgr's root and host/table watcher overloads are installed and drained
  using the contract from `ztcwatch.md`;
- existing children are traversed by `allDBHosts` and `allDBTables`, with the
  returned count incremented for each callback invocation;
- host/table creation and deletion publish synchronously in parent-first,
  child-first-on-delete order; and
- keys and telemetry identity use the same complete identifier values.

Preserve the existing telemetry inputs:

- DB: self, leader, previous, next, state, active, recovering, replicating,
  table/host/peer/connection counts, thread, and timing configuration;
- host: ID, priority, state, vote, IP, and port; and
- table: ID, shards, threads, record count, cache size, loads, misses,
  evictions, and mode.

Replace `Zdb_::CacheMode` and `Zdb_::HostState` dependencies on
telemetry-generated enums with the generic `Ztc` vocabularies or ordinary
Zdb declarations whose ordinals are checked against them. Do not change
replication, election, cache, sampling, or database lifecycle behavior.

## Implementation plan

### Phase 1: Generic values, schema, and metadata

1. Inventory every field, width, enum ordinal, key, constructor index,
   mutable/series/delta marker, and RAG rule in `ZdbTelemetry.hh` and the three
   telemetry-specific schemas.
2. Rename and install `ZtcRAG.hh` as `ZtcTypes.hh`, add the two common callback
   heap-ID aliases, replace all active includes, and remove the old header
   without a compatibility forwarder.
3. Convert every incumbent `Ztc*` root/child traversal callback to
   `Ztc::AllFnHeapID` and every lifecycle add/delete callback to
   `Ztc::WatchFnHeapID` across `zm`, `zi`, and `zv`. Update all dependents and
   allocation-identity tests in the same breaking change.
4. Add `ZtcDBTable.hh`, `ZtcDBHost.hh`, and complete `ZtcDB.hh` with
   full-width plain values, abstract interfaces, and their colocated
   `ZfbStruct` metadata.
5. Move the telemetry-only schema vocabulary and generation into `zv`, using
   `Ztc.fbs` names and no `zdb` include path. Keep unrelated Zdb wire/storage
   schemas untouched.
6. Add schema tests with distinctive values, high 64-bit bits, nontrivial enum
   values, full-length identifiers, and every generated accessor. Add
   compile-time enum ordinal checks.
7. Build `zv` alone and audit headers, direct includes, generated dependencies,
   constructor indices, identifier storage, and allocation paths against
   `GUIDELINES.md`.

### Phase 2: Manager and lifecycle watcher

1. Complete `ZtcDB.cc` with the same pointer-tree singleton and visited-count
   `all` traversal shape as `ZtcHub.cc`.
2. Add `DBMgr::watch`/`unwatch` and synchronous add/delete callbacks using
   `Ztc::WatchFnHeapID` and `ztcwatch.md` ordering. Use
   `Ztc::AllFnHeapID` for every root and child traversal callback.
3. Add mock manager tests for uniqueness, actual traversal counts,
   watch-before-`all` overlap, same-key/same-pointer idempotence at a consumer,
   second-watch rejection, in-flight callback drainage, and object validity
   through delete.
4. Run the `zv` tests and retain the complete watcher suite from
   `ztcwatch.md`, updated to expect the common aliases rather than its
   precursor-era incumbent IDs.

### Phase 3: Zdb producer conversion

1. Derive DB, Host, and AnyTable from their generic interfaces and replace
   FlatBuffers-producing telemetry methods with direct plain-value population.
2. Implement root registration, DB-to-DBMgr child lifecycle notification,
   child traversal, and parent/child publication order at the existing
   ownership points.
3. Preserve current field reads and cache aggregation exactly; remove obsolete
   shim serializers only after all callers use the generic functions.
4. Test complete population, full-width keys, actual traversal counts,
   initialization failure, start/stop/restart, host/table churn, and
   synchronous deletion during a locked mock-consumer sample.
5. Rebuild from the top level and run focused `zdb` and generic `zv` tests.

### Phase 4: Remove old ownership and harden

1. Convert every active dependent, then remove `ZdbTelemetry.hh` from includes
   and install/build lists.
2. Remove `zdb_telemetry.fbs`, `zdb_cache_mode.fbs`, and
   `zdb_host_state.fbs` plus their generated/install entries from `zdb`.
   Retain `zdb_.fbs` and every non-telemetry schema.
3. Verify `zv` has no source, include, link, or generated-schema dependency on
   `zdb`, and `zdb` contains no telemetry serialization.
4. Run a top-level warning-free build and affected tests, then perform a final
   line-by-line `GUIDELINES.md` red/amber audit.

## Code references

- `zm/src/ZtcRAG.hh` - rename/remove target; no forwarding header remains.
- `zm/src/ZtcTypes.hh` - common `Ztc::RAG`, `Ztc::AllFnHeapID`, and
  `Ztc::WatchFnHeapID` declarations.
- all `zm/src/Ztc*.hh`, `zi/src/Ztc*.hh`, and `zv/src/Ztc*.hh` callback
  declarations and their active dependents - replace incumbent heap IDs with
  the common aliases.
- `zdb/src/ZdbTelemetry.hh` and `zdb/src/fbs/zdb_{telemetry,cache_mode,host_state}.fbs`
  - migration source, then removal targets.
- `zv/src/ZtcDBTable.hh`, `ZtcDBHost.hh`, `ZtcDB.hh`, and `ZtcDB.cc` - generic
  values, colocated DB metadata, interfaces, manager, and watcher.
- `zi/src/ZtcHub.hh` and `ZtcHub.cc` - manager API and exact `all` traversal
  implementation precedent.
- `zdb/src/Zdb.hh` and `Zdb.cc` - DB/host/table implementers and existing
  telemetry reads.
- `zv/src/Makefile.am` and `zdb/src/Makefile.am` - public headers, sources,
  schema generation, libraries, and removal of old ownership.
- focused `zv/test` and `zdb/test` files - schema, manager, lifecycle, and
  population coverage.

## Acceptance criteria

- `ztcwatch.md` has passed before this plan starts and its complete suite
  remains green.
- The public generic hierarchy is exactly
  `ZtcDB.hh`/`ZtcDB.cc`/`ZtcDBHost.hh`/`ZtcDBTable.hh`.
- `ZtcRAG.hh` has been removed and all active dependents include
  `ZtcTypes.hh`; no forwarding header or compatibility alias remains.
- `ZtcTypes.hh` is the sole definition point for `Ztc::RAG`,
  `Ztc::AllFnHeapID`, and `Ztc::WatchFnHeapID`.
- The `ZtcDB*` headers own the migrated DB-specific `ZfStruct`/`ZfbStruct`
  metadata as the documented exception to `ZtcFB.hh`; metadata is not
  duplicated.
- `Ztc::DBMgr::all` matches the `ZtcHubMgr_::all` iterator/counting shape and
  returns the number actually visited.
- Every DB root/child traversal callback uses the shared
  `Ztc::AllFnHeapID`; every lifecycle callback uses
  `Ztc::WatchFnHeapID`. Individual callback aliases do not repeat heap-ID
  template literals.
- Every non-DB `Ztc*` traversal/lifecycle callback and dependent has also been
  migrated from its incumbent heap ID to the corresponding common alias, and
  allocation-identity tests expect `"Ztc.AllFn"` or `"Ztc.WatchFn"`.
- `Ztc::DBMgr` provides the exact root and host/table `watch` overloads and
  single `unwatch`, callback heap IDs, ordering, and synchronous deletion
  lifetime from `ztcwatch.md`; DB child collections provide traversal only.
- `Zdb_::DB`, `Host`, and `AnyTable` fill complete generic telemetry values
  and publish stable full-width keys without serializing FlatBuffers.
- All current DB, host, and table fields, enum ordinals, metadata traits,
  derived RAG values, and 64-bit widths round-trip through the migrated
  schema.
- `zv` builds and links without `Zdb`; telemetry-specific schemas and generated
  headers are owned by `zv`.
- Active code no longer includes or installs `ZdbTelemetry.hh`; `zdb` retains
  only its non-telemetry schemas.
- Focused `zv`/`zdb` tests and a top-level build pass without warnings, and the
  final `GUIDELINES.md` audit has no unresolved red or amber finding.

## Non-goals

- `Ztc::App`, the public request/response protocol, subscriptions, alerts,
  App-owned indices, or network serving.
- Migration of Zdb replication, recovery, persistence, or record schemas.
- Changes to DB sampling, cache aggregation, counters, election, replication,
  threading, or synchronization.
- Compatibility aliases, forwarding APIs, duplicate managers, global
  host/table registries, multiple watchers, or observer lists.
