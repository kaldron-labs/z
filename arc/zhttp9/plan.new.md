## Summary

The goal is to harden `zquic` for the focused failing interop case first:
`curl` > `zhttpd`, H3/QUIC, `-j10 -n1000`, 20s timeout.  The immediate
engineering target is not a local workaround for that case; it is to make the
QUIC runtime obey the I/O sharding rules in `iosharding.md` so the focused case
and then the full `zhttp` interop matrix can run without wrong-thread stream,
packet, endpoint, recovery, or crypto state access.

This revised plan replaces the earlier layer-by-layer plan with vertical slices
that each produce one usable end-to-end path.  The first implementation slice
establishes a Tx-owned packet send path from stream publish through endpoint
send.  The next slice routes ACK/loss/PTO into the same Tx path.  Later slices
move remaining control, crypto/key-update, diagnostics, and tests into the same
ownership model.

Research inputs:

- `AGENTS.md`, `GUIDELINES.md`, `CODEBASE.md`, `iosharding.md`, and the current
  `plan.md`.
- `plan.feedback.md` was checked and does not exist.
- RFC 9002 confirms that sent-packet tracking and ACK processing are per packet
  number space, while RTT/congestion state is connection-wide; this supports a
  single Tx-owned recovery path that accepts ACK snapshots from Rx.
  Source: https://datatracker.ietf.org/doc/html/rfc9002
- The ngtcp2 programmer guide models timer expiry as an event that updates QUIC
  state and then schedules packet writing; this maps to `zquic` by having timer
  processing enqueue Tx-owned send work rather than directly sending from Rx.
  Source: https://nghttp2.org/ngtcp2/programmers-guide.html
- Local `../zngtcp2` examples keep application socket send activity as the
  packet-output side of the QUIC loop; that is aligned with making actual send
  and packetization Tx-owned in `zquic`.

No web finding changes the repository-specific rule: `ZmPQRx` is Rx-owned in
its entirety, `ZmPQTx` is Tx-owned in its entirety, and locks are not an
acceptable substitute for owner-thread execution.

## Architecture Documentation

### Ownership Model

Every I/O data member must be in exactly one of these groups:

- Shared immutable/config after open: stable values read by either shard after
  initialization, such as configured thread IDs and immutable peer/local
  addresses.  If a value can change during runtime, it is not shared immutable.
- Rx-owned: datagram receive, frame parsing, stream receive queues, received
  packet ACK tracking, pending ACK generation, peer-driven state transitions,
  link close/drain state, and callbacks that deliver received data to the app.
- Tx-owned: application stream send queues, stream packetization, connection
  send credit consumption, packet protection, packet number assignment, sent
  packet tracking, loss marking, retransmit queues, PTO timers that touch
  recovery state, and endpoint send queues.
- Diagnostics: per-shard counters by default; mixed reads use snapshot APIs or
  atomics only for counters that are intentionally shared.

`Stream` may continue to inherit both `ZmPQRx` and `ZmPQTx`, but Rx functions
must only use the `ZmPQRx` side and Rx-owned members, while Tx functions must
only use the `ZmPQTx` side and Tx-owned members.

### Thread Processes

Rx thread:

- Receives UDP datagrams from `ZiMultiplex`.
- Parses and decrypts packets using Rx crypto state.
- Records received packet numbers in `AckTracker`.
- Processes STREAM frames into `StreamRxPQueue`.
- Builds small fixed-size events for Tx when received frames affect Tx state:
  ACK frames, MAX_* credit frames, STOP_SENDING / RESET_STREAM consequences,
  PATH_CHALLENGE responses, and key updates.
- Requests Tx flush by posting a compact event to the Tx shard.

Tx thread:

- Accepts application stream payloads.
- Owns `ZmPQTx` queue mutation and stream packetization.
- Consumes connection and stream Tx credit.
- Builds control and STREAM frames from Tx-owned queues/snapshots.
- Protects packets, assigns packet numbers, records sent packet metadata, and
  submits datagrams to the endpoint.
