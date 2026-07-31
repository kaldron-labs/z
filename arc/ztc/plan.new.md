# Ztc Telemetry Service Development Plan

## Implemented baseline

The `ztcwatch.md` and `zdbtel.md` precursors have been implemented in the
current tree in this order:

```text
ztcwatch.md -> zdbtel.md -> plan.new.md
```

This plan describes the implemented tree as it exists after subsequent review.
Where either precursor still describes an earlier implementation detail, the
current-tree baseline and explicit contracts below are authoritative.

The baseline now includes:

- producer-side root and child lifecycle watcher APIs, notification ordering,
  synchronous scratch queue adapters, structured queue keys, and the
  synchronous deletion-lifetime contract;
- the sole callback heap aliases `Ztc::AllFnHeapID` and
  `Ztc::WatchFnHeapID` in `zm/src/ZtcTypes.hh`, used by every active Ztc
  traversal and lifecycle callback;
- generic `Ztc::DB`, `Ztc::DBHost`, and `Ztc::DBTable` values and interfaces,
  their colocated `ZfStruct`/`ZfbStruct` metadata, `zv/src/fbs/ztc_db.fbs`,
  and `Ztc::DBMgr`;
- a `ZtcDBMgr_` whose pointer-unique `ZmRBTree` uses `ZmNoLock` behind one
  external `ZmRWLock m_watchLock`, so write-locked `add`, `del`, `watch`, and
  `unwatch` plus read-locked `all` share one lifecycle serialization point;
- `guard(L &&)` on every locked top-level manager (`HeapMgr`, `HashMgr`,
  `ThreadMgr`, `MxMgr`, `HubMgr`, and `DBMgr`), permitting a watcher-owned
  secondary index to run under that manager's existing lock without acquiring
  a second lock; the concrete manager exposes the same inline template
  adjacent to `all`, while any lock accessor is private and friended;
- class-wide `Lock`, `Guard`, and, where used, `ReadGuard` aliases in those
  managers; HeapMgr/HashMgr retain their latency-sensitive `ZmPLock`, while
  the managers already using `ZmRWLock` retain it;
- a pointer-keyed `ZiMxMgr_` primary index; Mx telemetry identity belongs in
  App's secondary index rather than the publication registry;
- `Ztc::Pool::allQueues(QueueMgr::AllFn)` aligned with
  `Ztc::Link::allQueues`, with implementations free to supply callback-local
  scratch Queue adapters rather than retaining telemetry-only members;
- pointer-set `ZmHash<Ztc::Link *>` Hub link collections in `Ztcp` and `Ztls`,
  without redundant pointer-to-pointer KV storage;
- explicitly constructed `m_id` members on transport Hubs/Links and ordinary
  Hub lifecycle counters (`m_down`, `m_transient`, `m_up`, and total links)
  maintained by link publication/state changes; those are transport state
  sampled by telemetry, not telemetry-owned shadow counters; and
- `Zdb_::DB`, `Zdb_::Host`, and `Zdb_::AnyTable` implementing those generic
  interfaces and filling plain telemetry values, with no Zdb telemetry
  FlatBuffers serializer or schema remaining.

The current producer lifecycle conventions are part of that baseline:

- initialized root objects publish directly into unique manager trees and
  finalization/destruction deletes directly; Mx and DB do not maintain
  duplicate publication flags;
- `ZiMultiplex` publishes at the end of its constructor because it has no
  post-construction `init()`, and deletes unconditionally in its destructor;
  start/stop does not control registry membership;
- `Zdb_::DB` publishes after successful `init()` and deletes after successful
  `final()`;
- `ZmThreadMgr_` is itself a `ZmSingleton` with `final()` and has no cleanup
  proxy or separate shutdown path; each `ZmThreadContext` uses the plainly
  named `m_published` bit to make repeated `self()` publication safe, and the
  manager clears it while finalizing its index; and
- the unique manager containers, rather than parallel booleans, provide
  idempotent keyed add/delete behavior.

The accepted baseline is covered by `ZmWatchTest`, `ZtcMgrTest`, `ZtcDBTest`,
`ZdbTelTest`, `ZiMxLoopTest`, `ZtcpLoopTest`, `ZtlsBufHookTest`, and
`ZquicLoopTest`. This plan consumes those contracts to build and maintain
`Ztc::App` indices. It does not reimplement the precursor work, but those
focused tests remain regression gates for every phase.

The current tree also contains the Phase 1--6 service implementation:
`ZtcRAGMap.hh`, `ZtcApp.hh/.cc`, `ZtcMsg.hh`, `ZtcFB.hh`, the service schemas,
the test wire client, subscriptions, and alert persistence/replay. The
owner-qualified Connection key, locked manager capture, and transport Link
state normalization described below are implemented contracts, not deferred
producer corrections.

## Summary

The objective is to make `Ztc` the sole first-class telemetry service exposed by
`libZv`. Plain telemetry contracts remain in the lowest layer that owns the
sampled state (`zm`, `zi`, or `zv`). `zv/src/ZtcFB.hh` owns the non-DB
FlatBuffers metadata, `zv/src/ZtcRAGMap.hh` supplies the `zv`-layer reflected
names for the low-layer `Ztc::RAG` ordinals, and the `ZtcDB*` headers own their
DB-specific metadata as established by `zdbtel.md`. `Ztc::App` owns the
server, discovery, serialization, subscriptions, application state, and alert
history. `Zdb` implements generic database telemetry interfaces exported by
`zv`; `zv` never includes or links `Zdb`.

The service is a bounded, loopback-by-default TCP endpoint with:

- a four-byte little-endian body-length header and a verified FlatBuffers
  `Msg` root whose union selects Request, Ack, Telemetry, SnapshotComplete, or
  Error;
- positive and negative request acknowledgement with sequence correlation;
- one-shot snapshots and independently scheduled subscriptions;
- discovery through App-owned key indices, initially populated from the
  existing root/child traversals and synchronously maintained through the
  watcher contracts delivered by `ztcwatch.md`, including the completed DB
  implementation delivered by `zdbtel.md`;
- worker-owned request, snapshot, subscription, App, and alert state;
- generic DB/host/table traversal using the completed `zdbtel.md` values;
- explicit `ZiLog` sink installation, daily alert persistence, indexed replay,
  and a duplicate-free replay-to-live handoff; and
- deterministic cleanup and lifecycle drainage across timer, Rx, worker, Tx,
  and logger work.

The implementation is divided into vertical slices. The first slice produces a
real loopback App snapshot over the public wire. Each later slice expands that
same path end-to-end: process/network discovery, generic DB discovery,
subscriptions, live alerts, then persistence/replay. A slice is not handed off
until its tests pass and every applicable red or amber flag in
`GUIDELINES.md` has either been repaired or explicitly justified in the code.

The following decisions are fixed:

- Telemetry values are serialized as supplied by their existing producers.
  This work does not alter sampling, counters, synchronization, queue
  measurements, or accuracy. Lower-level enrichment is limited to the required
  `rag()` functions and Heap `allocated()`, which derives from existing
  members; the three structural Phase 2 contract corrections identified by
  the source audit do not add or improve telemetry data.
- Telemetry is a normalized read of existing component state. Link telemetry
  maps existing transport/lifecycle state directly, without separate telemetry
  state or counters.
- App resolves keyed requests and subscriptions through unlocked
  `ZmRBTreeKV` secondary indices keyed by owning forms of each producer's
  `telKey()` and protected by the corresponding producer manager's `guard()`.
  It consumes the precursor's synchronous watcher APIs to seed and maintain
  those indices without a second lock acquisition; subscriber watch lists
  remain worker-owned.
- Exactly one `Ztc::App` may be initialized at a time in a process because
  each producer manager intentionally exposes one watcher slot. Sequential
  App lifecycles are supported; a concurrent second initialization fails
  before replacing any callback.
- The protocol has no authentication, flow control, delivery acknowledgement,
  or App-owned transport backpressure/accounting. Existing `Ztcp`, `ZiTx`,
  queue/ring, socket-error, and connection-teardown behavior is unchanged.
- The header contains only the FlatBuffers body length. Message kind is not
  duplicated in the header; it comes from the root union.
- FlatBuffers are built directly into named pooled `ZiIOBuf` storage through
  `Zfb::IOBuilder`, detached/prepended/sent without body copying, verified once
  on receive, and accessed through zero-copy generated/`Zfb::Load` views while
  the buffer remains owned.
- A complete, bounded frame with an invalid request receives a negative Ack or
  Error and the connection remains usable when safe. An invalid/overflowing
  length or loss of frame synchronization is fatal; a bounded Error may be
  enqueued before disconnect only when that cannot amplify the failure.
- An incomplete final alert transaction is discarded by truncating the data
  and index files to the last complete, verified record. The next append
  overwrites that tail. Corruption inside the committed prefix is not treated
  as an incomplete tail.
- Thread RAG uses the `cpuUsage` value supplied by `ZmThread`. No sample-valid
  bit or first-sample state is added in `Ztc`; the existing first call yields
  `0.0` and therefore follows the ordinary CPU thresholds.
- Plain `Ztcp` on IPv4 loopback is the initial trust boundary. IPv6 loopback is
  supported and tested where available. Authentication and encryption are
  separate future work.
- Alert identity is `{date, seqNo}`, where date is unsigned `YYYYMMDD` and
  `seqNo` is a daily 64-bit sequence.
- Logger acceptance means accepted for posting to the App worker. Alert data is
  appended data-first and index-second without an `fsync` per record.
- Queue discovery is part of `Ztc::Mx`, and a Queue query also includes
  link/pool queues.
- A subscription overrun coalesces missed ticks and starts one next snapshot
  after the current snapshot completes.

## Architecture Documentation

### Components and module boundaries

```text
zm: Ztc::RAG, Heap, Hash, Thread, Queue
              |
zi: Ztc::Mx/Connection, Hub/Link/Pool, ZiMultiplex, ZiLog, ZiFile
              |
zv: generic DB contracts + ZtcFB + protocol + Ztc::App
              ^
zdb: implements Ztc::DB/DBHost/DBTable

client <-> Ztcp::Server<App>/Link <-> Rx <-> worker <-> Tx
                                      ^        |
                                      |        +-> DB/host/table traversal
                                  ZiLog sink   +-> alert files/index
```

Implemented baseline public files consumed by this plan:

- `zm/src/ZtcTypes.hh`
- `zv/src/ZtcDBTable.hh`
- `zv/src/ZtcDBHost.hh`
- `zv/src/ZtcDB.hh`
- `zv/src/fbs/ztc_db.fbs`, whose `zv/src/Makefile.am` rule generates and
  installs `ztc_db_fbs.h`

New public files in this plan:

- `zv/src/ZtcRAGMap.hh`
- `zv/src/ZtcFB.hh`
- `zv/src/ZtcMsg.hh`
- `zv/src/ZtcAppTypes.hh`
- `zv/src/ZtcApp.hh`
- generated headers for schemas under `zv/src/fbs`

New implementation files:

- `zv/src/ZtcApp.cc`
- internal `zv/src/ZtcAlert.hh` and `zv/src/ZtcAlert.cc` if keeping the alert
  state out of `ZtcApp.cc` materially improves ownership clarity

New schema files:

- `zv/src/fbs/ztc_types.fbs` - groups, statuses, states, flags, and other
  shared enums, including the already-established RAG ordinal mapping
- `zv/src/fbs/ztc_telemetry.fbs` - non-DB telemetry tables and
  `TelemetryBody`, including the DB tables already generated from
  `ztc_db.fbs`
- `zv/src/fbs/ztc_request.fbs` - Request, Ack, SnapshotComplete, and Error
- `zv/src/fbs/ztc_msg.fbs` - the outer `Body`, `Msg`, and root declaration

New test files:

- `zv/test/ZtcSchemaTest.cc`
- `zv/test/ZtcAppTest.cc`
- `zv/test/ZtcAlertTest.cc`
- `zv/test/ZtcTestClient.hh` for the test-only public-wire client

The lower layers expose plain data, traversal, and the precursor's lifecycle
callbacks only. They do not include FlatBuffers or the service layer.
`ZtcFB.hh` supplies `ZfStruct`, `ZfbStruct`, enum matching, key, delta/series,
formatting, and read-only derived-field metadata for non-DB telemetry.
The `ZtcDB*` headers delivered by `zdbtel.md` intentionally retain their
colocated DB-specific metadata; `ZtcFB.hh` consumes and does not redeclare it.

### Threads, state ownership, and posting

`App` owns one `ZiMultiplex` constructed in-place with `ZuUnion<void,
ZiMultiplex>`. The default scheduler has four distinct isolated worker SIDs:
timer role 1, Rx 2, Tx 3, and App worker 4. Configuration may name or number
other SIDs, but initialization resolves them through `ZvMxCf`/`ZvMxParams` and
rejects zero, out-of-range, non-isolated, or aliased roles.

Ownership is:

- Rx: TCP stream framing, size validation, one contiguous pooled body buffer,
  FlatBuffers verification, and posting a moved buffer plus fixed link
  identity/generation to the worker.
- App worker: verified request-buffer ownership, bounded persistent request
  values, filters, pending snapshots, subscriptions, stack `Zfb::IOBuilder`
  use over pooled buffers, frame selection/order, application telemetry, alert
  sequence/tail/files, and all persistent App containers.
- Timer role: timer callbacks only; callbacks post fixed subscription/group
  identity to the worker and never traverse worker containers.
