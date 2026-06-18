# QUIC Recovery State GC Plan

## Goal

Bound the lifetime of QUIC recovery and routing state in `zquic/src` so normal
traffic, stream churn, CID churn, sparse packet numbers, and blocked Tx cannot
make connection-owned containers grow monotonically.

The frame-tracking part of this is to replace the monotonic `m_ackdFrames`
tombstone set with positive unackd-frame state.  `STREAM` frame tracking should
be per stream and backed by `ZmPQueue`, so the source of truth becomes "which
byte ranges remain unacknowledged" rather than "which previously sent frame keys
were acknowledged".

The broader plan also covers GC or bounding for sent packet history, closed
streams, CIDs/routes, ACK receive ranges, outbound control queues, and dependent
references released during stream/link teardown.

## Mandatory implementation guidelines
IMPORTANT: Read `GUIDELINES.md` fully and align with it.

## Current State

- Sent packet history is tracked by packet number in `PktTxSpace::m_packets`
  using `TxPktQueue`, a `ZmPQueue<TxPkt>`.
- Retransmission work is queued in `RetransmitQueue::m_frames`; duplicate
  queued frame refs are suppressed by `RetransmitQueue::m_pending`, a
  `SentFrameAckHash`.
- ACKd retransmittable frame refs are remembered in
  `PktTxSpace::m_ackdFrames`, also a `SentFrameAckHash`.
- `m_ackdFrames` is monotonic until packet-space `clear()`.  This is correct
  as a tombstone cache, but it is the wrong long-lived shape for high-volume
  stream data.
- `m_packets` also grows for the lifetime of a packet space because ACKd/lost
  packet nodes are marked but not retired.
- `m_streams` grows with stream churn; stream objects are added to the stream
  table but are not reaped once protocol- and application-side work is complete.
- CID route state has retired/tombstone states but no explicit bounded
  time-/ACK-based reclamation policy.
- Receive-side ACK range state in `AckTracker::m_packets` can grow with sparse
  packet-number gaps until the gaps fill.
- Pending outbound control work in `m_controlQueue` is drained on successful
  sends, but should be coalesced and bounded while Tx is blocked.

## Target Model

- Every long-lived recovery/routing container has an explicit owner, terminal
  condition, and GC trigger.
- Keep packet-number state in `PktTxSpace::m_packets`, but do not keep every
  packet node for the lifetime of the packet space.
- Replace ACKd tombstones for range-based frames with positive unackd range
  indexes:
  - one `ZmPQueue` per stream for `STREAM` data;
  - one `ZmPQueue` per crypto level/packet space for `CRYPTO` data.
- Keep non-range control frames in direct indexed pending/unackd control arrays
  unless a specific control frame has natural range semantics.
- A range absent from its unackd queue is no longer retransmittable.
- Loss/PTO retransmission should draw from unackd range state, not from an
  ever-growing ACKd-frame set.
- Closed stream objects should be reaped once no Tx/Rx/API/recovery references
  remain.  Keep compact stream-limit/final-offset state where needed; do not
  keep the full stream object just to remember that a stream existed.
- CID state should be bounded active/unused/retired state.  Retired routes and
  reset-token lookup entries should be held only while useful for in-flight
  packets or RETIRE_CONNECTION_ID/NEW_CONNECTION_ID accounting.
- ACK receive ranges should remain compact under normal reordering and bounded
  under adversarial packet-number gaps.
- Outbound control state should be represented as latest-value/coalesced pending
  work where possible, with explicit GC of stale entries.

## Reference Implementation Notes

- ngtcp2 removes a stream from `conn->strms`, removes it from the Tx priority
  queue if present, frees it, and returns the object to the allocator once both
  directions are shut and Tx data/FIN or reset is ACKd.  Its destination CIDs
  are fixed-size unused/bound/retired ring buffers; retired DCIDs are kept for
  `3*PTO` to catch packets on old paths.
  ACK receive tracking is capped at `NGTCP2_MAX_ACK_RANGES + 1`; inserting a
  new disjoint range past the cap drops the oldest range.  ACK frames sent by
  ngtcp2 are also tracked in a small ring, and when the peer ACKs a packet that
  carried an ACK frame, ngtcp2 removes the receive ranges covered by that ACK.
  ACKd sent packets are removed from the retransmission buffer during ACK
  processing.  Lost sent packets can remain briefly for spurious-loss handling,
  but stale lost entries are removed by both count and time (`3*PTO`-style)
  limits.
