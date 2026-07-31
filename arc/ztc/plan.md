# Ztc Telemetry Service Development Plan

## Summary

The active tree currently has useful telemetry producers and discovery interfaces
spread across `zm` and `zi`, while `Zdb` still owns database-specific telemetry
serialization. `zv` has no concrete telemetry service, does not generate
telemetry schemas, and does not link `Ztcp`. The in-progress `Ztc` manager work
also needs to be completed and tested.

The target is a first-class `Ztc` stack whose layering is:

```text
zm/zi producers and generic DB interfaces
        -> plain Ztc telemetry structures
        -> zv/ZtcFB FlatBuffers metadata and schemas
        -> worker-owned Ztc::App snapshot/subscription/alert service
        -> typed header/body frames over loopback-only Ztcp
```

`Ztc::App` will own its `ZiMultiplex`, transport, worker/timer/Rx/Tx roles,
subscriptions, snapshot coordination, builders, outbound accounting, application
status, and alert persistence. `Zdb` will implement generic `Ztc` DB interfaces
and fill plain structures; it will no longer own telemetry FlatBuffers metadata
or schemas. Current `Ztc` fields and widths are the sole source of truth. This
is a complete replacement with no compatibility aliases, adapters, or alternate
generated names.

Implementation should proceed as vertical slices. First define the new typed
frame contract and establish schema/build foundations. Then deliver a
minimal real loopback request/ack/telemetry/completion path before expanding
discovery, DB coordination, subscriptions/backpressure, and alerts. Each slice
adds public-protocol tests and leaves lifecycle teardown working.

Phase 0 defines and freezes the sole authoritative `Ztc` typed header/body
contract.

## Architecture Documentation

Update `CODEBASE.md` as the implementation lands, in the same commits as the
corresponding architectural changes:

- Document `Ztc::App` as the active telemetry service and the `Ztc` contracts as
  its only supported API, schema, and wire surface.
- Document the ownership boundary: lower modules expose plain structures and
  traversal/control interfaces; `zv/src/ZtcFB.hh` is the only FlatBuffers
  enrichment layer; `Ztc::App` owns serialization and delivery.
- Add the generic DB dependency direction (`zv` contracts, `zdb`
  implementation) and state that `zv` must never include `zdb`.
- Document the four scheduler roles and data flow:
  Rx validates framing and bodies, worker owns mutable service state, timer
  posts due tokens, and Tx owns socket writes.
- Document asynchronous snapshot completion, especially DB/table continuations,
  and the lifetime rule for raw pointers supplied during manager/child
  traversal.
- Document canonical textual keys and filter grammar, including escaping,
  composite component order, and the trailing-wildcard rule.
- Document the wire contract after Phase 0: exact header bytes, byte order,
  message type values, maximum length handling, body-root selection, and the
  explicit completion/error additions.
- Document subscription scheduling, overrun coalescing, outbound-byte
  accounting, and slow-client disconnect policy.
- Document alert acceptance versus durability, daily file/index formats,
  `YYYYMMDD:seqNo` identity, retention, validation, and the disk/live replay
  handoff.
- Update stale active comments in `zm/src/ZmHeap.hh`, `zm/src/ZmHash.hh`,
  `zv/test/ZvEngineTest.cc`, and the active README module summary so they
  describe only the new `Ztc` service.

## Detailed Design and Implementation Plan

### Phase 0: Define the new protocol and establish a baseline

1. Specify the complete `Ztc` frame contract before implementing its parser:

   - a fixed 16-byte header serialized field-by-field, never by copying a C++
     struct;
   - 32-bit magic `ZTC1`, 16-bit protocol version, 16-bit body type, 32-bit body
     length, and 32-bit flags/reserved field;
   - network byte order for every header integer;
   - body length excludes the header and is bounded by `AppCf::maxFrame`;
   - version 1 requires flags/reserved to be zero;
   - distinct body type values for Request, Ack, Telemetry, SnapshotComplete,
     and Error.

   Freeze the numeric constants in `ZtcMsg.hh` and document how an incompatible
   future protocol increments the version rather than changing version-1
   semantics.
2. Add canonical byte-vector tests constructed directly from this specification.
   Verify exact encoding/decoding, byte order, length semantics, every body type,
   reserved-bit rejection, and version rejection. These vectors define the new
   contract.
3. Capture a clean baseline of `make -j8`, affected `zm`, `zi`, `zv`, and `zdb`
   tests, and current warnings. Keep the user's existing worktree changes
   intact; distinguish pre-existing failures from migration regressions.
4. Inventory active includes, installed headers, generated artifacts, and
   comments tied to `ZvRAG` or `ZdbTelemetry` so the replacement leaves a single
   active telemetry ownership path.

Exit gate: implementation of network framing does not start until the new
header specification and its canonical byte vectors are reviewed.

### Phase 1: Complete plain telemetry contracts and schema ownership

