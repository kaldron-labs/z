# ECN implementation plan

## Objective

Finish ECN as the remaining transport feature relevant to `zquic`'s intended
low-latency REST/API profile.

Do not add pacing, BBR, or CUBIC as part of this work.  Pacing deliberately
delays otherwise-sendable packets and is not aligned with the library's
latency-first target.  BBR is pacing-centric.  CUBIC mainly improves high-BDP
throughput rather than short request/response latency.

The implementation should mesh with the current recovery, path, socket, qlog,
and diagnostics code.  Prefer small extensions to existing structures over new
subsystems.

## Current State

- `Datagram` already carries an `EcnMark::T` for received datagrams.
- Receive-side ECN marks already flow through `noteAck_()`, `AckManager`,
  runtime diagnostics, ACK_ECN frame generation, and qlog receive fields.
- `validateAckECN_()` already validates monotonic peer ACK_ECN counters,
  disables ECN on impossible counters, updates `m_peerAckECN`, and emits
  `quic:ecn_state_updated`.
- `Path` currently has only `m_ecnDisabled`, initialized disabled.
- `Sock::initUDP()` handles PMTUD/error-queue socket options but not ECN receive
  ancillary data or outbound ECN marking.
- Sent packet qlog currently records `EcnMark::N` for protected packets because
  there is no outbound marking decision.
- `NewReno` has loss and persistent-congestion responses, but no CE signal path.

## Design Principles

- Preserve immediate-send behavior.  ECN must not introduce pacing, deliberate
  send delay, or timer-based probing.
- Re-use `AckManager`, `AckECN`, `m_peerAckECN`, existing qlog event types, and
  `NewReno` instead of adding parallel accounting paths.
- Keep ECN state path-local.  Avoid global endpoint state except for socket
  capability diagnostics.
- Keep ACK processing independent from ECN validation.  Invalid ACK_ECN disables
  ECN only; it must not reject the ACK frame or suppress loss/RTT processing.
- Avoid broad `ZiMultiplex` rewrites.  If ancillary data is needed, add a narrow
  UDP/QUIC extension point or wrapper around the existing endpoint send/receive
  path.
- Keep the first implementation ECT0-only.  ECT1 can remain decoded/validated
  because the frame format already supports it, but `zquic` should not emit ECT1
  until there is a concrete reason.

## GUIDELINES.md Audit

The ECN work should be reviewed against these repository constraints before
implementation and again before commit.

### API And Type Shape

- Do not use C++ concepts, `requires`, STL containers, or `enum class`.
- New enum-like state should follow Z style.  If it is transport-internal and
  does not need JSON mapping, use a small namespace enum:

  ```text
  namespace PathECNState { enum { Disabled, Testing, Capable, Failed, N }; }
  ```

  If it needs external diagnostics or JSON mapping, use `ZtEnum`/`ZtEnumMap`.
- Avoid compatibility shims beyond the deliberate `ecnDisabled()` and
  `setEcnDisabled()` wrappers needed to keep the change incremental.  Remove
  any dead boolean-only paths once dependent tests are updated.
- Use `switch` for ECN mark/state mapping, not chains of `if`.

### Storage And Allocation

- Do not add heap allocation in the packet send/receive hot path.
- Re-use existing fixed packet-number-space arrays where the bound is protocol
  defined (`PktNumSpace::N`).  These are acceptable fixed arrays because the
  size is a QUIC protocol constant and already used throughout `ZquicLink`.
- Do not add dynamically allocated ECN tables, hashes, or per-packet side
  records.  Extend existing sent-packet/update structures only with fixed-size
  scalar fields if the existing ACK traversal needs to return more information.
- Keep diagnostics as plain shard-owned counters.  Do not make them atomic or
  locked unless a caller needs precise cross-thread reads, which current runtime
  diagnostics do not.
- Keep `Datagram` as the receive metadata carrier.  Do not introduce a second
  heap object for ancillary data.

### I/O And Sharding

- Preserve Rx/Tx ownership:
  - Rx owns received datagram ECN, `noteAck_()`, and ACK snapshot production.
  - Tx owns outbound ECN marking, peer ACK_ECN validation, `m_peerAckECN`,
    `m_txECN`, `m_actedECN`, and congestion response.
