# `Zhttp` Hot-Path Linear-Search Repair Plan

## Objective

Repair every finding and adjacent linear queue operation in `zhttp20.md`.
Eliminate workload-scaled searches, repeated scans, and contiguous-array
shifts from request, response, stream, connection-selection, cancellation,
and H2 frame-scheduling paths.  Preserve protocol ordering, fairness,
flow-control, completion, sharding, and shutdown behavior while reducing
state and deleting superseded algorithms.

Do not modify `zhttp20.md`.  It is the audit record and source of requirements
for this plan.  Do not add compatibility paths or retain old and new queueing
algorithms in parallel.

## Non-Negotiable Outcomes

The work is complete only when all of the following are true:

- H2 frame admission, selection, transmission, cancellation, and completion
  perform no search or contiguous-array splice over the connection-wide frame
  backlog;
- `finishTx()` and `fenceTx()` locate their target frame directly;
- H2 preserves per-stream frame order, HEADERS/CONTINUATION atomicity,
  connection and stream flow control, bounded fair draining, and exact frame
  admission accounting;
- H1 response ordering is per connection and the server never rotates blocked
  responses through a global FIFO;
- H3 physical-link selection is keyed, and a down physical link is removed
  deterministically from every owning/indexing container;
- H3 active-stream completion and pre-assignment cancellation perform direct
  removal without pointer searches;
- H3 and H2 pending-stream FIFOs do not shift array prefixes during normal
  admission;
- active client cancellation resolves the request and its transport-owned
  logical link directly;
- QPACK insertion planning does not perform separate exact-value and name
  searches for each planned insertion;
- all new queues, hashes, membership state, callbacks, and admission credits
  are empty or balanced after reset, connection failure, stop, and finalization;
  and
- no new hot-path heap allocation, locking, cross-shard access, or redundant
  validation is introduced without a demonstrated requirement.

## Engineering Constraints

- `GUIDELINES.md` compliance is the overriding constraint.  Read its current
  storage, intrusion, I/O sharding, lifetime, and test sections before each
  stage and change this plan's implementation detail if necessary while
  preserving the outcomes above.
- Prefer deleting arrays, cursors, sequence numbers, flags, scans, and helper
  functions made unnecessary by intrinsic queue membership.  Do not add a
  hash beside a container when the indexed object can instead be the owning
  intrusive node.
- Use `ZmHash`, `ZmList`, and their intrusive nodes and named `ZmHeap` policies.
  Stack multiple intrusions only when one object genuinely participates in
  multiple containers.
- Keep Rx-owned connection and stream membership on Rx.  Keep H2 pending
  frames, flow-control scheduling, and transport completion on Tx.  Preserve
  `QueueAdmission` as the deliberately shared atomic count used before an Rx
  to Tx post; do not misclassify its admission-only access as Tx-owned or add
  any other shared queue state.  Cross-shard functions must post fixed metadata
  or references to destination-owned objects; do not add locks or atomics to
  bypass ownership.
- `ZiIOBuf` remains the frame/body owner.  Move references; do not copy payloads
  into queue records or temporary contiguous storage.
- Short-lived nodes use raw back-pointers to longer-lived owners where that is
  the local pattern.  Owners must cancel or drain all nodes before destruction.
- Put non-template-dependent queue keys, nodes, and policies outside templates.
  Use concise names under the 32-byte limit and existing `Zhttp` heap IDs.
- Do not add production telemetry merely to prove complexity.  Use test-only
  observation or an implementation-time benchmark harness, and retain a new
  benchmark target only if it has ongoing regression value.
- Each stage updates affected tests and leaves `zhttp/src` and `zhttp/test`
  buildable before the next stage begins.

## Required Invariants Before Refactoring

Document these invariants in the implementation or tests before deleting the
old state:

1. Every admitted H2 frame consumes exactly one `QueueAdmission` credit and
   every transmitted, rejected, reset, or cleared frame returns it exactly
   once.
2. A stream's frames are transmitted in enqueue order.  Once a non-final
   HEADERS frame begins, only that stream's CONTINUATION frames may be emitted
   until END_HEADERS.
3. DATA consumes both the connection and stream windows; non-DATA frames and
   zero-length DATA are not incorrectly blocked by a zero window.