1. Add dependency-free `zm/src/ZtcRAG.hh` and include it only where a telemetry
   structure needs `Ztc::RAG::T`. Implement derived members on their owning
   lower-level structures:

   - heap `allocated()` and RAG, guarding counter underflow or asserting and
     documenting the producer invariant;
   - hash target/load RAG with overflow-safe comparisons;
   - thread CPU RAG, treating an unavailable sample as Off if an availability
     signal is added, otherwise retaining the producer's current zero sample;
   - multiplexer/hub engine-state RAG;
   - connection socket-buffer occupancy RAG;
   - queue occupancy RAG;
   - link and pool state RAG;
   - DB, DB host, and DB table RAG in their new generic structures.

   Preserve every current producer-populated field, especially 64-bit lifetime
   counters, heap `crossFrees`, and connection Rx/Tx call and byte counters.
   Synthetic `allocated` and `rag` metadata are read-only unless the metadata
   machinery requires a no-op setter.

2. Complete manager semantics in `ZmHeap`, `ZmHash`, `ZmThread`,
   `ZiMultiplex`, and `ZtcHub`:

   - unique pointer-indexed, library-lifetime registration;
   - counts incremented for actual callback visits;
   - iterator scope ended before any operation that can mutate, post, serialize,
     or send;
   - registration/deregistration ordering that leaves no stale raw pointers.

   Retain applicable in-progress manager changes already present in the
   worktree and extend them rather than replacing unrelated user edits.

3. Add the generic DB contract as three public headers:

   - `ZtcDBTable.hh`: owning/full table name, cache mode enum, table telemetry,
     stable `{dbID, tableID}` key, `telemetry`, and an explicit telemetry-shard
     dispatch callback.
   - `ZtcDBHost.hh`: host state enum, host telemetry, stable
     `{dbID, hostID}` key, and `telemetry`.
   - `ZtcDB.hh`: DB telemetry, full DB key, `start`, `stop`, `allHosts`,
     `allTables`, DB-owner-thread telemetry dispatch, and `DBMgr::allDBs`.

   Include a parent DB identifier in host and table payloads/keys so multiple
   registered databases cannot collide. Keep these headers independent of
   `ZdbLib`, `ZdbTypes`, generated `zdb` schemas, `ZiMultiplex`, and
   FlatBuffers. Express asynchronous dispatch with named `ZmFn` types and
   document that raw interface pointers remain valid only through the callback
   or explicitly scheduled continuation.

4. Replace the incomplete in-progress `ZtcDB.hh/.cc` implementation with the
   split contract. Implement `DBMgr` using the `ZtcHubMgr_` pattern:
   library-cleanup `ZmSingleton`, unique pointer-keyed `ZmRBTree`, `ZmPLock`,
   and a named `ZmHeap` ID. Return the number actually visited from
   `allDBs`.

5. Add plain application/alert structures in a small public header such as
   `ZtcAppTypes.hh` to avoid circular inclusion:

   - application ID, version, role, start time, current RAG, and lifecycle
     state;
   - alert unsigned date, daily 64-bit sequence, timestamp, 64-bit TID,
     severity, and owning message.

6. Recreate telemetry schemas under `zv/src/fbs` with namespace `Ztc.fbs`.
   Use current structures as the source of truth:

   - one union covering Heap, Hash, Thread, Mx, Cxn, Queue, Hub, Link, Pool,
     DB, DBHost, DBTable, App, and Alert;
   - request groups Heap, Hash, Thread, Mx, Queue, Hub, DB, App, and Alert;
   - Ack with request sequence, status/error, and effective interval;
   - Telemetry with originating sequence and the telemetry union;
   - SnapshotComplete with sequence, status, and emitted count;
   - Error with optional sequence, stable error code, and bounded message.

   Use the authoritative current widths: 64-bit hash count and TIDs, unsigned
   thread SID, connection counters, heap derived/cross-free fields, 32-bit DB
   shard count, pool payload, and correct cache-eviction constructor mapping.

7. Add `zv/src/ZtcFB.hh` as the sole `ZfbStruct` enrichment point. Provide:

   - metadata for every server-visible structure and all primary/composite keys;
   - mutable, series, delta, enum/flag, precision, and synthetic annotations;
   - `ZtEnumNames` and `ZfbEnumMatch` checks for every C++/schema enum;
   - explicit union save/load dispatch with no alternate metadata path.

8. Move telemetry FlatBuffers generation into `zv/src/Makefile.am`, following
   the existing `flatc`, embedded BFBS, include rewrite, `clang-format`, install,
   and clean rules. Add `zfb` inputs but defer the `Ztcp` link change until the
   minimal App slice. Remove `zdb_telemetry.fbs`, `zdb_cache_mode.fbs`, and
   `zdb_host_state.fbs` from `zdb` only after all active `Zdb` consumers have
   migrated; keep `zdb_.fbs`.

