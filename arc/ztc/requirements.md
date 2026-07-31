## Summary

The goal is to replace the legacy `Ztel` telemetry implementation in `zcmd`
with a first-class `Ztc` telemetry service in the `zv` layer. The work includes
moving FlatBuffers schemas and metadata to `zv`, completing the lower-layer
`Ztc` telemetry contracts, introducing generic database telemetry interfaces,
re-platforming `Zdb` on those interfaces, and providing a self-contained
telemetry-serving `Ztc::App` with its own multiplexer, network endpoint, worker
thread, subscription engine, and `ZiLog` alert sink.

Key codebase findings that shape these requirements are:

- The existing telemetry data originates in `zm` and `zi`, while its
  `ZfbStruct` metadata currently lives in `zcmd/src/Ztel.hh`. This creates the
  correct motivation for a split in which plain telemetry declarations remain
  in their lowest viable layer and all FlatBuffers enrichment lives in
  `zv/src/ZtcFB.hh`.
- `Ztc::Heap`, `Hash`, `Thread`, `Mx`, `Connection`, `Queue`, `Hub`, `Link`,
  and `Pool` already expose telemetry interfaces. `Hub` is globally registered
  and owns traversal of dependent links and pools; the database interfaces
  must follow that same ownership model.
- `zdb/src/ZdbTelemetry.hh` mixes database sampling, RAG calculation,
  FlatBuffers metadata, and `Zdb` vocabulary. Moving it requires generic
  `Ztc::DB`, `DBHost`, and `DBTable` contracts in `zv`, with `Zdb` deriving from
  them rather than `zv` including any `Zdb` header.
- The legacy server depends on `Zcmd` framing and manually maintained
  `ZvEngineMgr` containers. A standalone `Ztc::App` cannot retain those
  dependencies; it needs a `Ztc`-owned framed protocol and must discover
  telemetry through the `Ztc` manager and parent/child interfaces.
- Several legacy schemas do not match the current C++ telemetry types. Examples
  include missing heap `crossFrees`, missing connection call/byte counters,
  a 32-bit schema field for the 64-bit hash count, signedness/width mismatches
  for thread and alert identifiers, and a duplicated DB-table constructor
  index. The migrated schemas and metadata must be treated as a corrected new
  protocol rather than copied mechanically.
- Whenever a newer `Ztc` type definition, member, field type, key, enum, or
  behavior conflicts with legacy `Ztel` code or schemas, the newer `Ztc`
  contract is authoritative. Legacy `Ztel` is research input only and must not
  cause a current `Ztc` capability to be removed, narrowed, renamed back, or
  otherwise regressed.
- The legacy subscription timer sends every watcher at the shortest interval,
  and its alert queue can grow indefinitely when no alert watcher is active.
  Both behaviors conflict with the requested service semantics and the
  repository's 24x7 and bounded-memory requirements.

The resulting product is a reusable telemetry server library in `libZv`, not a
compatibility wrapper around `Ztel`. Backward API and wire compatibility with
`Ztel` are non-goals unless explicitly selected in the open questions below.

## Product Requirements

### Layering and ownership

- Newer `Ztc` declarations and their current implementers are the source of
  truth for the migration. `Ztel` declarations, adapters, metadata, and schemas
  may supply missing product intent, but they must never override a conflicting
  `Ztc` type or member.
- Plain telemetry data structures and abstract collection interfaces must live
  in the lowest layer that owns the underlying facility:
  - `ZtcRAG.hh`, heap/hash/thread/queue telemetry in `zm`;
  - multiplexer/connection/hub/link/pool telemetry in `zi`;
  - database telemetry contracts and the telemetry application in `zv`.
- Lower layers must not include `Zfb`, generated FlatBuffers headers, or
  `zv` headers solely to expose telemetry.
- `zv/src/ZtcFB.hh` must be the single public enrichment point that binds all
  supported `Ztc` telemetry structures and enums to `ZfStruct`/`ZfbStruct`
  metadata and generated FlatBuffers types.