- Tx: physical queueing and socket write through the existing `Ztcp`/`ZiTx`
  behavior. It never inspects App request or subscription state.
- ZiLog thread: copies event metadata and bounded message bytes, passes a moved
  telemetry-owned object through a lifetime gate, and does no file/network or
  FlatBuffers work.
- DB/host/table producers: retain their existing sampling, ownership, and
  approximate-read behavior. App adds no dispatcher, lock, atomic, counter, or
  resampling path. Raw interface pointers are retained only in App's unlocked
  secondary indices and are sampled only inside `DBMgr::guard()`, which blocks
  the synchronous deletion watcher on the manager's existing lock.

`ZmScheduler::run` is always asynchronous. `invoke` is synchronous only when
already on the destination SID and asynchronous otherwise. Public App methods
that can be called from arbitrary threads are thin dispatchers; they snapshot
only fixed data or move an owned buffer and post to a trailing-underscore
worker method. On-shard variants use `ZiAssert` and access only their owner's
state. No caller waits on a scheduler or I/O thread.

### Storage, allocation, and work bounds

All persistent and per-request containers use the corresponding Z container,
never an STL container. App's object lookup indices are `ZmRBTreeKV`; other
lookup state uses `ZmHash`/`ZmLHash` or `ZmRBTree` as appropriate. Deadline
ordering uses an intrusive ordered node/queue; the live alert tail is a bounded
Z ring/list. A subscription node owns the intrusions needed for both lookup and
deadline ordering instead of allocating separate container nodes. Every
allocating container, callback, variable string/byte array, sample object, and
FlatBuffers/I/O buffer has a named `ZmHeapID`, `ZmHeap`, or `ZmVHeap` path.
The allocation-ID tables below are part of the interface design, not examples:
implementation must use those exact IDs and add a similarly specific ID for
any additional allocating callback, buffer, node, or App container before it
is introduced while coding. `ZmFn` captures use the built-in capture/move path
and consolidate to one handle or fixed metadata where practical; lambdas name
each capture rather than using `[&]` or `[=]`. Any deferred `ZiLOG` lambda
captures only the bounded values it needs, by copy.

I/O data remains in pooled `ZiIOBuf` objects and moves by reference. Bounded
text/key state uses named `ZtString`/`ZtArray`; hot temporary key/filter work
uses `ZtLocalArray` with heap fallback. Decoding/canonicalization writes
directly into its final bounded destination and does not create temporary
contiguous copies. A complete receive frame already contained in one queued
`ZiIOBuf` is detached and moved without copying. A fragmented frame is gathered
once, directly into its final pooled frame buffer, without `ZtLocalArray`
scratch or a second copy. The one private alert-frame copy required for each
recipient's correlation value is the only other intentional body-byte copy;
these paths are called out and measured in the relevant phases.

Worker turns cap serialization, due-subscription dispatch, replay, validation,
and fanout work, then post a continuation for the remainder. A keyed telemetry
snapshot enters the corresponding manager `guard()` only while sizing and
filling one plain telemetry scratch array from its unlocked App index. Filter
parsing precedes the manager guard; maintained-key matching and telemetry
capture occur under it; serialization, sending, and subscription work happen
after it returns. Member layout groups state by ownership and alignment,
orders fields to limit padding, and isolates the shared ingress gate from
worker-only state to avoid false sharing.

### Protocol and receive processing

`ZtcMsg.hh` follows the proven header/body processing shape used by
`zdb/src/ZdbMsg.hh`, but is a self-contained `Ztc` protocol:

```cpp
#pragma pack(push, 4)
struct Hdr {
  ZuLittleEndian<uint32_t> length; // FlatBuffers body bytes
};
#pragma pack(pop)
static_assert(sizeof(Hdr) == 4);
```

The schema shape is:

```fbs
namespace Ztc.fbs;

union Body { Request, Ack, Telemetry, SnapshotComplete, Error }
union TelemetryBody {
  Heap, Hash, Thread, Mx, Connection, Queue, Hub, Link, Pool,
  DBTelemetry, DBHostTelemetry, DBTableTelemetry, App, Alert
}

table Telemetry {
  seqNo:ulong;            // originating request/subscription
  value:TelemetryBody;
}

table Msg {
  body:Body;
}

root_type Msg;
```

FlatBuffers use follows the same direct-buffer pattern as Zdb:

- Each frame uses a stack `Zfb::IOBuilder` backed by a named pooled
  `ZiIOBuf`; the builder is not heap-allocated and never builds into an
  intermediate `std::vector`, `ZtArray`, or temporary FlatBuffer that is later
  copied into a Tx buffer.
- Allocators follow Zdb's `Zi::IOBufAlloc`/typed-buffer posture: separate named
  pools and workload-measured builtin sizes for small control frames versus
  telemetry/alert frames, with a hard allocator `MaxSize` and the stricter
  runtime `AppCf::maxFrame` bound. Builder growth remains a cold fallback
  rather than routine serialization.
- Frame allocation initializes an aligned, non-zero `ZiIOBuf::skip` before
  attaching the buffer to `Zfb::IOBuilder`. The builder subtracts the existing
  skip from the capacity exposed to FlatBuffers. `ZiIOBuf::realloc` preserves
  that skip during builder growth, and detach adds FlatBuffers' unused prefix
  to it, so growth cannot consume header headroom.
- `ZfbStruct::save` and generated `Create*` functions write tables directly
  into that builder. Strings are created once from existing `ZuCSpan` values.
  Primitive vectors, where present, use `Zfb::Save::pvector_`/
  `pvectorIter` or `CreateUninitializedVector` and are filled in place.
  Offset vectors use the existing `Zfb::Save`/`ZmAlloc` scratch technique;
  nested FlatBuffers, if ever required by a schema member, use
  `Zfb::Save::nest` instead of serialize-then-copy.
- After `Finish()`, `fbb.buf()` detaches the same intrusive buffer by
  `ZmRef`. `saveHdr` prepends into the reserved prefix and that buffer is moved
  unchanged to `Ztcp::Link::send` or `ZiFile`; there is no serialization
  staging allocation.
- Receive and replay retain the verified `ZmRef<ZiIOBuf>` for the complete
  accessor lifetime. `flatbuffers::Verifier` runs once on untrusted frame
  bytes, then code uses `Zfb::GetRoot`, generated union accessors, and
  trusted-buffer `_` helpers without reverifying the same immutable bytes.
  `Zfb::Load::str` and `Zfb::Load::bytes` expose `ZuCSpan`/`ZuBSpan` views
  directly into the buffer. An owning copy is made only for a value that must
  be canonicalized, mutated, or retained after that buffer is released.

Every outbound/persistence frame allocation provisions at least `sizeof(Hdr)`
bytes of `ZiIOBuf::skip` headroom before FlatBuffers body construction and
preserves that reserve if the builder grows. Following `fbb.Finish(...)`,
`saveHdr` detaches the builder buffer, requires
`buf->skip >= sizeof(Hdr)`, and uses `prepend(sizeof(Hdr))` only on that
guaranteed fast path. The call adjusts `skip`/`length` and placement-constructs
`Hdr`; it never grows the buffer, shifts the FlatBuffer, or copies body bytes.
Missing headroom is an allocation/builder-contract failure, not permission to
use `ZiIOBuf::prepend`'s move/grow fallback.

`loadHdr` first requires four bytes, reads through the packed little-endian
member, and validates in widened unsigned arithmetic before any `int` or
allocator conversion. `AppCf::maxFrame` is defined as total header plus body,
so a declared body must be no larger than `maxFrame - sizeof(Hdr)` and
`INT_MAX - sizeof(Hdr)`. `verifyHdr` requires the declared complete frame.
`loadBody` runs `flatbuffers::Verifier` against `fbs::Msg`, verifies its union,
and dispatches from `body_type()`. The verified buffer is immutable after Rx
and moves to the worker. The worker reads scalars and zero-copy string/byte
spans from that buffer, copying only canonical filter/subscription state or
other values that must outlive it. No generated-table pointer or span survives
release of the owning buffer.

Extend `ZiRxStream` with a narrowly scoped complete-frame extraction operation
modeled on Zdb's `ZiRx` framing:

1. Scan the four-byte header across queued spans without allocating; reject an
   invalid/oversized body before any frame allocation.
2. Return incomplete without consuming when the complete declared frame has
   not arrived.
3. When the complete header+body occupies one queued `ZiIOBuf`, detach that
   intrusive node and return it by `ZmRef` without copying. If the same buffer
   also contains following-frame bytes, copy only that trailing remainder into
   the successor Rx buffer and truncate the detached frame.
4. When a frame spans buffers, allocate one named pooled destination for the
   complete frame and copy each queued span directly into it exactly once.
   Do not gather through `ZtLocalArray` or another temporary.
5. Remove exactly the extracted bytes/nodes from the stream, preserve any
   following bytes, verify the root/union once on Rx, and move the same buffer
   to the worker.
6. Let `Ztcp::Link::rcvd_` continue processing coalesced frames; return
   incomplete only when no whole next frame is available.

The extraction API is generic to `ZiRxStream`/its queue node and has no
telemetry or FlatBuffers dependency. Tests prove the single-buffer fast path
preserves the buffer/data address and allocation identity, while fragmented
input performs exactly one direct gather into the final buffer.

Once a bounded frame boundary is known, an invalid root, union, enum, filter,
interval, or request operation does not poison following frames. A malformed
request that exposes a sequence receives a negative Ack; a structurally
invalid body receives Error without sequence if needed. Length overflow,
configured oversize, or a frame that cannot be synchronized disconnects.

### Request, response, key, and filter contracts

Every Request carries `seqNo`, group, filter, interval in milliseconds,
`subscribe`, and alert replay lower-bound fields where applicable. Ack echoes
`seqNo`, status, and effective interval. Telemetry and SnapshotComplete echo
the originating sequence. Error has a bounded code/message and optional
sequence.

Groups are Heap, Hash, Thread, Mx, Queue, Hub, DB, App, and Alert. Mx includes
its connections; Hub includes its links and pools; DB includes its hosts and
tables. Queue is the complete queue view, including multiplexer scheduler
queues and link/pool queues.

Each server-visible type has one structured `telKey()`. App's secondary index
owns the value form of that exact key; reference components become value
components in the index node, but no alternate identity is synthesized.
Queues have no flattened ID type. Their key is
`ZuTuple<const ZuID &, const ZuID &, QueueType::T>`, generically
`{ownerID, id, type}`, and `QueueTelemetry` exposes those same three fields.
Link queues use `{hubID, linkID, Rx/Tx}`, pool queues use
`{hubID, poolID, Tx}`, and scheduler queues use
`{schedulerID, threadID, Thread}`. The type component discriminates link Rx
and Tx queues without direction suffixes or concatenated IDs.

Owner IDs are never duplicated in contained producer objects. Every child key
obtains its owner component from the existing containing-object relationship:
`telKey()` returns the owner's `ZuID` by reference when the component is part
of the producer key, and manager/App callback context supplies it only where
the existing hierarchy API already provides that context. A value copy is
made only when App materializes the owning form of a secondary-index key, a
plain telemetry snapshot, or a serialized protocol field. This applies
uniformly to Link, Pool, Queue, Connection, DBHost, DBTable, and scheduler
Thread ownership, not only to scheduler queues. For example,
`ZmScheduler::Thread` retains its owner pointer and its own thread ID,
`ZiMultiplex` uses inherited `ZmScheduler::id()`, and neither retains a
duplicate scheduler ID.

Structured object keys contain every component of the implemented `telKey()`.
The current Connection callback slots are global in `ZiMxMgr_`, not scoped to
one `ZiMultiplex`, and endpoints alone are not globally unique. Before App
builds its Connection index, extend `Connection::Key`/`telKey()` to include
the containing Mx ID by reference as its leading component. `ZiConnection`
obtains that reference from `m_mx->id()`; it does not cache another ID. The
resulting protocol/index components are:

| object | key components |
| --- | --- |
| Heap | `id/size/alignment/partition/sharded` |
| Hash | `id/address` |
| Thread | `tid` |
| Mx | `id` |
| Connection | `mxID/remoteIP/remotePort/localIP/localPort` |
| Queue | `ownerID/id/type` |
| Hub | `linkType/id` |
| Link | `hubID/id` |
| Pool | `hubID/id` |
| DB | `id` |
| DBHost | `dbID/id` |
| DBTable | `dbID/id` |

`ZuPercent::Codec<ZtcKeyPolicy>` performs uppercase percent encoding within
filter components. The policy escapes `%`, `:`, `*`, control bytes, and bytes
outside the permitted ASCII filter set; decode rejects incomplete/non-hex
escapes and non-canonical spellings. A Request filter is only a small,
domain-specific textual pattern over `telKey()`; it is not a predicate
language over telemetry fields and does not introduce alternate lookup keys.

Each request group has one fixed minimal spelling over the useful components
of that group's `telKey()`:

| group | filter |
| --- | --- |
| Heap | `id` |
| Hash | `id` |
| Thread | `tid` |
| Mx | `id` |
| Queue | `type:id` or `type:ownerID:id` when owner qualification is wanted |
| Hub | `linkType:id` |
| DB | `id` |
| App | `id` |

The Queue matcher's `type`, optional `ownerID`, and `id` values all come from
its `{ownerID, id, type}` `telKey()`; the filter's presentation order is the
small domain convention, not a generic tuple-query syntax. Each colon-separated
component is matched independently:

- an empty (zero-length) component or `*` means all values;
- an enum component otherwise resolves directly with that enum's `lookup()` and
  matches the resulting value;
- a string component otherwise names one exact value, except that one terminal
  `*` after a non-empty prefix means prefix match;
- a numeric component otherwise contains one exact canonical numeric value.

Thus `:*`, `Rx:`, `*:orders*`, and `Tx:hubA:orders` are representative forms.
Empty input means all components. Canonicalization converts every empty/all
component to the single `*` spelling and expands a two-component Queue filter
with an owner component of `*`, so semantically identical subscriptions and
unsubscribes have the same key. Wrong component counts, unknown enum names,
non-canonical numeric values, and any other wildcard placement are invalid.
Matching reads the maintained index key/producer `telKey()` only; it never
calls `telemetry()` to decide membership.

Component splitting and string matching use the new `ZuSpan` `find`, `match`,
and `exact` operations as appropriate; enum matching uses the enum's existing
`lookup()` directly. Do not add a regex, temporary token array, generic
expression evaluator, or parallel hand-written substring matcher. The parser
scans the bounded filter in place, and percent-decoding writes each persistent
canonical component directly into its final named storage.

Filter compilation is separate from hot-path matching. Request acceptance
performs the delimiter `find`, percent decoding, wildcard classification, enum
`lookup()`, numeric parsing, validation, and canonicalization exactly once. It
produces a small type-specific compiled filter owned inline by the pending
request or subscription node:

- each component has a preclassified `All`, `Exact`, or `Prefix` mode;
- enum and numeric exact components retain their already-parsed values;
- string exact/prefix components are spans into the filter's one stable
  canonical backing string, with the terminal `*` excluded from a prefix span;
- the whole filter records the traversal shape needed by its App index.

The backing type is
`ZtString<ZtStringHeapID<"Ztc.App.Filter">>`, retaining normal builtin storage
for the common short filter and named heap fallback for an unusually long
bounded filter. Do not add a fixed custom capacity without measured workload
evidence.

The compiled object contains no type erasure, callback, regex state, token
container, or separately allocated component string. Its type-specific
matcher is template/inlined code over the concrete `telKey()` type. A
subscription reuses the same compiled filter for every interval poll; it does
not split, decode, canonicalize, call enum `lookup()`, parse a number, or
allocate again. Canonical backing storage reaches its final address and
capacity before component spans are installed; the owning pending/subscription
node is then intrusive and never moved. An update that would change the filter
is a different subscription key rather than an in-place backing-string
replacement.

The common cases have dedicated paths selected once before entering the index
loop:

1. all components `All`: traverse without invoking a per-key matcher;
2. fully exact: construct the canonical index key once and use direct tree
   lookup;
3. an exact/prefix-constrained leading index-key sequence: use the narrowest
   ordered range, then apply only residual component checks;
4. other wildcard shapes: scan keys, checking cheap parsed enum/numeric
   components before string components.

Exact string checks use `ZuSpan::exact`; prefix checks use `ZuSpan::match`.
There is no mode switch, delimiter search, wildcard search, enum lookup, or
numeric conversion inside the candidate loop. Type-specific matchers order
their remaining comparisons for the normal domain: reject cheap fixed-width
components first, then exact strings, then prefixes. Use `ZuLikely`/
`ZuUnlikely` only where measurements or the overwhelmingly dominant
all/exact paths justify the hint; do not scatter speculative branch hints.

For aggregate groups, filtering selects the containing objects by their own
`telKey()`: a selected Mx emits its current Connections, a selected Hub emits
its current Links and Pools, and a selected DB emits its current DBHosts and
DBTables. The filter is not reinterpreted against subsidiary telemetry fields.
Alert replay position remains in the Request's typed alert lower-bound fields,
not in this object-key filter.

The maximum filter length is enforced on encoded input before canonicalization
and again on canonical output. Decode and canonicalization write in place or
directly into one named bounded destination rather than through intermediate
strings. Each group matcher is a small specialization over its `telKey()` type;
there is no generic query evaluator. The canonical domain-specific pattern,
rather than the caller's spelling, is used as the subscription key, so the
same selection identifies a re-subscribe or unsubscribe deterministically.

### App secondary indices over the watcher precursor

`ztcwatch.md` supplies all root and child watcher APIs, notification ordering,
synchronous queue traversal/publication, manager guards, and synchronous
delete-lifetime behavior used here. `zdbtel.md` completes their allocation
contract by migrating every
traversal/lifecycle callback to `Ztc::AllFnHeapID`/`Ztc::WatchFnHeapID` from
`ZtcTypes.hh`. Those producer-side contracts are prerequisites, not
implementation work in this plan.

`App::CtrlFn` and `App::RagFn` use the exact
`"Ztc.App.CtrlFn"`/`"Ztc.App.RagFn"` IDs shown in the lifecycle API. Any
additional App callback alias required during implementation must be entered
in this plan's allocation inventory with a type-specific ID before use; a
default `ZmFn` heap is not acceptable.

After constructing its index state and before accepting requests, App installs
the root watcher and then calls `all` on each of `HeapMgr`, `HashMgr`,
`ThreadMgr`, `MxMgr`, `HubMgr`, and `DBMgr`. `QueueMgr` is an abstract
contained-object traversal and has no global watcher or `all`.

The two Connection callback slots reached through `Mx::watch` are stored once
in `ZiMxMgr_`; they are not per-Mx callback sets. Fixed scheduler queues have
no dead callback slots. App installs that one Connection watcher through its
in-place service Mx before `MxMgr::all`. For each Mx observed through the
manager, App then calls the directly declared
`Mx::allCxns` and `Mx::allQueues`. A live Connection callback constructs its
globally qualified index key directly from the owner-qualified
`Connection::telKey()`; it does not capture a particular Mx or call
`telemetry()` for identity. Scheduler queues are fixed children of
`ZmScheduler`: they are seeded through `allQueues`, do not emit synthetic
add/delete callbacks from `ZiMultiplex`, and are removed from App's Queue
source index when the containing Mx is deleted. `ZiMultiplex` does not iterate
connections or scheduler queues from its destructor.

`DBMgr` likewise has a root two-callback watcher overload and a four-callback
host/table watcher overload. App installs both before `DBMgr::all`, then seeds
each observed DB through `DB::allDBHosts` and `DB::allDBTables`.

`HubMgr` has the implemented pair of watcher overloads: the two-callback
`watch(AddFn, DelFn)` for root Hubs and the six-callback
`watch(AddLinkFn, DelLinkFn, AddPoolFn, DelPoolFn, AddQueueFn, DelQueueFn)`
for children. App installs both overloads before `HubMgr::all`; the single
`HubMgr::unwatch()` clears and drains all eight slots. Existing children are
then seeded through each Hub's `allLinks` and `allPools`, each Link's
`allQueues`, and each Pool's `allQueues`. Live link publication is link, Rx
queue, Tx queue. While holding `HubMgr`'s write lock, link deletion calls
`allQueues` once and invokes the delete callback synchronously for each queue,
then invokes the Link delete callback. Link and Pool `allQueues` implementations
may construct `Ztc::Queue` adapters as stack scratch temporaries; callback
pointers are valid only for the synchronous call and are never retained. The
two Link queue keys share `{hubID, linkID}` and differ by QueueType. Pool
publication is Pool followed by every queue yielded from `Pool::allQueues`;
deletion yields those queues before the Pool delete callback.

`HubMgr::add(Hub *)` and `HubMgr::del(Hub *)` manage only root Hub registry
membership. An owning Hub reports child lifecycle through the explicitly named
protected `Hub::linkAdded_`/`linkDeleted_` and
`Hub::poolAdded_`/`poolDeleted_` forwarders. They alone can reach HubMgr's
private, friended notification functions, which drive the six child callbacks
but do not place Links or Pools in a HubMgr collection.

`ZiMultiplex` publishes only its own Mx, Connection, and fixed scheduler Queue
hierarchy. It has no knowledge of Hub, Link, Pool, or the fact that a concrete
transport connection may be associated with any of them. It has no
Link-oriented publication bridge, declaration, include, or callback.
Concrete `Ztcp`/`Ztls` Hub implementations own their Link collection,
publication, traversal, and synchronous HubMgr notifications entirely within
their transport hierarchy; they do not recover Links by asking generic
`ZiConnection` objects.

Connection removal remains on the I/O lifecycle path and must stay minimal.
`ZiMultiplex::cxnDel` performs the platform poller deletion and uses the node
returned by `m_cxns->del(socket)` to recover the removed connection; it does
not perform a preceding `findVal()` or a second hash deletion. Connection
watcher notification is conditional on that returned connection.

`DBMgr::add(DB *)` and `DBMgr::del(DB *)` likewise manage only root DB
registry membership. An owning DB reports child lifecycle through protected
`DB::hostAdded_`/`hostDeleted_` and `DB::tableAdded_`/`tableDeleted_`
forwarders into DBMgr's private, friended notification functions. Host and
table callbacks are manager-owned; there is no per-DB watcher.

`Zdb_` does not directly adopt normalized telemetry enum namespaces throughout
its implementation. Its local `HostState` namespace imports
`Ztc::DBHostState`, and its local `CacheMode` namespace imports
`Ztc::DBCacheMode`. Zdb code names `HostState` and `CacheMode`, allowing its
internal state vocabularies to diverge later while conversion to normalized
telemetry remains localized.

This watch-before-all order closes the creation gap. Insert is idempotent for
the same owning `telKey()` and pointer, so an object reported both by the watch
callback and the initial traversal appears once; the same key with a different
live pointer is an invariant failure. App never calls a producer `all`,
`watch`, or `unwatch` from inside that producer manager's `guard()`.

App owns one `ZmRBTreeKV<Key, Object *>` secondary index for each
persistent producer type: Heap, Hash, Thread, Mx, Connection, Hub, Link, Pool,
DB, DBHost, and DBTable. `Key` is the owning value form of that interface's
declared `telKey()` type: reference components such as `const ZuID &` become
`ZuID`, while scalar and enum components retain their exact types. A fully
exact component pattern uses direct lookup; an exact or prefix-constrained
leading component sequence uses the narrowest ordered range supported by that
typed key; other wildcard patterns briefly scan the index. No path
reconstructs keys from telemetry or adds an ad-hoc request-time index.

Every secondary tree is unique and explicitly `ZmNoLock`. Its containing
producer manager serializes primary-index mutation, watcher callbacks, and
`guard()` with one coarse lock. `ThreadMgr`, `MxMgr`, `HubMgr`, and `DBMgr`
already invoke `all` callbacks while their manager lock is held. The current
`HeapMgr` and `HashMgr` traversals intentionally retain one current object by
reference and invoke the callback between lock acquisitions. Preserve that
latency-sensitive traversal: the retained reference keeps the callback object
alive, and a concurrent final release cannot complete its manager deletion
callback until the traversal releases that reference, so watch-then-`all`
seeding cannot leave a stale secondary-index pointer.

After that prerequisite, App watcher and initial `all` callbacks update the
corresponding unlocked secondary tree directly under the same manager lock.
Queries enter that manager's `guard()`, traverse the tree, and capture
telemetry before returning. Consequently there is no App-side lock
acquisition, nested manager/index lock pair, or duplicate lock state.

The lock domains are fixed:

| manager guard | App indices protected |
| --- | --- |
| `HeapMgr::guard` | Heap |
| `HashMgr::guard` | Hash |
| `ThreadMgr::guard` | Thread |
| `MxMgr::guard` | Mx, Connection, Mx Queue sources |
| `HubMgr::guard` | Hub, Link, Pool, Link/Pool Queue sources |
| `DBMgr::guard` | DB, DBHost, DBTable |

Queue adapters are not persistent producer objects and App never stores a
`Queue *`. Queue discovery instead maintains separate manager-domain source
indices:

- Mx queue key to containing `Mx *`;
- Link queue key to containing `Link *`; and
- Pool queue key to containing `Pool *`.

Each source index is updated from synchronous discovery while its manager lock
is held. Mx fixed queues are indexed while `Mx::allQueues` supplies their
containing Mx; the Hub callback resolves the already-published Link or Pool by
the queue's
`{ownerID, id, type}` key while still in the same HubMgr critical section.
Sampling an entry calls that owner's `allQueues` and copies the matching
scratch queue's telemetry before the callback returns. The three source
indices remain separate because MxMgr and HubMgr are independent lock domains;
a Queue request captures each domain in turn and combines only the resulting
plain telemetry arrays.

The concrete declaration shape is:

```cpp
using HeapKey =
  ZuTuple<ZuID, uint32_t, uint8_t, uint16_t, uint8_t>;
using HeapIdx =
  ZmRBTreeKV<HeapKey, Heap *,
    ZmRBTreeUnique<true,
      ZmRBTreeLock<ZmNoLock,
        ZmRBTreeHeapID<"Ztc.App.HeapIdx">>>>;
```

Every other typed index uses the owning form of its own `telKey()`, the same
unique/`ZmNoLock` policy, and its value pointer and exact heap ID from this
table:

| App index | value | heap ID |
| --- | --- | --- |
| Heap | `Heap *` | empty (allocator-registry recursion exception) |
| Hash | `Hash *` | `"Ztc.App.HashIdx"` |
| Thread | `Thread *` | `"Ztc.App.ThreadIdx"` |
| Mx | `Mx *` | `"Ztc.App.MxIdx"` |
| Connection | `Connection *` | `"Ztc.App.CxnIdx"` |
| Mx Queue source | `Mx *` | `"Ztc.App.MxQueueIdx"` |
| Link Queue source | `Link *` | `"Ztc.App.LinkQueueIdx"` |
| Pool Queue source | `Pool *` | `"Ztc.App.PoolQueueIdx"` |
| Hub | `Hub *` | `"Ztc.App.HubIdx"` |
| Link | `Link *` | `"Ztc.App.LinkIdx"` |
| Pool | `Pool *` | `"Ztc.App.PoolIdx"` |
| DB | `DB *` | `"Ztc.App.DBIdx"` |
| DBHost | `DBHost *` | `"Ztc.App.DBHostIdx"` |
| DBTable | `DBTable *` | `"Ztc.App.DBTableIdx"` |

App telemetry is one worker-owned value, so an App-to-App pointer index would
serve no lookup. Alerts are immutable tail/file records rather than live
telemetry producer objects, so they likewise do not enter the object-pointer
indices.

App's non-object lookup/ordering indices are also fixed now rather than left
to default allocator names:

| App-owned index | purpose | exact container heap ID |
| --- | --- | --- |
| client state | link pointer/generation to worker-owned request state | `"Ztc.App.ClientIdx"` |
| pending snapshot | `{link generation, seqNo}` lookup | `"Ztc.App.PendingIdx"` |
| subscription | canonical per-link subscription-key lookup | `"Ztc.App.SubIdx"` |
| subscription due | ordered deadline membership using the same intrusive node | `"Ztc.App.SubDueIdx"` |

The corresponding fixed-size worker objects use these exact `ZmHeap` IDs:

| worker object | exact `ZmHeap` ID |
| --- | --- |
| App worker state | `"Ztc.App.State"` |
| per-link client state | `"Ztc.App.Client"` |
| pending captured snapshot | `"Ztc.App.Pending"` |
| alert ingress gate | `"Ztc.App.Ingress"` |
| returned alert sink | `"Ztc.App.AlertSink"` |

Variable-size captured alert events use `ZmVHeap<"Ztc.App.AlertEvent">`; the
message bytes are constructed in the same allocation immediately after the
event object.

The alert live tail and replay handoff are bounded sequential ring/list
structures, not additional lookup indices; the daily alert index is a file,
not a heap container. If implementation introduces another App lookup or
ordering index, it must first be added to this table with its exact container
type and heap ID.

The snapshot arrays also have exact allocation identity:

| telemetry snapshot type | exact `ZtArrayHeapID` |
| --- | --- |
| Heap | `"Ztc.App.HeapSamples"` |
| Hash | `"Ztc.App.HashSamples"` |
| Thread | `"Ztc.App.ThreadSamples"` |
| Mx | `"Ztc.App.MxSamples"` |
| Connection | `"Ztc.App.CxnSamples"` |
| Queue | `"Ztc.App.QueueSamples"` |
| Hub | `"Ztc.App.HubSamples"` |
| Link | `"Ztc.App.LinkSamples"` |
| Pool | `"Ztc.App.PoolSamples"` |
| DB | `"Ztc.App.DBSamples"` |
| DBHost | `"Ztc.App.DBHostSamples"` |
| DBTable | `"Ztc.App.DBTableSamples"` |

The pooled frame roles remain distinct:

| `ZiIOBuf` role | exact allocator heap ID |
| --- | --- |
| fragmented receive gather frame | `"Ztc.App.RxFrame"` |
| Ack, Error, and SnapshotComplete control frame | `"Ztc.App.CtrlFrame"` |
| ordinary telemetry frame | `"Ztc.App.TelFrame"` |
| canonical alert persistence/live frame | `"Ztc.App.AlertFrame"` |

Each role has its own typed `Zi::IOBufAlloc`; roles are not collapsed into one
generic App buffer pool. Small-control versus telemetry/alert builtin sizes
remain workload-tuned parameters of those aliases, while `AppCf::maxFrame`
remains the hard runtime bound. A complete frame detached from one transport
Rx buffer deliberately retains that existing transport allocator identity;
it is not copied merely to acquire `"Ztc.App.RxFrame"`.

The Heap index alone uses the framework's established empty-heap-ID path,
matching HeapMgr's own internal indices. A named `ZmHeap` for that tree would
recursively enter HeapMgr when a Heap lifecycle callback first allocates a
node on an arbitrary producer thread while the manager lock is already held.
All other indices retain the exact named IDs in the table.

App instantiates every named index-node heap and every cache class of the
bounded snapshot-array VHeaps during `init`, before installing any producer
watcher. This is required in particular for `HeapMgr`: neither an index
insertion callback nor a Heap snapshot may lazily register a new ZmHeap while
the Heap manager lock is held. The warmup is initialization-only, retains no
dummy element, and is covered by a re-entrancy/deadlock regression test.

All subscriber watch-list creation, update, removal, and reconciliation occurs
only on the App worker. A producer watcher never touches those lists and does
not post a parallel lifecycle event: it performs only the synchronous
secondary-index mutation under the manager lock. Each interval snapshot
resolves its compiled filter against the then-current index, so producer
membership changes require no watch-list reconciliation.

### Snapshot capture from the indices

Top-level managers expose concrete `CaptureFn` signatures using
`ZuSpan<const Telemetry>`; lambdas convert directly to `ZmFn`, so there are no
template forwarding adapters. The manager locks its authoritative primary
container, obtains `count_()` while membership is stable, allocates one
uninitialized scratch array of that exact capacity, iterates the primary
container, and placement-constructs each selected telemetry value directly in
that storage before invoking `object->telemetry()`. It then unlocks and calls
the `CaptureFn` with the constructed span. The captures remain alive for the
duration of that call; serialization and sending therefore happen without the
manager lock.

Managers in `zi` and above use `ZtLocalArray`, with its normal stack-first,
heap-fallback behavior. `HeapMgr`, `HashMgr`, and `ThreadMgr` cannot depend
upward on `zt`; their implementation files use `ZmAlloc`, track the number of
values placement-constructed in that raw storage, and pass the callback a
`ZuSpan<const Telemetry>` over exactly those values. The synchronous callback
bridge is pointer-bound and non-owning, so capture adds no callback heap
allocation. The manager explicitly destroys the constructed span before
`ZmAlloc` releases its storage.

The unfiltered scalar-manager shape is deliberately:

```cpp
Manager::capture({}, [&result](ZuSpan<const Telemetry> captures) {
  result = consume(captures);
});
```

Managers without a matcher take only `CaptureFn`.

Managers used by scalar groups also provide the corresponding matcher
overload. It evaluates the compiled pattern only against `object->telKey()`
while locked and calls `telemetry()` only for matches:

```cpp
Manager::capture(
  [&filter](Object *object) { return filter.match(object->telKey()); },
  [&result](ZuSpan<const Telemetry> captures) { result = consume(captures); });
```

The guarded `count_()` is scratch capacity; the constructed span length is the
captured/emitted count. Holding the manager lock during `telemetry` makes each
producer pointer stable. The lock is released before `CaptureFn`, so the
critical section contains no canonicalization, FlatBuffers work, sending,
subscription mutation, or entry into another manager domain. No telemetry
field is read to decide whether an object matched.

Aggregate groups retain their App secondary indices because one selected Mx,
Hub, or DB root also selects its current subsidiary objects. App enters that
root manager's `guard()`, sizes each aggregate scratch array from the stable
root/child index counts, selects roots by maintained `telKey()`, and captures
the root plus its current children before unlocking. Exact and prefix requests
use the secondary index's exact/range path; match-all uses whole-index
iteration.

Queue capture uses the same shape with the source indices described above.
Inside `MxMgr::guard()` or `HubMgr::guard()`, App invokes the selected owner's
`allQueues`; the callback compares only `queue->telKey()` and immediately
uses placement new for the telemetry value in `samples.push()` storage, then calls
`queue->telemetry()` directly into it. No `Queue *`, generated key span, or
adapter reference escapes the callback or manager guard.

A snapshot context contains only link generation, request sequence,
group/filter, failure/completion state, and its captured telemetry batches.
There is no per-snapshot dedupe set: the unique persistent-object indices and
three unique Queue source indices are the sole membership authorities.
Completion is emitted exactly once after every captured batch has been
serialized or cancelled during shutdown.

### Telemetry service self-observation

The service is not hidden from discovery. Once initialized, its owned
`ZiMultiplex` appears through `MxMgr`, its `Ztcp::Server<App>` appears through
`HubMgr`, and every accepted telemetry client appears as the normal
Connection/Link objects and link queues already exposed by those components.
Consequently:

- an Mx request includes the service Mx and its accepted client connections;
- a Hub request includes the service TCP hub and its active telemetry links;
- a Queue request includes the service scheduler queues and each active
  telemetry link's Rx/Tx queues, each represented once by its manager-domain
  Queue source index;
- Heap, Hash, and Thread requests naturally include facilities registered by
  the service process; and
- the App request reports the service's separate application identity/state.

The listening socket is not fabricated as an accepted Connection. It remains
represented by the server Hub and the App's bound endpoint. No special
“internal” flag, exclusion rule, counter adjustment, or special-case index is
added for service-owned objects; normal filters apply to them exactly as to
other telemetry.

Self-observation is causal rather than recursive. The producer is sampled
before the worker serializes and hands the corresponding Telemetry frame to
`Ztcp::Link::send`. Therefore the Tx call/byte counters and Tx queue length in
that sample cannot include the frame carrying the sample itself, nor the later
SnapshotComplete frame. They may include an earlier Ack or earlier telemetry
frames if Tx has already processed those frames by the time a later object is
sampled. Likewise, the request has been consumed from the link Rx stream before
worker sampling, although the connection's existing receive counters may
already include its socket read.

A snapshot is not a fixed-point or transaction across all producers. Objects
are sampled at their normal traversal times, and the act of
serializing/posting responses changes values that can be observed by a
subsequent request or subscription tick. The App reports the producer values
it reads and does not predict, add, or subtract the effects of the request
currently being served.

### Outbound transport behavior

The protocol has no application-level flow control, delivery acknowledgement,
or backpressure mechanism. The worker builds a frame and hands it to the
existing `Ztcp::Link::send`; `Ztcp`, `ZiTx`, their queues/rings, and the socket
remain the sole owners of transport buffering and queue-length accounting.
If those facilities reject or discard data, or a socket error occurs, the
existing connection teardown applies.

`Ztc::App` does not maintain duplicate Tx counters, does not inspect Tx queues
from the worker, and does not change
`Ztcp::Link::send`, `ZiTx` completion behavior, or transport queue semantics.
Link and queue telemetry are served with the accuracy already supplied by
their originating components.

### App lifecycle and endpoint handling

`App` exposes:

```cpp
class App {
public:
  using CtrlFn =
    ZmFn<void(bool), ZmFnHeapID<"Ztc.App.CtrlFn">>;
  using RagFn =
    ZmFn<void(RAG::T), ZmFnHeapID<"Ztc.App.RagFn">>;

  bool init(const AppCf &);
  void start(CtrlFn);
  bool start();
  void stop(CtrlFn);
  bool stop();
  void final();

  ZiIP localIP() const;
  uint16_t localPort() const; // actual bound endpoint after listen
  void rag(RAG::T);
  void rag(RagFn) const;      // completion runs from the App worker
  ZmRef<ZiSink> alertSink() const;
};
```

The concrete exception/result spelling follows adjacent `Zv`/`Zi` lifecycle
APIs, but failure always names the invalid field or failing stage. `init`
constructs configuration and in-place Mx state but does not call
`Ztcp::Server::init` until the Mx is running, because the Hub validates a
running Mx and distinct Rx/Tx SIDs.

The callback lifecycle overloads are the primary state-machine API. Synchronous
wrappers use the established `ZmEngine`/`ZmBlock` pattern only for external
threads and assert/reject use from an owned scheduler or I/O SID. Start
completion occurs after the listen callback has published the actual endpoint;
stop completion occurs only after the full drainage chain.

`localIP()` returns the configured loopback address. `localPort()` returns the
actual bound port after the listen callback and otherwise the configured port.
Neither accessor reads live Rx-owned listener state; the bound port is
published before start completion and cleared during stop.

`ZiMultiplex::listen_` already knows the requested IP. Only when the requested
port is zero, the common listen path calls `getsockname` after `bind` (using
`socklen_t` on Unix and `int` on Windows), synchronizes `ZiSockAddr`, and stores
the assigned port in listener info. Nonzero-port listens perform no redundant
socket-name query. App records configured and bound ports separately.
`Ztcp::Server` listens with the configured endpoint, the listening callback
publishes the bound endpoint, and stopListening uses that exact bound endpoint.
Failed/restarted listens clear stale bound state.

Startup is:

1. validate configuration and construct App index state and the in-place Mx,
   without opening request ingress;
2. establish the worker, install root and child watchers, then run the
   root/child `all` traversals to seed every index; the insertion rule absorbs
   watch-versus-seed overlap and also catches an object registered during
   `init`;
3. start the Mx; subsequent lifecycle changes synchronously maintain the
   already-seeded indices;
4. initialize/start the TCP server and alert files;
5. listen and open request ingress; publish running only from the listen
   callback, after indices are seeded.

A non-transient listen failure, or any failure when rebind is disabled,
completes start with failure and rolls back. A transient failure with rebind
enabled keeps only the asynchronous start callback pending while the Rx-owned
timer retries; a concurrent stop cancels that timer and resolves the pending
start as failed before continuing shutdown. This extends the existing
`Ztcp::Server` rebind pattern without leaving `ZmEngine` stuck in Starting.

