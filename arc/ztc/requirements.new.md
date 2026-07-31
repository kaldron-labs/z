## Summary

Replace the legacy `Ztel` telemetry implementation with a first-class `Ztc`
telemetry service in the `zv` layer. The work comprises:

- completing lower-layer `Ztc` telemetry types and derived RAG data;
- centralizing all FlatBuffers enrichment in `zv/src/ZtcFB.hh`;
- migrating telemetry schemas into `zv/src/fbs`;
- adding generic `Ztc::DB`, `DBHost`, and `DBTable` interfaces and
  re-platforming `Zdb` on them;
- implementing a concrete `Ztc::App` that owns its multiplexer, plain loopback
  `Ztcp` listener, timer/Rx/Tx/worker threads, query and subscription state,
  and alert persistence;
- providing a `ZiLog` sink that transfers alerts to the App worker for
  persistence and live/replay delivery; and
- adding a real telemetry server and protocol client under `zv/test`.

The current `Ztc` declarations and their current implementers are authoritative.
When a current `Ztc` type, member, key, field type, enum, or behavior conflicts
with legacy `Ztel` code or schemas, preserve `Ztc` and update the new metadata,
schema, server, tests, and supported dependents to match it. `Ztel` is research
input only: it may reveal intended presentation or behavior, but it must not
remove, narrow, or rename a newer `Ztc` capability.

The codebase already supplies `Ztc` interfaces for heaps, hashes, threads,
queues, multiplexers, connections, hubs, links, and pools. Their telemetry is
owned by `zm` and `zi`, while the legacy FlatBuffers metadata is incorrectly
coupled to `zcmd`. Database telemetry currently combines sampling,
serialization, RAG policy, and `Zdb` vocabulary in `ZdbTelemetry.hh`; this must
be split so `zv` defines the generic contract and `Zdb` implements it without
creating a `zv` to `zdb` dependency.

The new service reuses the established typed header/body binary protocol. It
does not introduce a FlatBuffers envelope or adopt FlatBuffers size-prefix
framing. The header/body declarations needed by the active telemetry service
must be owned by `Ztc` code in `zv`, without depending on `Zcmd`.

The initial transport is unauthenticated plain `Ztcp` restricted to loopback.
TLS, authentication, authorization, and a wider trust model are deferred.

## Product Requirements

### Authoritative types and module layering

- Current `Ztc` declarations and members are the source of truth throughout
  the migration. On conflict, preserve their names, meanings, widths,
  signedness, key roles, counters, and current implementation behavior.
- Plain telemetry structures and abstract collection interfaces must live in
  the lowest layer that owns the underlying facility:
  - `ZtcRAG.hh` and heap/hash/thread/queue telemetry in `zm`;
  - multiplexer/connection/hub/link/pool telemetry in `zi`;
  - generic database telemetry contracts and the telemetry App in `zv`.
- Lower layers must not include `Zfb`, generated FlatBuffers headers, or
  higher-layer headers solely to expose telemetry.
- `zv/src/ZtcFB.hh` must be the single public enrichment point binding all
  server-visible `Ztc` structures and enums to `ZfStruct`/`ZfbStruct` metadata
  and generated FlatBuffers types.
- `zv` must not include or link against `Zdb`. `Zdb` remains a dependent of
  `zv` and implements generic `Ztc` database interfaces.
- No supported code outside the frozen legacy `zcmd` subtree may define or
  reference `Ztel`. Do not add aliases, forwarders, compatibility schemas, or
  adapters.
- The legacy `zcmd` subtree is otherwise out of scope and must not be
  re-platformed or repaired. It may retain archival `Ztel` sources because it
  is excluded from the active build; those files do not define the active API,
  schema ownership, or compatibility requirements.

### Common RAG vocabulary

- Add `zm/src/ZtcRAG.hh` with the dependency-free declaration:

  ```cpp
  namespace Ztc {
  namespace RAG {
    using T = int8_t;
    enum { Off = 0, Red, Amber, Green };
  }
  }
  ```