- MsQuic removes closed streams from the active stream table into a short
  `ClosedStreams` list, then drains that list by releasing the stream-set
  reference.  Destination CID count is bounded by the active CID limit; retired
  destination CIDs are sent as RETIRE_CONNECTION_ID work and capped for abuse.
  Source CIDs are removed from the lookup and freed on peer retirement or
  connection teardown.  Sent packet metadata is removed from `SentPackets` as
  ACKd and returned to the packet pool after ACK/loss/congestion callbacks.
  Lost packets move to a separate `LostPackets` list, which is purged after
  they are older than roughly `2*PTO` and below the largest ACKd packet.  ACK
  receive state is split into bounded ranges for duplicate detection and bounded
  ranges for packets still needing ACK; ACK-of-ACK processing raises the minimum
  packet number retained in the ACK-to-send range.  Outbound control work is
  primarily connection and stream send flags, not a FIFO of repeated frames.
- QUICHE moves closed streams from `stream_map_` to `closed_streams_`; streams
  waiting for ACKs become temporary zombies until ACK state clears.  A cleanup
  alarm clears the closed stream list.  Peer-issued CIDs are split into active,
  unused, and to-be-retired vectors bounded by active CID limit.  Self-issued
  retired CIDs are kept until a `3*PTO` alarm fires.  Its unackd packet map
  pops useless packets from the front after retransmittable frames, RTT/loss,
  and congestion uses have drained.  Receive ACK ranges are capped by
  `max_ack_ranges_`; the smallest intervals are trimmed, and
  `DontWaitForPacketsBefore()` removes ranges below the peer's ACK-of-ACK
  watermark.  Its control frame manager still has a queue, but it has a hard
  maximum, removes ACKd frames from the head, tracks lost retransmissions
  separately, and treats newer WINDOW_UPDATE frames as superseding older ones.
- mvfst records closed stream IDs in `closedStreams_` and reaps each stream once
  read callbacks, peek callbacks, and byte-event callbacks no longer need it.
  `removeClosedStream()` erases the stream from the main map and every scheduler
  set.  Peer CIDs are bounded by the active CID limit; retiring one queues a
  RETIRE_CONNECTION_ID frame and erases it from `peerConnectionIds`.  ACKd
  outstanding packets are erased after ACK visitors run.  Declared-lost packets
  are retained only briefly and cleared once older than PTO.  ACK receive ranges
  are an interval set; ACK-of-ACK handling withdraws acknowledged intervals and
  opportunistically purges old receive timestamp side data.  Pending outbound
  control state is structured as booleans, sets, and maps (`pendingEvents` and
  stream-manager sets); path responses are keyed per path and replaced rather
  than appended repeatedly.

## Reference-Derived Implications

- Positive unackd state is the right source of retransmission truth.  None of
  the references keep an ever-growing "ackd frame key" tombstone set for stream
  or crypto data.
- ACKd sent packet nodes should be removed immediately after ACK callbacks,
  frame ACK processing, bytes-in-flight updates, and compact metadata capture.
  Lost packet nodes may be retained only in a bounded spurious-loss window.
- ACK receive state needs two GC triggers:
  - hard range caps on insertion or encode, retaining the highest/newest ranges;
  - ACK-of-ACK low-watermark advancement when a packet carrying an ACK frame is
    itself ACKd by the peer.
- Outbound control state should be mostly latest-value slots, bits, and
  per-stream/per-path keyed sets.  A generic FIFO is acceptable only for controls
  whose values are truly distinct and still bounded.
- Routes and CIDs need explicit active/unused/retired/tombstone lifetimes.
  `3*PTO` is a common conservative retention window for retired local CID route
  lookup; peer-retired destination CID state should disappear after RETIRE work
  is ACKd or after bounded retry/close cleanup.

## Data Structures

### Stream Unackd Queue