Shutdown is explicitly multi-phase:

1. close public request and alert-ingress gates and stop listening;
2. disarm the value-owned rebind and subscription `ZmScheduler::Timer` objects
   on their callback SIDs and wait for their posted deletion continuations;
3. drain Rx and disconnect links using the server's existing link shutdown;
4. on the worker cancel subscriptions, finish/fail snapshots, drain accepted
   alert work, and close alert files; no App index iterator remains active;
5. through the still-live in-place service Mx, clear the single global Mx
   child watcher; then call each root manager's `unwatch` once. `HubMgr` and
   `DBMgr` each clear both their root and subsidiary callback sets, and every
   unwatch drains callback execution;
6. clear every App secondary index without dereferencing its raw values;
7. let the existing server/link shutdown drain or discard Tx state;
8. stop/finalize the server and Mx, destroy in-place state, and leave any
   outstanding external sink reference inert.

`start`, `stop`, and `final` are idempotent. No phase blocks a scheduler/I/O
thread. Raw App back-pointers in links are valid because links are drained
before App destruction. `final` releases the process-wide App watcher claim
only after every manager callback is cleared and drained.

### Configuration and tunable defaults

`AppCf` has `ZfStruct` metadata and contains `ZvMxCf`/`ZvMxParams`, timer/Rx/Tx/
worker role names, listen IP/port, accept concurrency, transient rebind
interval in seconds, subscription interval bounds in milliseconds, frame and
filter bounds, pending-request and subscription bounds, App identity/version/
role, alert path/prefix/retention, alert message/tail/replay bounds, and buffer
sizes.

Defaults are usable without exposing the host:

- listen `127.0.0.1:0`, 8 concurrent accepts, transient rebind disabled;
- four isolated roles at SIDs 1/2/3/4 as described above;
- subscription interval 100 ms minimum and 3,600,000 ms maximum;
- 1 MiB total frame, 1 KiB filter, 64 pending snapshots, and 64 subscriptions
  per link;
- 64 KiB accepted alert message, 4096-entry in-memory live tail, 1024 alerts
  buffered per replay handoff, and seven retained calendar days.

These are named configuration defaults, not hard-coded storage capacities.
Each default has a maintenance comment stating its operational/footprint
rationale; integral compile-time constants use the repository enum idiom.
Tests cover boundary values. Deployments can tune them without rebuilding.
Initialization rejects wildcard and non-loopback listen addresses in this
release rather than letting a tuning change silently widen the trust boundary.
It also validates relational bounds before any subtraction/allocation:
`maxFrame` must accommodate the header and smallest verified body, must fit the
`ZiIOBuf`/`int` parser limits, and every message/filter/replay limit must fit
its enclosing frame or container domain.
The scheduler's existing overflow queue means accepted logger posts are not
given a new drop policy in this work; the alert live tail and each replay
handoff remain explicitly bounded.

### RAG and lower-level data contracts

`zdbtel.md` has already replaced `zm/src/ZtcRAG.hh` with
`zm/src/ZtcTypes.hh`. The old header no longer exists. `ZtcTypes.hh` owns
`Ztc::RAG`, `Ztc::AllFnHeapID`, and `Ztc::WatchFnHeapID`; all `Ztc*`
traversal and lifecycle callbacks already use those namespaced aliases.
The implemented DB `ZfbStruct` declarations still use `ZvRAG::Map` only as
their reflected name map. Phase 1 replaces that remaining metadata dependency
with `zv/src/ZtcRAGMap.hh`, which reopens `Ztc::RAG` and supplies its
`ZtEnumNames`/`Map` without declaring another enum or changing the low-layer
type and ordinals. The DB metadata then uses `Enum<RAG::Map>`, as does the new
non-DB metadata.

The RAG portion remains:

```cpp
namespace Ztc {
namespace RAG {
  using T = int8_t;
  enum { Off = 0, Red, Amber, Green };
}
}
```

Derived methods live on the plain telemetry types:

| Type | Derived behavior |
| --- | --- |
| Heap | `allocated = cacheAllocs + heapAllocs - frees`; Off without cache target, Red above target, Amber after heap fallback, otherwise Green. |
| Hash | Red when `resized` is non-zero; otherwise Amber when `effLoadFactor >= loadFactor * 0.8`; otherwise Green. There is no separate Off case. |
| Thread | Red at `cpuUsage >= .80`, Amber at `>= .50`, otherwise Green. |
| Mx, Hub | Running Green; Starting/StartPending Amber; Stopped/Stopping/StopPending Red; unknown Off. |
| Connection | Red when `rxBufLen * 10 >= (rxBufSize << 3)` or `txBufLen * 10 >= (txBufSize << 3)`; otherwise Amber when `(rxBufLen << 1) >= rxBufSize` or `(txBufLen << 1) >= txBufSize`; otherwise Green. There is no zero-capacity Off special case. |
| Queue | Off when `size == 0`; otherwise Red when `count * 10 >= (size << 3)`, Amber when `(count << 1) >= size`, otherwise Green. |
| Link | Up Green; connecting/reconnect/disconnect pending states Amber; Down/Failed Red; Disabled/Deleted Off. |
| Pool | Up Green; Down/Failed Red; unknown Off. |
| DB table | With `total = cacheLoads + cacheMisses`: Off when `total == 0`; Red when `cacheMisses * 10 > (total << 3)`; Amber when `(cacheMisses << 1) > total`; otherwise Green. |
| DB/host | Instantiated Off; Initialized/Electing/Inactive/Stopping Amber; Active Green; invalid Off. |

These are direct, side-effect-free functions over the values already present
in each telemetry structure. `HeapTelemetry::allocated()` is simply
`cacheAllocs + heapAllocs - frees`. The work does not validate, reconcile,
resample, synchronize, or otherwise improve the originating measurements.
Multi-counter snapshots retain their existing accuracy; no new counters,
validity state, atomics, or locks are added. Preserve these formulas and state
tables exactly; do not add new zero/unknown normalization or defensive
semantics.

Other externally named enum vocabularies use the established `ZtEnumNS`/
`ZtEnumNames` pattern where runtime names are required; small internal
vocabularies use namespace/struct enums. Do not introduce `enum class`, static
lookup arrays, or integer-to-enum mapping branches. State-to-RAG functions use
`switch` or `ZuSwitch` and return the existing ordinals directly.

Current producer fields are authoritative. Schema fields include heap
`crossFrees`, derived `allocated` and RAG; 64-bit hash `count`; connection
`rxCalls`, `rxBytes`, `txCalls`, and `txBytes`; unsigned thread SID and 64-bit
TID; full-width DB/table identifiers and shard count; cache loads/misses/
evictions; and every current link, pool, queue, Mx, Hub, App, and alert field.
`AppTelemetry` includes configured ID/version/role, process start time,
lifecycle/degraded status, and current RAG.

### Generic database interfaces

`zdbtel.md` has already delivered the complete
`ZtcDBTable.hh`/`ZtcDBHost.hh`/`ZtcDB.hh`/`ZtcDB.cc` hierarchy, including
plain values, full-width keys, DB-specific `ZfbStruct` metadata, schema
vocabulary, `DBMgr::all`, its root and child `watch` overloads and `unwatch`,
and the
`Zdb_::DB`/`Host`/`AnyTable` implementations. Those definitions are
prerequisites, not implementation work in this plan.

The established names remain `DBMgr::all`, `DB::allDBHosts`, and
`DB::allDBTables`. App installs both DBMgr watcher overloads before
`DBMgr::all`, seeds each DB with `allDBHosts`/`allDBTables`, and retains the
resulting raw pointers only in unlocked secondary indices accessed through
`DBMgr::guard()` under the synchronous deletion contract. It serializes the
already-colocated DB metadata without
redeclaring it in `ZtcFB.hh` and does not add a dispatcher, stable-read
mechanism, telemetry synchronization, compatibility API, or second manager.

### Subscription scheduling

A subscription key is `{link generation, group, canonical telKey filter}`.
Interval zero with `subscribe=true` is a one-shot query. A positive interval
with `subscribe=true` creates a watch and immediately runs its first snapshot;
each later interval performs a fresh filtered snapshot from the current App
indices, so producer additions and deletions are reflected without storing
object pointers in the watch. Re-subscribe updates the interval and sequence
correlation in-place and starts a new immediate snapshot rather than creating
a duplicate watch. `subscribe=false` removes the watch having the same link
generation, group, and canonical `telKey()` filter; a miss is acknowledged as
a no-op and never creates a temporary snapshot. Intervals below the minimum
are clamped and the effective value is returned; intervals above the maximum
are rejected.

Each subscription is one named-heap intrusive node indexed by key and linked
into its group's ordered due set. The worker stores due time and state (`idle`,
`running`, `dirty`) in that node. One value-owned scheduler timer represents
the single logical earliest subscription deadline across all groups. On
expiry, the timer role posts only fixed generation/deadline metadata to the
worker. The worker detaches a bounded due batch, starts snapshots, and re-arms
from current state. If a subscription becomes due while running, it is marked
dirty; completion schedules one next run from completion time. No missed-tick
queue or per-tick allocation accumulates.

Application RAG mutation occurs on the worker, marks App subscriptions dirty,
and follows the same scheduler. All watch creation, canonical-filter lookup,
update, removal, due-list mutation, and disconnect cleanup occurs on the App
worker. Disconnect removes every watch and pending context for its link
generation after iterators/locks have ended. Removing the last subscription
disarms the single timer with the full three-phase timer deletion pattern:
cancel/prevent re-arm, post to its callback SID to drain late work, then
complete teardown.

### Alert ingress, persistence, and replay

`alertSink()` returns a `ZmRef<ZiSink>` but never installs it globally. The sink
and App each own a ref-counted `Ingress`, never a sink-to-App reference.
`Ingress` has a small gate, an open/closed target pointer, and an
accepted/in-flight count:

1. The ZiLog callback copies timestamp, 64-bit TID, severity, and bounded
   message bytes into one named telemetry allocation. It captures no borrowed
   pointer or reference in deferred logging/posting work.
2. Under the gate it rejects if closed; otherwise it increments accepted work
   and posts the moved event to the worker.
3. The worker assigns `{date, seqNo}`, serializes and persists/fans out, then
   decrements accepted work.
4. Stop closes and nulls the target under the gate, then uses a worker barrier
   to drain all work accepted before closure.

Thus a sink reference can outlive App and becomes inert without a ref cycle or
use-after-free. The callback never retains `ZeLogBuf`/`ZeEventInfo`, performs
I/O, serializes, sends, or calls App through an unguarded raw pointer.
Short-lived event objects carry only a raw `Ingress` back-pointer; App retains
its ingress reference until the accepted count drains, avoiding per-event
reference churn while preserving lifetime.

Daily storage uses:

- `<prefix>.<YYYYMMDD>.data`: consecutive complete
  header+`Msg(Telemetry(Alert))` frames;
- `<prefix>.<YYYYMMDD>.index`: little-endian 64-bit data offsets, where index
  position is the daily sequence number.

On startup/rotation, validation walks index entries in order, requires
contiguous/in-range offsets, loads the bounded frame at each offset, verifies
the FlatBuffer and Alert identity, and computes the end of the last committed
record. Startup validation finishes before request ingress opens. If a live
rotation must validate a pre-existing partition, it runs in bounded worker
turns and posts its continuation so other worker traffic is not starved.
“Incomplete final alert transaction recovery” means:

- data bytes after the last complete indexed record are truncated;
- partial index bytes or an index entry without a complete verified data
  record are truncated;
- both files are sought to those verified ends; and
- the next data-first/index-second append overwrites the discarded tail.

A bad offset, invalid frame, or identity mismatch inside the fully committed
prefix is interior corruption, not an incomplete tail. That partition is
marked unusable, App status/RAG degrades, replay reports an explicit error, and
new appends do not rewrite around it.

Each accepted alert is serialized once on the worker as a canonical frame.
The inner Alert keeps its daily `seqNo`; the outer Telemetry carries the
request/subscription correlation. Because outer correlation differs per
subscription, the canonical builder forces that outer scalar into the
FlatBuffer even when its value is the default. Fanout copies the canonical
bytes into each required intrusive Tx buffer and uses the generated scalar
mutator to set only outer correlation before the buffer leaves the worker;
tests require the mutator to succeed and the declared frame length to remain
unchanged. The canonical frame itself remains unchanged and is used for
persistence. Private Tx ownership and distinct correlation values make this
one copy per recipient intentional; there is no per-field allocation or
intermediate serialization.

The in-memory live tail is a bounded ring of canonical alert refs/keys.
Persistence, not that ring, is the replay source. Replay:

1. captures the current worker high-water `{date, seqNo}`;
2. reads validated indexed records strictly after the requested key and no
   later than that high-water, in bounded worker batches;
3. stores alerts accepted after the high-water in a bounded per-replay pending
   list;
4. drains that list in key order with duplicate checks; and
5. changes the subscription to live delivery in the same worker turn.

Exceeding the per-replay pending bound fails that replay and closes its
connection rather than growing replay state. Retention uses `ZuDateTime`
Julian-day arithmetic across month/year boundaries and runs only during
startup or rotation, never as a periodic background scan; live rotation work
is batched. File failures use a direct non-recursive diagnostic path, never
`ZiLOG`, and set worker-owned degraded App state.