- The exact namespace is `Ztc::RAG`; every telemetry `rag()` returns
  `RAG::T`.
- `ZtcFB.hh` must add enum name lookup and compile-time ordinal matching
  against the `Ztc.fbs` RAG enum using established `ZtEnumNames` and
  `ZfbEnumMatch` patterns.
- Remove `ZvRAG` from active telemetry and migrated supported dependents.
  `Ztc::RAG` is the sole telemetry RAG vocabulary.
- RAG and other derived metrics used by multiple consumers must be member
  functions on the lower-level telemetry structures. `Ztc::App`, metadata
  shims, and protocol clients must not duplicate the calculations.
- Synthetic metadata fields such as `rag` and `allocated` must be read-only
  where supported. Add a no-op setter only if the established metadata
  machinery requires one.
- Threshold arithmetic must avoid overflow and explicitly handle unknown or
  zero capacities.

### Derived telemetry behavior

The migrated behavior must follow the current `Ztc` data and retain applicable
legacy RAG intent:

| Telemetry | Required derived behavior |
| --- | --- |
| `HeapTelemetry` | `allocated()` is `cacheAllocs + heapAllocs - frees`. `rag()` is Off when no cache target exists, Red above `cacheSize`, Amber after any heap fallback, otherwise Green. |
| `HashTelemetry` | Red when resized, Amber at or above 80% of configured load factor, Green below it, and Off when there is no meaningful target. |
| `ThreadTelemetry` | Red at 80% CPU or above, Amber at 50% or above, otherwise Green; an unavailable sample may be Off. |
| `MxTelemetry` | Running Green; Starting and StartPending Amber; Stopped, Stopping, and StopPending Red; unknown/null Off. |
| `CxnTelemetry` | For known socket-buffer capacities, Red when either side is at least 80% occupied, Amber at 50%, otherwise Green; Off when neither capacity is known. |
| `QueueTelemetry` | Off when size is zero/unknown, Red at 80% occupancy, Amber at 50%, otherwise Green. |
| `LinkTelemetry` | Up Green; Connecting, ReconnectPending, Reconnecting, Disconnecting, ConnectPending, and DisconnectPending Amber; Down and Failed Red; Disabled and Deleted Off. |
| `HubTelemetry` | Use the same engine-state mapping as `MxTelemetry`. |
| `PoolTelemetry` | Up Green; Down and Failed Red; unknown/null Off. |
| `DBTableTelemetry` | Off before any cache lookup; Red when misses exceed 80% of lookups; Amber when misses exceed 50%; otherwise Green. |
| `DBHostTelemetry` and `DBTelemetry` | Instantiated Red; Active Green; valid transitional or inactive states Amber; unknown/null Off. |

- `HeapTelemetry::allocated()` must preserve the current counter semantics.
  Counter invariants must be documented or subtraction must be protected from
  underflow.
- Existing or subsequently discovered newer `Ztc` derived members override
  this migrated legacy behavior if they conflict.

### Complete lower-level telemetry contracts

- Retain every member currently populated by `Ztc` implementers, including
  members missing from the legacy `Ztel` schema.
- Public field widths and signedness must match producers. Process-lifetime
  counters remain 64-bit.
- Keys must remain stable for the registered lifetime of an object and must
  round-trip without identifier truncation.
- Telemetry is a best-effort low-overhead snapshot. Diagnostic counters do not
  require new atomics or locks solely to make multi-field reads perfectly
  consistent.
- Respect shard ownership. State that can only be sampled on a specific shard
  must be sampled there and completed through a continuation; do not add locks
  to legitimize wrong-shard access.
- Manager and child traversal APIs must return the number of objects actually
  visited. Increment counts during successful traversal rather than
  snapshotting a container count.
- A callback receiving a raw telemetry-interface pointer may not retain it
  beyond the traversal or explicitly documented continuation lifetime.