Add a stream-local queue item for sent-but-unackd stream data:

- key: stream offset
- length: byte count
- metadata: stream ID is implicit from owning stream; FIN may need a sentinel
  or a separate final-size flag if FIN is carried on a zero-length or tail
  frame
- storage: `ZmPQueue<StreamUnackdRange, ...>`

The queue must support:

- adding newly sent ranges;
- subtracting ACKd subranges, including head/tail clipping and splitting a
  queued range when the ACK covers the middle;
- iterating ranges eligible for retransmission.

If existing `ZmPQueue` APIs do not provide subtract/split, add a narrow API to
`ZmPQueue` rather than encoding subtraction indirectly in QUIC recovery.

### Crypto Unackd Queue

Use the same interval model as streams, but one queue per crypto level instead
of per stream:

- Initial crypto stream;
- Handshake crypto stream;
- 1-RTT crypto stream.

CRYPTO ACK/loss handling should use crypto offset ranges and not stream IDs.

### Control Frames

Control frames are not generally byte ranges.  Keep them separate:

- flow-control updates and blocked frames can remain keyed by semantic
  `SentFrameKey`;
- RESET_STREAM and STOP_SENDING should be reviewed carefully because their
  retransmission semantics are stateful and stream-specific;
- PATH_RESPONSE, HANDSHAKE_DONE, and similar one-shot controls should stay out
  of stream byte-range queues.

Use a pending/unackd array directly indexed by control type.  The frame type
value should be the index, with a fixed offset only if the relevant frame type
range is not zero-indexed.  This avoids hashing for the small, fixed control
surface while still keeping control frames out of byte-range queues.

For controls that carry stream-specific state, the array entry can own a
secondary compact per-stream map or queue only where necessary.  Do not force
all controls into one generic hash.

### Sent Packet History GC

`PktTxSpace::m_packets` should not retain every sent packet indefinitely.  Once
unackd frame indexes become the source of truth for retransmission, packet
history can be garbage-collected independently.

Packet nodes may be removed only after they are no longer needed for:

- ACK idempotence;
- packet-threshold and time-threshold loss detection;
- persistent congestion detection;
- PTO candidate selection;
- bytes-in-flight and congestion accounting;
- late ACK handling;
- diagnostics and counters.

Start with a conservative retention rule:

- keep outstanding packets;
- keep lost packets only while useful for spurious-loss accounting and late ACK
  processing, with both count and time bounds;
- drop ACKd packet payload/frame-ref nodes after ACK processing releases
  bytes-in-flight and subtracts frame ranges from unackd state;
- if duplicate ACK idempotence, persistent-congestion bookkeeping, diagnostics,
  or packet-number history need ACK metadata, record that as separate compact
  metadata, not by retaining the full `SentPkt`;
- never keep packets solely to answer whether a stream/crypto frame was ACKd.

Add an explicit bounded `PktTxSpace` GC pass, e.g. `gcPackets()`, using the same
batching posture as ACK/loss scans.  Run it after ACK/loss batches, PTO reclaim,
packet-space discard, and retransmit queue drain points where more packet
history may become collectable.

Use separate retention classes so the code cannot accidentally preserve the
current monotonic behavior:

- outstanding: full packet node, frame refs, and congestion accounting live;
- ackd: full node is removed at the end of ACK processing;
- lost-retained: compact/full node retained only until the spurious-loss window
  expires or a count cap is hit;
- discarded: packet-number-space teardown or obsolete packet cleanup removes all
  remaining full nodes.

### Stream GC

`m_streams` should not retain every stream object for the full connection
lifetime.  Add an explicit stream reap path with the same ownership checks used
by the reference implementations:

- the stream is in terminal Rx state, or RESET_STREAM final size has been
  processed;
- the stream is in terminal Tx state, and STREAM FIN or RESET_STREAM has been
  ACKd or no longer requires retransmission;
- the per-stream unackd queue is empty;
- no stream control frame that requires the stream object remains pending;
- no Tx scheduler, writable queue, blocked queue, delivery callback, read/peek
  callback, or byte-event callback owns the stream;
- flow-control credit has already been returned and stream-limit updates have
  been scheduled if needed.