4. At most one terminal response callback and at most one outstanding body
   fence callback can be attached to the intended transport buffer.
5. One H1 response is active per connection, and queued responses for that
   connection retain request order.
6. Every H3 physical-link key identifies at most one selectable live link.
   Down links are not selectable or retained by the hub index.
7. A logical request or stream belongs to at most one pending, waiting,
   active, or terminal state, and its membership handle is invalidated on
   removal.
8. A client request key identifies at most one active `LiveReq`; every terminal
   path removes that active entry exactly once.

## Stage 0: Baseline and Complexity Characterization

Before changing containers:

- run the current focused `zhttp` tests and record the configured build type;
- exercise H2 queues at depths 1, 16, 64, 1,024, and 4,096 with stream and
  connection windows independently blocked;
- exercise H1 pipelining with many connections whose head response is active;
- exercise repeated H3 endpoint creation/failure and confirm that the current
  owner array grows;
- exercise cancellation at increasing client concurrency; and
- measure QPACK planning with unique names, repeated names, and duplicate
  name/value pairs.

Record median and tail latency or deterministic operation counts against queue
depth.  These results are comparative evidence, not pass/fail timing thresholds
for unit tests.  Do not retain sleep-based or machine-speed-sensitive tests.

## Stage 1: Replace the H2 Global Pending-Frame Array

### 1.1 Make frame ownership per stream

Replace `PendingFrameQueue m_pendingFrames` with stream-owned frame FIFOs under
the existing Tx-owned `TxWindowEntry` records.  Do not allocate a sidecar node
for every frame.  Each non-null H2 frame is already a pooled
`Transport_::TxBufNode`, derived from `ZiTxQueue::Node`, and can move directly
from a per-stream `ZiTxQueue` to the native transport queue.  Derive the DATA
flow-control length and END_STREAM state from the encoded nine-byte H2 frame
header rather than duplicating them in another queue record.

The current null-buffer end marker should become per-stream state ordered
after the buffer FIFO.  It consumes and returns one admission credit exactly
as today and fires local-end only after preceding buffers drain; it does not
justify allocating an empty frame node.  Per-stream FIFO order makes `seq`,
`headSeq`, and `nextSeq` redundant; delete them if no remaining invariant uses
them.

Retain the connection-wide `QueueAdmission` limit.  Admission occurs before a
node becomes visible to the Tx queue.  Failure to allocate or enqueue must
return the reserved credit immediately.

### 1.2 Schedule runnable streams, not frames found by scanning

Give each `TxWindowEntry` explicit membership in at most one scheduling state:
runnable, blocked by its stream window, blocked by the connection window, or
not scheduled.  Stack one shadow `ZmList` intrusion with the existing hash node
using the established `ZmCache` composition pattern, so the hash and scheduler
share one allocation per active Tx stream.  The mutually exclusive scheduling
queues reuse that one list intrusion; do not allocate a scheduler node on each
state transition and do not search all streams to change one stream's state.

The wake-up rules are:

- enqueue schedules a stream when its FIFO changes from empty to non-empty and
  its head is runnable;
- a stream WINDOW_UPDATE wakes that stream directly;
- a connection WINDOW_UPDATE walks only the connection-blocked stream queue,
  reevaluates each affected head once, and moves newly runnable streams; this
  work is O(blocked streams), not O(queued frames), and updates each stream's
  explicit scheduling state consistently;
- completing or cancelling the head reevaluates only that stream; and
- when a HEADERS block is open, its owning stream is selected directly until
  END_HEADERS rather than found by scanning the ready queue.

Drain at most `FrameDrainBatch` ordinary frames per scheduler turn.  Requeue a
still-runnable stream at the tail after one frame to preserve fairness, except
that CONTINUATION atomicity overrides ordinary rotation.  Return to the
scheduler between batches.

Do not introduce a priority tree, dependency scheduler, or additional HTTP/2
feature not required by the existing behavior.

### 1.3 Make removal and shutdown intrinsic

`removeFramesTx_(id)` must find `TxWindowEntry` through `m_txWindows`, detach
its buffer FIFO and scheduling intrusion in O(1), complete each owned buffer
with `Reset`, return the detached buffer count plus any null end-marker credit
to admission, and delete the entry.  Iteration over frames actually owned by
that stream is proportional required cleanup, not a search through unrelated
frames.