9. Add a schema/metadata test that initializes distinctive values in every
   field, including values above `UINT32_MAX`, serializes every union member,
   verifies the buffer, reloads it, and compares all fields and derived values.
   Include compile-time enum ordinal checks and negative invalid-union tests.

### Phase 2: Deliver a minimal real loopback App vertical slice

1. Add `zv/src/ZtcMsg.hh` (and `.cc` only if non-inline helpers are warranted)
   containing the Phase 0 typed header and save/load/verify helpers. Parsing must
   be incremental over `Ztcp::RxStream`:

   - peek/copy only the fixed header until complete;
   - reject an invalid type or length before allocating a body-sized buffer;
   - wait for the complete declared frame before body access;
   - verify the selected FlatBuffers root and union before worker handoff;
   - consume multiple coalesced frames and retain fragmented tails;
   - return a protocol error or disconnect according to the documented
     syntactic/structural failure table.

2. Add public `Ztc::AppCf` with `ZfStruct` parsing. Reuse
   `ZvMxCf`/`ZvMxParams` and include named timer/Rx/Tx/worker roles, loopback IP
   and port, accepts/rebind interval, min/max subscription interval, frame and
   filter bounds, per-link pending request/subscription/outbound bounds,
   application identity/version/role, alert prefix/retention, and buffer/pool
   tuning. Give every limit a safe nonzero default and documented unit.
   Validate all names/SIDs during `init`; reject wildcard/non-loopback IPs and
   reject role aliases where independent ownership is required.

3. Fix port-zero endpoint reporting in `ZiMultiplex` at the listener creation
   boundary. After successful `bind`, use `getsockname` on Unix and Winsock to
   populate the actual local IP/port in `ZiListenInfo`. Ensure listener lookup
   and `stopListening` use that actual endpoint. Test fixed ports and port zero
   for IPv4 and available IPv6.

4. Implement concrete `Ztc::App` as the CRTP application for
   `Ztcp::Server<App>`, unifying App and server:

   - hold the non-movable `ZiMultiplex` in explicit in-place storage constructed
     during `init` and destroyed by `final`, avoiding an anonymous heap
     allocation;
   - `init` validates and constructs configuration but does not listen;
   - `start` starts the multiplexer/worker, initializes and registers the TCP
     hub after the multiplexer is running, starts the hub, then listens;
   - `stop` is idempotent and completes only after the full continuation chain;
   - `final` requires stopped state, finalizes transport/persistence, and
     destroys the multiplexer storage;
   - expose configured and actual bound endpoints distinctly so a port-zero
     restart and `stopListening` use the right values.

   Roll back each startup stage in reverse order on failure, leaving no manager
   registration, listener, timer, scheduler task, link, or sink callback.

5. Add an App-specific server link deriving from `Ztcp::SrvLink`:

   - raw App back-pointer under the established transport ownership rule;
   - connection generation used to discard late worker/Tx completions;
   - bounded incremental `process` implementation;
   - Rx-to-worker handoff containing only fixed frame metadata and a moved
     pooled body buffer;
   - disconnect notification posted to the worker for deterministic state
     cleanup.

6. Establish worker-owned structures now even though this slice serves only an
   App one-shot:

   - per-link state and outbound-byte count;
   - bounded pending request map;
   - snapshot context with originating sequence, emitted count, outstanding
     continuations, cancellation/generation, and completion;
   - one by-value due timer and due-time queue skeleton;
   - named `ZmHeap`/`ZiIOBuf` allocators for input, Ack, Telemetry, Completion,
     Error, and later alert frames.

7. Implement the first public protocol path: App one-shot request -> Ack ->
   application Telemetry -> SnapshotComplete. Ack every syntactically complete
   request, including defined negative Ack for invalid group/filter/interval.
   App RAG getter/setter dispatches to the worker; a changed value marks App
   telemetry dirty.

8. Implement exact outbound accounting. Before enqueue, the worker checks the
   per-link byte allowance and increments accepted bytes. Extend the App link's
   Tx completion hook to post `{link generation, length, result}` back to the
   worker for decrement. If the existing `Ztcp` send path cannot distinguish a
   rejected enqueue during disconnect, add the smallest reusable `Ztcp` hook
   needed to report acceptance; do not infer acceptance from a void call.
   Exceeding the allowance removes worker state and disconnects the slow link.

9. Add a real test-only `Ztcp` client, using only public framing and sockets.
   Cover IPv4 port-zero App telemetry, negative Ack, sequence correlation,
   explicit completion, fragmented header/body, coalesced requests, actual
   bound endpoint, start/stop idempotence, and unavailable-IPv6 TAP skip.

### Phase 3: Expand discovery to process and network telemetry

1. Make `Ztc::Mx` derive from `Ztc::QueueMgr`. Because `ZiMultiplex` already
   obtains a QueueMgr contract through `ZmScheduler`, implement an explicit
   `ZiMultiplex::allQueues` override that forwards to
   `ZmScheduler::allQueues`. This resolves the direct `Ztc::Mx` interface
   without RTTI, concrete casts, virtual inheritance, or duplicate queue
   storage.