On reap:

- remove the stream from `m_streams`;
- remove it from stream Tx/Rx scheduler queues and any per-stream pending sets;
- release or cancel dependent receive buffers, pending delivery callbacks,
  retransmit references, control refs, and queued cross-thread posts that hold a
  stream reference;
- preserve only compact protocol state required to reject invalid future frames
  and maintain stream limits, preferably watermarks/ranges by stream type rather
  than a closed-stream object table;
- cancel or complete application callbacks before dropping the last reference.

The target shape is closest to QUICHE/mvfst: closed streams may remain briefly
as zombies while ACK/API work drains, but full stream objects should not be
monotonic.

For server links, teardown must also release dependent route and link-table
references.  `Server::m_links` should drop the link after timers, routes,
pending Tx/Rx posts, and route tombstones no longer require a strong reference.

### CID and Route GC

CID state should follow the active/unused/retired split used by the references:

- local/source CIDs: active in the router/lookup while valid for peer traffic;
  retired after a peer RETIRE_CONNECTION_ID frame; removed from route lookup once
  in-flight packets using the old CID can no longer arrive, using `3*PTO` as the
  conservative default;
- peer/destination CIDs: active or unused while available for paths; retired
  when `retire_prior_to` or path migration makes them obsolete; queue
  RETIRE_CONNECTION_ID for reliable transmission, then remove after the frame is
  ACKd or after bounded retry/connection-close cleanup;
- route tombstones: bounded by count and/or expiry.  They may be useful for
  stateless reset suppression, duplicate retirement, or late packets, but should
  not grow with every retired CID for the connection lifetime.

Add explicit GC to CID route state:

- expire retired local CID routes after `3*PTO` from retirement, unless a route
  is still required by a pending frame or validation path;
- cap retired/tombstone entries at a small multiple of active CID limit and close
  or drop excess according to QUIC error semantics;
- clear all CID routes on connection close/drain teardown;
- keep duplicate NEW_CONNECTION_ID/RETIRE_CONNECTION_ID idempotence as compact
  sequence-number/range state, not as full CID records.

### ACK Receive Range GC

`AckTracker::m_packets` already collapses the contiguous ACK prefix into
`m_ackHead`, but sparse received packet-number ranges above the head can still
grow with loss, reordering, or malicious gaps.  Add a bounded retention policy:

- retain ranges needed to emit accurate ACK frames for recently received packets;
- discard packet spaces wholesale when Initial/Handshake spaces are discarded;
- cap the number of retained sparse ranges per packet space;
- when the cap is hit, preferentially retain the newest/highest ranges and merge
  or drop older low ranges that are no longer useful for peer loss recovery;
- keep ECN and largest-received timing as compact metadata, not as extra packet
  range nodes;
- treat excessive disjoint ranges as a protocol/abuse signal if ACK generation
  can no longer represent useful state.

This is independent of sent-packet GC: it controls how much receive history is
kept solely to generate ACK frames.

Also record compact ACK-frame send metadata in `SentPkt` when an ACK frame is
emitted:

- packet number carrying the ACK frame;
- packet number space whose receive ranges were acknowledged;
- largest received packet covered by that ACK frame;
- ECN/timestamp side metadata only if needed for the existing ACK path.

When that sent packet is ACKd by the peer, advance an ACK receive low watermark
and drop/trim receive ranges below it.  This mirrors ngtcp2, MsQuic, QUICHE, and
mvfst ACK-of-ACK behavior and prevents retained receive ranges from depending
only on local sparse-gap caps.

### Outbound Control Queue GC

`m_controlQueue` should not grow with repeated flow-control, blocked, path, or
one-shot control events while Tx is blocked.  Replace or augment queue scanning
with coalesced pending state:

- MAX_DATA, MAX_STREAM_DATA, MAX_STREAMS, DATA_BLOCKED, STREAM_DATA_BLOCKED, and
  STREAMS_BLOCKED should retain only the latest still-valid value per semantic
  key;
- HANDSHAKE_DONE should be one pending bit until ACKd or no longer needed;
- PATH_RESPONSE should be bounded by the number of outstanding challenges that
  can be answered usefully;