`clearFramesTx_()` must detach all per-stream queues, settle callbacks and
credits exactly once, clear scheduler memberships and header ownership, and
leave the Tx hash empty.  Add debug assertions for empty queues, zero admission
count, and no posted drain at `finalWire()`.

Delete `m_pendingFrames`, `m_frameCursor`, `m_frameScan`, the old scan loop,
and all splice-adjustment logic in the same change.

### 1.4 Tests

Extend `ZhttpH2HubTest` to cover:

- fair progress across many runnable streams;
- one stream blocked while unrelated streams progress;
- connection-window exhaustion and wake-up;
- stream-window wake-up without disturbing other streams;
- interleaved multi-frame header blocks with no illegal CONTINUATION;
- reset of a stream with zero, one, and many queued frames;
- queue saturation and exact admission-credit recovery;
- GOAWAY, disconnect, stop, and finalization with queued frames; and
- no starvation across repeated `FrameDrainBatch` turns.

## Stage 2: Make H2 Completion and Fence Attachment Direct

Fold this stage into Stage 1 while the queue representation is changing.

- Use the per-stream `ZiTxQueue::tailPtr()` as the single source of truth;
  do not add cached latest-frame or terminal-frame pointers.
- `fenceTx()` attaches to that tail buffer directly.
- `finishTx()` uses the same tail and verifies END_STREAM from its encoded H2
  header.  Because `localEndQueued` prevents later frames, a buffered terminal
  frame must be that stream's tail.  A null end marker has no eligible buffer
  and preserves the existing failure result.
- Preserve the existing refusal to overwrite an already installed
  `txComplete` callback.
- Preserve the existing failure result when no eligible queued buffer exists.

Delete both reverse scans.  Test fences on header-only responses, DATA,
multi-frame headers, streaming chunks, resets before drain, and completion
racing with a posted drain on the same Tx shard.

## Stage 3: Replace H1 Global Blocked-Response Rotation

### 3.1 Put response order on the H1 link

Move the H1 response FIFO from the profile-wide queue into each H1 link type.
The link owns its queued `LiveReq` nodes and its active-response state.  Give
the link one reusable ready node and an explicit sentinel/state preventing
duplicate readiness.  Allocate that node at most once per connection from a
named heap, or stack it into the link allocation if the concrete nested link
types permit clean intrusive composition; never allocate it per response or
per ready transition.  The ready node must retain the link while queued, and
disconnect must detach it before releasing link-owned responses.

Maintain separate ready-link queues for H1 TCP and H1 TLS so their concrete
link types remain compile-time known.  H2 TLS and H3 QUIC retain their direct
response FIFOs and batched drains.

### 3.2 Admission and completion

On H1 response enqueue:

- append to that link's FIFO;
- if the link is idle and not already queued, append the link once to its
  profile ready queue; and
- schedule the profile drain once.

The drain pops a ready link, removes its oldest response, marks it active, and
sends it.  It never examines a response belonging to a blocked link.  On
completion, clear active state and requeue the link once if more responses
remain.  Preserve `ResponseDrainBatch` as a bound on responses sent per turn.

On disconnect, parse failure, cancellation, or shutdown, detach the link from
the ready queue and complete every queued request exactly once.  Do not rely on
a later global scan to garbage-collect link-owned responses.

Delete H1's `responseActive`-based global rotation and any branches that exist
only to put a blocked `LiveReq` back onto the global FIFO.

### 3.3 Tests

Add server tests for:

- pipelined response order on one H1 connection;
- independent progress on another connection while the first is active;
- many blocked connections plus one newly runnable connection;
- duplicate scheduling prevention during enqueue/completion races;
- disconnect with queued and active responses;
- request-error responses using the same per-link ordering; and
- shutdown with populated per-link and ready-link queues.

## Stage 4: Key and Deterministically Remove H3 Physical Links

### 4.1 Prove the key

Confirm every `ClientHub<App>` instantiation's concrete `Logical` usage.  The
selection key is `(host, remote, port)`.  Add a link-type discriminator only
if one hub can actually contain different `CliLink<App, Logical>` types; do not
add speculative type erasure or runtime type IDs.

