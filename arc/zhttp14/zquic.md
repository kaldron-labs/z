# zquic hot-path linear-scan audit

The audit found five avoidable linear-scan patterns on `zquic` runtime paths.

## 1. High: packet transmission repeatedly scans sent-packet state

[`recordTxPkt_()`](zquic/src/ZquicLink.hh#L6140) calls both `schedulePTO_()`
and `scheduleLossTimer_()` for every ACK-eliciting packet. Those paths reach:

- `ackElicitingInFlight()`, which walks `m_packets`;
- `nextLossTime()`, which examines up to 256 packets; and
- further `ackElicitingInFlight()` calls through PTO fallback checks.

Evidence:

- [`PktTxSpace::nextLossTime()`](zquic/src/ZquicRecovery.hh#L1327)
- [`PktTxSpace::ackElicitingInFlight()`](zquic/src/ZquicRecovery.hh#L1349)
- [`Link::nextLossTime_()`](zquic/src/ZquicLink.hh#L4729)
- [`Link::ptoLevel_()`](zquic/src/ZquicLink.hh#L8945)

This is particularly expensive after loss because as many as 1,024 lost
packets remain in the same container. The 256-entry cap also creates a
correctness hazard: `lossPending_()` can find an in-flight packet beyond
retained lost entries, while `nextLossTime()` sees none in its first 256
entries and cancels the loss timer.

Recommendation: maintain an ACK-eliciting in-flight count and incrementally
maintain the earliest eligible loss deadline. Lost-history retention should
not be on the traversal path for active recovery state.

## 2. High: retransmission suppression repeatedly scans all sent packets

When a packet becomes lost, every frame calls `frameOutstanding_()`, which
walks every retained packet and up to eight frames per packet:

- [`PktTxSpace::enqueueRetransmit_()`](zquic/src/ZquicRecovery.hh#L1500)
- [`PktTxSpace::frameOutstanding_()`](zquic/src/ZquicRecovery.hh#L1601)

When retransmission is dequeued, `clipOutstanding_()` performs the same
traversal and restarts from the beginning whenever it advances over an
overlapping range:

- [`PktTxSpace::clipOutstanding_()`](zquic/src/ZquicRecovery.hh#L1559)

This can become quadratic in outstanding packets or range fragments during
concentrated loss or PTO recovery.

Recommendation: maintain destination-specific outstanding indexes: stream ID
plus interval, CRYPTO interval, and exact control-frame key. Update these
indexes as packets enter and leave the sent-packet store. The existing
retransmit-queue hash only deduplicates queued frames, not frames still
referenced by packets.

## 3. Medium-high: every ACK initiates a packet-threshold loss scan

After processing acknowledged ranges, `processAckFrameTx_()` calls
`markPktThreshLossBatch()`:

- [`Link::processAckFrameTx_()`](zquic/src/ZquicLink.hh#L6568)
- [`PktTxSpace::markPktThreshLossBatch()`](zquic/src/ZquicRecovery.hh#L1257)

The scan begins at the start of `m_packets` and visits the whole container,
including already-lost entries and packets newer than the packet-threshold
boundary. Batching limits scheduler starvation but does not reduce total work;
successive ACKs repeatedly rescan retained history.

At minimum, the ordered traversal can stop once
`pn > largestAckd - threshold`. Preferably, maintain a cursor or separate
active recovery queue so already-final packets are not revisited.

## 4. Medium: short-header routing performs up to 20 hash probes per datagram

Because short headers do not encode CID length, `matchShortRoute_()` tries
every length from 20 down to 1:

- [`CxnRouter::matchShortRoute_()`](zquic/src/Zquic_.hh#L167)

This runs on the server receive path for every short-header packet. An unknown
CID is scanned again by stateless-reset lookup, resulting in up to 40 hash
probes per invalid datagram. The scan is bounded by the protocol maximum, but
it remains a substantial constant-factor cost and a packet-flood amplification
point.

Recommendation: track the small set of CID lengths actually issued or
installed, or use the configured fixed local CID length where applicable.

## 5. Medium-low: stream unacknowledged ranges use a fragmented flat array

`TxUnackdRanges::add()` and `clear()` binary-search the starting position, but
then scan overlapping ranges and splice the flat array:

- [`TxUnackdRanges::add()`](zquic/src/ZquicPQueue.hh#L214)
- [`TxUnackdRanges::clear()`](zquic/src/ZquicPQueue.hh#L248)
- [stream transmit range bookkeeping](zquic/src/ZquicStream.hh#L373)

These operations execute while recording transmitted stream frames and
processing ACKs. Out-of-order ACKs can create hundreds of fragments, making
each splice shift a workload-proportional tail.

Recommendation: use an intrusive interval or priority-queue representation if
fragmented workloads matter. At minimum, add fragmentation telemetry and a
benchmark before deciding whether the flat-array locality wins in production
traffic.

## Bounded or cold-path traversals not classified as findings

- ACK parsing and serialization are bounded at 64 ranges.
- Packet and frame byte walks are necessary parsing or cryptographic work.
- Full stream-table traversals are confined to diagnostics, shutdown, or
  one-time 0-RTT transitions.
- CID collection scans inside a link are bounded by negotiated or local CID
  limits.

## Resolution

All five findings are repaired by the implementation described in
[`zquic2.md`](zquic2.md).

1. `PktTxSpace` now keeps active ACK-eliciting packets in a Tx-exclusive
   `RecoveryPktList` (`ZmList`) separate from retained packet history.
   `linkRecovery_()` is an O(1) monotonic append, unlinking uses the packet's
   raw node back-pointer, `ackElicitingInFlight()` is an O(1) count query, and
   `nextLossTime()` reads the active head without the former 256-packet cap.
2. `OutstandingFrames` replaces sent-packet scans with an exact-frame hash and
   counted STREAM/CRYPTO interval trees. ACK, loss, discard, and PTO reclaim
   update the index at their lifecycle transitions. A debug verifier rebuilds
   the index from packet history for focused tests.
3. Packet- and time-threshold loss walk the active recovery list, stop at the
   ordered eligibility boundary, and apply the batch budget only to active
   candidates.
4. `CxnRouter` maintains per-length active-route counts and a length mask, so
   short-header matching probes only installed lengths. Endpoint routing
   carries the matched reset token through the same lookup, avoiding a second
   CID-length pass on an unrouted datagram.
5. `TxUnackdRanges` retains an allocation-free eight-range builtin form and
   promotes once to a named-heap `ZmPQueue` interval index for fragmented
   streams. The indexed form stays active until full clear.

The recovery list, outstanding flow objects, and promoted range nodes are
thread-exclusive and use non-atomic `ZuObject`/`ZuRef` ownership. The list
nodes and all other dynamic index nodes use named Z heaps.

Regression coverage is in `ZquicRecoveryTest`, `ZquicCIDTest`, and
`ZquicPQueueTest`: 1,024 retained lost packets, operation-count gates,
randomized outstanding lifecycle reconstruction, overlapping STREAM/CRYPTO
and FIN/PTO cases, all CID lengths and prefixes, 256 fragmented ranges, and a
4,096-operation bitmap-model comparison.

The remaining `m_packets` traversals in `ZquicRecovery.hh` are intentional:

- ACK processing visits only packet-number ranges supplied by the peer (the
  legacy `AckTracker` overload is test/API compatibility code);
- `reject()` is a packet-type lifecycle teardown;
- PTO reclaim is an event-driven newest-first selection capped by the probe
  limit (normally two packets), rather than a per-packet scheduling query;
- `persistentCongestion()` is a deliberately budgeted historical analysis;
  and
- the debug outstanding-index verifier scans history only when called by
  tests.