## Detailed Design and Implementation Plan

### Phase 1: Public protocol and one-shot App vertical slice

Deliver one real request over loopback:

```text
client Request(App) -> Ack -> Telemetry(App) -> SnapshotComplete
```

1. Establish a clean baseline without modifying unrelated working-tree
   changes. Record the existing targeted test results before touching
   interfaces.
2. Consume the precursor's installed `ZtcTypes.hh`; add `ZtcRAGMap.hh` as the
   reflected-name map over the existing `Ztc::RAG` ordinals and migrate the
   DB metadata from `ZvRAG::Map` to `RAG::Map`. Add App plain types, only the
   schema members needed for Request, Ack, App Telemetry, SnapshotComplete,
   and Error, plus the outer `Msg` union. Add `ZtcFB.hh` metadata for App and
   compile-time RAG enum matching.
3. Add `ZtcMsg.hh` with the four-byte length header and bounded
   save/load/verify/body helpers. Do not copy unchecked integer behavior:
   validate declared length in widened arithmetic against total max frame and
   `INT_MAX` before returning a parser length or allocating. Extend
   `Zfb::IOBuilder` to honor an attached buffer's initial `skip`, and make
   `ZiIOBuf::realloc` preserve that skip through growth; build directly into
   named pooled frame buffers.
   `saveHdr` may only adjust buffer metadata and construct the header in place.
4. Add `AppCf` metadata and deterministic defaults. Resolve the four isolated
   roles, reject invalid aliases, and reject wildcard/non-loopback endpoints
   before constructing persistent state.
5. For a zero requested port only, add the cross-platform `getsockname` update
   to `ZiMultiplex::listen_` and test that listener info and stopListening use
   the assigned port without re-querying the already-known local IP.
6. Add the generic `ZiRxStream` complete-frame extraction operation with a
   detached-buffer fast path and direct single-gather fragmented path.
7. Add `App`, its in-place Mx, `Ztcp::Server<App>`, service link, bounded frame
   extraction/verification, worker dispatch, App telemetry state, per-link
   generation, and pending request state. Use named pooled allocators for Rx,
   Ack, Telemetry, SnapshotComplete, and Error frames; serialize each outbound
   frame through a stack `Zfb::IOBuilder` directly into its final buffer and
   pass it to the existing `Ztcp::Link::send` unchanged. Skeleton
   subscription/DB/alert fields may exist only where they simplify later
   ownership; they must be inert and allocation-free.
8. Integrate schemas, generated headers, sources, `libZtcp` dependency, and
   tests into `zv` Automake files without adding a dependency cycle.
9. Add a test-only wire client using the public protocol. Cover fragmented and
   coalesced requests, port-zero discovery, sequence correlation, negative Ack,
   completion, oversize, malformed body, reconnection after a recoverable
   error, and stop/final with an active client.

Handoff acceptance:

- A clean App request completes over IPv4 loopback; IPv6 loopback passes or
  emits a TAP skip.
- `saveHdr` leaves the FlatBuffers body at the same address with identical
  bytes; only the data pointer moves backward into reserved headroom and frame
  length grows by `sizeof(Hdr)`.
- Serialization allocates one final pooled frame buffer, writes into it
  directly, and sends that same allocation. Single-buffer receive detaches and
  moves the original allocation; fragmented receive performs one direct gather
  into the final pooled buffer.
- Verified request strings/bytes remain `Zfb::Load` spans while the Rx buffer
  is retained; only persistent/canonical state is copied.
- Frame allocation is impossible before bounded length validation.
- Top-level `make -j8` rebuilds all lower dependencies before
  `make -C zv/test -j8 && make -C zv/test test`; affected Zfb, Zi, Ztcp, and
  Zdb tests then pass.
- Audit all Phase 1 changes against every `GUIDELINES.md` red/amber flag:
  headers/direct includes, tabs/names, anonymous namespace prohibition,
  specific lambda captures (no `[&]`/`[=]`), STL/casts, heap IDs, hidden
  copies, fixed capacities,
  wrong-shard access, long loops, timer/I/O teardown, and test sleeps/polling.
  Repair every finding before Phase 2.

### Phase 2: Process, I/O, and queue telemetry end-to-end

Expand the working one-shot path through all non-DB, non-alert managers. The
accepted `ztcwatch.md` implementation already provides root/child watchers,
queue exposure through synchronous scratch adapters, notification order,
manager guards, and callback heap IDs.

1. Add only the required `rag()` methods and Heap `allocated()` method to the
   existing Heap, Hash, Thread, Mx, Connection, Queue, Hub, Link, and Pool
   structures. Do not change their counters, sampling, synchronization, or
   producer implementations, except to remove the remaining
   `Ztcp`/`Ztls` telemetry-only Link state shadow and normalize the reported
   state from existing transport/lifecycle state. Do not add replacement
   telemetry state. Traverse queues through the directly declared
   `Ztc::Mx::allQueues` surface on `Ztc::Mx *` without a concrete cast.
2. Add the corresponding unique, `ZmNoLock` App `ZmRBTreeKV` indices with
   their exact heap IDs. Install watcher callbacks, then seed with the existing
   `all` methods before opening request ingress. Preserve HeapMgr/HashMgr's
   ref-retaining traversal between brief lock acquisitions; do not hold their
   latency-sensitive manager locks across callbacks. Warm every App
   index-node/snapshot allocator that can be touched by manager callbacks.
   Maintain each index only from
   its manager's callback/`all` scope and query it only through that manager's
   `guard()`. Make insertion idempotent for the same key/pointer and reject a
   live key collision. Queue indices retain containing Mx/Link/Pool sources,
   never callback-local `Queue *` adapters. First extend
   `Connection::Key`/`telKey()` with its owner Mx ID by reference; the global
   `ZiMxMgr_` child watcher must neither infer identity from telemetry nor
   retain a duplicate owner ID.
3. Expand the schema union and `ZtcFB.hh` metadata one telemetry family at a
   time. Add every current field with exact width/signedness, key, update,
   series/delta, format, and read-only derived metadata. Add enum ordinal checks
   and distinctive-value generated-accessor tests at each expansion.
4. Implement `ZtcKeyPolicy` with `ZuPercent::Codec`, canonical filter generation,
   the small per-`telKey()` component-pattern parsers, exact/component-wildcard/
   textual-prefix matching, direct enum `lookup()`, and configured length
   enforcement. Use `ZuSpan` `find`/`match`/`exact` for component parsing and
   string matching. Compile once into the inline type-specific
   all/exact/prefix representation and retain it with the pending request or
   subscription. Do not inspect telemetry fields while filtering.
5. Serve each query from its App index. Inside the corresponding manager
   `guard()`, read `count_()`, size the exactly named snapshot `ZtArray`,
   dispatch once to the compiled filter's direct/all/range/scan path, match
   only the maintained index key, construct each selected telemetry value with
   placement new directly in `push()` storage, and call the persistent object's
   `telemetry` directly into it. Return from the guard before serializing,
   posting, or sending. For queues, use the selected source
   object's synchronous `allQueues` callback and copy telemetry before that
   callback returns. Do not retain `Queue *` or create a per-request dedupe
   container.
6. Add representative real and lightweight mock producers in tests. Validate
   App watch-then-seed integration, canonical composite keys, filter semantics,
   all RAG boundaries, first thread sample behavior, 64-bit values above
   `UINT32_MAX`, completion after capture/serialization finishes, and discovery
   of the service's own Mx, Hub, requesting Connection/Link, and queues. The
   producer watcher ordering and queue-lifetime tests remain in the precursor
   suite.

Handoff acceptance:

- Every required non-DB/non-alert union member round-trips all existing fields
  and all RAG boundary tests pass.
- Heap/Hash/Thread/Mx/Queue/Hub requests work through the public client with
  exact components, per-component `*`, textual component prefixes, escaped
  components, and invalid `telKey()` filters. Tests prove Thread selection
  uses only TID; Queue accepts the minimal `type:id` and owner-qualified
  `type:ownerID:id` forms using only its `{ownerID, id, type}` `telKey()`; Hub
  accepts minimal `linkType:id` patterns including `*:*`; and no telemetry
  field becomes an alternate filter key. Mx and Hub tests prove a selected
  root emits all of its current subsidiary telemetry and no subsidiary of an
  unselected root.
- Every App callback and index allocation reports the exact heap ID in this
  plan's allocation inventory; the precursor callback-ID suite remains green.
- A delete callback cannot begin and permit destruction while an App sample
  still holds the corresponding manager guard.
- Service-owned objects are returned through the same managers and filters as
  other objects, without counter adjustment or self-exclusion.
- Top-level `make -j8` completes before relevant `zm`, `zi`, `ztcp`, and `zv`
  tests, and all pass.
- Repeat the complete `GUIDELINES.md` red/amber audit for touched modules,
  including approximate-counter posture, pointer callback lifetimes,
  batching/fairness, allocation sites, container heaps, and direct includes.
  Repair all findings before Phase 3.

### Phase 3: Generic database snapshot vertical slice

Add DB, host, and table output through the existing approximate telemetry and
collection interfaces. The generic values, DB metadata/schema, manager,
watcher APIs, Zdb lifecycle wiring, and producer population are already
accepted `zdbtel.md` work.

1. Add the existing DB, DBHost, and DBTable generated types to the service
   telemetry union without moving or redeclaring their `ZfbStruct` metadata in
   `ZtcFB.hh`. Verify their enum ordinals and union discriminants alongside the
   rest of the service schema.
2. Add and maintain App's DB, DBHost, and DBTable indices using the exact App
   index heap IDs here and the accepted watcher callback IDs. Install both
   DBMgr watcher overloads before `DBMgr::all`, seed each DB's hosts/tables,
   and retain pointers only under the synchronous index deletion contract.
3. Capture each DB query into its named plain-value `ZtArray` inside
   `DBMgr::guard()`, traversing the unlocked secondary index only while the
   producer lock is held. Return from the guard before serialization or
   posting, and send SnapshotComplete only after every captured batch is
   serialized.
4. Test public-wire DB snapshots and App integration with lightweight generic
   mocks plus a focused real-Zdb path. Complete producer population,
   full-width keys, root/child lifecycle, watcher overlap, synchronous delete
   lifetime, host/table churn, schema accessors, and callback allocation
   identities remain covered by the precursor suites and are not duplicated.

Handoff acceptance:

- `zv` continues to build and link without any Zdb header/library dependency.
- Both precursor suites remain green; App discovery, stop drainage, public
  union selection, and full-width key round-trip tests pass.
- Public DB requests do not complete early and never access a pointer after
  its callback lifetime.
- Relevant `zdb` and `zv` tests pass with no dependency flowing from `zv` to
  Zdb and no serialization in Zdb; top-level `make -j8` precedes those tests.
- Run the full `GUIDELINES.md` audit, especially approximate-counter posture,
  locks/iterators, raw/ref pointer flags, names, heap IDs, and hidden
  allocations. Repair before Phase 4.

### Phase 4: Independent subscriptions

Turn the one-shot engine into a bounded subscription service without changing
the snapshot collectors.

1. Add the canonical per-link subscription index, effective interval handling,
   resubscribe update, unsubscribe, group due queues, running/dirty coalescing,
   and timer-role-to-worker posts. Use one intrusive subscription object for
   lookup and due ordering, with named heaps for every fallback allocation.
   All subscription/watch-list creation, lookup, update, reconciliation, and
   deletion runs only on the App worker.
2. Reuse the Phase 2/3 snapshot entry point for the immediate subscription
   snapshot and every interval run. Give each run the current subscription
   sequence, resolve the canonical `telKey()` filter against the then-current
   App indices, and suppress overlap. Schedule a dirty rerun from completion
   time, not from every missed deadline.
3. Enforce pending request and subscription limits before durable allocation.
4. Resolve subscription keys and capture telemetry through the maintained App
   secondary indices. Producer lifecycle watchers perform no worker post and
   never mutate subscriber watch lists. Do not hold an App index or
   subscription iterator while serializing or sending, and do not add
   protocol-level flow control or duplicate Tx queue accounting.
5. Make worker-owned App RAG updates dirty App subscriptions. Add worker getters
   through asynchronous callback/invoke vocabulary rather than cross-thread
   reads.
6. Implement timer disarm and late-callback generation checks. Disconnect,
   stop, and final remove all watches, due entries, pending snapshots, and
   App-owned link state deterministically.
7. Test multiple intervals against a controlled clock/timer hook where
   practical; use `ZmBlock`/`ZmSemaphore`, never sleeps or polling. Cover
   immediate first snapshot, clamping, maximum rejection, canonical-equivalent
   re-subscribe/update and unsubscribe, unsubscribe miss, objects added or
   removed between polls, overrun coalescing, dirty App update, link reuse, and
   shutdown races.

Handoff acceptance:

- Each watch emits no faster than its own effective interval; a faster watch
  does not accelerate a slower one.
- A successful subscribe emits one immediate filtered snapshot, subsequent
  polls re-evaluate current membership, and an unsubscribe of the same
  canonical `telKey()` filter prevents any later poll.
- Overrun creates at most one next run, and disconnect removes all App-owned
  subscription and pending-request state.
- Timer removal follows the three-phase contract and no callback reaches
  destroyed worker state.
- Top-level `make -j8`, subscription/integration tests, and affected lower
  transport tests pass in that order.