Define the non-template-dependent key, equality, hash extraction, and hash
policy outside the hub template.  Use Z hash composition and a named heap ID.

### 4.2 Make one container authoritative

Prefer an authoritative `ZmHash` whose entry owns the existing `ZmContext` and
endpoint key, allowing the linear `m_links` owner array to be deleted.  Iterate
the hash for diagnostics and shutdown, which are control-plane paths.  If
`ZmContext` or incomplete-type constraints make that unsafe, retain a compact
owner array only for lifetime/iteration and let the hash point directly to its
slots; then use stored slot identity and swap removal.  Do not retain dead
slots in either design.

`connect_()` performs one keyed lookup and reuses only a live selectable link.
Creating a link inserts it atomically with respect to the Rx-owned hub state.
Insertion failure must not leave an unowned live link.

Change endpoint-down completion to identify the exact link/key being removed.
Erase the selection entry immediately, detach hub ownership, and only then
notify stop accounting.  Hold the executing link alive locally while removing
the hub's owning reference.  Make repeated down notification idempotent without
searching.

### 4.3 Tests

Extend `ZhttpH3HubTest` for:

- repeated reuse of the same endpoint;
- distinct host, explicit-remote, and port keys;
- endpoint failure followed by immediate replacement;
- many sequential endpoint failures with stable live index/owner counts;
- repeated and late down notification;
- stop while links are migrating or going down; and
- empty index/ownership assertions after finalization.

## Stage 5: Make H3 Stream and Pending Membership Direct

### 5.1 Active streams

Keep active client/server streams iterable for disconnect and migration, but
make removal direct.  The smallest acceptable change is an active-slot index
in each native stream plus swap removal from the existing arrays.  Use an
invalid-slot sentinel, validate the slot against the expected stream in debug
builds, update the moved stream's slot, and invalidate the removed stream's
slot.

Factor one concise helper for client and server active-array removal if it
does not weaken concrete typing.  Delete the immediate and deferred pointer
search loops.

### 5.2 Pending and waiting logical streams

Keep the client `pending` and `waiting` arrays unless measurement shows their
contiguous storage itself is a problem, but add head cursors, tombstones, and a
queue kind plus slot in the logical link.  Enqueue records its slot; direct
cancellation validates that slot and nulls it; dequeue advances past cancelled
tombstones as required work and periodically compacts in one amortized pass,
updating surviving slots.  Reset an empty queue to release all tombstones.

This uses the existing array allocation instead of adding one heap node per
logical request.  Use intrusive membership only if it can be stacked into the
existing logical-link allocation without a sidecar allocation.  Store explicit
pending or waiting membership, never both, and invalidate it before invoking
application callbacks.

Opening a QUIC stream consumes the waiting head without shifting the suffix.
Cancellation removes the logical entry directly by its recorded slot.
Connection failure walks only occupied entries, invalidates their membership,
and notifies exactly the logical requests it owned.

### 5.3 H2 pending-stream prefix removal

Replace both H2 client `m_pending` array-prefix splices with head cursors and
amortized compaction.  The current H2 logical cancellation marks the logical
request cancelled and lets ordered admission observe it; it does not search or
remove an arbitrary pending element, so a per-request intrusive allocation is
not justified.  Preserve `maxPending` as the number of live queued elements,
FIFO admission, `RequeueBatch`, protocol-choice ownership, and stop/failure
results.  If later behavior requires immediate arbitrary removal, add direct
slot/tombstone membership rather than reintroducing a search.

Delete all normal-path `splice(0, n)` operations.

### 5.4 Tests

Cover active-stream swap removal at first, middle, and last slots; immediate
and deferred close; cancellation in pending and waiting states; stream-open
failure; migration iteration after removals; large pending FIFO admission;
batched H2 requeueing; endpoint failure; and shutdown with every membership
state populated.

## Stage 6: Make Client Cancellation Direct

### 6.1 Index active requests

Add an Rx-owned `ZmHash` from the existing application request key to active
slot.  Insert only when a request becomes active, before any asynchronous
operation can expose it to cancellation.  Remove it in the single path that
returns the `LiveReq` slot to `m_free`; consolidate terminal cleanup first if
the current paths do not provide one such point.