- Processes ACK snapshots, updates RTT/PTO/loss state, immediately enqueues
  retransmittable frame references on loss, and sends retransmission probes.

### Interfaces And Data Flows

New or clarified internal interfaces:

- `rx..._()` suffix: function asserts `app()->rxInvoked()` and touches only
  Rx-owned members.
- `tx..._()` suffix: function asserts `app()->txInvoked()` and touches only
  Tx-owned members.
- Public methods callable from either shard are dispatchers only.  They capture
  data needed by the destination shard and immediately call `rxInvoke`/`rxRun`
  or `txInvoke`/`txRun`.
- Rx-to-Tx event structs are small, trivially movable data objects when bounded.
  Variable-sized data is copied into a destination-owned `ZiIOBuf` or queue node
  before the Tx handler consumes it.

Important existing APIs and behavior:

- `ZiMultiplex::rxRun`, `rxInvoke`, `txRun`, and `txInvoke` already dispatch to
  the configured isolated scheduler threads; use these for shard handoff.
- `ZiMultiplex::invoked(thread)` is already used by `App::rxInvoked()` and
  `App::txInvoked()`; assertions should use those wrappers where a `Link` is
  available.
- `ZmPQTx` defaults to `ZmNoLock`; any locking added through policy is a queue
  implementation detail, not permission for Rx to touch Tx-owned state.
- `ZmPQRx` contains its own queue state and callbacks; it remains wholly
  Rx-owned in `Stream`.
- `ZmScheduler::Timer` callbacks run on the thread passed to `run(...)`; PTO
  callbacks that touch Tx recovery state must be scheduled on `app()->txThread()`.
- `ZiConnection::send(...)` is already driven from the Zi Tx side; endpoint
  packet submission should hand off to Tx before touching endpoint send buffers.

### Event-Driven And Timer Processing

ACK processing:

- Rx parses ACK frames and validates packet number ranges are syntactically
  sane for the packet number space.
- Rx snapshots bounded ACK information and posts it to Tx.
- Tx validates against Tx packet state, marks packets acknowledged/lost, updates
  RTT/PTO, immediately enqueues retransmittable frame refs for loss, and drains
  retransmission work.

PTO processing:

- PTO timers that inspect `m_txPkts`, `m_rtt`, or `m_ptoBackoff` must fire on
  Tx.
- PTO expiry reclaims at least the intended probe count, schedules immediate
  retransmittable frame sends, and then arms the next Tx PTO timer.
- Rx can request that Tx re-evaluate PTO when an incoming datagram changes
  anti-amplification or address-validation state.

Control and ACK frame generation:

- Rx owns received packet tracking and pending ACK state.
- For small ACK data, Rx snapshots into a bounded event and posts to Tx.
- For large ACK data, Rx writes an ACK frame or compact ACK description into a
  Tx-owned buffer/event node and posts only the handle.
- Tx packetization appends ACK/control snapshots without reading `m_rxPkts` or
  `m_pendingAck` directly.

Network send:

- Endpoint send queue and active send buffer are Tx-owned.
- Endpoint receive callbacks and connection lifecycle callbacks remain Rx-owned.
- Endpoint `m_cxn` lifetime must be explicit: either split Rx/Tx handles or
  capture a `ZmRef<Cxn_>` from an owner-thread handoff.  A raw `m_cxn` read from
  arbitrary caller context is not acceptable as a final design.

No new data store is introduced.

## Detailed Design and Implementation Plan

### Phase 0: Freeze The Baseline And Remove Ambiguity

- Rebuild the current tree before implementing fixes:
  `make clean; ./z.config -c -d -D -L /usr; make -j8`.
- Record whether the tree builds as-is.  The current tree has provisional
  endpoint and recovery changes that are not yet verified after the latest plan
  edits.
- Run only the focused reproducer after a successful build:
  `curl` > `zhttpd`, H3/QUIC, `-j10 -n1000`, 20s timeout, retaining the temp
  directory and debug logs on failure.
- Do not run the full 27-case matrix until the focused case passes repeatedly.

Exit criteria:

- ASan build result is documented.
- Focused reproducer result is documented, including temp dir on failure.
- No behavior movement is made in this phase.

### Phase 1: Make Ownership Enforceable At Declaration Sites

- Regroup `Stream` members into `Shared identity`, `Rx-owned`, and `Tx-owned`.
  Current examples:
  - Shared identity: `m_link`, `m_id`.
  - Rx-owned: `m_rxCredit`, `m_rxState`, `m_rxDelivered`, `m_rxQueue`,
    reset/stop received state, Rx app error state.
  - Tx-owned: `m_txBytes`, `m_txBufferedBytes`, `m_txCredit`, `m_txQueue`,
    `m_fin`, `m_finDequeued`, reset/stop sent state, Tx app error state.
- Regroup `Link` members into `Shared immutable/config`, `Rx-owned`,
  `Tx-owned`, and `Diagnostics`.  The current declaration mixes these groups
  around `zquic/src/Zquic.hh:3401-3454`; split it before moving logic.
- Regroup `Endpoint` and `Endpoint::Cxn_` members.  The current comments are
  only a first pass; `m_cxn` is still read by `Endpoint::send()` outside a
  guaranteed Rx context and must be called out as transitional.
- Add assertions to internal shard-only functions.  Prefer `rx..._()` and
  `tx..._()` function names for functions that assume an owner thread.
- Keep this phase comments/assertions only except for compile fixes required by
  declarations.

Exit criteria:

- Ownership is auditable at data-member declarations.
- Build passes.
- Assertions expose wrong-thread call paths without changing behavior.

### Phase 2: Establish One Tx-Owned Stream-To-Endpoint Send Path

This phase is the first vertical implementation slice.  It handles application
payload publication, stream scheduling, packetization, protection, sent-packet
recording, and endpoint submission on Tx without requiring ACK/loss refactoring
to be complete.

- Make application `CliLink::send()` and `SrvLink::send()` dispatch to Tx, then
  keep all work on Tx:
  - copy bounded payload into `AsyncSendPayload`,
  - publish to `Stream::Tx` through `txStream_()`,
  - update `m_txBytes`, `m_txBufferedBytes`, `m_fin`, and Tx queue state,
  - enqueue stream ID on a Tx-owned stream scheduler,
  - call a Tx-only flush function.
- Replace `queueRxFlush_()` after application send with `queueTxFlush_()`.
  Current `send_()` paths post back to Rx and then call
  `flushControlAndStreams_()`; that is the central wrong-thread path to remove.
- Split stream scheduling so Tx never looks up scheduled IDs in the Rx-owned
  `m_streams` hash.  Use one of these final shapes:
  - Preferred: Tx scheduler stores `StreamRef` nodes, not stream IDs, so Tx owns
    the scheduling queue and can packetize without `findStream()`.
  - Acceptable if cheaper locally: keep stream IDs for diagnostics but store a
    Tx-owned `StreamRef` alongside the ID in the scheduler node.
- Keep `m_streams` Rx-owned for received-frame dispatch and stream creation.
  Stream creation must post or directly initialize the Tx side with a `StreamRef`
  before any Tx scheduling is possible.
- Refactor `StreamPktizer::writeNext()` so it is documented and asserted as a
  Tx-only utility.  It may call Tx-side stream methods such as `nextTxRange`,
  `commitTxRange`, `consumeTxCredit`, and `dequeueFin`.
- Create Tx-only wrappers for packet protection and send:
  - `txSendInitialPkt_()`
  - `txSendHandshakePkt_()`
  - `txSendShortPkt_()`
  - `txFlushStreams_()`
- In this phase, ACK/control appending can be temporarily absent from
  Tx-driven stream packets if needed to get the vertical stream send path
  correct.  Do not read Rx-owned `m_rxPkts` or `m_pendingAck` from Tx.