- Iterator lifetimes must end before container mutation, cleanup, outbound
  send, or cross-thread posting that does not require the iterator.

### Generic database telemetry hierarchy

- Add and install:
  - `zv/src/ZtcDBTable.hh`;
  - `zv/src/ZtcDBHost.hh`;
  - `zv/src/ZtcDB.hh`;
  - `zv/src/ZtcDB.cc`.
- These headers must contain plain generic telemetry structures and abstract
  interfaces. Their `ZfbStruct` metadata belongs exclusively in `ZtcFB.hh`.
- `ZtcDBTable.hh` must define:
  - the complete generic table telemetry structure;
  - its stable key;
  - the cache-mode vocabulary required by telemetry;
  - abstract table key and `telemetry(...)` functions.
- `ZtcDBHost.hh` must define:
  - the complete generic host telemetry structure;
  - its stable key;
  - the generic database-host state vocabulary;
  - abstract host key and `telemetry(...)` functions.
- `ZtcDB.hh` must define:
  - `DBTelemetry`;
  - abstract `DB`;
  - `virtual unsigned allHosts(AllHostsFn) const`;
  - `virtual unsigned allTables(AllTablesFn) const`;
  - the DB key and `telemetry(...)` functions;
  - `virtual bool start()` and `virtual bool stop()` controls matching the
    established `Hub` pattern and implementing DB lifecycle; and
  - a root manager exposing `static void add(DB *)`, `static void del(DB *)`,
    and `static unsigned allDBs(AllDBsFn)`.
- `DBMgr` must follow `ZtcHubMgr_`: use a library-lifetime `ZmSingleton`
  containing a unique pointer-indexed `ZmRBTree`, `ZmPLock`, and named
  `ZmHeap` ID.
- A DB registers once after successful `init` and deregisters in its
  destructor. Failed initialization must not leave an entry. Destruction is
  valid only after stop and after dependent telemetry traversal has drained.
- Hosts and tables remain owned and traversed by their parent DB; do not add
  separate global managers for them.
- DB, host, and table identifiers must round-trip in full. Do not preserve the
  legacy fixed 28-byte DB-table telemetry name when the implementing database
  accepts longer identifiers.
- Generic DB headers must not include `ZdbLib.hh`, `ZdbTypes.hh`, or generated
  `zdb` schema headers.

### Re-platform `Zdb` on `Ztc`

- `Zdb_::DB`, `Zdb_::Host`, and `Zdb_::AnyTable` must derive from
  `Ztc::DB`, `Ztc::DBHost`, and `Ztc::DBTable`, respectively.
- Implement the generic key, telemetry, and traversal virtual functions
  directly; do not add compatibility shims around the current
  FlatBuffers-producing telemetry functions.
- Replace DB methods that build telemetry FlatBuffers with methods that fill
  generic `Ztc` structures. Serialization is owned by
  `ZtcFB.hh`/`Ztc::App`.
- Preserve all current sampled data:
  - DB identity; leader, previous, and next identities; state and activity
    flags; table, host, peer, and connection counts; configured thread; and
    heartbeat, reconnect, and election settings;
  - host identity, priority, state, vote, IP, and port;
  - table identity, shard/thread configuration, record count, cache size,
    loads, misses, evictions, and cache mode.
- DB telemetry and host traversal must run on the DB thread. Table telemetry
  must run on its proper shard or use an existing safe statistics access path.
  Cross-shard traversal completes asynchronously.
- `Zdb_::CacheMode` and `Zdb_::HostState` must not depend on generated
  telemetry schemas to obtain enum names. Use ordinary lower-layer enum
  declarations or alias the generic `Ztc` vocabularies, with compile-time
  ordinal checks in `ZtcFB.hh`.
- Remove active use and installation of `ZdbTelemetry.hh`. Keep `zdb_.fbs` and
  other replication/persistence schemas in `zdb`; migrate only
  telemetry-specific schema ownership to `zv`.

