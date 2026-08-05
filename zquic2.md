# Plan: remove zquic hot-path linear scans

This plan repairs every finding in [`zquic.md`](zquic.md). The work is ordered
so that active recovery state is separated from retained packet history first,
then the same indexed-range machinery is reused for retransmission and stream
ACK bookkeeping.

Status: implemented and verified on 2026-08-05. Resolution details and the
classification of remaining packet traversals are recorded in
[`zquic.md`](zquic.md#resolution).

## Goals and constraints

- No workload-proportional sent-packet traversal while sending a packet,
  arming PTO, or arming the loss timer.
- No full sent-packet traversal to decide whether a frame or byte range is
  already outstanding.
- Loss processing remains bounded per scheduler turn without repeatedly
  visiting final packets.
- The common one-range stream path remains allocation-free.
- Rx- and Tx-owned state remains on its current shard; no locks or new atomics.
- Retained lost-packet history remains available where required, but is not
  used as the active recovery index.
- All new dynamic nodes use named `ZmHeap`-backed Z containers.
- Containers and objects confined to the owning Tx thread use non-atomic
  reference counting (`ZuObject` / `ZuRef`). Do not introduce `ZmObject` or
  atomic reference-count churn unless an object genuinely crosses shards.
- Protocol-bounded parsing loops and cold lifecycle traversals are unchanged.

## Phase 1: establish measurements and invariants

### Instrument operation counts

Add debug/test-only counters around the affected operations rather than using
wall-clock assertions in unit tests:

- sent-packet nodes visited while scheduling recovery;
- sent-packet/frame entries visited while suppressing retransmission;
- packet-threshold and time-threshold candidates examined;
- short-header CID hash probes; and
- unacknowledged-range representation promotions and range nodes visited.

Expose the counters through existing diagnostics or test fixtures only where
production telemetry is genuinely useful. Do not make accuracy-sensitive
cross-thread counters or add locking.

### Capture baseline cases

Extend the focused tests before changing behavior:

- `ZquicRecoveryTest`: more than 256 retained lost packets followed by a live
  eligible packet; sparse ACKs; repeated ACK batches; overlapping stream and
  CRYPTO retransmissions; FIN-only retransmission; and PTO reclaim.
- `ZquicCIDTest`: one active CID length, several active CID lengths, unknown
  short-header CID, tombstoned CID, route replacement, and route cleanup.
- `ZquicPQueueTest` and `ZquicStreamTest`: contiguous ranges, hundreds of
  fragmented ranges, split clears, complete clears, FIN ranges, and transition
  back to an empty set.

The retained-lost test must demonstrate the current timer blind spot and
become a regression test for the repaired behavior.

## Phase 2: separate active recovery state from packet history

Repairs findings 1 and 3.

### Add exact active-state accounting to `PktTxSpace`

In `ZquicRecovery.hh`, add Tx-thread-owned state:

- an exact count of ACK-eliciting packets currently in flight;
- a monotonic recovery frontier packet number; and
- any cached packet number/time needed to identify the earliest active loss
  candidate without rescanning retained history.

Update the count and frontier only at the packet lifecycle points:

- successful `add()`;
- `release_()` on ACK or loss;
- PTO reclaim when a packet ceases to suppress another transmission;
- `discard()`, `reject()`, packet-space discard, and `clear()`.

Keep the mutations centralized and add debug invariants that reconstruct the
count from `m_packets` only when assertions are enabled.

### Replace `ackElicitingInFlight()`

Make `ackElicitingInFlight()` an O(1) count check. This removes the scans from:

- `lossPending_()`;
- `ptoLevel_()`;
- Initial, Handshake, and application fallback-probe checks; and
- the per-packet `schedulePTO_()` / `scheduleLossTimer_()` sequence.

### Replace `nextLossTime()`

Use the recovery frontier to locate the oldest active ACK-eliciting packet
below `largestAckd`. Packet numbers and send times advance monotonically on the
Tx shard, so later active packets cannot have an earlier time-threshold
deadline. Skip final retained entries once and advance the frontier; do not
restart from packet number zero on later calls.

Remove the 256-entry truncation from timer selection. Work needed to discard
stale frontier entries is amortized over packet lifecycle, while loss marking
itself remains explicitly batched.

### Rewrite threshold loss batches around the frontier

For packet-threshold loss:

- compute the last eligible packet number without unsigned overflow;
- stop as soon as the ordered iterator passes that boundary; and
- never revisit packets already passed by the recovery frontier.

For time-threshold loss:

- start at the active frontier;
- stop at `largestAckd`;
- stop at the first active packet whose monotonic send time has not expired;
  and
- retain `RecoveryScanBatch` only as a scheduler fairness limit over actual
  candidates.

Preserve continuation state when a real candidate batch exceeds the limit.
Do not post another turn merely because retained lost history was encountered.

### Phase 2 acceptance

- Recovery scheduling performs no `m_packets` traversal after steady-state
  frontier cleanup.
- The retained-lost regression always arms the correct loss timer.
- Repeated ACKs do not revisit already-final packet numbers.
- Each loss-detection scheduler turn processes no more than
  `RecoveryScanBatch` active candidates.

## Phase 3: add indexed outstanding-frame coverage

Repairs finding 2 and supplies reusable range machinery for finding 5.

### Introduce an outstanding-frame index

Add a Tx-owned `OutstandingFrames` component to each `PktTxSpace`. It should
represent only packets that currently suppress retransmission: packets that
are not ACKed, lost, or PTO-reclaimed.

Use two representations:

- a reference-counted hash keyed by `SentFrameKey` for exact control frames;
  and
- counted disjoint intervals for STREAM and CRYPTO data, keyed by frame kind,
  packet-number space where applicable, and stream ID.

Track STREAM FIN separately by stream ID and final offset so FIN-only and
partially covered data-plus-FIN retransmissions retain current semantics.

The interval representation must support:

- add coverage in O(log R + K);
- remove coverage in O(log R + K);
- test exact control-frame presence in expected O(1); and
- subtract covered intervals from a retransmission candidate in
  O(log R + K), where `K` is the number of intersecting boundaries.

Use non-atomically reference-counted pooled intrusive nodes (`ZuObject` /
`ZuRef`) with a named heap because the index and its nodes are Tx-exclusive.
Do not allocate one index node per packet when adjacent equal-count intervals
can be coalesced.

### Couple the index to packet lifecycle

Update outstanding coverage at the same centralized transitions used by the
active recovery count:

- add every retransmittable frame after a sent packet is committed;
- remove its coverage before enqueueing frames after loss;
- remove it on ACK and discard;
- remove it before PTO reclaim queues replacement work; and
- clear it with the packet space.

Order matters: the packet being lost or reclaimed must be removed before its
frames are queried, otherwise it suppresses its own retransmission.

### Delete packet-store scans

Replace and then remove:

- `frameOutstanding_()`;
- `finOutstanding_()`; and
- the scan/restart implementation of `clipOutstanding_()`.

`enqueueRetransmit_()` should query the exact/range index. Retransmit dequeue
should subtract current indexed coverage once and enqueue at most the uncovered
head/tail ranges required by the result. It must never restart a traversal of
the sent-packet store.

### Debug verification

Provide a debug-only verifier that rebuilds expected outstanding coverage from
`m_packets` and compares it with the index. Call it at focused lifecycle
boundaries in tests, not on the production hot path.

### Phase 3 acceptance

- No outstanding-frame query iterates `m_packets`.
- Loss of a packet containing eight frames performs bounded index operations,
  independent of retained packet count.
- Overlapping original sends, retransmits, and PTO reclaims neither duplicate
  nor omit bytes or FIN.
- Clearing a packet space leaves no outstanding-index nodes.

## Phase 4: make stream unacknowledged ranges hybrid and indexed

Repairs finding 5.

### Preserve the allocation-free common case

Keep a small sorted builtin representation for up to
`TxUnackdRanges::BuiltinRanges` disjoint ranges. Its linear work is therefore
strictly bounded by the named builtin capacity.

### Promote fragmented streams

When an insertion or split would exceed the builtin capacity, promote once to
a Tx-exclusive, non-atomically reference-counted pooled intrusive interval tree
or `ZmPQueue`-based representation:

- migrate existing ranges in order;
- preserve `m_length`, FIN semantics, and coalescing;
- perform subsequent add, clear, and overlap operations in
  O(log R + K); and
- remain in indexed mode until `clear()` or stream reset, avoiding promotion
  and demotion churn.

Factor the interval split/coalesce primitives with the counted coverage index
from Phase 3 where that does not weaken either representation. The unacknowledged
set is boolean coverage; the outstanding retransmission index is counted
coverage, so keep their policies distinct even if they share node mechanics.

Maintain the existing `TxUnackdRanges` public surface so callers in
`ZquicStream.hh` and `ZquicLink.hh` do not acquire representation knowledge.

### Phase 4 acceptance

- One through eight ranges use no heap storage.
- Operations above the builtin threshold do not splice or shift a flat array.
- `count_()`, `length_()`, `spans()`, FIN handling, and clear/split behavior
  match the existing implementation for randomized operation sequences.
- All promoted nodes are returned to their named heap on stream teardown.

## Phase 5: index active connection-ID lengths

Repairs finding 4.

### Track installed route lengths

In `CxnRouter`, maintain an active-length mask plus per-length reference counts
for lengths `1..CxnIDMax`. This is a protocol-mandated fixed domain, not a
workload-sized lookup table.

Update the counts on:

- insertion of a new active route;
- replacement or reactivation where permitted;
- retirement and deletion;
- transition to tombstone; and
- `clear()` / `final()`.

Add debug verification against the route table after mutations.

### Probe only installed lengths

Change `matchShortRoute_()` to try only lengths present in the active-length
mask. Prefer the normal generated CID length first when it is known, then the
remaining installed lengths. Preserve longest-match behavior if mixed-length
CIDs can form prefixes of one another.

Refactor routing and stateless-reset lookup to share one match result so an
unknown datagram is not subjected to a second length walk. During this work,
verify whether tombstoned or retired routes can legitimately retain a reset
token; do not preserve a second scan that cannot produce a usable token.

### Phase 5 acceptance

- A server using the normal single CID length performs one route hash probe
  per short-header datagram.
- Mixed-length routing performs one probe per installed length, not 20.
- Unknown-CID handling does not repeat the same probe sequence.
- Add, retire, tombstone, clear, and prefix-collision tests preserve routing
  semantics.

## Phase 6: verification and performance review

### Correctness

Build before running tests, following repository rules:

```sh
make -C zquic/src -j8
make -C zquic/test -j8
make -C zquic/test test
```

Then run a top-level `make -j8` to catch dependent-module breakage. Run the
focused binaries directly when diagnosing failures:

```sh
./zquic/test/ZquicRecoveryTest
./zquic/test/ZquicCIDTest
./zquic/test/ZquicPQueueTest
./zquic/test/ZquicStreamTest
./zquic/test/ZquicRuntimeTest
```

Use `libtool exec` for debugger or profiler runs; do not execute `.libs`
binaries directly.

### Stress and randomized tests

- Compare the hybrid unacknowledged-range set with a simple test-only reference
  model over randomized add/clear/spans sequences.
- Compare the outstanding-frame index with a test-only reconstruction from
  packet history after randomized send/ACK/loss/PTO sequences.
- Exercise at least 1,024 retained lost packets and a substantially larger
  active packet-number sequence to prove work is based on active candidates,
  not retained history.
- Exercise CID prefix collisions across every installed length ordering.

### Performance gates

Use operation counts as hard acceptance gates:

- O(1) in-flight-state query;
- no history traversal during per-packet timer scheduling;
- no packet-history traversal during retransmission suppression;
- one short-header hash probe in the normal single-length configuration; and
- no unbounded flat-array shift above the builtin range threshold.

Add a focused benchmark harness if no existing runner can measure these paths.
Measure common-case and adversarial p50/p99 latency separately. The common
one-range stream send/ACK path must not regress materially; fragmented ACK,
heavy-loss recovery, and unknown-CID cases should show scaling independent of
retained packet history wherever the new indexes apply.

### Final cleanup

- Remove superseded scan helpers and temporary instrumentation not retained as
  useful diagnostics.
- Update comments that still describe scan budgets as the recovery strategy.
- Re-run `rg` over `zquic/src` for `m_packets` iterators and document every
  remaining traversal as cold-path, bounded test verification, or deliberately
  batched lifecycle work.
- Update [`zquic.md`](zquic.md) with resolution references once every
  acceptance criterion passes.

## Suggested implementation order and commits

1. Tests and operation-count instrumentation.
2. Active in-flight count, recovery frontier, and timer correctness fix.
3. Frontier-based packet/time-threshold loss processing.
4. Counted outstanding-frame interval index and removal of packet scans.
5. Hybrid `TxUnackdRanges` using the shared interval primitives.
6. CID-length index and single-pass unknown-CID handling.
7. Stress tests, benchmarks, dead-code removal, and documentation updates.

Keep each step buildable and testable. Do not combine the recovery frontier,
frame-coverage index, and range-container migration into one unreviewable
change.