- Endpoint send:
  - `Endpoint::Cxn_::sendPkt()` remains Tx-only.
  - Replace arbitrary-thread `Endpoint::send()` raw `m_cxn` access with an
    explicit Tx-facing handle.  The simplest final shape is a Tx-owned
    `ZmRef<Cxn_>` set/cleared by Rx lifecycle handoffs; Tx send captures that
    ref before touching `Cxn_` send buffers.
  - Endpoint receive callbacks stay Rx-owned.

Implementation sketch:

```cpp
void txQueueFlush_()
{
  ZiAssert(app()->txInvoked(), "Zquic", (),
    "QUIC Tx flush outside Tx thread", return);
  // consume Tx-owned stream/control queues, protect packets, endpoint send
}
```

Exit criteria:

- Application stream payloads can be packetized and submitted without any Rx
  access to `ZmPQTx`, `m_streamScheduler`, Tx credit, packet numbers, or endpoint
  send queue.
- `StreamPktizer` is only called from Tx.
- Focused build passes.  Focused interop may still fail due to ACK/loss, but it
  must not fail because Tx stream data is flushed through Rx.

### Phase 3: Move ACK, Sent Packet, Loss, Retransmit, And PTO Fully To Tx

This phase completes the recovery path for the focused H3 concurrent case.

- Introduce bounded ACK snapshot events:

```cpp
struct AckEvt {
  CryptoLevel::T level;
  uint64_t delay;
  AckRanges ranges;
};
```

`AckRanges` must be a bounded local array or a Tx-owned buffer handle.  Do not
capture arbitrary variable-sized ACK frame memory into a lambda.  If ACK ranges
can exceed the builtin capacity, write a Tx event node/buffer and capture only a
`ZmRef` handle.

- Change `processAckFrame_()` into an Rx parser/dispatcher and a Tx handler:
  - `rxProcessAckFrame_()` validates and snapshots ACK data.
  - `txProcessAck_()` mutates `m_txPkts`, `m_rtt`, and `m_ptoBackoff`.
- Move `recordTxPkt_()`, `recordProtPktTx_()`, `schedulePTO_()`,
  `reclaimPTO_()`, `nextRetransmit_()`, and retransmit dequeue to Tx.
- PTO timers must be scheduled on `app()->txThread()`, because they inspect and
  mutate `m_txPkts`, `m_ptoBackoff`, and retransmit queues.
- Preserve the intended `ZiTx` pattern: when Tx marks frames lost, immediately
  enqueue retransmittable frame references.  The ref enqueue can happen in the
  ACK Tx handler; actual packet send also remains Tx.
- Refactor `CliLink::pto_()`, `SrvLink::pto_()`, `queueRetransmit_()`, and
  `retransmit_()` from Rx-only to Tx-only.
- Rebuild retransmit packets on Tx using retained `SentFrameRef` data.  Do not
  look up Rx-owned stream state while retransmitting stream frames.
- ACK frame inclusion during retransmit must use Tx-owned ACK snapshot/control
  events only; do not call `appendPendingAck_()` if it still reads Rx-owned
  `m_rxPkts` or `m_pendingAck`.

Exit criteria:

- `m_txPkts`, `m_txPN`, `m_rtt`, `m_ptoBackoff`, and `m_ptoTimer` are Tx-owned
  and only touched on Tx.
- Loss-triggered retransmittable frames are enqueued immediately and sent on Tx.
- Focused H3 concurrent case passes repeatedly or produces a new non-ownership
  failure with logs.

### Phase 4: Split Rx ACK/Control Preparation From Tx Packetization

This phase replaces the remaining mixed `flushControlAndStreams_()` design.

- Delete the mixed-owner role of `flushControlAndStreams_()`:
  - Rx prepares ACK/control events.
  - Tx packetizes control and streams from Tx-owned queues/events.
- Move `m_controlQueue` ownership decision out of ambiguity:
  - Flow-control updates caused by Rx state are Rx-generated events posted to
    Tx.
  - Tx owns the queue that is actually consumed during packetization.
- `appendPendingAck_()` must not read `m_rxPkts` from Tx.  Replace it with:
  - Rx-owned `rxBuildAckEvt_()` or `rxSnapshotAck_()`.
  - Tx-owned `txAppendAckEvt_()` that appends already-snapshotted ACK data.