- Cross-shard posts should capture only fixed-size values such as
  `EcnMark::T`, packet-number-space ids, counters, and `ZiSockAddr`; continue
  moving `ZiIOBuf` by reference.
- Do not broaden `ZiMultiplex` behavior for TCP or generic UDP users.  If
  ancillary metadata is required, add a narrow QUIC/UDP extension or wrapper
  that keeps ordinary I/O paths unchanged.
- Socket send/receive changes must preserve pooled `ZiIOBuf` ownership and
  avoid temporary contiguous copies.
- Timer-based ECN probing is out of scope.  Use ACK progress from already-sent
  packets rather than adding timers or sleeps.

### Logging, Qlog, And Diagnostics

- `ZiLOG`/qlog lambdas must capture by value only.  Do not capture pointers or
  references to link/path/socket state that may be consumed later on a logger
  thread.
- Prefer existing qlog events (`quic:ecn_state_updated`,
  `quic:congestion_state_updated`, packet/datagram ECN fields) over private new
  events.
- Keep qlog event construction on the existing call sites where packet metadata
  is already available.  Do not add a second logging pass over packet history.

### ACK Processing

- Do not add a new scan of sent packets solely for ECN.  Extend the existing
  ACK processing update path to return any CE-related metadata needed by
  `validateAckECN_()` or the congestion response.
- Container/iterator lifetimes should stay local to the existing ACK traversal.
  Finish traversal before qlog callbacks, cleanup, or cross-thread posts.
- Preserve ACK/loss/RTT processing if ECN validation fails.  ECN failure is a
  path feature fallback, not an ACK frame failure.

### Tests

- Use `ZuTestUtil`/`ZuTest` style tests.
- Use `ZmBlock` for concurrent runtime/socket tests; use `ZmSemaphore` only
  where it is already the better local fit.  Do not rely on sleeps or arbitrary
  timing intervals.
- Socket tests must tolerate platform unsupported cases through `SockDiag`
  rather than failing on missing kernel options.
- After code changes, verify with top-level `make -j8` before focused test
  targets.

## Data Model

### Path

Replace the boolean-only ECN path state with a small enum and compatibility
helpers:

```text
Disabled  - ECN intentionally unused for this path.
Testing   - outbound packets are marked ECT0 and awaiting ACK_ECN validation.
Capable   - ACK_ECN validation proved ECN works on this path.
Failed    - ECN was attempted and disabled due to missing/invalid feedback.
```

Keep `ecnDisabled()` and `setEcnDisabled()` as compatibility wrappers over the
new state so existing tests and call sites continue to compile during the
transition.

Add minimal path methods:

```text
ecnState()
ecnTesting()
ecnCapable()
startECNTest()
markECNCapable()
failECN()
txECNMark()
```

`txECNMark()` returns `ECT0` only in `Testing` or `Capable`; otherwise it returns
`NotECT`.

Do not put packet-number-space ACK_ECN counters in `Path`.  Those already live
in `ZquicLink` as `m_peerAckECN[PktNumSpace::N]`, and the existing validation
logic is link/packet-space aware.  Keep that shape.

### Link

Add small transmit-side ECN counters beside `m_peerAckECN`:

```text
AckECN m_txECN[PktNumSpace::N];       // locally sent ECT0/ECT1/CE counts
AckECN m_actedECN[PktNumSpace::N];    // peer CE counts already applied to cwnd
```

Only `ect0` should grow initially.  `ect1` and `ce` stay useful for generic
counter arithmetic and tests.

Update these counters when a protected packet is successfully recorded for
transmission, using the same point that already updates packet number,
bytes-in-flight, qlog, and PTO state: `recordProtPktTx_()`.

Reset both arrays in the same runtime reset path that already resets
`m_peerAckECN`, so no long-lived stale state survives connection reuse.

### NewReno

Add a congestion-signal method that reuses the existing loss response math but
does not release bytes-in-flight:

```text
bool congestionAt(uint64_t sentTime)
```

The method should:

- no-op if `sentTime <= recoveryStartTime()`;
- set `recoveryStartTime = sentTime`;
- set `ssthresh = max(cwnd / 2, 2 * maxDatagram)`;
- set `cwnd = ssthresh`;
- return whether the window changed.

Do not call `lostAt()` for CE.  CE is congestion feedback, not packet loss, and
must not subtract bytes from `bytesInFlight`.