### FlatBuffers schema ownership and metadata

- Recreate the active request, acknowledgement, telemetry payload, and related
  enum schemas under `zv/src/fbs`, using the legacy `zcmd/src/fbs` files only as
  migration input.
- Move or replace active `zdb_telemetry.fbs`, `zdb_cache_mode.fbs`, and
  `zdb_host_state.fbs` definitions under `zv/src/fbs`. The generic telemetry
  schema must build without a source include path or dependency on `zdb`.
- Rename the active schema namespace and generated types to `Ztc.fbs`.
  Propagate `Ztel` to `Ztc` renames through all active dependent code. Do not
  add alias schemas.
- `ZtcFB.hh` must enrich every server-visible current `Ztc` type:
  heap, hash, thread, multiplexer, connection, queue, hub, link, pool, DB,
  DB host, DB table, application, and alert.
- Metadata must identify:
  - primary and composite keys;
  - mutable/update fields;
  - series and delta counters;
  - enum and flag mappings;
  - numeric presentation such as CPU/load-factor precision;
  - synthetic `rag` and `allocated` fields.
- Derive schemas and metadata from the current `Ztc` structures, not from
  legacy tables. At minimum, the migrated schema must include/correct:
  - heap `crossFrees`, `allocated`, and `rag`;
  - 64-bit hash `count`;
  - connection `rxCalls`, `rxBytes`, `txCalls`, and `txBytes`;
  - unsigned thread SID and 64-bit thread/alert TIDs;
  - DB table `nShards` width and the cache-eviction mapping;
  - every other current field and required derived RAG field.
- Every enum mapping requires compile-time ordinal assertions.
- Every struct mapping requires a round-trip or generated-accessor test that
  detects omissions, duplicated constructor indexes, width loss, signedness
  loss, and incorrect union membership.
- Use the repository's established `flatc`, embedded-BFBS, include rewriting,
  and formatting rules. Generated telemetry headers are built and installed
  by `zv`, not by active `zdb` or any active dependency on `zcmd`.

### Existing header/body protocol

- Reuse the established telemetry header/body binary protocol. Do not define a
  new FlatBuffers envelope and do not use FlatBuffers size-prefix framing.
- Move or re-express the required header declarations and save/load/verify
  functions in `Ztc`-owned `zv` code so `Ztc::App` and the test client do not
  include or link `Zcmd`.
- Preserve the existing header layout and semantics: a bounded body length and
  a message/body type select the corresponding verified FlatBuffers root.
- Header parsing must support fragmented and coalesced TCP input and must not
  access a body until the complete declared frame is available.
- Body kinds must distinguish at least request, acknowledgement, telemetry,
  snapshot completion, and protocol error. Adding completion/error body types
  extends the existing typed protocol without replacing its framing.
- Every frame must be length-checked and its selected FlatBuffers root
  verified before any generated accessor is used.
- `AppCf` must configure maximum frame and filter lengths. Oversized,
  truncated, malformed, type/body-mismatched, or invalid-union frames must not
  reach worker logic or trigger unbounded body allocation.
- Every syntactically complete request receives an acknowledgement containing
  the original sequence number and success/failure status. Invalid request
  types, filters, intervals, and unsubscribe targets receive a defined
  negative acknowledgement.
- Acknowledgements report the effective interval after clamping.
- Telemetry and completion bodies carry the originating request/subscription
  sequence number. One-shot snapshots end with an explicit completion body;
  clients must not infer completion from silence.
- Request groups cover:
  - heaps;
  - hashes;
  - threads;
  - multiplexers and their connections;
  - queues;
  - hubs and their links and pools;
  - databases and their hosts and tables;
  - application status;
  - alerts.
- Filters support match-all, exact match, and a trailing-prefix wildcard.
  Empty is match-all. Composite keys use one documented unambiguous textual
  form.
- Output is deterministic within each synchronous manager traversal. No global
  order is required across independently sharded continuations, but completion
  waits for all work belonging to the snapshot.