- Preserve fixed-size control snapshots for `MaxData`, `MaxStreamData`,
  `MaxStreams`, `DataBlocked`, `StreamDataBlocked`, `StreamsBlocked`,
  `HandshakeDone`, and `PathResponse`.
- For variable-size future control data, allocate a destination-owned buffer and
  capture only the handle.
- Remove `withTxLock_()` from stream/control flush paths once all callers are
  Tx-only.

Exit criteria:

- No Rx function calls `Stream::txRangeCount()`, `finReady()`, `nextTxRange()`,
  `commitTxRange()`, `consumeTxCredit()`, or `dequeueFin()`.
- No Tx function reads `m_rxPkts` or `m_pendingAck` directly.
- Focused H3 concurrent case passes repeatedly.

### Phase 5: Split Crypto And Key-Update Ownership

- Split crypto state by direction:
  - Rx-owned: packet decryption state, Rx traffic secrets, received CRYPTO
    reassembly, received key phase handling.
  - Tx-owned: packet protection state, Tx traffic secrets, Tx packet number/key
    phase, outgoing CRYPTO flights.
- Replace `installPeerKeyUpdate_()` locking with a handoff:
  - Rx validates the incoming key phase and derives/validates the next Rx
    secret.
  - Tx receives a small event or destination-owned secret object, derives the
    next Tx secret, updates Tx protection state, and flips `m_txKeyPhase`.
- Review `advanceTLS_()` and `sendCryptoFlights_()` because they currently mix
  TLS input handling with packet emission.  Final shape:
  - Rx handles TLS input and produces crypto flight bytes/events.
  - Tx owns packetization/protection/sent recording of those flights.
- Initial and Handshake packet sends must follow the same Tx packetization
  rules as 1-RTT packets.
- Remove `m_txLock` from crypto/key state after all Tx crypto mutation is
  Tx-only.

Exit criteria:

- No `ZmGuard<ZmLock> guard(m_txLock)` remains in packet protection or key
  update paths.
- Packet protection, packet number assignment, sent recording, and endpoint
  submit are one Tx-owned path for all encryption levels.

### Phase 6: Endpoint Lifecycle And Shutdown Drain

- Align endpoint lifetime with the raw back-pointer principle:
  - queued buffers may use raw endpoint/link back-pointers if the owner drains
    all queues before destruction;
  - do not add buffer-level `Link` reference churn solely to keep links alive.
- Make `Endpoint::closeUDP()` a multi-step owner-thread operation:
  - Rx stops ingress and clears receive callbacks.
  - Tx drains/cancels endpoint send buffers and queue.
  - Rx clears connection handles after Tx has no live send work.
- Ensure `ZiMultiplex` 3-phase stop and `ZiConnection` close paths do not leave
  live Tx lambdas with stale raw endpoint/link pointers.
- Convert any required cross-shard teardown to explicit `rxInvoke`/`txInvoke`
  ordering.

Exit criteria:

- No queued endpoint send work can dereference a destructed endpoint/link.
- The design uses owner drain/cancel, not blanket refcounting of every
  dependent buffer.

### Phase 7: Diagnostics And Debug Logging

- Split `RuntimeDiag` and `EndpointDiag` counters into Rx and Tx groups, or make
  individual mixed counters atomic only where snapshot handoff would be worse.
- Add snapshot functions:
  - `rxDiagSnapshot_()`
  - `txDiagSnapshot_()`
  - public combined snapshot that invokes owners as needed.
- Keep `ZiMultiplex` debug output usable with `ZiLog` level debug.  Debug
  options already added to `zhttpd` and mirrored in `zhttp` should be kept.
- Add ownership-oriented debug messages only at state transition points:
  endpoint ready/down, Tx queue drain, ACK snapshot posted, ACK processed on Tx,
  PTO armed/fired, retransmit queued/sent, stream scheduled/unscheduled.
- Avoid logging in hot packet loops unless guarded by debug checks.

Exit criteria:

- Diagnostics do not require foreign-shard live reads.
- Focused failure logs show enough endpoint, packet, stream, ACK/loss/PTO, and
  retransmit activity to identify stalls.

### Phase 8: Unit Tests And Focused Interop

- Update tests that inspect stream queues to inspect on the owning shard or
  through snapshot helpers.
- Add focused tests for:
  - `Stream` Rx-only methods do not touch `ZmPQTx`.
  - `Stream` Tx-only methods do not touch `ZmPQRx`.
  - endpoint send dispatch and drain notification are Tx-owned.
  - ACK snapshot events are processed on Tx.
  - loss immediately enqueues retransmittable frame references.
  - PTO timer fires on Tx and sends probes on Tx.
- Run focused interop repeatedly:
  `curl` > `zhttpd`, H3/QUIC, `-j10 -n1000`, 20s timeout.
- Retain temp dirs and debug logs for every failure until stable.

Exit criteria:

- ASan build passes.
- Focused H3 concurrent case passes repeatedly under 20s.
- No ASan heap corruption or use-after-free in the focused case.

### Phase 9: Expand To `curl` > `zhttpd` Matrix

Run the 9 `curl` > `zhttpd` combinations:

- Workloads:
  - `-j1 -n1`
  - `-j1 -n1000`
  - `-j10 -n1000`
- Protocols:
  - H1/TCP: `http:` URL.
  - H1/TLS: `https:` URL with curl option that disables HTTP/3/QUIC and uses
    HTTP/1.1 as appropriate for the installed curl.
  - H3/QUIC: `https:` URL with curl HTTP/3 forcing option as appropriate for
    the installed curl.

Each case must complete within 20s.

Exit criteria:

- All 9 cases pass under 20s.
- Debug logging remains quiet enough for passing runs and detailed enough for
  failing runs when `--debug` is enabled.

### Phase 10: Full 27-Case Interop Matrix And Cleanup

Run the full automated interop suite:

- Client/server combinations:
  - `zhttp` > `caddy`
  - `zhttp` > `zhttpd`
  - `curl` > `zhttpd`
- Workloads:
  - `-j1 -n1`
  - `-j1 -n1000`
  - `-j10 -n1000`
- Protocols:
  - H1/TCP: `http:` URL.
  - H1/TLS: `https:` URL.
    - `zhttp`: `--http3=disable`.
    - `curl`: force HTTP/1.1 / disable HTTP/3 as appropriate.
  - H3/QUIC: `https:` URL.
    - `zhttp`: `--http3=force`.
    - `curl`: force HTTP/3 as appropriate.

Cleanup:

- Remove obsolete lock-based helpers such as `withTxLock_()` after all callers
  are gone.
- Remove stale comments implying mixed ownership is acceptable.
- Update `iosharding.md` if implementation clarifies any ownership decisions.
- Keep ASan suppressions limited to known intentional Z Framework mutability or
  type-punning false positives; do not suppress real heap corruption.

Exit criteria:

- All 27 interop cases pass under 20s.
- No lock remains as a substitute for owner-thread execution.
- Ownership comments match the code.

## Code References to Impacted Code

- `zquic/src/Zquic.hh:797` - `App::rxRun`/`rxInvoke`; use these as Rx handoff
  primitives.
- `zquic/src/Zquic.hh:806` - `App::txRun`/`txInvoke`; use these as Tx handoff
  primitives.
- `zquic/src/Zquic.hh:1258` - `Stream` inherits both `ZmPQRx` and `ZmPQTx`;
  split methods and members by owner.
- `zquic/src/Zquic.hh:1380` - `Stream::nextTxRange()` currently exposes Tx
  queue inspection; keep Tx-only.
- `zquic/src/Zquic.hh:1456` - `Stream::txStream_()` already asserts Tx
  invocation; build on this pattern.
- `zquic/src/Zquic.hh:1626` - `Stream::send()` posts to Tx; preserve thin
  dispatcher shape.
- `zquic/src/Zquic.hh:1633` - `Stream::send_()` mutates `ZmPQTx` and Tx byte
  counters; keep Tx-only.