- `zv` must not include or link against `Zdb`. `Zdb` remains a dependent of
  `zv` and implements the generic database telemetry interfaces.
- The implementation must not introduce `Ztel` aliases, forwarding headers, or
  compatibility adapters. Dependents selected for continued support must be
  migrated to the new API directly.

### Common RAG vocabulary and derived telemetry

- Add `zm/src/ZtcRAG.hh` containing the dependency-free declaration:

  ```cpp
  namespace Ztc {
  namespace RAG {
    using T = int8_t;
    enum { Off = 0, Red, Amber, Green };
  }
  }
  ```

- The exact namespace spelling must be `Ztc::RAG`; all telemetry structures
  must return `RAG::T`.
- `ZtcFB.hh` must enrich this enum with name lookup and verify ordinal equality
  with the FlatBuffers RAG enum using `ZtEnumNames`, `ZfbEnumMatch`, or the
  equivalent established metadata mechanism.
- Derived values used by every consumer must be implemented on the lower-level
  telemetry structures, not recomputed by `Ztc::App` or duplicated in schema
  adapters. At minimum:

  | Telemetry | Required derived behavior |
  | --- | --- |
  | `HeapTelemetry` | `allocated()` returns total cache plus heap allocations minus frees; `rag()` is Off when no cache target exists, Red above the cache target, Amber after any heap fallback, otherwise Green. |
  | `HashTelemetry` | Red after resize, Amber at or above 80% of the configured load factor, Green below that threshold, and Off when no meaningful capacity/load target exists. |
  | `ThreadTelemetry` | Red at 80% CPU or above, Amber at 50% or above, otherwise Green; unavailable samples may be Off. |
  | `MxTelemetry` | Map engine states as follows: Running Green; Starting and StartPending Amber; Stopped, Stopping, and StopPending Red; unknown/null Off. |
  | `CxnTelemetry` | Red when either known socket buffer is at least 80% occupied, Amber at 50%, Green below 50%, and Off when neither capacity is known. |
  | `QueueTelemetry` | Off for an unbounded/unknown size, Red at 80% occupancy, Amber at 50%, otherwise Green. |
  | `LinkTelemetry` | Preserve the established link-state mapping: Up Green; Connecting, ReconnectPending, Reconnecting, Disconnecting, ConnectPending, and DisconnectPending Amber; Down and Failed Red; Disabled and Deleted Off. |
  | `HubTelemetry` | Use the same engine-state mapping as `MxTelemetry`. |
  | `PoolTelemetry` | Up Green, Down or Failed Red, unknown/null Off. |
  | `DBTableTelemetry` | Off before any cache lookup, Red when misses exceed 80% of lookups, Amber when misses exceed 50%, otherwise Green. |
  | `DBHostTelemetry` and `DBTelemetry` | Instantiated Red, Active Green, other valid transitional/inactive states Amber, unknown/null Off. |

- Threshold arithmetic must avoid overflow and must handle zero/unknown
  capacities explicitly.
- Synthetic metadata fields such as `allocated` and `rag` must be read-only
  where the metadata framework permits it; no-op setters may be added only if
  required by the established `ZfbStruct` contract.
- `ZvRAG` must be removed from migrated telemetry and its supported dependents.
  There must be one RAG vocabulary for `Ztc`.

### Complete lower-level telemetry contracts

- Existing `Ztc` telemetry structures must retain every metric currently
  populated by their implementers. The migration must not silently drop fields
  that were added after the legacy schemas were written.
- Public telemetry field widths and signedness must match their producers and
  expected lifetime. Counters that can grow for process lifetime must remain
  64-bit.
- Keys used for filtering and serialization must be stable for the registered
  lifetime of the object and must not truncate valid identifiers.
- Telemetry collection must be a best-effort, low-overhead snapshot. Diagnostic
  counters do not require new atomics or locks merely to make a multi-field
  snapshot perfectly consistent.