### Initial transport and trust boundary

- The initial implementation uses plain `Ztcp`; it does not use `Ztls`.
- The listener is loopback-only. Configuration must reject non-loopback bind
  addresses rather than silently exposing unauthenticated telemetry.
- No login, authentication, authorization, certificate, or trust-model
  integration is required in this version.
- The server must support IPv4 loopback and IPv6 loopback where the platform
  provides it.
- Protocol input remains hostile from a correctness/resource perspective even
  on loopback and must undergo all framing, schema, and size validation.

### `Ztc::AppCf`

- Add public `Ztc::AppCf` with `ZfStruct` configuration metadata, following
  the value-object and parsing pattern of `Zdb::DBCf`.
- Configuration includes:
  - normal `ZvMxCf`/`ZvMxParams` multiplexer settings;
  - configurable timer, Rx, Tx, and worker thread identities;
  - loopback listen IP and port;
  - accept concurrency and transient rebind interval;
  - minimum and maximum subscription intervals with documented units;
  - maximum frame/filter sizes and per-link outbound limits;
  - application ID, version, and role;
  - alert file prefix/path, replay retention, and replay-cache/pool tuning.
- Thread names/SIDs must resolve during initialization. Timer, Rx, Tx, and
  worker roles must be valid isolated scheduler threads. Reject aliases where
  independent ownership is required.
- Defaults must create a usable loopback server and must never bind a wildcard
  or non-loopback address.
- A listen port of zero is valid and requests a dynamically assigned port.
- Invalid configuration fails with a specific `ZeException` or `ZiAssert`
  diagnostic and leaves no live multiplexer, listener, timer, worker state,
  manager registration, or sink callback.

### `Ztc::App` API, lifecycle, and application telemetry

- Add `zv/src/ZtcApp.hh` and required `.cc` implementation as concrete,
  reusable `Ztc::App`; legacy `App` and `Server` are unified into this type.
- `App` owns its `ZiMultiplex`, `Ztcp` server/link objects, scheduler roles,
  subscription state, and alert persistence.
- `App` must not depend on legacy `ZvEngine`, `Zcmd`, or `Ztel`.
- Public lifecycle follows repository vocabulary: `init`, idempotent `start`,
  idempotent `stop`, and `final`.
- Initialization configures all state but does not accept traffic. Startup
  validates/starts the multiplexer and worker before listening.
- Shutdown must:
  1. reject new work and stop listening;
  2. cancel subscription and rebind timers;
  3. drain Rx and disconnect links;
  4. drain worker requests, DB continuations, accepted alerts, and
     subscriptions;
  5. drain Tx buffers;
  6. finalize transport, persistence, and the owned multiplexer.
- Timer and I/O teardown use owning-thread continuations. Scheduler/I/O threads
  must not block.
- Links may use raw App back-pointers under the established transport pattern;
  App must drain every dependent link before destruction.
- Expose the actual bound loopback endpoint after listening, including the
  selected port when configured with port zero.
- `Ztc::App` owns application ID/version/role, start time, and current RAG.
  It exposes worker-dispatched accessors/mutators for current RAG.
- Changing App RAG marks application telemetry changed and causes subscribed
  application updates to be sent from the worker.

### Worker ownership and cross-shard handoff

- The App worker exclusively owns:
  - decoded request state after Rx handoff;
  - subscriptions, canonical filters, and due times;
  - FlatBuffers builders used for outbound telemetry;
  - snapshot aggregation and completion state;
  - alert date/sequence state, active files, replay state, and optional
    bounded replay tail;
  - application telemetry and App RAG.
- Rx only parses bounded framing, verifies the selected FlatBuffers body, moves
  the pooled input buffer, and posts fixed metadata plus the buffer handle to
  the worker.
- The worker selects, samples/coordinates, serializes, orders, and enqueues
  telemetry. The physical socket write remains Tx-owned.