- Run and repair the complete `GUIDELINES.md` audit, emphasizing timer
  teardown, batching, captures, mutable ownership, allocation per tick/
  subscriber, intrusive nodes, and deterministic container removal.

### Phase 5: Safe live alert ingestion and fanout

Deliver an explicitly installed sink through to live Alert subscribers before
adding historical replay.

1. Add Alert plain type/schema/metadata with unsigned date, daily 64-bit
   sequence, timestamp, 64-bit TID, severity, message, composite key, and App
   degradation fields.
2. Implement the ref-counted ingress gate and `ZiSink` subtype. Bound/copy the
   message once, post a moved owned event, drain accepted work at stop, and
   prove a sink reference remains inert after App destruction.
3. On the worker assign identity, build one forced-default canonical frame,
   add it to the bounded live tail, and fan it out through the subscription and
   existing send path. Patch only request correlation in each private
   worker-owned copy before Tx handoff.
4. Add non-recursive diagnostics and worker App degradation for serialization,
   allocation, or future persistence failures.
5. Test concurrent sink callbacks, oversized message policy, no subscribers,
   multiple subscribers with different correlation, disconnect during fanout,
   stop during ingress, and post-final sink use with barriers and only the
   minimum events needed to establish each behavior.

Handoff acceptance:

- Every accepted event is either processed before stop completes or reported
  through the explicit worker failure path; none accesses App after final.
- One alert serialization supplies all live recipients, correlation mutation
  succeeds, canonical bytes remain unchanged, and the live tail stays at its
  configured bound.
- No sink callback performs file/network/FlatBuffers work or recursively logs.
- Top-level `make -j8` precedes alert live tests, which pass under repeated
  concurrency runs.
- Run and repair the full `GUIDELINES.md` audit, with special attention to
  short-lived references, long-lived ownership cycles, cross-thread variable
  data, heap identity, lambdas, approximate counters, and blocking.

### Phase 6: Alert persistence, recovery, replay, and retention

Extend the live alert slice through disk and back to live delivery.

1. Implement the daily data/index writer with named file paths, bounded frame
   lengths, little-endian offsets, data-first/index-second appends, daily
   sequence recovery, and no per-record `fsync`.
2. Implement startup/rotation validation and incomplete-tail truncation exactly
   as documented above. Separate incomplete final transaction from committed
   interior corruption and never rewrite around interior corruption.
3. Implement bounded indexed replay through the same verified body loader.
   Read each indexed frame directly into a pooled `ZiIOBuf`, retain it for
   zero-copy access, and do not stage through a byte array. Capture high-water,
   replay disk to that point, buffer the later live tail, dedupe/drain it, and
   switch to live in one worker turn.
4. Use `ZuDateTime` calendar/Julian arithmetic for retention. Enumerate/delete
   only deterministic expected partition names at startup/rotation; do not add
   an unbounded periodic directory scan.
5. Add fault injection at every partial-write boundary and for open/read/write/
   truncate/close failure through a fake file backend. Verify non-recursive
   diagnostics and degraded App telemetry without generating large histories.
6. Test month/year/leap-day retention, empty history, cross-date replay,
   invalid lower bound, replay disconnect, pending-tail overflow, and exact
   duplicate-free ordered handoff using a fake clock and minimal records per
   partition. Set tail/replay bounds to tiny test values so overflow requires
   only a few records.

Handoff acceptance:

- Every partial final data/index state truncates to the last complete verified
  record and the next append reuses the discarded region.
- Interior corruption is detected and isolated, never silently truncated as a
  tail.
- Replay order/identity is exact across date boundaries, transition to live
  has no gap/duplicate, and all replay memory is bounded.
- Alert persistence tests pass on Linux and Windows/msys2 file paths where
  available, after a top-level `make -j8`.
- Run and repair the complete `GUIDELINES.md` audit, focusing on file error
  paths, integer/offset overflow, bounded loops, deletion target precision,
  file/buffer teardown, fixed arrays, and recursive diagnostics.

### Phase 7: Build cleanup and release hardening

1. Remove `ZvRAG.hh` and superseded schema/metadata and mixed-layer telemetry
   artifacts from the active build after all supported dependents use
   `Ztc::RAG` plus `ZtcRAGMap.hh`. Do not add aliases, forwarding headers,
   duplicate schemas, or dual dispatch.
2. Verify all new public headers follow the library-header skeleton and contain
   only direct ordered dependencies. Confirm C++ source identifiers remain at
   or below the authoritative 32-byte `GUIDELINES.md` naming limit; this limit
   does not apply to telemetry IDs, structured keys, filters, or wire values.
   Confirm every new
   source has the standard modelines/license header and all implementation
   indentation uses hard tabs.
3. Re-run dependency generation from a clean tree and prove `zv` owns every
   telemetry-generated header without source-tree residue from another module.
4. Run the complete test/build matrix and long-running stress: repeated
   start/stop, active client shutdown, invalid-frame flood, manager churn,
   DB root/child watcher and index churn, subscription churn, persistence faults,
   and connection loss. Do not use high-volume alert generation as a stress
   load.
5. Run clang ASan/LSan and the integration test under
   `libtool exec valgrind --leak-check=full`. Resolve all warnings, leaks,
   use-after-free, queue residue, and generated-code mismatches.
6. Perform a final line-by-line `GUIDELINES.md` audit across every changed
   module. Record any intentional amber exception with a measured reason;
   repair every other red/amber finding before release.

Handoff acceptance:

- The complete Acceptance Criteria below are met from a clean checkout on the
  supported gcc/clang configurations and available Linux/Windows targets.
- The active dependency graph contains one telemetry API/schema/protocol
  surface and no compatibility scaffolding.

## Code References to Impacted Code

Producer-side watcher, notification-order, scratch-queue, generic DB,
DB-metadata, and Zdb-conversion changes are listed in `ztcwatch.md` and
`zdbtel.md` and are not repeated here.

### Lower-level contracts and producers

- `zm/src/ZtcHeap.hh`, `ZmHeap.cc`, `ZtcHash.hh`, `ZmHashMgr.cc`,
  `ZtcThread.hh`, and `ZmThread.cc` - add only RAG and Heap `allocated()`
  enrichment over existing telemetry values; preserve the Heap/Hash
  telemetry-manager `all` traversal that retains the current object while
  invoking callbacks without the manager lock; retain the direct
  `ZmThreadMgr_` singleton finalization and `ZmThreadContext::m_published`
  lifecycle already implemented.
- `zm/src/ZtcQueue.hh` - add only the required RAG; do not change queue
  counters or sampling.
- `zm/src/ZtcTypes.hh`, `zm/src/Makefile.am`, and `zm/test/Makefile.am` -
  consume the precursor's common RAG/callback heap types and retain its lower
  contract tests.
- `zi/src/ZtcMx.hh`, `zi/src/ZiMultiplex.hh`, and `zi/src/ZiMultiplex.cc` -
  RAG, owner-qualified Connection keys, endpoint publication, and listen/stop
  over the completed discovery surface; retain pointer-keyed
  constructor/destructor Mx registration, the single manager-owned child
  watcher set, direct hash-return-based `cxnDel`, inherited scheduler ID use,
  traversal-only fixed scheduler queues, and the absence of all Hub/Link
  knowledge from `ZiConnection` and `ZiMultiplex`.
- `zi/src/ZtcHub.hh`, `zi/src/ZtcHub.cc`, `zi/src/ZtcLink.hh`, and
  `zi/src/ZtcPool.hh` - RAG over existing values.
- `ztcp/src/Ztcp.hh` and `ztls/src/Ztls.hh` - transport-owned Link collection,
  publication, traversal, pointer-set `ZmHash<Ztc::Link *>`, and synchronous
  scratch queue adapters without generic `ZiConnection` Link recovery;
  explicitly constructed IDs, ordinary Hub lifecycle counters, and Link-state
  normalization directly from existing transport state; send, queue, and
  completion semantics remain unchanged.
- `zquic/src/ZquicLink.hh` - retain the same synchronous scratch
  `allQueues` contract for QUIC links.
- `zi/src/ZiRxStream.hh` and `zi/test/ZiRxStreamTest.cc` - generic
  complete-frame extraction,
  zero-copy detach, fragmented direct gather, and trailing-byte preservation.
- `zi/src/ZiFile.hh:217` - alert-tail truncation primitive.
- `zi/src/ZiLog.hh` - explicit sink subtype/lifetime contract.
- `zi/test/ZiMxLoopTest.cc` and `zi/test/Makefile.am` - bound-endpoint coverage.
- `zu/src/ZuPercent.hh:63` - canonical composite-key codec.
- `zu/src/ZuDateTime.hh:93` - real calendar-day retention.

### Service, schema, and transport

- `zfb/src/Zfb.hh`, `zi/src/ZiIOBuf.hh`, and `zfb/test/zfbtest*.cc` -
  extend/test `Zfb::IOBuilder` support for pre-provisioned `ZiIOBuf::skip` and
  its preservation through growth, while reusing the direct allocator,
  detach, `Zfb::Save`, and zero-copy `Zfb::Load` facilities.
- `zdb/src/ZdbMsg.hh:33` - reference shape for the independent four-byte
  header and save/load/verify flow; no source dependency is introduced.
- `zdb/src/ZdbBuf.hh` and the `Zfb::IOBuilder` call sites in `zdb/src/Zdb.cc`
  - reference direct pooled-buffer serialization, single verification, and
  trusted-buffer accessor lifetime.
- `zv/src/ZvCf.hh` and `zv/src/ZvCf.cc` - reuse/extend Mx configuration parsing
  for AppCf role validation.
- `zv` consumers of `ZtcTypes.hh`, `ZtcAppTypes.hh`, `ZtcMsg.hh`, `ZtcFB.hh`,
  `ZtcApp.hh/.cc`, and optional internal `ZtcAlert.hh/.cc` - new service,
  exact-heap `ZmNoLock` `ZmRBTreeKV` secondary indices, producer-manager
  `guard()` integration, guarded snapshot capture, and worker-only subscriber
  watch lists.
- `zv/src/fbs/*.fbs` - owned request/response/telemetry schemas in
  `Ztc.fbs`.
- `zv/src/ZtcRAGMap.hh` - add reflected names/`Map` over the low-layer
  `Ztc::RAG` ordinals without defining a second RAG type.
- `zv/src/ZvRAG.hh` - remove after DB metadata and all other active consumers
  use `Ztc::RAG`/`ZtcRAGMap.hh`.
- `zv/src/Makefile.am` - schemas, generated installs, headers, sources, and
  `libZtcp` link dependency.
- `zv/test/Makefile.am` - schema, manager, service/client, and alert tests.

### Generic DB and implementation

- The completed files and producer conversion are owned by `zdbtel.md`.
- `zdb/src/Zdb.hh` and `zdb/src/Zdb.cc` - retain the Zdb-local `HostState`
  and `CacheMode` namespaces that currently import `Ztc::DBHostState` and
  `Ztc::DBCacheMode`; implementation code uses only the local names so either
  Zdb vocabulary can diverge without pervasive changes.
- `zv/src/ZtcFB.hh` and `zv/src/fbs/*.fbs` - integrate the existing DB
  generated types into the service union without duplicating the metadata
  already colocated in `ZtcDBTable.hh`, `ZtcDBHost.hh`, and `ZtcDB.hh`.
- `zv/test/ZtcAppTest.cc` and the test client - App-index and public-wire DB
  snapshot integration over the accepted generic producers.

## Detailed Test Plan

### Unit and schema tests

- RAG threshold tables reproduce the established formulas exactly: Heap and
  Queue zero-size Off cases, Hash without a separate Off case, Connection
  zero-capacity behavior from the direct arithmetic, exact 50%/80% boundaries,
  every engine/link/pool/DB state, and the initial thread CPU value.
- Schema round-trip constructs every telemetry type with distinctive values,
  high bits set above `UINT32_MAX`, signed edge values, nontrivial enum/flags,
  and all derived fields. Generated accessors must reveal omission, wrong
  constructor index, truncation, or signedness changes.
- Compile-time `ZfbEnumMatch` checks cover every enum ordinal.
- Framing tests cover empty/incomplete header, zero body, boundary maximum,
  maximum+1, `UINT32_MAX`, widened addition, fragmented body, coalesced frames,
  invalid root, invalid union, and exact consumed lengths.
- Header-save tests retain the pre-save body pointer and bytes, exercise normal
  and builder-growth allocations, and assert after save that
  `frame.data() + sizeof(Hdr)` is that same pointer. They also inject missing
  headroom and require a clean failure rather than allocation, `memmove`, or
  body relocation.
- IOBuilder tests record the pooled `ZiIOBuf` allocation identity before
  serialization, through growth, after `Finish()`/detach, after header prepend,
  and at the send/file seam. No intermediate FlatBuffer allocation or body
  copy is permitted. Primitive-vector fixtures verify direct uninitialized
  vector fill, and received string/byte spans must point inside the retained
  frame buffer.
- RxStream extraction tests cover fragmented headers/bodies, an exact
  single-buffer frame, multiple coalesced frames, and a frame followed by a
  partial next frame. They assert zero copies/allocations on the detachable
  single-buffer path, only a trailing-remainder copy when splitting a
  coalesced buffer, and exactly one final-buffer gather for a multi-buffer
  frame.
- Checked-in “golden wire captures” are small `ZuBArray`/`ZuArray` test
  fixtures whose exact capacity is the reviewed protocol byte sequence, with a
  maintenance comment naming that reason. They assert the four little-endian
  length bytes, FlatBuffers root/union selection, sequence values, and
  representative 64-bit fields. They catch accidental wire changes between
  compiler/build updates and are regenerated only by an explicit reviewed
  schema change.