- Collection code must respect existing shard ownership. An interface whose
  state can only be sampled on a particular shard must dispatch to that shard
  and complete through a continuation; adding a lock to permit wrong-shard
  access is not acceptable.
- Manager and child traversal functions must return the number of entries
  actually visited. Counts must be incremented during successful traversal,
  not snapshotted from a container before iteration.
- Callbacks receiving raw telemetry-interface pointers must not retain those
  pointers beyond the documented traversal or continuation lifetime.

### Generic database telemetry hierarchy

- Add and install:
  - `zv/src/ZtcDBTable.hh`;
  - `zv/src/ZtcDBHost.hh`;
  - `zv/src/ZtcDB.hh`;
  - `zv/src/ZtcDB.cc`.
- `ZtcDBTable.hh` must declare the generic table telemetry data, table key,
  cache-mode vocabulary needed on the wire, `telemetry(...)`, and any
  table-specific control-free interface required by the server.
- `ZtcDBHost.hh` must declare the generic host telemetry data, host key,
  database-host state vocabulary, and `telemetry(...)`.
- `ZtcDB.hh` must declare:
  - `DBTelemetry`;
  - abstract `DB`, deriving only from appropriate generic Z facilities;
  - `virtual unsigned allHosts(AllHostsFn) const`;
  - `virtual unsigned allTables(AllTablesFn) const`;
  - telemetry and, if retained from the `Hub` pattern, start/stop control
    functions with the same lifecycle semantics as the implementing database;
  - a root manager exposing `static void add(DB *)`, `static void del(DB *)`,
    and `static unsigned allDBs(AllDBsFn)`.
- The root manager must use the established `ZtcHubMgr_` pattern: a
  library-lifetime singleton containing a unique, pointer-indexed `ZmRBTree`
  with an appropriate `ZmPLock` and a named `ZmHeap` ID.
- A database registers exactly once after successful `init` and deregisters in
  its destructor. Failed initialization must not leave a registry entry.
  Destruction must occur only after the database is stopped and dependent
  telemetry traversal is drained.
- Hosts and tables are owned and traversed by their parent `DB`; they are not
  separately registered in global managers.
- Database, host, and table identifiers must round-trip in full. The legacy
  fixed 28-byte DB-table telemetry name must not truncate the longer identifiers
  accepted by `Zdb`.

### Re-platform `Zdb` on `Ztc`

- `Zdb_::DB`, `Zdb_::Host`, and `Zdb_::AnyTable` must derive from
  `Ztc::DB`, `Ztc::DBHost`, and `Ztc::DBTable`, respectively, and implement
  their virtual key, telemetry, and traversal functions.
- Replace `Zdb` methods that build telemetry FlatBuffers directly with methods
  that populate generic `Ztc` telemetry structures. Serialization belongs to
  `ZtcFB.hh`/`Ztc::App`.
- Preserve the existing sampling content:
  - DB identity, leader/previous/next IDs, state and activity flags, connection
    and peer counts, configured thread, and heartbeat/reconnect/election
    settings;
  - host identity, priority, state, vote, IP, and port;
  - table identity, shard/thread configuration, record count, cache size,
    loads, misses, evictions, and cache mode.
- DB telemetry traversal must run on the DB thread. Table telemetry must run on
  the appropriate table shard or use the existing safe statistics access path.
  Completion must be asynchronous where traversal crosses shards.
- `Zdb_::CacheMode` and `Zdb_::HostState` must no longer depend on generated
  telemetry schema headers merely to obtain enum names. They must use normal
  lower-layer enum declarations or alias the corresponding generic `Ztc`
  vocabularies, with compile-time ordinal checks in `ZtcFB.hh`.
- Remove `ZdbTelemetry.hh` and its installation/build references after all
  dependents have migrated. Keep `zdb_.fbs` and other replication/persistence
  schemas in `zdb`; only telemetry-specific schemas move to `zv`.

### FlatBuffers schemas and metadata

