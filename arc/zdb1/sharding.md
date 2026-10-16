# Zdb DB-wide sharding

## Status

Implemented as the prerequisite for `ZdbSaga`.  The change is intentionally
breaking: per-table shard counts, per-table shard-to-thread mappings, and
table scheduling forwarders have been deleted and repository dependents use
the DB shard API directly.

Changing the persistent shard count remains an offline migration boundary;
this change deliberately does not provide a resharding utility.  A configured
count which differs from existing store metadata fails startup.  When adopting
a PostgreSQL database created before the metadata row existed, the operator
must first ensure that every existing table used the selected count (or perform
the offline migration described below); the first open records that selected
count authoritatively.

## Decision

A `Zdb::DB` owns one shard topology shared by every table registered with that
DB:

```text
DB shard s
  -> one scheduler SID
  -> table A shard s
  -> table B shard s
  -> ...
  -> saga intent and operation-journal shard s
```

For any two tables `a` and `b` in the same DB and every valid shard `s`:

```c++
a->db() == b->db()
a->db()->shardSID(s) == b->db()->shardSID(s)
```

All tables have exactly `db->nShards()` shards.  The numeric `ZdbShard` is
therefore a DB-wide partition identifier, rather than an integer whose meaning
depends on the table receiving it.

This does not require every row touched by a saga to use one shard.  A saga may
perform one step on shard 2 and the next on shard 7.  It means that whenever
two tables use the same numeric shard, those partitions have the same
scheduler owner and can participate in one non-intervenable scheduler turn.

A saga belongs to exactly one DB and cannot span DBs.  Applications which need
different shard counts or scheduler mappings for independent parts of their
data model must instantiate separate DBs.  Cross-DB consistency and cross-DB
sagas remain application concerns.

## Why this is needed

Before this change, `TableCf` independently owned `nShards`, `threads`, and
its resolved `sids`.  `DB::init()` resolved those arrays table by table.
Consequently,
`orders` shard 3 and `accounts` shard 3 may be different scheduler partitions.
The shared `ZdbShard` typedef does not currently imply a shared namespace.

That was the wrong abstraction for multi-table operations:

- related partitions cannot be reasoned about uniformly across tables;
- code cannot safely enter one DB shard and synchronously mutate multiple
  tables on that shard;
- a saga operation journal cannot be placed on the target operation's shard
  and rely on immediate journal/effect adjacency; and
- every multi-table facility would otherwise need to validate or reproduce
  table-specific SID mappings.

The DB is the correct ownership boundary.  A table is one projection of the
DB-wide partition set; it is not an independent scheduler topology.

## Configuration model

The implementation moves `nShards`, `threads`, and the resolved SID array from
`TableCf` to `DBCf`.  It keeps the existing DB `thread` as the
control/lifecycle thread.  The configuration shape is:

```text
thread: zdb
shards: 8
threads: [zdb0, zdb1, zdb2, zdb3]
tables: {
  orders:   {cacheMode: All}
  accounts: {cacheMode: Normal}
  saga:     {cacheMode: All}
  sagaStep: {cacheMode: All}
}
```

The direct C++ shape should be equivalent to:

```c++
struct DBCf {
  ZuID thread;                 // DB control/lifecycle thread
  unsigned nShards = 1;       // persistent partition count
  Threads threads;            // DB shard scheduler threads
  mutable SIDArray sids;       // resolved once by DB::init()
  // store, tables, hosts, election configuration ...
};

struct TableCf {
  IDString id;
  int cacheMode = CacheMode::Normal;
};
```

Preserve the current constraints, but validate them once for the DB:

- `nShards` is a power of two in `[1, 64]`;
- `threads.length()` is either zero or a power of two;
- `threads.length() <= nShards`; and
- every configured thread resolves to a valid non-Rx/non-Tx scheduler SID.

When `threads` is empty, every DB shard maps to the DB control `thread`.  This
preserves the useful single-thread default.  Otherwise:

```text
shardSID(s) = sids[s & (sids.length() - 1)]
```

The SID mapping may be changed while the DB is stopped because it controls
execution placement, not persistent partition identity.  `nShards` may not be
changed without explicitly resharding every table and rebuilding all
per-shard replication/store state.

Table configuration containing `shards` or `threads` becomes invalid.  Do not
silently ignore it and do not accept it only when it happens to equal the DB
configuration; that would preserve two apparent sources of truth.

## Public and internal API changes

### DB owns shard dispatch

Add DB-level accessors and explicit shard dispatch:

```c++
unsigned DB::nShards() const;
unsigned DB::shardSID(Shard) const;

template <typename ...Args>
void DB::shardRun(Shard, Args &&...) const;

template <typename ...Args>
void DB::shardInvoke(Shard, Args &&...) const;

bool DB::shardInvoked(Shard) const;
```

Use names distinct from the existing control-thread `run()`, `invoke()`,
`invoked()`, and `sid()` APIs.  This avoids overload ambiguity in calls whose
first application argument is an integer.

Every entry point validates `shard < nShards`; the thread-selection mask is
not a substitute for shard-range validation.

### Delete table dispatch

Delete all table-level shard scheduling APIs:

```c++
Table::run(Shard, ...);
Table::invoke(Shard, ...);
Table::invoked(Shard);
Table::sid(Shard);
```

Shard execution is owned exclusively by `DB::shardRun()`,
`DB::shardInvoke()`, `DB::shardInvoked()`, and `DB::shardSID()`.  Tables expose
data operations, not scheduler topology or forwarding conveniences.

This is an intentional breaking API change.  Propagate it through every
repository dependent rather than retaining forwarding methods or compatibility
shims.  A typical migration is:

```c++
// before
table->run(shard, [table]() { /* table operation */ });

// after
db->shardRun(shard, [table]() { /* table operation */ });
```

Callers which do not already retain the DB obtain it once from `table->db()`
before dispatch; they do not recreate a table wrapper.  Zdb internals similarly
replace `table->run()`/`invoke()` with dispatch through the owning DB.  Mutation
and cache methods assert `db()->shardInvoked(shard)` directly.

Internal table allocation, warmup, recovery, replication, UN arrays, caches,
and telemetry loops use `db()->nShards()`, never
`table->config().nShards`.

`TableCf` continues to exist for table identity and table-local policy such as
cache mode.  It exposes no inherited or copied topology fields.

### Store interface

A store instance belongs to one DB, so configure its shard count once rather
than passing a nominally per-table value to every `Store::open()` call.
Preferred interface direction:

```c++
Store::init(cf, mx, dbNShards, failFn);
Store::open(id, fields, keyFields, schema, allocFn, openFn);
```

`StoreTbl` still owns a per-table/per-shard `maxUN` vector because UN streams
remain independent for each `(table, shard)`.  The vector length comes from
the owning store's DB shard count.  This distinction is important:

```text
shared topology:       DB.nShards and DB.shardSID(s)
independent ordering:  table.nextUN(s), table cache, table row set
```

Propagate the interface break through `ZdbMemStore`, `ZdbPQ`,
`ZdbMockStore`, and all test stores.  Do not leave an unused `nShards`
parameter on `open()`.

### Replication and DB state

Replication continues to identify records by `(tableID, shard, UN)`.
`DBState` therefore remains a map from `(tableID, shard)` to the next UN; only
the loop bound becomes `DB::nShards()`.

Advertise the DB shard count in the peer handshake or other pre-replication
metadata and reject a peer whose count differs before accepting records.
Validate every received record and commit shard against the DB range before
indexing per-shard arrays.

No protocol needs to carry a table-specific shard count after this change.

## Persistence and migration boundary

Shard identity is already persistent even where the backend does not have a
single `nShards` metadata field:

- every record carries `_shard`;
- PostgreSQL primary/replication bookkeeping is partitioned by shard;
- each table recovers a per-shard maximum-UN vector; and
- replication state is keyed by `(tableID, shard)`.

Add one authoritative store-level DB metadata record containing at least the
DB shard count.  On first creation, initialize it before opening application
tables.  On every later open, compare it with `DBCf::nShards` and fail startup
on mismatch.  Do not infer the count from the largest shard currently present:
an empty high shard is still part of the schema.

The PostgreSQL store keeps its driver-owned relations in the `zdb` schema:
`"zdb"."meta"` stores this metadata and `"zdb"."mrd"` stores replication
state.  These are schema-qualified relations, not quoted identifiers containing
dots.  This is a greenfield layout; no compatibility path for the former
public `"zdb.meta"` and `"zdb.mrd"` names is provided.

The scheduler thread names/SIDs are deliberately not durable schema.  They may
be retuned while stopped as long as all tables use the new one-to-one DB
mapping when restarted.

Existing databases fall into two cases:

1. Every table already uses the selected DB-wide `nShards`.  Install the DB
   metadata value, move configuration to the DB level, and reopen without row
   movement.
2. Table counts differ, or the selected count changes.  Stop the DB and run an
   explicit offline migration which recomputes every row's shard, rebuilds
   table indexes and per-shard UN/MRD state, and resets or translates
   replication state consistently on every host.  Merely changing
   configuration is invalid.