- `zquic/src/Zquic.hh:1688` - `m_rxQueue` / `m_txQueue` must be separated in
  member comments.
- `zquic/src/Zquic.hh:2300` - `nextWritableStream_()` currently uses stream ID
  lookup into `m_streams`; replace with Tx-owned `StreamRef` scheduling.
- `zquic/src/Zquic.hh:2376` - `flushControlAndStreams_()` is the mixed-owner
  flush point to split.
- `zquic/src/Zquic.hh:2428` - `withTxLock_()` is transitional and should be
  removed after Tx ownership is enforced.
- `zquic/src/Zquic.hh:2434` - `sendQueuedStreamPkt_()` packetizes streams and
  consumes Tx credit; make Tx-only.
- `zquic/src/Zquic.hh:2497` - `noteAck_()` / pending ACK state is Rx-owned.
- `zquic/src/Zquic.hh:2502` - `appendPendingAck_()` reads Rx ACK tracker; split
  into Rx snapshot and Tx append.
- `zquic/src/Zquic.hh:2511` - `schedulePTO_()` currently schedules on Rx while
  reading Tx recovery state; move to Tx.
- `zquic/src/Zquic.hh:2522` - `reclaimPTO_()` mutates Tx recovery; Tx-only.
- `zquic/src/Zquic.hh:2531` - `nextRetransmit_()` mutates Tx recovery queue;
  Tx-only.
- `zquic/src/Zquic.hh:2590` - `processAckFrame_()` currently mutates
  `m_txPkts` from frame processing; split Rx snapshot from Tx ACK handling.
- `zquic/src/Zquic.hh:2710` - `sendProtInitialPkt_()` protects packets and
  records Tx state under `m_txLock`; make Tx-only and remove lock.
- `zquic/src/Zquic.hh:2755` - `sendProtHandshakePkt_()` same.
- `zquic/src/Zquic.hh:2796` - `sendProtShortPkt_()` same.
- `zquic/src/Zquic.hh:3401` - `m_streamScheduler`, `m_controlQueue`, and
  runtime state declarations need ownership grouping.
- `zquic/src/Zquic.hh:3445` - `m_txPN`, `m_txPkts`, `m_rtt`, `m_ptoBackoff`,
  `m_ptoTimer`, and `m_txKeyPhase` are Tx-owned.
- `zquic/src/Zquic.hh:3552` - client `send_()` currently uses `withTxLock_()`
  and posts `queueRxFlush_()`; change to Tx flush.
- `zquic/src/Zquic.hh:3570` - client `pto_()` currently asserts Rx; make
  Tx-only.
- `zquic/src/Zquic.hh:3586` - client `retransmit_()` currently asserts Rx; make
  Tx-only.
- `zquic/src/Zquic.hh:3770` - client `txDrained_()` currently calls Rx flush;
  replace with the correct owner handoff after endpoint drain.
- `zquic/src/Zquic.hh:4146` - server `send_()` has the same Tx flush issue.
- `zquic/src/Zquic.hh:4183` - server `pto_()` currently asserts Rx; make
  Tx-only.
- `zquic/src/Zquic.hh:4198` - server `retransmit_()` currently asserts Rx; make
  Tx-only.
- `zquic/src/ZquicEndpoint.cc:57` - `Endpoint::Cxn_::sendPkt()` is the Tx-only
  endpoint send entry.
- `zquic/src/ZquicEndpoint.cc:225` - `Endpoint::send()` currently reads
  `m_cxn` outside a guaranteed owner context; replace with explicit Tx-facing
  connection handle or owner-thread handoff.
- `zquic/src/ZquicEndpoint.hh:93` - endpoint `m_cxn` ownership/lifetime must be
  resolved, not just commented.
- `zquic/src/ZquicSched.hh:213` - `StreamPktizer::writeNext()` calls Tx queue
  methods; assert/document Tx-only.
- `zi/src/ZiMultiplex.hh:873` - existing `ZiMultiplex` Rx/Tx run/invoke API.
- `zm/src/ZmPQueue.hh:1738` - `ZmPQRx` queue implementation; Rx-owned in
  `Stream`.