- Schema and metadata design must begin from the complete current `Ztc`
  structures, not from the legacy `Ztel` tables. On any conflict, preserve the
  `Ztc` member name, semantic meaning, width, signedness, key role, and derived
  behavior, then update the new schema and all consumers accordingly.
- Move the telemetry request, acknowledgement, telemetry payload, and related
  enum schemas from `zcmd/src/fbs` to `zv/src/fbs`.
- Move or replace `zdb_telemetry.fbs`, `zdb_cache_mode.fbs`, and
  `zdb_host_state.fbs` in `zv/src/fbs`, because the generic telemetry schema
  must be buildable without a source include path or dependency on `zdb`.
- Use the `Ztc.fbs` namespace for the new protocol and generated types.
- `ZtcFB.hh` must provide metadata for every server-visible telemetry type:
  heap, hash, thread, multiplexer, connection, queue, hub, link, pool, DB,
  DB host, DB table, application, and alert.
- Metadata must identify:
  - primary and composite keys;
  - mutable/update fields;
  - series and delta counters;
  - enum and flags mappings;
  - numeric formatting such as CPU/load-factor precision;
  - synthetic `rag` and `allocated` fields.
- The migrated schema must exactly match the C++ structures. At minimum it must
  correct:
  - heap `crossFrees` and derived `allocated`/`rag`;
  - 64-bit hash `count`;
  - connection `rxCalls`, `rxBytes`, `txCalls`, and `txBytes`;
  - unsigned thread SID and 64-bit thread/alert TIDs;
  - DB table `nShards` width and cache-eviction field mapping;
  - all other derived RAG fields expected in serialized output.
- Every enum mapping must have compile-time ordinal assertions. Every struct
  mapping must have a round-trip or generated-accessor test that catches field
  omission, constructor-index duplication, width loss, and signedness loss.
- FlatBuffers generation must use the repository's existing `flatc`,
  embedded-BFBS, include rewriting, and formatting conventions. Generated
  headers must be installed by `zv`, not `zcmd` or `zdb`.

### Standalone wire protocol

- `Ztc::App` must not use `Zcmd::Hdr`, `Zcmd::Dispatcher`, login messages, or
  any other `zcmd` transport facility.
- Define a `Ztc`-owned streaming envelope that distinguishes requests,
  acknowledgements, telemetry data, snapshot completion, and protocol errors.
  TCP messages must have an unambiguous bounded length, using either a
  size-prefixed FlatBuffer or the repository's conventional little-endian
  32-bit body-length header.
- Every received frame must be length-checked and FlatBuffers-verified before
  any accessor is used. Oversized, truncated, malformed, or invalid-union
  frames must not reach the worker logic.
- `AppCf` must configure a maximum frame size and maximum filter length. A peer
  that violates framing limits must be disconnected without unbounded
  allocation.
- Every syntactically valid request must receive an acknowledgement carrying
  the original sequence number and success/failure status. Invalid request
  types, filters, intervals, and unsubscribe targets must receive a defined
  negative acknowledgement.
- Telemetry and completion messages must carry the originating request or
  subscription sequence number. One-shot snapshots require an explicit
  completion message; clients must not infer completion from a period of
  silence.
- Request groups must cover:
  - heaps;
  - hashes;
  - threads;
  - multiplexers and their connections;
  - queues;
  - hubs and their links/pools;
  - databases and their hosts/tables;
  - application status;
  - alerts.
- Filters must support match-all, exact match, and trailing-prefix wildcard.
  Composite child keys must have one documented, unambiguous textual form.
  Empty filters must have the same semantics as match-all.
- Query output must be deterministic within each manager traversal. No global
  ordering guarantee is required across independently sharded continuations,
  but snapshot completion must wait for all of them.

### `Ztc::AppCf` configuration