- Worker-built frames are immutable when handed to Tx. Tx must not read or
  mutate worker subscription state.
- Variable-sized data crosses shards in moved pooled buffers, not captured
  strings/arrays or temporary contiguous copies.
- Large manager/subscriber traversals must be processed in bounded turns and
  continued by posting, so the App worker does not starve other work.

### Discovery and snapshot processing

- Discover telemetry from `HeapMgr`, `HashMgr`, `ThreadMgr`, `MxMgr`,
  `HubMgr`, and `DBMgr`. Do not retain duplicate manual registries for
  engines, queues, or DBs.
- `Ztc::Mx` must derive from `Ztc::QueueMgr`. `ZiMultiplex`, which already
  implements both contracts, must implement the resulting direct interface
  without a concrete cast or RTTI.
- Multiplexer snapshots emit the matching `MxTelemetry`, matching dependent
  connections, and its scheduler queues when requested by the appropriate
  query group.
- Hub snapshots emit matching `HubTelemetry` and dependent links and pools
  through `allLinks` and `allPools`.
- Standalone Queue queries include:
  - scheduler/multiplexer queues via `Mx::allQueues`;
  - every link queue via `Link::allQueues`;
  - every pool queue exposed by `Pool::txQueue`.
- Link and pool queues must appear in Queue queries even if they are also
  emitted as dependent data in another snapshot.
- DB snapshots emit each matching DB and all matching hosts and tables. Delay
  completion until every asynchronous table sample finishes.
- Deduplicate an object reachable through multiple discovery paths within one
  request by its canonical composite key.
- Do not hold manager/container iterators across serialization, sends,
  cleanup, mutating callbacks, or cross-thread posts.

### Subscription semantics and scheduling

- A subscription is identified per connection by request type and canonical
  filter. Re-subscribing that key updates its interval instead of creating a
  duplicate.
- Interval zero is a one-shot snapshot.
- A non-zero interval with `subscribe=true` creates or updates a
  subscription.
- `subscribe=false` removes the matching subscription and does not create a
  transient watch.
- Requests below the configured minimum interval are clamped to the minimum
  and the effective value is returned in the acknowledgement. Requests above
  the maximum are rejected.
- Each subscription runs no faster than its own interval. Do not reproduce the
  legacy behavior where all watchers run at the shortest interval.
- Use one next-due timer per request group or a worker-owned due-time queue,
  rather than one timer allocation per subscription.
- If a snapshot is still running when another interval becomes due, coalesce
  all missed ticks into one pending tick. Start the next snapshot only after
  the current snapshot completes; do not queue each missed interval.
- Disconnect deterministically removes the link's subscriptions, pending
  snapshot state, and queued outbound references.
- Disarm a group timer when it has no subscriptions. Cancel all timers and
  drain late callbacks during stop/final.
- Subscription iteration/removal must not keep container iterators or locks
  alive while serializing or sending.

### `ZiLog` sink and alert acceptance

- `Ztc::App` provides a `ZmRef<ZiSink>` suitable for explicit installation
  with `ZiLog::sink(...)`. App construction must not silently replace the
  process-global sink.
- The sink executes on the `ZiLog` thread. It copies required fixed metadata
  and message bytes into telemetry-owned storage and enqueues the event for
  the App worker.
- The sink must not retain `ZeLogBuf`/`ZeEventInfo` references, perform file
  I/O, serialize telemetry, send network data, or capture a destructible App
  pointer without a lifetime-safe indirection.
- Alert acceptance means successful enqueue to the worker path, not persistence
  or `fsync`.
- The worker path deliberately uses the existing queue/ring behavior and its
  unbounded dead-letter fallback (`m_queue`) when the fast queue is full.
  Alert ingress must not drop because of a fixed queue bound. This is an
  explicit exception to the general bounded-container posture.
- The exception applies only to pending accepted alert work. Processed alert
  history must not accumulate in an unbounded App container; persistence is
  the replay source.