The repository does not promise dependent compatibility, so no runtime mode
will support old and new topology semantics simultaneously.

## Telemetry and management schemas

`nShards` and `threads` move from `DBTableTelemetry` to `DBTelemetry` and its
FlatBuffers schema.  A table telemetry row reports table-local cache and
row statistics only.  Repeating identical topology on every table is both
misleading and wasteful.

Update telemetry codecs, dashboards/consumers, and `ZdbTelTest`.  This is a
schema break and should be propagated directly rather than represented in
both locations.

## Multi-table execution guarantee

After this change, code running on DB shard `s` may synchronously call mutation
APIs on any table in that DB using shard `s`.  Since all those APIs resolve to
the current SID, the scheduler cannot run another callback on that SID until
the outer callback returns.

This gives a precise guarantee:

> Synchronous, non-yielding commits to any same-DB tables on the same numeric
> shard occur in program order without an intervening scheduler callback on
> that shard owner.

It is not a transaction.  A process failure may cut between commits, other DB
shards continue independently, and asynchronous lookup or any other yield ends
the non-intervenable region.  All potentially asynchronous preparation must
finish before entering such a region.

The guarantee is deliberately expressed in terms of the same numeric DB
shard, not merely an accidentally equal SID.  Several shards may map to one
SID, but code must not use that implementation detail to conflate their
persistent identities.

## Implemented sequence

1. Move shard types/containers needed by both configurations to a common DB
   configuration location, then add `nShards`, `threads`, and `sids` to
   `DBCf`.
2. Delete those fields and their `ZfStruct` mappings from `TableCf`; move
   `shards` and `threads` parsing and validation to `DBCf`.
3. Resolve the DB SID array once in `DB::init()` and add the explicit DB shard
   dispatch/accessor API.
4. Delete `AnyTable`/`Table` `run()`, `invoke()`, `invoked()`, and `sid()`.
   Convert their internal call sites, constructors, arrays, cache loops,
   open/recovery, DB-state refresh, and replication loops to the DB topology.
5. Move store initialization to one DB shard count, remove the count from
   `Store::open()`, and update the memory, PostgreSQL, mock, and test stores.
6. Add durable store-level shard-count metadata and peer topology validation.
7. Move topology telemetry from table to DB schemas and update consumers.
8. Migrate every repository configuration from table-level to DB-level shard
   fields, delete tests which encode per-table variance, and migrate every
   dependent table dispatch to `DB::shardRun()`/`shardInvoke()` without
   compatibility wrappers.
9. Add the cross-table same-shard tests below before building `ZdbSaga`'s
   UN-assisted operation journal on the guarantee.

## Verification

Add focused tests for:

- configuration rejects non-power-of-two counts, excessive counts, invalid
  thread arrays, and table-local `shards`/`threads`;
- every registered table reports the DB count;
- `DB::shardRun()`, `shardInvoke()`, and `shardInvoked()` dispatch every
  numeric shard through exactly `DB::shardSID(shard)`;
- no table scheduling accessor or forwarding method remains, and all Zdb,
  test, integration-test, and other repository dependents use DB dispatch;
- two DB instances may use different topologies while tables within each DB
  cannot;
- table cache, UN, recovery, and mock-store arrays all have the DB count;
- synchronous commits to two tables on the same DB shard are adjacent in that
  SID's callback execution and preserve enqueue order;
- an asynchronous lookup explicitly ends the claimed adjacency;
- replication rejects out-of-range shards and peers with a different count;
- restart rejects a configured count inconsistent with durable DB metadata;
- existing same-count tables can be adopted without row movement;
- DB telemetry contains the topology once and table telemetry no longer does;
  and
- memory-store and PostgreSQL integration tests cover more than one shard and
  more than one table.

Use scheduler barriers, deferred mock-store callbacks, and explicit
synchronization rather than timing sleeps.

## Consequence for `ZdbSaga`

`ZdbSaga` is implemented only after this precursor lands.  It uses the owning
DB's shard count and SID mapping; it has no independent `K`, thread array, or
logical shard topology.  Both its pending-intent table and its per-operation
journal are ordinary tables in the same DB and necessarily inherit the same
shards.

For an assisted update on target shard `s`, `ZdbSaga` places the operation
intent row on saga-operation-journal shard `s`, then commits the target update
in the same non-yielding outer turn.  That relationship would be unsafe under
the current per-table mapping and is the immediate reason this work precedes
the saga facility.