- Add a public `Ztc::AppCf` with `ZfStruct` configuration metadata.
- Configuration must include:
  - the normal `ZvMxCf`/`ZvMxParams` multiplexer configuration;
  - configurable timer, Rx, Tx, and worker thread identities;
  - listen IP and port;
  - accept concurrency and transient rebind interval;
  - minimum and maximum subscription intervals, in explicitly documented
    units;
  - maximum frame/filter sizes and per-link outbound limits;
  - application ID, version, and role used by application telemetry;
  - alert path/prefix, replay retention, and bounded in-memory queue/pool
    tuning.
- Thread names/SIDs must resolve during initialization. Timer, Rx, Tx, and
  worker roles must be valid isolated scheduler threads and must be rejected
  when a configuration aliases roles that require independent ownership.
- Defaults must be usable for a loopback test server, but the default listen
  address must not expose telemetry on all interfaces.
- Invalid configuration must fail initialization with a specific `ZeException`
  or `ZiAssert` diagnostic and must not leave a running multiplexer, listener,
  manager registration, timer, or log-sink callback.

### `Ztc::App` lifecycle and transport

- Add `zv/src/ZtcApp.hh` and the required `.cc` implementation as a concrete,
  reusable `Ztc::App`.
- `App` must own its `ZiMultiplex` and the TCP server/link objects used to
  accept telemetry clients. It may build on `Ztcp`, but it must not depend on
  legacy `ZvEngine` or `Zcmd`.
- Public lifecycle must follow the repository vocabulary:
  `init`, idempotent `start`, idempotent `stop`, and `final`.
- Startup order must ensure configuration and worker state are valid before
  the multiplexer accepts traffic. Shutdown must:
  1. prevent new requests and stop listening;
  2. cancel subscription and rebind timers;
  3. drain Rx work and disconnect links;
  4. drain worker-owned requests, DB continuations, alert writes, and
     subscriptions;
  5. drain Tx work and buffers;
  6. stop/finalize the owned multiplexer and persistence state.
- Timer teardown and I/O teardown must use posted continuations on their owning
  threads. The implementation must not block scheduler/I/O threads.
- Connection/link objects may use raw back-pointers to `App` under the local
  ownership pattern, but `App` must drain them before destruction.
- `App` must expose its bound endpoint after a port-zero listen so tests and
  embedding applications can discover the selected port.
- Application telemetry must expose configured identity/version/role, process
  start time, and current RAG. Updating application RAG must be a worker-owned
  operation that causes subscribed application telemetry to be emitted.

### Worker-thread ownership and cross-shard handoff

- The application worker thread exclusively owns:
  - decoded request state after Rx handoff;
  - subscription/watch containers and due times;
  - FlatBuffers builders used for telemetry output;
  - snapshot aggregation/completion state;
  - alert sequence state, in-memory alert queue, and alert files;
  - application telemetry state.
- Rx code may only validate framing/FlatBuffers, retain or move the pooled
  `ZiIOBuf`, and post fixed metadata plus the buffer handle to the worker.
- The worker builds immutable outbound `ZiIOBuf` frames and hands them to Tx.
  Tx code must not inspect or mutate worker subscription state.
- Variable-sized payloads must cross shards by moved pooled buffers, not by
  captured strings/arrays or temporary contiguous copies.
- The phrase “all telemetry sending on the worker” means that selection,
  serialization, ordering, and enqueueing are worker-owned; the physical
  socket write remains Tx-owned.
- Work that can traverse many heaps, hashes, connections, tables, or
  subscribers must be performed in bounded batches with posted continuations
  so the worker does not starve other scheduler work.

### Discovery and snapshot processing

- `Ztc::App` must discover roots from `HeapMgr`, `HashMgr`, `ThreadMgr`,
  `MxMgr`, `HubMgr`, and `DBMgr`; it must not maintain duplicate manual
  registration containers for engines, queues, or databases.
- Multiplexer queries must emit the matching `MxTelemetry` followed by the
  multiplexer’s matching connections.