Queued requests remain cancellable through `Tx::abort(key)` and must not appear
in the active hash.  Treat duplicate active keys according to the existing Tx
queue contract; assert the invariant rather than silently replacing an entry.
Callbacks that retain slot identity must continue validating attempt
generation.

### 6.2 Retain direct pool membership

Store the selected pool slot/handle in `LiveReq` when TCP, TLS, or QUIC opens
the logical link.  Pool cancellation indexes that slot directly and validates
that the link still owns the same `LiveReq` before closing it.

When a multiplexed client link or TLS pair is swap-removed, update both the
moved link's slot and its owning `LiveReq` handle.  Invalidate the handle on
stop, completion, retry, redirect, and transport change.  Do not introduce a
heap-allocating `ZmFn` cancellation callback when a slot plus transport tag is
sufficient.

Delete the `m_liveReqs`, `m_links`, and `m_pairs` cancellation scans.

### 6.3 Tests

Test queued versus active cancellation, every transport, cancellation during
discovery and connection establishment, pair/link swap removal, stale attempt
callbacks, retry and redirect generations, timeout storms, repeated cancel,
completion racing with cancel across Rx/Tx posts, and empty active index after
stop/finalization.

## Stage 7: Collapse QPACK Plan Searches

Replace `planned_()` and the subsequent backward name search with one reverse
pass over prior insertion plans.  The pass must determine both:

- whether the exact name/value pair is already planned; and
- the nearest prior insertion with the same name for relative name reference.

Do not append the new plan until the search result is known.  If an exact match
exists, emit no duplicate insertion.  Otherwise append once and use the saved
name-match position, falling back to the committed dynamic table exactly as
today.  Delete `planned_()` and the second scan.

Do not add a per-message hash unless baseline and post-change measurements
show that the single scan remains material for realistic maximum header lists
and that the hash improves tail latency after allocation cost.  Such a hash is
not part of this plan without that evidence.

Extend `ZhttpQPackDynamicTest` with no dynamic table, never-index fields,
static-name references, committed dynamic-name references, repeated planned
names, exact duplicates, capacity fallback/replan, and maximum accepted header
lists.  Verify emitted instructions and decoded headers, not implementation
counters alone.

## Stage 8: Consolidation and Final Verification

After all stages:

- search `zhttp/src` for the old scan/splice patterns named in `zhttp20.md` and
  confirm each hot-path occurrence is deleted;
- remove obsolete aliases, heap IDs, counters, cursors, sequence fields,
  membership flags, and helpers;
- inspect all new structs for member order, padding, ownership, intrusion, and
  template independence;
- assert every owner container, ready queue, active index, frame admission
  count, and outstanding callback is empty at finalization;
- run `make -C zhttp/src -j8`, then `make -C zhttp/test -j8` and
  `make -C zhttp/test test`;
- run affected `zhttp` integration and interop tests for H1, H2, H3, fallback,
  request errors, streamed bodies, cancellation, and shutdown;
- run the configured top-level `make -j8` because dependent `zws` and utilities
  consume the changed headers; and
- repeat the Stage 0 workload characterization at identical queue depths and
  concurrency.

The final performance record must show queue-depth-independent lookup/removal
for the repaired operations.  Iteration proportional to frames actually
transmitted or cancelled, responses actually sent, or objects actually torn
down remains legitimate.  No timing claim replaces correctness, saturation,
and lifecycle tests.

## Completion Checklist

- [ ] H2 global pending-frame array and scan state deleted.
- [ ] H2 frame removal is proportional only to that stream's owned frames.
- [ ] H2 fence and terminal completion attachment are direct.
- [ ] H1 blocked-response rotation deleted.
- [ ] H1 per-link FIFO and ready-link membership drain fairly.
- [ ] H3 physical-link lookup is keyed and down links are removed immediately.
- [ ] H3 active, pending, and waiting removals are direct.
- [ ] H2 and H3 FIFO admission performs no front-array splice.
- [ ] Active client request and pool membership cancellation are direct.
- [ ] QPACK planning uses one insertion-plan pass.
- [ ] Reset, failure, cancellation, stop, and finalization balance every node,
      callback, and admission credit.
- [ ] Focused unit, integration, interop, dependent, and top-level builds pass.
- [ ] Before/after depth and concurrency measurements are recorded.