2. Define and document canonical textual keys. Use percent-escaped path
   components, `/` separators, and uppercase hex escapes so literal `/`, `%`,
   and `*` in IDs cannot collide with structure. A raw final `*` is the only
   wildcard. Suggested component order:

   - heap: `id/size/alignment/partition/sharded`;
   - hash: `id/address`;
   - thread: `tid`;
   - multiplexer: `id`;
   - connection: `mxID/localIP/localPort/remoteIP/remotePort`;
   - queue: `type/id`;
   - hub: `type/id`;
   - link and pool: `hubID/id`;
   - DB: `dbID`; DB host/table: `dbID/id`.

   Reuse the existing URI percent codec rather than introducing a new encoder.
   Canonicalize and validate once at request ingress; store only the canonical
   filter. Empty and `*` match all, an exact string matches one key, and a
   trailing raw `*` performs a byte-prefix match. Reject interior or multiple
   wildcards and values over the configured filter bound.

3. Add worker snapshot adapters for `HeapMgr`, `HashMgr`, `ThreadMgr`, `MxMgr`,
   and `HubMgr`. Copy plain telemetry while the callback is valid, close the
   iterator, then filter/serialize/send. Preserve callback visit order within a
   synchronous traversal.
4. For an Mx query, emit the matching Mx and matching child connections. For a
   Hub query, emit the matching Hub and child links/pools. For a Queue query,
   traverse:

   - each Mx scheduler through `Mx::allQueues`;
   - every discovered Link through `Link::allQueues`;
   - each Pool's `txQueue`.

5. Deduplicate within one request by canonical type/key, because links and
   queues can be reachable through several discovery paths. The dedupe set is
   snapshot-scoped and bounded by configured/workload-derived traversal limits.
   It is discarded on completion or disconnect.
6. Process large discovery and serialization work in a configured record/byte
   quantum and repost a continuation to the worker. Do not hold manager or
   child iterators across turns. Keep sampled variable-sized data in named-heap
   reference-counted records or pooled buffers rather than lambda-captured
   arrays/strings.
7. Extend the public client test with representative current fields, exact and
   prefix filters, composite escaping, manager counts, nested discovery,
   scheduler/link/pool queues, dedupe, deterministic synchronous order, and
   multi-turn traversal fairness.

### Phase 4: Re-platform Zdb through asynchronous generic contracts

1. Derive `Zdb_::DB`, `Host`, and `AnyTable` from `Ztc::DB`, `DBHost`, and
   `DBTable`. Alias `Zdb_::CacheMode` and `HostState` to the generic vocabularies
   where ordinals and semantics match; otherwise keep ordinary lower-level
   enums and assert all mappings in `ZtcFB.hh`. Remove enum-name dependencies on
   generated telemetry headers.
2. Replace `Zdb` FlatBuffers-producing telemetry methods with plain fillers:

   - DB: full identity, leader/previous/next IDs, state/activity flags, table,
     host, peer, and connection counts, configured thread, and heartbeat,
     reconnect, and election settings;
   - host: parent DB ID, full ID, priority, state, vote, IP, and port;
   - table: parent DB ID, full name, shard/thread configuration, record count,
     cache size/loads/misses/evictions, and cache mode.

   Keep current cache statistics aggregation, but execute it through the
   table's documented telemetry shard/safe access path.

3. Implement `DB::allHosts` and `allTables` as actual-visit-count traversals.
   Add generic dispatch overrides:

   - DB dispatch schedules a continuation on the existing DB SID;
   - table dispatch schedules on its telemetry shard (initially shard 0 where
     current `Zdb` telemetry already executes) and uses the existing locked
     cache-stat access to aggregate all shards.

4. Register a DB only after successful `init`. Deregister in its destructor,
   not opportunistically in `final`; assert stopped state and zero outstanding
   telemetry continuations before destruction. Failed initialization must never
   reach `DBMgr`.
5. Implement App DB snapshots as an asynchronous fan-out/fan-in:

   - during `DBMgr` traversal, immediately call each DB's dispatch callback;
     never retain a naked DB pointer in worker state;
   - on the DB thread, fill DB/host samples, enumerate tables, and immediately
     schedule each table sample;
   - move each result back in a named-heap reference-counted sample object;
   - increment the snapshot outstanding count before scheduling and decrement
     only when the worker consumes or cancels the result;
   - send completion only after all DB and table continuations settle.

   Stop first blocks new snapshots, then waits through continuations; it never
   blocks a scheduler or I/O thread.

6. Remove `ZdbTelemetry.hh` includes, installation, superseded telemetry functions,
   and telemetry-specific schema generation only after the new paths compile.
   Keep replication and persistence schemas and behavior unchanged.