- Hub queries must emit the matching `HubTelemetry` and its dependent links,
  pools, and relevant queues through `allLinks`, `allPools`, `Link::allQueues`,
  and `Pool::txQueue`.
- Queue queries must cover scheduler/multiplexer queues and link/pool queues.
  The lower interfaces must be extended, where necessary, so queues are
  discoverable without RTTI, casts to concrete implementations, or application
  registration callbacks.
- DB queries must emit each matching DB, then all of that DB’s hosts and
  tables, and must delay snapshot completion until every asynchronous table
  sample completes.
- A queue or other dependent object reachable through more than one path must
  be emitted once per snapshot, keyed by its canonical composite key.
- Manager/container iterators must end before follow-on sends, cleanup,
  callbacks that mutate the same container, or cross-thread posts.

### Subscription semantics

- A subscription is uniquely identified per connection by request type and
  canonical filter. Re-subscribing the same key updates its interval rather
  than creating a duplicate watch.
- A zero interval requests a one-shot snapshot. A non-zero interval with
  `subscribe=true` creates or updates a subscription. `subscribe=false`
  removes the matching subscription and does not create a transient watch.
- Requested intervals below the configured minimum must be clamped or rejected
  consistently and the effective interval must be reported in the
  acknowledgement. Intervals above the maximum must be rejected.
- Each subscription must be emitted no faster than its own effective interval.
  The legacy behavior in which every watcher runs at the shortest interval is
  not acceptable.
- Scheduling should use one next-due timer per request group or a worker-owned
  due-time queue, rather than one independently allocated timer per watch.
- Disconnecting a link must deterministically remove all of its subscriptions,
  pending snapshot state, and queued outbound references.
- A timer must be disarmed when its group has no subscriptions. All timers must
  be cancelled and late callbacks drained during stop/final.
- Subscription scanning and removal must not leave long-lived iterators holding
  locks while telemetry is serialized or sent.

### `ZiLog` alert sink, persistence, and replay

- `Ztc::App` must provide a `ZmRef<ZiSink>` suitable for installation with
  `ZiLog::sink(...)`. Installing it must be an explicit embedding-application
  decision; constructing `App` must not silently replace a process-global sink.
- The sink callback runs on the `ZiLog` thread. It must immediately copy the
  required fixed event metadata and message bytes into a bounded,
  telemetry-owned buffer and post that buffer to the App worker.
- The sink callback must not retain references to `ZeLogBuf` or
  `ZeEventInfo`, perform file I/O, serialize telemetry, send network data, or
  capture unsafe external pointers.
- A sink reference that outlives a stopped/finalized `App` must become inert
  without dereferencing the destroyed app. Stop/final must drain already
  accepted sink events.
- On the worker, every accepted alert must receive a stable timestamp,
  process-lifetime or date-qualified sequence number, severity, TID, and
  message; it must be serialized once and used for both persistence and fanout.
- Preserve the useful legacy persistence model: date-partitioned append-only
  data files plus an index that supports replay from a sequence number.
  File/index validation must reject corrupt offsets and truncated records.
- Alert persistence errors must not call `ZiLOG` through the same sink and
  recurse. They must use a non-recursive fallback diagnostic path and expose a
  degraded App RAG/status.
- Replay retention must use real calendar-day arithmetic, including
  month/year boundaries, rather than subtraction on `YYYYMMDD` integers.
- Retention cleanup must be deterministic at startup/rotation or another
  bounded lifecycle point. It must not use an unbounded periodic directory
  scan.
- In-memory alert buffering must have a configured hard bound independent of
  whether subscribers exist. Persistence is the replay source; a disconnected
  or slow consumer must not cause monotonically growing memory.
- Alert replay must preserve order, avoid duplicates between disk and the
  in-memory tail, stop at a stable handoff point, and then transition the
  subscription to live delivery.

### Resource bounds, performance, and resilience

- Use pooled `ZiIOBuf` allocations with named/tunable `ZmHeap` IDs for inbound,
  outbound, acknowledgement, and alert buffers.