- A sink reference that outlives stopped/finalized App becomes inert without
  dereferencing App. Stop/final drains alerts accepted before shutdown.
- On the worker, assign the alert time, severity, TID, message, and the
  `YYYYMMDD:seqNo` identity used by persistence and the wire protocol.

### Alert persistence, identity, and replay

- Alert identity is `date:seqNo`, where `date` is an unsigned `YYYYMMDD`
  integer and `seqNo` starts at zero for each date.
- Preserve date-partitioned append-only data files with a companion index
  mapping each daily sequence number to its data-file offset.
- The worker serializes each alert once, appends it in order, and uses the same
  immutable buffer for live fanout where possible.
- Logger acceptance does not promise persistence. The worker writes data and
  index in order without an `fsync` per alert.
- File/index validation must reject corrupt offsets, decreasing/out-of-range
  indexes, truncated bodies, and invalid serialized alerts.
- Persistence failures must not call `ZiLOG` through the same sink. Use a
  non-recursive fallback diagnostic and set degraded App RAG/status.
- Replay requests use the explicit `YYYYMMDD:seqNo` key.
- Retention and lower-bound clamping use real calendar arithmetic across month,
  leap-year, and year boundaries; never subtract integers from `YYYYMMDD`.
- Retention cleanup occurs at a bounded lifecycle point such as startup or
  daily rotation and must not use an unbounded recurring directory scan.
- Replay establishes a stable disk/live handoff, emits alerts in order without
  duplicates, and then transitions the subscription to live delivery.
- Any in-memory replay tail is bounded and exists only to bridge the stable
  disk/live handoff. Slow or disconnected clients must not retain alert
  history in App memory.

### Resource use, performance, and resilience

- Use pooled `ZiIOBuf` storage and named/tunable `ZmHeap` IDs for inbound,
  outbound, acknowledgement, telemetry, and alert buffers.
- Avoid allocation per telemetry field or subscriber. Share an immutable
  serialized update among recipients when correlation and ordering permit.
- Except for the explicitly accepted alert-work dead-letter fallback, every
  long-lived container has deterministic removal and an explicit bound or a
  workload-derived tuning policy.
- Configure bounds for frames, filters, pending non-alert requests,
  subscriptions per link, and outbound bytes per link.
- A slow telemetry client exceeding its outbound allowance is disconnected;
  it must not stall the worker or grow an App-owned queue indefinitely.
- Repeated invalid requests must not allocate durable state.
- RAG/statistical snapshots may be approximate, but keys, framing, sequence
  correlation, persistence offsets, and lifecycle transitions must be exact.
- Use `ZiMultiplex`/`Ztcp` and framework containers/callbacks; do not introduce
  STL containers, ad hoc allocators, polling, or blocking worker/I/O loops.
- The implementation must operate on current gcc/clang, x64/ARM64 Linux, and
  Windows through msys2/mingw.

### Active build integration and legacy scope

- Add all new public headers, `.cc` files, telemetry schemas, generated
  headers, and library dependencies to `zv/src/Makefile.am`.
- Add `libZtcp` as the transport dependency of `libZv` without creating a
  cycle.
- Remove active use and installation of telemetry-specific schema/header
  artifacts from `zdb`; active telemetry schema generation belongs to `zv`.
- Do not add `zcmd` to the top-level build, do not make its standalone binary a
  release gate, and do not migrate its telemetry client.
- Leave unrelated and legacy `zcmd` source unchanged. Its archival copies of
  `Ztel` do not grant compatibility status and must not be included by active
  code.
- Remove `Ztel` naming from all active code, generated schema namespaces,
  tests, installed active headers, comments, and documentation.
- `zv/test/Makefile.am` must build and run the telemetry server/client tests.
- A clean top-level build must generate all `zv` telemetry schemas in
  dependency order without generated artifacts from `zcmd` or `zdb` source
  directories.

### Test telemetry-serving App and client