7. Test the generic hierarchy first with lightweight mock DB/host/table
   implementations that can hold continuations at controlled barriers. Verify
   counts, full long identifiers, multiple DB key isolation, out-of-order table
   completion, disconnect cancellation, and stop while continuations are
   outstanding. Then add affected `Zdb` tests for registration, sampling, and
   lifecycle.

### Phase 5: Add subscriptions, coalescing, and sustained backpressure

1. Key subscriptions by `{link generation, request group, canonical filter}`.
   Interval zero remains a one-shot and creates no durable subscription.
   `subscribe=true` with a nonzero interval creates or updates; `false` removes
   exactly the matching subscription and negatively acknowledges a missing
   target according to the frozen status table.
2. Clamp nonzero intervals below the configured minimum and report the
   effective interval in Ack. Reject intervals above the maximum. Enforce
   per-link subscription and pending non-alert request bounds before allocation.
3. Use one worker-owned due-time priority queue and one by-value scheduler timer,
   armed for the earliest due item. The timer callback only posts a due token to
   the worker. Re-arm after worker processing; disarm when empty. Cancel the
   timer on its owner thread and drain a late callback during stop.
4. Track each subscription as idle/running/pending:

   - when due and idle, start a snapshot and advance its next due time from the
     scheduled deadline;
   - when due and running, set one pending bit regardless of missed tick count;
   - on completion, start exactly one pending snapshot or return to idle;
   - an interval update recalculates the next deadline without duplicating
     state.

5. On App RAG changes, mark application subscriptions dirty and schedule their
   next worker send without crossing into Tx-owned state.
6. On disconnect, remove subscriptions, cancel snapshot contexts, remove due
   entries, and detach outbound accounting by generation. Late DB/Tx/timer
   callbacks become bounded no-ops. Never retain an iterator while erasing,
   serializing, or sending.
7. Continue to create a distinct `ZiIOBuf` queue node per link because the Tx
   queue is intrusive. Share sampled plain records or canonical serialized
   bytes only when sequence correlation permits; copy into per-link pooled
   nodes without reserializing fields.
8. Test independent subscription periods, update-in-place, clamping/rejection,
   exact unsubscribe, overrun coalescing, timer disarm, disconnect cleanup,
   application dirty updates, outbound decrement, and slow-client disconnect.
   Use `ZmBlock`/`ZmSemaphore` and controlled continuations, never sleep or poll.

### Phase 6: Add safe alert ingress, persistence, and replay

1. Add an internal alert component, for example `ZtcAlert.hh/.cc`, owned by the
   App worker so persistence mechanics do not overwhelm the public App header.
2. Return an explicitly installable `ZmRef<ZiSink>`; never install it from
   construction or `init`. The sink owns a reference-counted ingress/mailbox
   object rather than an App pointer. On the logger thread it copies timestamp,
   64-bit TID, severity, and message into named telemetry storage and enqueues
   one worker task. It does no serialization, file I/O, or network work and
   retains no `ZeLogBuf`/`ZeEventInfo` references.
3. Acceptance is successful enqueue into the scheduler path. Preserve the
   scheduler's existing ring plus unbounded dead-letter fallback as the explicit
   pending-alert exception. Mark ingress closed during stop, drain the accepted
   count on the worker, then detach App state; sink references that outlive App
   become inert.
4. On the worker, derive the real calendar date with `ZuDateTime`/Julian
   arithmetic, reset the daily 64-bit sequence to zero on date transition, and
   assign `YYYYMMDD:seqNo`. Do not perform integer subtraction on `YYYYMMDD`.
5. Define a portable append-only format:

   - a date-partitioned data file containing verified typed Alert frames;
   - a companion index of explicit little-endian `uint64_t` offsets, never
     native `size_t`;
   - data write before index write, with no per-alert `fsync`;
   - on open, validate index length, monotonic/in-range offsets, frame headers,
     body lengths/types, and FlatBuffers roots;
   - serve only the verified prefix and mark the App degraded if a corrupt tail
     is encountered; never return a corrupt record or silently trust later
     offsets.

6. Serialize each accepted alert once as a canonical sequence-zero typed frame,
   append it, and reuse the immutable bytes for live fanout where correlation
   permits. For subscriber-specific correlation, copy to a pooled per-link node
   and patch only the documented top-level request sequence scalar; do not
   reserialize alert fields.
7. On persistence failure, emit a non-recursive low-level diagnostic (direct
   stderr/platform path), set App RAG/status degraded on the worker, and
   continue bounded live handling as configured. Never route that failure
   through `ZiLOG`.
8. Implement bounded startup/daily retention:

   - calculate expired dates using calendar arithmetic;
   - scan/clean once at startup in bounded turns;
   - at rotation, target the exact expired partition rather than repeatedly
     scanning the directory;
   - close and drain data/index files during stop.

9. Implement a stable replay/live handoff:

   - validate and clamp the requested lower `date:seqNo` to retention;
   - capture the next worker alert key as a high-water mark;
   - replay verified disk records up to that mark in record/byte-bounded turns;
   - append alerts accepted during replay to a configured bounded per-replay
     tail;
   - drain the tail in identity order without duplicates, then mark the
     subscription live;
   - disconnect a slow client or tail overflow rather than retaining unbounded
     history.