- PATH_CHALLENGE should be tied to the active validation path and cleared on
  success, expiry, or path replacement;
- RESET_STREAM and STOP_SENDING should be keyed by stream and removed when the
  stream terminal state makes them obsolete or ACKd.

Flush should continue to drop stale controls before send, but stale-control GC
should not depend on getting a send opportunity.  Run a bounded cleanup pass when
control state is updated, when stream/CID/path state changes, and before
connection close/drain teardown.

For superseding controls, ACK/loss must be value-aware:

- ACK of an older MAX_* or BLOCKED value must not clear a newer pending or
  unackd value for the same key;
- loss of an older value must not resurrect it if a newer value was already sent
  or queued;
- one-shot frames such as HANDSHAKE_DONE need a pending/sent/ackd bit, not
  repeated queue entries;
- path validation controls should be keyed by path/challenge data and bounded by
  path validation state, not by packet retransmission history alone.

## Recovery Flow

### On Packet Send

For each recorded `SentFrameRef`:

1. Insert `STREAM` data into that stream's unackd range queue.
2. Insert `CRYPTO` data into the crypto-level unackd range queue.
3. Insert retransmittable control refs into the direct indexed control unackd
   array.
4. Continue recording the frame refs in `SentPkt` so packet ACK/loss accounting
   can update congestion and per-packet state.

The add path must be idempotent for retransmitted ranges.  Overlapping stream
or crypto data should merge or be ignored according to the unackd interval
semantics.

### On ACK

When `ack_(SentPkt &p)` processes a packet:

1. Release bytes-in-flight and record any compact ACK metadata needed for
   duplicate ACK, loss, congestion, or diagnostics.
2. For each frame in the packet:
   - subtract `STREAM` ranges from the owning stream unackd queue;
   - subtract `CRYPTO` ranges from the crypto-level queue;
   - clear ACKd control refs from the direct indexed control unackd array.
3. Do not add ACKd stream/crypto keys to a tombstone hash.

Late ACKs of already-lost packets must still subtract their frame ranges.  This
is what prevents stale queued retransmits from being emitted later.

After ACK processing, drop the packet payload/frame refs once any required
compact ACK metadata has been recorded.

### On Loss

When `lose_(SentPkt &p)` marks a packet lost:

1. Preserve the existing packet and congestion accounting.
2. For each frame in the lost packet, only enqueue retransmission work for the
   still-unackd portion:
   - consult the per-stream queue for `STREAM`;
   - consult the crypto queue for `CRYPTO`;
   - consult the direct indexed control unackd array for controls.
3. If an ACK already removed the range/key, do not enqueue it.

This replaces `frameAckd_()` checks for range-based frames.

After loss processing, mark the packet as GC-eligible once its retransmittable
frame work has been represented in the unackd indexes and/or direct indexed
control array.

### On Retransmit Pop

`nextRetransmit()` must validate popped work against current unackd state:

- skip a stream/crypto range if it has since been ACKd;
- clip or split popped retransmit work if it partially overlaps still-unackd
  state;
- skip control refs no longer present in the direct indexed control unackd
  array.

This preserves correctness when an ACK arrives after retransmit work has already
been queued.

### On PTO

`reclaimOnPTO()` should select retransmission work from currently unackd state,
not from packet tombstones.  Packet history can still determine recency and PTO
candidate priority, but the emitted work must be intersected with the unackd
range queues.

Once PTO selection no longer needs a packet, it should not prevent packet
history GC.

## Required `ZmPQueue` Work

Audit existing `ZmPQueue` APIs for range subtraction support.  If absent, add a
small operation with these properties:

- remove a `[key, end)` interval from the queue;
- clip head/tail overlaps in place;
- split a node when the removed interval lies inside it;
- return whether anything changed;
- preserve existing queue ordering, span/gap iteration, and heap behavior;
- work with `ZmPQueueOverwrite<false>` queues.

Add focused `zm/test` coverage for:

- exact interval removal;
- head clipping;
- tail clipping;
- middle split;
- removal spanning multiple queued nodes;
- no-op removal outside queued ranges.