- Add a telemetry-serving App and a protocol client under `zv/test`. The client
  may be test-only but must use the public network protocol rather than private
  App functions.
- Use loopback and port zero. IPv4 is required; IPv6 runs when available and
  otherwise emits a TAP skip diagnostic.
- The client must test:
  - the existing header/body framing with fragmented and coalesced TCP input;
  - positive/negative acknowledgements and request-sequence correlation;
  - explicit one-shot snapshot completion;
  - exact, match-all, prefix, and composite-key filters;
  - representative heap, hash, thread, multiplexer, connection, queue, hub,
    link, and pool payloads;
  - scheduler, link, and pool queue discovery;
  - DB/host/table hierarchy through lightweight `Ztc` mock implementers;
  - per-subscription interval scheduling, update, overrun coalescing,
    unsubscribe, and disconnect cleanup;
  - App-owned RAG updates;
  - `ZiLog` sink enqueue acceptance, dead-letter fallback drainage,
    persistence, `YYYYMMDD:seqNo` replay, replay/live handoff, and
    non-recursive persistence failure;
  - malformed bodies, incorrect body types, oversized frames, invalid enums,
    and slow-client disconnection;
  - orderly stop/final with clients, timers, DB continuations,
    subscriptions, and accepted log events active.
- Schema tests serialize and load every telemetry union member and compare
  every field, including 64-bit values above `UINT32_MAX`.
- Manager tests verify registration/deregistration, pointer uniqueness, nested
  traversal, and returned counts equal actual callback visits.
- Concurrent tests use `ZmBlock` or `ZmSemaphore`; sleeps and polling are not
  valid completion mechanisms.
- Tests must not require TLS certificates, credentials, authentication, or
  non-loopback access.

### Acceptance

- Run `make -j8`.
- Run `make -C zv/test -j8 && make -C zv/test test`.
- Run affected `zm`, `zi`, and `zdb` tests.
- Run top-level `make test`.
- Verify with clang AddressSanitizer and LeakSanitizer.
- Run the telemetry integration test through
  `libtool exec valgrind --leak-check=full`.
- Supported gcc/clang builds must complete without warnings.
- Shutdown tests must leave no registered DBs, listeners, timers,
  subscriptions, sink callbacks, pooled buffers, open alert files, or leaked
  objects.

### Non-goals

- Source, ABI, schema-name, generated-type, or payload compatibility with
  `Ztel`.
- Rebuilding or re-platforming the legacy `zcmd` command/authentication stack.
- TLS, authentication, authorization, certificates, non-loopback listening,
  or a WAN trust model in the initial version.
- A dashboard, aggregation database, or remote control plane.
- Perfectly atomic multi-counter telemetry snapshots.
- An `fsync` for each alert or a promise that sink acceptance means durable
  persistence.
- A fixed hard bound or drop policy for pending alert work in the existing
  dead-letter fallback; this exception does not permit unbounded retained alert
  history.

## Resolved Decisions

- Reuse the existing typed header/body binary protocol; do not introduce a
  FlatBuffers envelope or size-prefix framing.
- Use unauthenticated plain `Ztcp` on loopback only. Security and trust-model
  work is deferred.
- Keep legacy `zcmd` out of scope and out of the active build. Do not migrate
  its client; eliminate `Ztel` from all active code and schemas.
- `Ztc::App` owns current application RAG.
- Alert identity is unsigned `YYYYMMDD:seqNo`, with daily sequence reset.
- Alert acceptance means enqueue to the App worker path. The existing
  unbounded dead-letter fallback is retained deliberately; persistence occurs
  asynchronously and does not `fsync` per alert.
- `Ztc::Mx` derives from `Ztc::QueueMgr`; standalone Queue queries include
  multiplexer, link, and pool queues.
- Subscription overruns coalesce missed ticks and schedule one next snapshot
  only after the current snapshot completes.
- Rename active schemas and dependent code from `Ztel` to `Ztc`; do not add
  compatibility aliases.