10. Test daily sequence reset, month/year/leap-day retention, data/index
    validation, truncated and decreasing offsets, 64-bit offsets, replay
    clamping, stable replay/live transition, bounded-tail overflow, disconnect,
    logger dead-letter drainage, sink-outlives-App behavior, persistence failure
    recursion avoidance, and stop with accepted events in flight.

### Phase 7: Complete replacement cleanup and harden the release

1. Remove `ZvRAG` installation/use once all supported consumers use
   `Ztc::RAG`. Remove `ZdbTelemetry` and its telemetry schema artifacts after
   `Zdb` migration. Ensure active headers, generated namespaces, tests, comments,
   and documentation expose only the new `Ztc` vocabulary.
2. Complete `zv` build integration:

   - install new public contracts, App/config/message/FB headers, generated
     schema headers, and required sources;
   - add the `ztcp/src` include path and `libZtcp` dependency to `libZv`;
   - rely on the existing top-level module order where `ztcp` precedes `zv`,
     and verify no reverse dependency/cycle;
   - build and run new test binaries from `zv/test/Makefile.am`.

3. Run malformed-input and resource stress across fragmented/coalesced frames,
   invalid enum/union/type combinations, maximum-size boundaries, repeated
   invalid requests, many manager objects, long-running subscriptions, stalled
   clients, retained sink references, and repeated start/stop/final.
4. Audit all shutdown phases:

   1. reject new worker ingress and stop listening;
   2. cancel rebind/due timers on owning threads;
   3. drain Rx and disconnect links;
   4. drain/cancel request, subscription, DB continuation, replay, and accepted
      alert state;
   5. drain Tx and outbound accounting;
   6. close alert files, finalize `Ztcp`, and stop/destroy the multiplexer.

5. Run the full acceptance matrix and fix warnings/leaks before declaring the
   migration complete.

## Code References to Impacted Code

### Existing lower-level contracts and producers

- `zm/src/ZtcHeap.hh` and `zm/src/ZmHeap.cc:420`: heap fields/producer,
  `allocated`, RAG, and manager traversal.
- `zm/src/ZtcHash.hh` and `zm/src/ZmHashMgr.cc`: 64-bit hash statistics, RAG,
  registration, and actual-visit counts.
- `zm/src/ZtcThread.hh` and `zm/src/ZmThread.cc`: thread widths, CPU sampling,
  RAG, and manager lifecycle.
- `zm/src/ZtcQueue.hh` and `zm/src/ZmScheduler.cc:90`: generic queue contract and
  scheduler queue traversal.
- `zi/src/ZtcMx.hh:31` and `zi/src/ZiMultiplex.hh:554`: Mx/connection contracts
  and producer implementation.
- `zi/src/ZiMultiplex.cc:1160` and `zi/src/ZiMultiplex.cc:3028`: connection/Mx
  telemetry and manager registration.
- `zi/src/ZiMultiplex.cc:1243`: TCP listener creation; port-zero
  `getsockname`/actual-endpoint correction belongs here.
- `zi/src/ZtcHub.hh`, `zi/src/ZtcLink.hh`, `zi/src/ZtcPool.hh`, and
  `zi/src/ZtcHub.cc`: hub hierarchy, queue access, current state vocabularies,
  and manager implementation.

### Transport and service layer

- `ztcp/src/Ztcp.hh:94`: server connection lifetime and raw-back-pointer model.
- `ztcp/src/Ztcp.hh:136`: Link Rx stream, telemetry, intrusive Tx queue, send
  path, and disconnect drain.
- `ztcp/src/Ztcp.hh:599`: Hub lifecycle/manager registration and Rx/Tx
  dispatch.
- `ztcp/src/Ztcp.hh:852`: Server listen/stop/link enumeration lifecycle.
- New `zv/src/ZtcRAG` consumers, `ZtcAppTypes.hh`, `ZtcMsg.hh`,
  `ZtcFB.hh`, `ZtcApp.hh/.cc`, and internal `ZtcAlert.hh/.cc`.
- `zv/src/ZvCf.hh/.cc`: existing multiplexer configuration value-object
  patterns used by `AppCf`.
- `zv/src/Makefile.am:6`: schema generation, public headers, sources, Ztcp
  include/link integration.
- `zv/test/Makefile.am:6`: schema tests and real App/client integration tests.

### Generic DB and Zdb implementation

- In-progress `zv/src/ZtcDB.hh/.cc`: replace the `ZdbLib` dependency, FIXME
  telemetry, and superseded method names with the split generic contracts.
- New `zv/src/ZtcDBTable.hh` and `zv/src/ZtcDBHost.hh`.
- `zdb/src/Zdb.hh:590`: `AnyTable` derivation, generic key/filler, and
  telemetry-shard dispatch.