## ZQUIC Integration Steps

1. Introduce stream/crypto unackd range item types and queue aliases.
2. Add stream-owned unackd range queues, likely near existing stream Tx state.
3. Add crypto-level unackd queues to the recovery/connection state.
4. Add helper APIs:
   - `recordUnackd(frame, level)`;
   - `ackUnackd(frame, level)`;
   - `stillUnackd(frame, level, callback)`;
   - `queueUnackdRetransmit(frame, level)`.
5. Replace `m_ackdFrames` uses for `STREAM` and `CRYPTO`.
6. Replace the generic control-frame keyed set with direct indexed
   pending/unackd control arrays, with per-stream secondary state only for
   controls that require it.
7. Add `PktTxSpace` packet-history GC with bounded scan cursors.
8. Add stream reaping for closed/zombie streams after Tx/Rx/API/recovery state
   drains.
9. Add bounded CID route GC for active/unused/retired/tombstone state.
10. Add ACK receive range bounds/GC to `AckTracker`.
11. Record ACK-frame send metadata in sent packets and apply ACK-of-ACK receive
    range trimming when those packets are ACKd.
12. Add outbound control queue coalescing and stale-control GC.
13. Remove `m_ackdFrames` once all callers use positive unackd state.
14. Revisit `SentFrameKey::retransmittable()` after control-frame handling is
   split from range-frame handling.

## Tests

Extend `zquic/test/ZquicRecoveryTest.cc` to cover:

- ACK removes a stream range from the per-stream unackd queue;
- partial ACK clips/splits an unackd stream range;
- loss retransmits only the remaining unackd subranges;
- late ACK of a lost packet suppresses already-queued stream retransmit work;
- duplicate retransmit ranges are not emitted for overlapping lost packets;
- independent streams with identical offsets do not collide;
- crypto range ACK/loss behaves like stream ranges but per crypto level;
- control-frame retransmission remains correct after stream/crypto split;
- sent packet history stops growing after ACKd/lost packets become
  collectable;
- packet GC preserves duplicate ACK idempotence;
- packet GC does not break late ACK of a previously lost packet;
- packet GC does not hide persistent congestion within the retained decision
  window;
- packet GC is budgeted and resumes correctly across batches;
- lost packet retention is bounded by count and timeout while preserving
  spurious-loss handling inside the retention window;
- closed streams are removed from `m_streams` after terminal Tx/Rx state and
  callback drainage;
- stream GC does not break late stream-control frames, flow-control credit
  return, or stream-limit updates;
- stream GC preserves protocol rejection for invalid frames on closed or
  never-opened streams using compact state;
- retired local CID routes expire after the configured `3*PTO` retention;
- peer CID retirement queues RETIRE_CONNECTION_ID and then removes retired CID
  state after ACK/bounded retry;
- route tombstones are bounded and do not grow with repeated CID churn;
- ACK receive ranges remain bounded under intentionally sparse packet numbers;
- ACK receive range GC still emits correct ACKs for recent received packets;
- ACK-of-ACK trimming drops old receive ranges once the peer ACKs a packet that
  carried the relevant ACK frame;
- ACK receive timestamp/ECN side metadata remains compact when old ranges are
  trimmed;
- outbound control queue coalesces repeated flow-control and blocked updates;
- ACK of an older superseded control value does not clear a newer value;
- loss of an older superseded control value does not requeue stale work;
- PATH_RESPONSE/PATH_CHALLENGE state is keyed and bounded by path validation
  state;
- stale outbound controls are removed when stream/path/CID state changes even if
  no packet is sent;
- server link teardown removes link-table refs after dependent timers, routes,
  posts, and tombstones release the link.

Keep existing tests around:

- duplicate-frame suppression;
- late ACK after loss;
- PTO reclaim;
- batch ACK/loss cursors;
- flow-control control frame retransmission.

## Review Risks

- FIN handling can be subtly different from byte-range handling, especially for
  zero-length FIN frames.
- Stream-level send buffers and recovery state must agree on ownership and
  lifetime; avoid reference-count churn from short-lived recovery objects.