- Avoid a heap allocation per telemetry field or per subscriber. A serialized
  update that is identical for multiple subscribers should be shared by
  reference where sequencing permits.
- All long-lived containers must have named heap IDs and deterministic removal
  paths. There must be explicit bounds for frames, filters, pending requests,
  subscriptions per link, outbound bytes per link, and in-memory alerts.
- A slow client that exceeds its outbound bound must be disconnected or have a
  documented telemetry-drop policy; it must not stall the worker or grow a
  queue indefinitely.
- Repeated invalid requests must not allocate durable state. Unknown enum
  values and malformed filters must fail safely.
- RAG/statistical reads may be approximate, but keys, frame boundaries,
  sequence correlation, persistence offsets, and lifecycle state must be
  correct.
- The network endpoint must support Linux and Windows/msys2 through existing
  `ZiMultiplex`/`Ztcp` abstractions.

### Legacy removal and build integration

- Remove the migrated `Ztel.hh`, `ZtelServer.hh`, `ZtelClient.hh`, and telemetry
  schemas from `zcmd` installation/build lists once their supported dependents
  are migrated or retired.
- Remove telemetry-specific schemas and `ZdbTelemetry.hh` from `zdb` build and
  install lists.
- Add all new public headers, `.cc` files, schemas, generated headers, and
  `libZv` source/link dependencies to `zv/src/Makefile.am`.
- Add the transport dependency selected for `Ztc::App` (normally `libZtcp`) to
  `libZv` without creating a dependency cycle.
- `zv/test/Makefile.am` must build and run the telemetry server/client tests.
- A clean top-level build must generate schemas in dependency order without
  relying on generated headers left in another module’s source directory.
- Update stale comments and documentation that identify `Ztel` as the active
  telemetry provider.

### Test telemetry server and client

- Add a telemetry-serving test application and a protocol client under
  `zv/test`. The client may be test-only, but it must exercise the public wire
  protocol rather than private App functions.
- Tests must use loopback and a dynamically assigned port. IPv4 is required;
  IPv6 should run when available and skip with a TAP diagnostic when not.
- The client must verify:
  - fragmented and coalesced TCP frame parsing;
  - positive and negative request acknowledgements and sequence correlation;
  - one-shot completion;
  - exact, wildcard, prefix, and composite-key filtering;
  - representative heap/hash/thread/multiplexer/connection/queue/hub/link/pool
    payloads;
  - DB/host/table hierarchy using lightweight `Ztc` mock implementers;
  - per-subscription interval behavior, update, unsubscribe, and disconnect
    cleanup;
  - application RAG updates;
  - alert sink ingestion, persistence, replay-to-live handoff, and bounded
    behavior;
  - malformed FlatBuffers, oversized frames, invalid enums, and slow-client
    handling;
  - clean stop/final with active clients, timers, subscriptions, and log
    events.
- Schema tests must serialize and load every telemetry union member and compare
  all fields, including 64-bit values above `UINT32_MAX`.
- Manager tests must verify registration/deregistration, pointer uniqueness,
  nested traversal, and that returned counts equal actual callback visits.
- Concurrent tests must synchronize with `ZmBlock` or `ZmSemaphore`; they must
  not use sleeps or polling as completion criteria.
- Acceptance requires:
  - `make -j8`;
  - `make -C zv/test -j8 && make -C zv/test test`;
  - relevant `zm`, `zi`, and `zdb` tests;
  - the top-level `make test`;
  - clang AddressSanitizer/LeakSanitizer coverage;
  - `libtool exec valgrind --leak-check=full` for the telemetry integration
    test;
  - no compiler warnings on supported gcc/clang builds.

### Non-goals

- Backward source, ABI, or wire compatibility with `Ztel` is not required.
- Rebuilding the legacy `zcmd` authentication/command stack inside
  `Ztc::App` is not required.
- A dashboard, telemetry aggregation database, or remote control plane is not
  part of this goal.