- `zdb/src/Zdb.hh:1740`: DB derivation, lifecycle registration, DB-thread
  dispatch, host/table traversal, and plain telemetry fill.
- `zdb/src/Zdb.cc:1537`: replace table FlatBuffers construction with the plain
  generic table sample.
- `zdb/src/ZdbTelemetry.hh`: remove after all fields/metadata and consumers are
  migrated.
- `zdb/src/Makefile.am:9`: retain `zdb_.fbs`, remove telemetry schema generation
  and `ZdbTelemetry.hh` installation.

### Schemas and build ownership

- `zdb/src/fbs/zdb_telemetry.fbs`,
  `zdb/src/fbs/zdb_cache_mode.fbs`, and
  `zdb/src/fbs/zdb_host_state.fbs`: migrate/correct under `zv/src/fbs`.
- New `zv/src/fbs/*`: active `Ztc.fbs` namespace, request/Ack/telemetry/
  completion/error roots, enums, and union.
- `Makefile.am`: verify module order and active installation/distribution
  references.

## Detailed Test Plan

### Unit and contract tests

- RAG boundaries for every telemetry type: unknown/zero capacity, immediately
  below/at/above 50% and 80%, engine/link/pool/DB states, resized hash, heap
  fallback, and overflow-safe large counters.
- Heap `allocated()` normal and protected-underflow cases.
- Manager uniqueness, registration/deregistration, nested child traversal,
  callback lifetime, and returned count equal to callback visits.
- Canonical key escaping and round-trip for long IDs, IPv4/IPv6, literal
  separators/wildcards/percent signs, exact/match-all/trailing-prefix matches,
  and malformed wildcard rejection.
- Canonical header vectors, accepted network byte order and rejected reversed
  byte order,
  length boundaries, unknown types, body/type mismatches, truncated frames,
  and overflow attempts.
- Every FlatBuffers telemetry union member round-trips every field; use 64-bit
  values over `UINT32_MAX`, unsigned boundary values, long DB/table names, all
  enum values, and invalid-union verification.

### Network integration tests

- Start a real reusable App on `127.0.0.1:0`, obtain the actual endpoint, and
  connect through the test-only public client.
- Repeat on `::1:0` when supported; emit a TAP skip only for platform/socket
  unavailability.
- Fragment at every header/body boundary and coalesce multiple frames in one
  write/read. Confirm no accessor use before a complete verified frame.
- Verify positive/negative Ack, original sequence, effective clamped interval,
  per-frame telemetry correlation, and explicit completion.
- Query every group and representative child type, including scheduler, link,
  and pool queues and mock DB/host/table hierarchy.
- Exercise exact, empty, `*`, prefix, and composite filters and deterministic
  order within synchronous traversals.
- Send malformed bodies, wrong root types, invalid enums/unions, oversized
  lengths/filters, repeated invalid requests, and slow-client traffic; verify
  bounded memory and the defined Ack/error/disconnect behavior.

### Concurrency and lifecycle tests

- Use `ZmBlock` or `ZmSemaphore` to hold and release timer, Rx, worker, DB,
  table, Tx, and logger continuations deterministically. Do not use sleeps or
  polling.
- Stop/final from idle and while each of the following is active: fragmented
  receive, connected clients, manager batching, DB fan-out, subscription
  snapshot, missed subscription tick, Tx backlog, replay, and accepted log
  events.
- Verify idempotent start/stop, failure rollback at every startup stage, restart
  with port zero, late callback generation rejection, and sink references that
  outlive App.
- Verify independent subscription cadence, update, clamp, overrun coalescing,
  unsubscribe, timer disarm, disconnect cleanup, and App RAG dirty delivery.
- Verify exact pending outbound-byte increments/decrements and slow-client
  disconnect without blocking worker/Tx.

### Alert tests

- Sink enqueue acceptance and forced scheduler dead-letter fallback drainage.
- Daily sequence starts at zero and resets using real calendar transitions.
- Data-before-index behavior, reopen, verified prefix recovery, corrupt/
  decreasing/out-of-range offsets, truncated frames, invalid bodies, and
  persistence failure without recursive logging.
- Retention across month, year, and leap-year boundaries.
- Replay from exact, expired, and future keys; disk/live high-water handoff;
  ordered no-duplicate tail drain; bounded-tail/slow-client disconnect; and no
  retained processed history.

### Build and analysis matrix

Run, in order:

1. `make -j8`
2. `make -C zv/test -j8 && make -C zv/test test`
3. affected `zm`, `zi`, and `zdb` test binaries/targets
4. top-level `make test`
5. clang AddressSanitizer and LeakSanitizer build/tests
6. `libtool exec valgrind --leak-check=full` on the telemetry integration test
7. supported gcc and clang builds with no new warnings

Where available, cover x64 and ARM64 Linux and a Windows msys2/mingw build,
especially socket endpoint discovery, little-endian index encoding, file
rotation, and shutdown behavior.