- `zm/src/ZmPQueue.hh:1967` - `ZmPQTx` queue implementation; Tx-owned in
  `Stream`.

## Detailed Test Plan

- Build:
  - `make clean`
  - `./z.config -c -d -D -L /usr`
  - `make -j8`
- Focused interop:
  - Start `zhttpd` with `--debug` and ZiLog debug enabled.
  - Run `curl` H3/QUIC against it with `-j10 -n1000` equivalent workload and a
    20s timeout through the existing interop harness.
  - Repeat after Phases 3, 4, and 8 until stable.
- Unit/focused tests to add or update:
  - Stream Tx publication and packetization executes on Tx.
  - Stream Rx receive processing executes on Rx.
  - Endpoint send queue mutation executes on Tx.
  - ACK snapshot posted from Rx is consumed on Tx.
  - Loss ACK path immediately queues retransmittable frame refs.
  - PTO timer callback runs on Tx and queues Tx send work.
  - Key update modifies Rx and Tx secrets only on their owning shards.
- Matrix progression:
  - Focused H3 concurrent case.
  - `curl` > `zhttpd` 9-case matrix.
  - Full 27-case matrix.

Every automated interop case must complete within 20s.

## Acceptance Criteria

- The focused `curl` > `zhttpd`, H3/QUIC, `-j10 -n1000` case passes
  repeatedly within 20s.
- The `curl` > `zhttpd` 9-case matrix passes within 20s per case.
- The full 27-case interop suite passes within 20s per case.
- No Rx function accesses `ZmPQTx`, Tx stream queues, Tx packet tracking, Tx
  packet numbers, Tx PTO state, or endpoint send buffers.
- No Tx function accesses `ZmPQRx`, Rx stream queues, received packet ACK
  tracker, or live pending ACK state.
- `m_txLock` and `withTxLock_()` are removed from the final packetization,
  recovery, and key-update paths.
- Debug logging remains available via `--debug` on both `zhttpd` and `zhttp`
  and gives enough packet/stream/recovery information to diagnose stalls.
- ASan suppressions cover only known intentional Z false positives, not real
  heap corruption.

## Non-goals

- Retaining compatibility with transitional mixed-owner internal APIs.
- Solving every QUIC performance tuning issue before the focused correctness
  issue is stable.
- Adding locks to allow foreign-shard access.
- Adding per-buffer `Link` refcount churn unless a specific lifetime audit shows
  it is necessary.
- Running the full 27-case matrix before the focused H3 concurrent failure is
  stable.

## Options and Open Questions

No blocking open questions remain from `plan.md`.

Resolved decisions:

- `PktTxSpace` is Tx-owned in its entirety.
- `ZmPQTx` is Tx-owned in its entirety.
- `ZmPQRx` is Rx-owned in its entirety.
- Actual send is Tx-owned even when retransmission control is triggered by
  receive-side information.
- Loss processing should immediately enqueue retransmittable frames, matching
  the intended `ZiTx` pattern.
- Small fixed-size ACK/control information may be captured by value; large or
  variable-size information must be written into a destination-owned buffer or
  queue node and passed by handle.
- Endpoint send queue is Tx-owned; endpoint receive and lifecycle callbacks are
  Rx-owned.

Implementation options retained for local judgment:

- Tx stream scheduler node shape:
  - Preferred: scheduler stores `StreamRef`.
  - Alternative: scheduler stores ID plus `StreamRef` to preserve existing
    diagnostics.
- ACK snapshot storage:
  - Preferred for normal ACK frames: bounded builtin range array.
  - Fallback for larger frames: Tx-owned event node or `ZiIOBuf` holding encoded
    ACK data.
- Endpoint connection lifetime:
  - Preferred: explicit Tx-facing `ZmRef<Cxn_>` set/cleared by owner-thread
    handoff.
  - Alternative: split Rx and Tx connection handles if the current
    `ZiConnection` lifetime cannot support the single-ref handoff cleanly.