## Socket Integration

### Receive Metadata

Extend `SockConfig`, `SockPlan`, and `SockDiag` with ECN receive capability:

```text
SockConfig::ecn = true
SockPlan::ecnRecv
SockDiag::ecnRecvAttempted / ecnRecvApplied / ecnRecvUnsupported / ecnRecvErrors
```

In `Sock::initUDP()` enable receive ancillary data where available:

- IPv4 Linux: `IP_RECVTOS`
- IPv6 Linux: `IPV6_RECVTCLASS`
- Windows/MSYS2: guarded support only where an equivalent exists; otherwise
  count as unsupported.

Decode the received TOS / traffic-class low two bits into `EcnMark::T`:

```text
0b00 -> NotECT
0b10 -> ECT0
0b01 -> ECT1
0b11 -> CE
```

Use the existing `Datagram::ecn` field.  Do not add a second receive metadata
object.

If `ZiMultiplex` cannot expose control messages from its current UDP receive
path, add the smallest QUIC-specific UDP receive extension that returns
`ZiIOBuf`, `ZiSockAddr`, and `EcnMark::T`.  Do not disturb TCP or ordinary UDP
users.

### Outbound Marking

Thread an optional `EcnMark::T` through the QUIC endpoint send path, defaulting
to `NotECT`.

Prefer per-datagram control messages:

- IPv4: `IP_TOS`
- IPv6: `IPV6_TCLASS`

Avoid changing socket-global TOS per send on shared or unconnected server
sockets.  If a platform lacks safe per-datagram marking, leave ECN disabled or
unsupported on that platform rather than adding racy global socket mutation.

Implementation points should be narrow:

- `sendPathBuf_()`
- `sendPathProbeBuf_()`
- `sendInitialBuf_()`
- `sendHandshakeBuf_()`
- `sendZeroRTTPkt_()`
- `sendShortPkt_()`
- client/server endpoint `send()` wrappers

Long Initial/Handshake packets may be marked while testing, but ACK_ECN
validation is only useful when the peer returns ACK_ECN in that packet number
space.  The first pass can mark all ack-eliciting protected packets and validate
whatever ACK_ECN arrives.

## Link Integration

### Mark Selection

Add:

```text
EcnMark::T txECNMark_(PktNumSpace::T level, bool ackEliciting) const
```

Return `NotECT` when:

- the path is disabled or failed;
- the packet is not ack-eliciting;
- the packet is a close/drain response where minimizing dependencies is better
  than gathering ECN signal.

Otherwise return `m_path.txECNMark()`, which is initially `ECT0`.

Call this near packet protection/send, and pass the mark to both:

- `recordProtPktTx_()` for accounting and qlog;
- the endpoint send function for actual socket marking.

### ACK_ECN Validation

Keep `validateAckECN_()` as the central validation point.  Extend it rather than
adding a second ECN validator.

Current checks to preserve:

- ECT0/ECT1/CE counters must not decrease;
- summed counters must not overflow;
- summed counters must not exceed the largest acknowledged packet count.

New checks:

- When ECN is `Testing`, ACK_ECN must show ECT0/CE growth consistent with
  locally sent ECT0 packets in that packet number space.
- If ACK_ECN reports more ECT0+CE than locally marked packets that could have
  been acknowledged, fail ECN.
- If ACK_ECN never appears after a bounded number of locally marked,
  ack-eliciting packets have been acknowledged, fail ECN.  Use packet/ACK
  progress, not a timer, to avoid adding latency machinery.

On success:

- update `m_peerAckECN[level]` and diagnostics as today;
- transition `Testing -> Capable` once the peer proves it can report ECN marks;
- keep `Capable` stable while counters remain valid.

On failure:

- call `m_path.failECN()`;
- stop outbound marking immediately;
- continue ACK processing normally;
- emit the existing `quic:ecn_state_updated` qlog event with the existing
  reason enum.

### CE Congestion Response

After ACK_ECN validation succeeds, compute:

```text
ceDelta = ack.ecn.ce - m_peerAckECN[level].ce
```

Use the pre-update `m_peerAckECN` snapshot for the delta.