## Acceptance Criteria

- The active build has one telemetry API, schema, generated namespace, protocol,
  test surface, and documentation vocabulary: `Ztc`.
- `Ztc::RAG` is the sole active telemetry RAG vocabulary and every required
  derived calculation lives on the lower-level telemetry structure.
- All current `Ztc` producer fields, widths, signedness, keys, and counters are
  represented by the `Ztc.fbs` schema and `ZtcFB.hh`; all mappings have
  round-trip and enum-ordinal coverage.
- `zv` owns and installs the active telemetry schemas/generated headers.
  `zdb` retains only non-telemetry schemas and fills generic `Ztc` structures.
- `Ztc::App` is concrete and reusable, owns its multiplexer and state, uses
  plain loopback `Ztcp`, rejects non-loopback configuration, supports port zero,
  and exposes the actual IPv4/IPv6 endpoint.
- The new version-1 typed header/body protocol matches its specification and
  canonical byte vectors; all roots are bounded and verified, all complete
  requests are acknowledged, telemetry is correlated, and one-shots explicitly
  complete.
- All discovery groups and dependent objects/queues are emitted with canonical
  filtering, per-request dedupe, correct actual traversal counts, and
  asynchronous DB completion.
- Subscription keys, interval clamp/reject/update/unsubscribe behavior,
  independent scheduling, coalescing, timer drainage, and disconnect cleanup
  match the requirements.
- Rx, worker, timer, DB/table, logger, and Tx ownership boundaries are enforced
  without new wrong-shard locks, blocking scheduler/I/O loops, STL containers,
  or unbounded App history.
- Outbound state is bounded per link and slow clients are disconnected with no
  worker stall or leaked queue references.
- The explicitly installed sink safely enqueues/copies events, becomes inert
  after App lifetime, and alert persistence/replay preserves verified ordered
  `YYYYMMDD:seqNo` identities and the stable disk/live handoff.
- Repeated startup failure and stop/final leave no DB/hub registrations,
  listener, timer, subscription, snapshot, sink callback, pooled buffer, open
  file, or live dependent object.
- The complete build/test/sanitizer/valgrind matrix passes with no supported
  compiler warnings or leaks.

## Non-goals

- Backward source, ABI, generated-name, schema-name, payload, or wire
  compatibility of any kind. This is a new versioned `Ztc` contract.
- Rebuilding unrelated command, authentication, or client applications.
- TLS, certificates, authentication, authorization, non-loopback listening, or
  a WAN trust model.
- A dashboard, aggregation database, remote control plane, or durable telemetry
  history other than alert replay files.
- Perfectly atomic multi-counter snapshots or new locks/atomics solely to make
  diagnostic multi-field reads consistent.
- A per-alert `fsync` or a guarantee that logger acceptance implies durable
  persistence.
- A fixed bound/drop policy for the existing pending-alert dead-letter queue.
  This exception does not extend to processed history or replay tails.

## Options and Open Questions

1. **Protocol header review.** Confirm the proposed version-1 constants and
   16-byte layout during Phase 0. Once accepted, `ZtcMsg.hh`, documentation, and
   canonical byte-vector tests become the sole authority; implementation should
   not proceed with an implicit or platform-layout-dependent header.
   ANSWER: mimic `ZdbMsg.hh` for the header and associated processing (`saveHdr`, `loadHdr`, `verifyHdr`, `loadBody`, `msgRead`, `msgRead2`, etc.); like `Zdb`, use a flatbuffers union for the message type dispatching rather than a type in the header
2. **Protocol error after framing failure.** For a complete, bounded, structurally
   parseable frame, send a negative Ack or Error and keep the connection where
   safe. For an invalid header length/type, oversized frame, or stream
   desynchronization, the recommended behavior is an optional bounded Error
   followed by disconnect. Freeze the exact table with the header contract.
   ANSWER: agree with recommendation
3. **Alert corrupt-tail recovery.** The recommended policy is fail-closed for
   the corrupt suffix: expose only the fully verified prefix, emit a
   non-recursive diagnostic, and set App degraded. Automatic truncation should
   be a separately configured maintenance action because it mutates forensic
   data.
   ANSWER: discard any partially-written trailing alert and overwrite it
4. **Default limits and intervals.** Exact numeric defaults are not dictated by
   the requirements. Choose them from existing `ZiIOBuf`, scheduler ring, and
   expected local telemetry workloads, expose all as `AppCf` tuning, and lock
   them with configuration boundary tests. The design must not hard-code
   workload-independent object capacities.
   ANSWER: agree
5. **First unavailable thread CPU sample.** If the producer can expose validity
   without extra synchronization, add a sample-valid flag and return Off.
   Otherwise retain the current zero sample as Green and document that it is a
   best-effort snapshot; do not add a lock solely for RAG.
   ANSWER: `cpuUsage` is a `zm` concern, how it is calculated is not of concern to `Ztc`