- Perfectly atomic multi-counter snapshots are not required.
- General-purpose TLS identity, authorization, or WAN exposure is not implied
  unless selected below; the default service is a bounded query/subscription
  endpoint intended for a trusted or loopback deployment.

## Options and Open Questions

1. **Wire envelope and framing.** The recommended design is a new
   `Ztc.fbs.Envelope` union with a little-endian 32-bit length prefix, request
   sequence correlation, and an explicit snapshot-complete message. An
   alternative is FlatBuffers size-prefixing with separate root identifiers.
   The choice must be made before schema names are frozen.
   ANSWER: re-use the existing header/body binary protocol

2. **Transport security.** Should the first `Ztc::App` release be plain
   `Ztcp` bound to loopback by default, or must it support `Ztls` plus
   authentication from day one? The goal specifies a listen port but not a
   trust model. Plain loopback TCP is the smaller initial scope; non-loopback
   plaintext should require an explicit configuration choice.
   ANSWER: initial version is plain `Ztcp` loopback, no auth or trust model; these will be added later

3. **Legacy `zcmd` buildability.** Since `zcmd` is explicitly legacy and is
   currently excluded from the top-level build, the recommended scope removes
   its `Ztel` API without re-platforming the `zcmd` binary. If standalone
   `zcmd` builds must remain supported, its telemetry client must be rewritten
   against the new `Ztc` protocol rather than retained through shims.
   ANSWER: leave `zcmd` unchanged, it is legacy code and out of scope for now

4. **Application telemetry ownership.** The recommended concrete `AppCf`
   supplies ID/version/role and `Ztc::App` owns the current RAG. An alternative
   is an embedding callback that fills `AppTelemetry`; if selected, its thread
   and lifetime contract must be explicit.
   ANSWER: `Ztc::App` owns the current RAG

5. **Alert sequence identity.** The legacy format resets `seqNo` daily and
   identifies replay positions as `date:seqNo`. A monotonic 64-bit
   process/persistent sequence simplifies ordering and replay but requires
   persisted allocator state. Either is viable if the protocol key is
   unambiguous and replay-to-live delivery is duplicate-free.
   ANSWER: use `date:seqNo`, date as unsigned integer `YYYYMMDD`

6. **Alert durability level.** Clarify whether acknowledgement to the logger
   path means “accepted into the bounded worker queue,” “written to the OS page
   cache,” or “durably synced.” The recommended default is ordered append to
   data and index without an `fsync` per alert, with explicit degraded status
   on write failure.
   ANSWER: "accepted into the worker queue"; the worker queue has a "dead letter" fallback anyway (`m_queue`), it's not bounded

7. **Queue discovery API.** `ZiMultiplex` already implements both `Ztc::Mx` and
   `Ztc::QueueMgr`, but `Ztc::Mx` does not expose that relationship. The
   recommended change is for `Ztc::Mx` to derive from `QueueMgr`, allowing the
   App to enumerate scheduler queues without concrete casts. Confirm whether
   pool/link queues should also appear in a standalone Queue request as well as
   under their parent Hub snapshot.
   ANSWER: `Ztc::Mx` should derive from `Ztc::QueueMgr`; pool/link queues should appear in a `Queue` query

8. **Subscription overrun policy.** When a snapshot takes longer than its
   interval, the recommended behavior is to coalesce missed ticks and run one
   next snapshot after completion. Queuing every missed interval would violate
   bounded-memory and latency requirements.
   ANSWER: coalesce missed ticks, and schedule next snapshot after completion

9. **Schema naming.** The recommended corrected protocol uses the `Ztc.fbs`
   namespace and telemetry type names aligned with the C++ contracts. If
   external consumers already depend on `Ztel.fbs` names, a deliberate
   one-time migration plan is needed; alias schemas should not be added by
   default.
   ANSWER: propagate the rename to dependent code. `Ztel` must be removed entirely. Update flatbuffers schema files and dependent code accordingly