- Key/filter tests cover escaping of `%`, `:`, `*`, controls, and UTF-8
  bytes; canonical uppercase escapes; invalid escapes; equivalent empty and
  `*` all-components; exact and textual-prefix string components; per-`telKey()`
  component counts and types; enum `lookup()` acceptance/rejection; canonical
  numeric parsing; and encoded/decoded length limits. Thread fixtures prove
  only TID is consulted. Queue fixtures cover `type:id`, `:*`, `type:`,
  `type:ownerID:id`, and `*:*:*`, with all values sourced from its `telKey()`;
  Hub fixtures include `type:id`, `:*`, and `*:*`. Mx, Hub, and DB fixtures
  prove root selection controls emission of their subsidiary objects.
- Filter hot-path tests instrument parsing, enum lookup, numeric conversion,
  allocation, and candidate comparison. With small fake indices, prove those
  compilation operations occur once per accepted filter and never recur per
  candidate or subscription poll; match-all performs no candidate matcher,
  fully exact uses one tree lookup, leading prefix uses a bounded range, and
  scan paths invoke only their preselected residual checks. Cover the common
  all, exact, and prefix cases without using large object counts as a proxy for
  correctness.
- The focused root/child watcher, manager guard, Hub ordering, scratch queue
  key/telemetry, callback heap identity, and synchronous deletion tests defined by
  `ztcwatch.md`, plus the DB migration/lifecycle/schema/population tests
  defined by `zdbtel.md`, must pass before and remain green throughout this
  plan.
- Producer lifecycle regression tests retain the reviewed current behavior:
  Mx publication occurs at completed construction rather than start, Mx
  destruction performs no child traversal, DB publication brackets successful
  `init()`/`final()`, and neither producer uses a parallel publication flag.
  Fixed scheduler queues are seeded by `allQueues` and removed with their
  containing Mx rather than through fabricated queue delete callbacks.
- Ztcp/Ztls Link-state tests drive the existing transport lifecycle and verify
  normalized Link telemetry plus Hub down/transient/up totals without any
  telemetry-only state member or duplicate counter update.
- Composite-key identity tests verify every owner component is obtained from
  its containing object or existing manager/App callback context, with no
  duplicate owner-ID member in the contained producer. Connection tests prove
  the leading Mx ID is a reference returned through `telKey()`, including the
  global watcher path. Queue tests include scheduler Thread keys referencing
  the owning scheduler ID. Link deletion
  calls queue delete callbacks directly during its one stable `allQueues`
  traversal under the Hub manager lock.
- App index tests cover every typed tree, exact heap ID, `ZmNoLock` policy, and
  manager lock domain. A focused Hash traversal test proves `all` holds an
  additional reference to its current callback object without holding the
  manager lock across that callback. Tests also prove App allocator warmup prevents first-use
  heap registration/re-entry while the Heap manager lock is held. With
  barriers, hold a manager `guard()` during
  telemetry sampling, start deletion on another thread, and prove deletion
  cannot enter its watcher until sampling returns. Verify each manager
  `capture()` obtains `count_()` and allocates scratch while locked, constructs
  each selected element with placement new directly in scratch storage before
  telemetry is invoked exactly once, unlocks before its consumer callback,
  scratch Queue pointers never escape `allQueues`, and serialization begins
  only after the guard returns. These are App integration tests, not duplicate
  producer watcher tests.

### Public network integration tests

- Start App on `127.0.0.1:0`, obtain the actual endpoint, issue every group
  through a test-only client, validate Ack/Telemetry/Completion ordering and
  sequence correlation, then stop with a live connection.
- Repeat on `::1` when supported; otherwise TAP-skip with the platform error.
- Deliberately fragment each header/body boundary and coalesce multiple frames
  into one write.
- Exercise complete invalid requests without losing the connection; exercise
  fatal length violations and verify bounded disconnect.
- Exercise each `telKey()` filter grammar, including exact, empty,
  zero-length and `*` all-components, textual prefix, escaped components,
  scalar Thread TID, Queue `type:id`/`type:ownerID:id`, Hub
  `type:id`/`:*`/`*:*`, aggregate-root child emission, and unique queue-index
  membership for queues exposed by containing objects.
- Query the service's own Mx/Hub/Connection/Link/queues. With a small fake
  transport, prove sampling precedes submission of the frame carrying that
  sample. In the real loopback test, synchronize a later query and verify that
  prior response activity can become visible without asserting exact
  cross-thread counter deltas.
- Use generic DB mocks with nested collections and posted serialization
  batches; require one final completion only after all requested telemetry is
  emitted.
- Drive multiple subscription intervals, immediate first snapshots,
  canonical-equivalent updates/removals, membership changes between polls,
  overruns, App RAG changes, and disconnect cleanup.

### Concurrency and lifecycle tests

- Synchronize with `ZmBlock`, `ZmSemaphore`, callbacks, or injected clocks.
  Never use sleeping or polling as a completion condition.
- Race request handoff with disconnect/link generation reuse, producer
  add/delete with index capture, watch installation with initial `all`, timers
  with unsubscribe/stop, DB child deletion with sampling, and logger ingress
  with final.
- Using the accepted producer watchers from non-worker threads, prove App
  synchronously mutates only the manager-guarded secondary index and performs
  no worker post. Prove subscription/watch-list state changes occur only from
  worker-owned request, timer, App-RAG, disconnect, and shutdown paths.
- Repeatedly run init/start/stop/final, including idempotent duplicate calls and
  injected failure after each startup stage. Assert no listener, registration,
  timer, file, link, buffer, subscription, or queue remains.
- While one App is initialized, attempt a second initialization and prove it
  fails before changing any manager callback; finalize the first and prove a
  later App can claim and release the watcher set.

### Alert tests

- Prefer fake/mock seams for the alert clock, file operations, sink ingress,
  and subscriber frame capture. Each unit test should use only the two or three
  events/records needed to prove ordering, rollover, recovery, or handoff.
- Override live-tail and replay-pending limits with tiny test values when
  testing bounds; do not fill production-sized capacities.
- Keep one small real-`ZiFile` and public-wire integration path to verify the
  mocks match actual framing and file behavior; do not create large alert
  histories or use alert volume as a proxy for correctness.
- Validate sink copy bounds, metadata widths, daily sequence assignment,
  canonical one-time serialization, per-subscriber correlation, live-tail
  bound, and inert sink use after App final.
- Generate every possible partial final data write and partial index write.
  Reopen, verify truncation to the prior record, append, and prove the new
  record occupies the discarded offset.
- Corrupt an interior offset/frame/key separately and verify partition
  isolation rather than tail truncation.
- Replay empty/single/multi-day histories, dates across month/year/leap day,
  stable high-water plus concurrent alerts, bounded pending overflow, client
  disconnect, and exact ordered no-gap/no-duplicate transition to live.
- Inject every ZiFile failure and verify non-recursive fallback diagnostics and
  degraded App telemetry.

### Build and analysis matrix

- `make -j8`
- `make -C zv/test -j8 && make -C zv/test test`
- affected `zm`, `zi`, `ztcp`, and `zdb` test binaries
- top-level `make test`
- current gcc and clang debug configurations
- release verification through `./z.config`, followed by top-level
  `make clean` and `make -j8` before any release tests; never mix objects from
  different build types
- clang AddressSanitizer and LeakSanitizer
- `libtool exec valgrind --leak-check=full` for the main integration test
- Linux x64/ARM and Windows/msys2 checks where those builders are available
- warning-free generated and hand-written code

Every switch among debug, release, sanitizer, gcc, and clang configurations
uses `./z.config` as applicable, then a top-level `make clean` and `make -j8`
before tests. Source-tree binaries are run through `libtool exec` under
debuggers and analysis tools; `.libs` binaries are never invoked directly.

## Acceptance Criteria

- `Ztc::RAG` is the sole telemetry RAG vocabulary and every required derived
  method matches the specified zero/threshold/state behavior.
- Plain structures live at their owning layer; lower layers contain no
  FlatBuffers/`zv` dependency. `ZtcFB.hh` maps every non-DB public type and
  exact field; the `ZtcDB*` headers map DB types once as the accepted
  `zdbtel.md` exception.
- Every enum has compile-time ordinal checks and every union member
  round-trips high-width, signed, key, delta/series, and derived values.
- The wire header is exactly four little-endian body-length bytes; parsing
  bounds length before allocation and verifies the root/union before access.
- Outbound serialization writes directly through stack `Zfb::IOBuilder`
  instances into the final named pooled `ZiIOBuf`; detach, header prepend,
  send, and persistence retain that allocation and never copy the body.
- Receive detaches a complete single-buffer frame without copying, gathers a
  fragmented frame once directly into its final pooled buffer, verifies it
  once, and uses zero-copy `Zfb::Load` spans while that buffer remains owned.
- Every complete valid request receives Ack, every invalid bounded request has
  defined negative behavior, every payload is correlated, and every one-shot
  snapshot completes explicitly.
- Port-zero listen publishes the actual endpoint and the same endpoint is used
  to stop the listener on Unix and Windows.
- Rx, timer, worker, Tx, and logger state obey the documented owner and
  lifetime. DB/host/table sampling retains its producer's existing approximate
  telemetry behavior; no lock or dispatcher is added to improve it.
- Existing link/queue telemetry is serialized without changing its producer,
  counters, accuracy, send behavior, or queue semantics.
- The service's own Mx, Hub, accepted client Connection/Link, and queues are
  discoverable normally; a sample excludes the response frame that carries
  that sample and makes no recursive adjustment for its own observation.
- Every App callback and secondary index has the exact named heap ID specified
  here, and the precursor's traversal/watcher allocation-ID suite remains
  green.
- Unique `ZmNoLock` `ZmRBTreeKV` indices map owning forms of producer
  `telKey()` values to persistent object pointers, except Queue source
  indices, which map keys to their containing Mx/Link/Pool. Synchronous delete
  watchers and the corresponding
  producer-manager `guard()` protect pointer lifetime. Scalar manager
  `capture()` uses `ZtLocalArray` or, within `zm`, `ZmAlloc`; aggregate and
  queue capture use the maintained App indices. Both paths match only
  `telKey()`, construct each selected element with placement new directly in
  `push()` storage, and unlock before serialization, posting, sending, or
  subscriber mutation. No scratch `Queue *` escapes `allQueues`.
- Every filter is a fixed colon-separated component pattern over `telKey()`.
  Empty and `*` components mean all, enum components use `lookup()`, string
  components use `ZuSpan` `find`/`match`/`exact` for exact or terminal-prefix
  matching, and no telemetry field or generic query evaluator participates.
- Each filter is compiled once into inline type-specific state. Match-all,
  fully exact, leading-range, and residual-scan paths dispatch before
  iteration; subscription polls perform no parsing, decoding, enum lookup,
  numeric conversion, component allocation, or per-candidate mode switch.
- No contained telemetry producer caches a duplicate owner ID. Every composite
  owner component comes by reference from the owner or from the existing
  hierarchy callback context; copying is confined to App's owning secondary
  key, captured telemetry, and serialized output.
- App's watch-then-`all` integration produces a complete initial index without
  duplicates or a creation gap while consuming the precursor's already-tested
  notification ordering and synchronous queue keys.
- All subscriber watch-list maintenance occurs on the App worker; producer
  watcher threads synchronously update only secondary indices.
- Subscriptions honor their individual effective intervals, coalesce overruns,
  update/remove deterministically, and clean all state on disconnect/stop.
- Alert sink references are safe after final; accepted alerts drain; tail,
  replay handoff, frames, filters, requests, and subscriptions obey their
  stated bounds.
- Alert files recover only an incomplete final transaction, reject committed
  interior corruption, preserve `{date, seqNo}` identity/order, and transition
  replay to live without loss or duplication.
- Startup failure and idempotent shutdown leave no Mx, listener, manager entry,
  timer, link, file, continuation, or pooled buffer behind.
- A concurrent second App initialization fails without replacing the active
  App's single-consumer manager watchers; a later App can initialize after the
  first App has finalized.
- The clean build/test/analysis matrix passes without warnings, leaks,
  sanitizer defects, deadlocks, test sleeps/polling, or unresolved
  `GUIDELINES.md` audit flags.

## Non-goals

- Compatibility aliases, forwarding headers, duplicate schemas, dual wire
  dispatch, or source/ABI preservation.
- Changes to modules outside the active dependency graph solely to keep an
  unused binary buildable.
- Authentication, authorization, TLS, or WAN-safe deployment in the first
  release.
- A dashboard, aggregation database, historical store beyond alert replay, or
  remote control plane.
- Perfectly atomic multi-counter telemetry snapshots.
- Application-level flow control, delivery acknowledgement, outbound-byte
  accounting, or changes to `Ztcp`/`ZiTx` queue and completion behavior.
- Per-alert durable `fsync`, or a new bound/drop policy for the scheduler's
  accepted logger-post fallback queue.

## Options and Open Questions

No product or feasibility questions remain open. The header/union format,
framing-error policy, loopback transport, App RAG ownership, role defaults,
queue discovery, subscription coalescing, daily alert identity, acceptance
semantics, incomplete-tail recovery, and thread CPU behavior are resolved
above. Implementation may adjust private helper names during review, but it
must not change these contracts without a new explicit design decision.