- ACK subtraction must be correct for late ACKs of already-lost packets.
- `nextRetransmit()` must revalidate queued work to handle ACK/retransmit races
  within the Tx event flow.
- Packet history cleanup should separate compact ACK/loss metadata from full
  packet payload/frame refs; ACKd packets should not be kept just because some
  metadata may be useful.
- Packet GC must not recreate `m_ackdFrames` under another name; frame
  retransmission truth should remain in positive unackd indexes.
- Stream GC must not remove a stream while user callbacks or queued Tx/Rx
  scheduler entries still hold references.
- CID GC must not remove a local/source CID route before late packets for that
  CID are no longer expected; `3*PTO` is the initial conservative retention.
- ACK receive range GC must not hide useful ACK information too early and cause
  avoidable peer retransmission.
- ACK-of-ACK trimming requires accurate sent-packet metadata for ACK frames; if
  a packet carries ACKs for multiple packet spaces, the metadata must identify
  each affected `AckTracker`.
- Control queue coalescing must preserve retransmission semantics for one-shot
  controls and stream-specific RESET_STREAM/STOP_SENDING state.
- Superseded control handling must be value-aware so late ACK/loss callbacks
  for older frames cannot clear or resurrect newer state.
- Link teardown must not leave route tombstones, timers, or queued posts holding
  stale strong references.

## Acceptance Criteria

- Implementation is audited and aligned with `GUIDELINES.md`
- IMPORTANT: Do not stop until this plan is fully implemented and regression tested ok
  - `make -C zquic/test test` must `PASS`
  - `make -C zhttp/test test` must `PASS` (`zhttp` is a dependent user of `zquic`)
- `m_ackdFrames` is removed, and stream/crypto retransmission truth lives in
  positive unackd range state.
- `STREAM` unackd state is per stream and backed by `ZmPQueue`; CRYPTO uses
  the same interval model per crypto level/packet space.
- ACKing a stream or crypto range removes it from the unackd queue, and later
  loss/PTO/retransmit paths cannot emit that range again.
- ACKd sent packets release full packet/frame-ref storage after ACK processing;
  lost packets are retained only inside explicit count/time bounds.
- Closed streams are removed from `m_streams` once Tx/Rx/API/recovery references
  drain, while compact state still enforces protocol validity and stream limits.
- CID and route state is bounded: active/unused/retired/tombstone entries have
  explicit expiry, ACK, retry, or close cleanup paths.
- ACK receive range storage is bounded under sparse packet numbers and also
  trims old ranges via ACK-of-ACK low-watermarks.
- Outbound control state is coalesced or keyed by semantic owner; repeated
  flow-control, blocked, path, and one-shot controls cannot accumulate as an
  unbounded FIFO while Tx is blocked.
- Stream/link teardown releases dependent queues, timers, route refs,
  retransmit refs, callbacks, and queued cross-thread posts.
- The focused `zm` and `zquic` tests cover range subtraction, ACK/loss/PTO
  retransmission, packet GC, stream GC, CID/route GC, ACK receive range bounds,
  ACK-of-ACK trimming, control coalescing, and teardown cleanup.
- Stress tests or targeted counters demonstrate that the covered containers stop
  growing after their workload-specific retention windows under stream churn,
  CID churn, sparse receives, packet loss, and blocked Tx.
- Existing recovery behavior remains intact for duplicate-frame suppression,
  late ACK after loss, PTO reclaim, batch ACK/loss cursors, and flow-control
  control frame retransmission.

## Milestones

1. Add and test `ZmPQueue` interval subtraction if needed.
2. Implement per-stream unackd range queues for `STREAM`.
3. Implement crypto-level unackd range queues for `CRYPTO`.
4. Split control-frame tracking from range-frame tracking.
5. Add sent packet history GC for `PktTxSpace::m_packets`.
6. Add stream GC for terminal closed/zombie streams.
7. Add CID route/tombstone GC.
8. Add ACK receive range bounds/GC.
9. Add ACK-of-ACK receive range trimming.
10. Add outbound control queue coalescing/GC.
11. Remove `m_ackdFrames`.
12. Run `make -C zm/test -j8`, `make -C zquic/test -j8`, and the relevant
   recovery/runtime tests.