If `ceDelta > 0`, identify the newest acknowledged sent packet covered by the
ACK frame and call `m_congestion.congestionAt(sentTime)`.  Re-use the existing
sent-packet metadata; do not add a CE-specific packet table.

The preferred implementation is to extend the existing ACK processing result
(`PktTxUpdate` or adjacent local update structure) with the newest acknowledged
sent time for ECN-marked packets, avoiding any extra packet-history scan.

If `congestionAt()` changes the window:

- update congestion diagnostics;
- emit the existing congestion qlog event with `RecKind::NewReno`;
- increment an ECN CE response diagnostic counter.

Do not release bytes-in-flight for CE.  Lost packets still use the existing
loss path.

## Qlog And Diagnostics

Re-use existing qlog structures first:

- keep `quic:ecn_state_updated`;
- keep datagram and packet ECN fields;
- keep ACK_ECN frame fields.

Required qlog fixes:

- `recordProtPktTx_()` must emit the actual outbound mark instead of
  `EcnMark::N`;
- ECN failure/capability transitions should include the same counter fields
  already logged today;
- CE-triggered congestion response should appear as a normal congestion update,
  not a new private event unless the existing event cannot express it.

Add runtime diagnostics only where the existing structs already expose ECN:

```text
RuntimeTxDiag::ecnState
RuntimeTxDiag::ecnMarkedTx[PktNumSpace::N]
RuntimeTxDiag::ecnCECongestionEvents
RuntimeTxDiag::ecnTestFailures
```

Avoid adding large nested diagnostic objects unless tests need them.

## Implementation Sequence

1. Path state only.

   Add `ECNState`-like path state to `Path`, preserve `ecnDisabled()` and
   `setEcnDisabled()`, and update tests that currently expect the boolean
   behavior.  Use Z enum style, not `enum class`.

2. Local transmit ECN accounting.

   Add `m_txECN`, pass an `EcnMark::T` into `recordProtPktTx_()`, update sent
   packet qlog, and add tests that do not require real socket support.

3. ACK_ECN validation against local transmit counts.

   Extend `validateAckECN_()` in place.  Keep all existing qlog and failure
   behavior.  Add tests for valid capability promotion and invalid local/peer
   counter mismatches.  Extend the existing ACK update structure if CE response
   needs sent-time metadata; do not add a separate sent-packet scan.

4. CE response.

   Add `NewReno::congestionAt()`, call it from validated CE deltas, update
   diagnostics/qlog, and test that bytes-in-flight is not reduced by CE.

5. Socket receive ancillary metadata.

   Add `Sock` option planning and init support.  Add the narrow receive path
   needed to populate `Datagram::ecn` from kernel metadata without changing TCP
   or generic UDP semantics.

6. Socket outbound marking.

   Add the narrow send path needed to pass actual ECN marks to the kernel.
   Wire client and server endpoint sends without changing non-QUIC users.

7. Runtime tests.

   Add end-to-end tests for receive ACK_ECN, outbound ECT0 marking when
   supported, fallback when unsupported, invalid ACK_ECN failure, and CE
   congestion response.  Use `ZmBlock` or existing local synchronization, not
   sleeps.

8. Audit cleanup.

   Update `audit.md` to remove pacing/CUBIC/BBR from priority work for this
   latency-first profile and to mark ECN complete once tests pass.

## Tests

Focused unit tests:

- `ZquicPMTUDTest` or a new path-focused test for ECN state transitions.
- `ZquicSockTest` for `Sock::plan()` and guarded socket option diagnostics.
- `ZquicCongestionTest` for `NewReno::congestionAt()`.
- `ZquicStreamTest` for ACK_ECN validation and CE response using existing link
  test hooks.
- `ZquicLogTest` for outbound packet ECN and ECN state qlog fields.

Runtime tests:

- received ECT0/CE marks are emitted in ACK_ECN;
- ECT0 outbound marking is selected while testing/capable;
- valid ACK_ECN promotes `Testing -> Capable`;
- invalid ACK_ECN transitions to `Failed` and stops marking;
- CE counter growth reduces cwnd once per recovery epoch and does not reduce
  bytes-in-flight as if packets were lost.

Socket tests should tolerate missing platform support by checking unsupported
diagnostics.  Linux should exercise real ancillary data where available.

## Verification

Run:

```sh
make -j8
make -C zquic/test test
make -C zhttp/test test
```
