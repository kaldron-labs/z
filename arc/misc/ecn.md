# zquic ECN Path Feature Plan

## Goal

Finish ECN as a path-owned feature, not just ACK_ECN parsing:

- send active QUIC packets with the path-selected ECN codepoint;
- probe ECN capability per path before treating the path as ECN-capable;
- validate ACK_ECN counters against what was actually sent on that path;
- react to new CE marks through congestion control using the QUIC recovery
  model.

Existing useful pieces:

- `AckECN` frame encode/decode exists in `ZquicFrame`.
- Rx datagrams already carry an `EcnMark::T` and `AckManager` counts received
  ECT0/ECT1/CE for ACK_ECN emission.
- `validateAckECN_()` checks monotonic peer ACK_ECN counters and logs qlog ECN
  state changes.
- `Path` has a placeholder `m_ecnDisabled`, but it currently defaults disabled
  and has no sender-side state machine.
- `NewReno` has loss response but no explicit CE response.

## Design

ECN belongs to `Path` because capability is path-dependent and must be reset on
path migration or validation replacement. Keep the current Rx/Tx ownership
split:

- Rx records incoming ECN markings and emits ACK_ECN.
- Tx owns sent packet accounting, ACK_ECN validation, ECN probe state, and
  congestion response.
- Cross-thread posts must capture only fixed metadata, as current ACK handling
  does.

Use ECT0 for normal marking. Do not add ECT1/L4S behavior unless it is
explicitly requested later.

The implementation must use the existing `ZiIOContext::tos` path. `ZiMultiplex`
already enables TOS/TCLASS ancillary receive data and already emits per-datagram
ancillary send data when `ZiIOContext::tos` contains a `uint8_t`. Do not add a
parallel ECN socket-send API, and do not set `IP_TOS`/`IPV6_TCLASS` globally per
packet.

ECN is the low two bits of the traffic class byte:

- `NotECT`: `0x00`
- `ECT1`: `0x01`
- `ECT0`: `0x02`
- `CE`: `0x03`

When sending UDP, zquic should pass TOS through the optional final parameter to
`ZiIOContext::init(...)`: `io.init(fn, ptr, size, offset, addr, ecnTOS(mark))`.
For now `ecnTOS(mark)` should contain only the ECN bits. If a future API
preserves DSCP, that API must mask with `(dscp & 0xfc) | ecnBits(mark)`. When
reading `io.tos`, decode only `tos & 0x03`; ignore DSCP bits.

## Data Model

Add a small ECN state block to `Path`:

- a protocol enum in `ZquicTypes.hh`, e.g. `ZtEnumStruct(PathECNState, int8_t,
  Disabled, Testing, Capable, Failed)`. Do not reuse qlog-only `ECNState` from
  `ZquicLog.hh` for runtime protocol state.
- active mark: `NotECT` or `ECT0`.
- per packet number space sent counters for packets transmitted with ECT0,
  ECT1, and CE-capable marks. The first implementation should only increment
  ECT0.
- last validated peer ACK_ECN counters per packet number space.
- probe budget: number of marked ack-eliciting packets sent while testing and
  how many valid ACK_ECN observations are still needed.
- diagnostics: probes sent, probes ACKed, failures, validation failures, CE
  events, CE bytes/signals applied.

Keep counters on Tx-owned path state. The Rx-side `AckManager` counters remain
separate because those describe peer-sent packets we received and report back
to the peer.

Extend `SentPkt` with:

- ECN mark used for that packet.
- Whether the packet contributed to ECN validation/probing.

This avoids inferring sent ECN counts from ACK ranges and lets ACK processing
apply CE response only to newly acknowledged ECN-marked packets.

Add helpers instead of scattering bit manipulation:

```c++
static uint8_t ecnBits(EcnMark::T);
static ZiTOS ecnTOS(EcnMark::T);
static EcnMark::T ecnMarkFromTOS(ZiTOS);
```

Put them in zquic-local code (`ZquicSock.hh`/`.cc` or a small path helper), not
in `Zi`, because QUIC policy should not leak into the generic multiplexer.

## Socket Marking

Use existing Zi TOS support directly:

- `ZiMultiplex.cc` calls `ZiSetRecvTOS()` when UDP sockets are opened, and maps
  received control messages into `ZiIOContext::tos`.
- `ZiMultiplex.cc` calls `ZiPutTOS()` during UDP send when
  `m_txContext.tos.is<uint8_t>()`.
- Therefore zquic endpoint code must only preserve the selected ECN mark and
  supply the corresponding `ZiTOS` through UDP `ZiIOContext::init(...)`.

Make these concrete changes in `zquic/src/Zquic_.hh`:

- Extend `Endpoint_::Cxn_::TxNode` with `EcnMark::T ecn`.
- Add `EcnMark::T m_txECN = EcnMark::NotECT` beside `m_txBuf`/`m_txAddr`.
- Change `Cxn_::sendPkt()` to accept `EcnMark::T ecn`, store it in `m_txECN`,
  and enqueue it in `TxNode`.
- Change `enqueueTx_()` and `dequeueTx_()` to preserve the mark.
- In `sendStart_(ZiIOContext &io)`, pass the mark through UDP init:
  `io.init(fn, m_txBuf->data(), m_txBuf->length, 0, m_txAddr,
  ecnTOS(m_txECN))`. Do not assign `io.tos` separately after `init()`, and do
  not assign it before `init()` because non-UDP and older init overloads clear
  `tos`.
- In `recvDone_(ZiIOContext &io)`, construct
  `Datagram{ZuMv(buf), io.addr, ecnMarkFromTOS(io.tos)}`. The current code
  drops `io.tos` and defaults all received datagrams to `NotECT`; fix that
  before relying on ACK_ECN counts.
- Change `Endpoint_::send()` and private `send_()` to accept/pass
  `EcnMark::T`, defaulting to `NotECT` only for non-QUIC call sites if needed.

Do not add `SockConfig` fields for active marking. `Sock::initUDP()` may keep
PMTUD/error-queue duties; ECN send marking is per datagram via `ZiIOContext`.
If diagnostics are useful, add endpoint/path counters for whether a marked send
was requested; do not infer send support from `Sock::plan()`.

The send API should carry `EcnMark::T` beside `buf` and `addr`, mirroring
received `Datagram`.

Make these concrete changes in `zquic/src/ZquicLink.hh`:

- Add `Path::txECN(PktNumSpace::T level, bool ackEliciting, bool pmtudProbe)`
  or an equivalent link helper that returns `ECT0` only when:
  - ECN is enabled and the path is `Testing` or `Capable`;
  - `level == PktNumSpace::AppData`;
  - the packet is ack-eliciting;
  - the packet is not a PMTUD probe.
- Thread that mark through `sendPathPkt_()`, `sendPathProbePkt_()`, and
  `sendPathPktApp_()` into the `sendPkt` callback. For PMTUD probes, force
  `NotECT`.
- Update the call sites that currently pass lambdas shaped like
  `sendPkt(buf, addr)` or `sendPkt(buf, addr, sent)` so they pass
  `sendPkt(buf, addr, ecn)` or `sendPkt(buf, addr, ecn, sent)`.
- Update `sendProtInitialPkt_()`, `sendProtHandshakePkt_()`, and
  `sendProtZeroRTTPkt_()` to pass `NotECT` to their send callback explicitly.
  The first implementation marks only 1-RTT AppData.
- Update `sendProtShortPkt_()` to compute the AppData mark before
  `recordProtPktTx_()`, pass it into `recordProtPktTx_()`, and pass it to the
  endpoint send callback.
- Replace qlog datagram and packet sent `event.ecn = EcnMark::N` assignments
  in these send paths with the actual mark.

## Probing Policy

For the first implementation, make ECN explicitly enabled by an engine parameter
instead of silently enabling it for every application. Add the parameter to
`EngineParams`, copy it into client/server endpoint state during init, and use
that single flag to initialize new paths:

- disabled by parameter: `PathECNState::Disabled`, active mark `NotECT`;
- enabled: `PathECNState::Testing`, active mark `ECT0`.

Probe with ECT0 on a small number of ack-eliciting packets:

- Mark only AppData packets initially. Do not mark Initial/Handshake/0-RTT in
  this first pass; that keeps validation tied to one packet number space and
  avoids handshake special cases.
- Count marked ack-eliciting AppData packets until a configured threshold,
  initially 10 packets.
- Promote to `Capable` when ACK_ECN counters validate and include the sent ECT0
  packets without counter regression or impossible totals.
- Fail to `Failed` and switch to Not-ECT when:
  - ACK_ECN counters regress;
  - ACK_ECN totals exceed ackable packets;
  - marked packets are ACKed but no ACK_ECN is reported after the probe budget;
  - the path reports repeated socket/control-message failures for marking.

If a path is failed, do not retry ECN on that same path automatically. Migration
or a new path validation instance starts fresh.

## ACK_ECN Validation

Replace the current `m_peerAckECN`-only validation with path-aware validation:

- Validate monotonic counters as today.
- Validate cumulative ACK_ECN totals against ECN-marked packets sent in that
  packet number space, not just `largestAcked + 1`.
- Detect new CE as `ack.ecn.ce - last.ce` after validation.
- Track newly acknowledged ECN-marked bytes/packets through `PktTxUpdate`.
- Treat ACK_ECN frames from packets sent Not-ECT as harmless for peer Rx counts,
  but they must not promote local path ECN capability unless they account for
  locally marked packets.
- On validation failure, set path ECN state to `Failed`, clear active mark to
  `NotECT`, and log an ECN qlog state update.

Current `validateAckECN_()` should become the transition point that returns a
small result object:

- valid/invalid;
- newly observed CE count;
- newly ACKed ECN-marked bytes;
- state transition, if any.

Make this prescriptive in code:

- Move `m_peerAckECN[PktNumSpace::N]` into `Path`, or wrap it in path ECN state
  and delete the link-level member after callers are updated.
- Extend `PktTxUpdate` with:
  - `uint64_t ecnAckdBytes`;
  - `uint64_t ecnAckdPackets`;
  - `ZuTime ecnAckdSentTime`;
  - `uint64_t ceDelta`;
  - `bool ecnProbeAckd`;
  - `bool ecnValidationFailed`.
- When `SentPktTracker::ack(...)` processes ACK ranges, aggregate ECN-marked,
  non-PMTUD packets from the acknowledged `SentPkt` entries into those fields.
- Validate ACK_ECN before applying CE congestion response. If validation fails,
  stop marking immediately but still process the ACK for normal loss/recovery
  semantics.
- Promotion rule for `Testing`: when at least one ECN-marked packet is newly
  acknowledged and `ack.ecn.ect0 + ack.ecn.ce` advanced enough to cover that
  packet, set `Capable`.
- Missing-feedback rule: if the path has sent 10 ECN-marked ack-eliciting
  AppData packets and ACKs for those packets arrive without any ACK_ECN frame,
  set `Failed`.

## Congestion Response

Add an explicit CE response to `NewReno`:

- `congestionExperiencedAt(bytes, sentTime)` or equivalent.
- It should mirror loss recovery entry: reduce `ssthresh` and `cwnd` at most
  once per recovery epoch, using the sent time of the newly ACKed ECN-marked
  packet that triggered CE.
- Do not remove bytes in flight in the CE method; ACK processing already does
  that via `ackd()`.
- PMTUD probes should not trigger CE congestion response.

In ACK processing:

- Apply normal ACK credit first.
- If the ACK_ECN delta contains new CE and acknowledges ECN-marked non-PMTUD
  bytes, call the CE response once using the earliest or largest newly ACKed
  marked packet sent time. Pick one policy and test it; largest newly ACKed sent
  time best matches existing recovery epoch checks.
- Emit qlog congestion updates with a new `RecReason::ECNCE` or similar closed
  enum value.

Do not overload the existing loss path by synthesizing a lost packet. CE is a
congestion signal attached to an ACKed packet, so bytes-in-flight accounting must
remain ACK accounting.

## qlog and Diagnostics

Update qlog enums and events without putting qlog-only work on the hot path:

- Add ECN states/reasons for `testing`, `disabled`, `probe`, `no_ack_ecn`,
  `mark_failed`, and `ce`.
- Add recovery reason for CE congestion response.
- Log ECN state transitions only when state actually changes.
- Include the packet send ECN codepoint in `logPktSent`; it is currently always
  set to `EcnMark::N`.
- Keep all qlog event construction inside `ZquicLOG`.

Concrete qlog implications:

- Packet sent events must carry the selected datagram ECN mark for every sent
  QUIC packet. The first implementation should show `ECT0` only on marked
  AppData packets and `Not-ECT` on Initial, Handshake, 0-RTT, PMTUD probes, and
  packets sent after ECN failure.
- Datagram sent events must carry the same ECN mark as the packet path when a
  datagram contains one packet. For coalesced datagrams, use the datagram mark
  actually passed to `ZiIOContext::init(...)`; with this plan that should be
  `Not-ECT` because only AppData is marked.
- Packet received and datagram received events must continue using the decoded
  `Datagram::ecn` value derived from `io.tos`.
- ECN state update events must identify:
  - packet number space;
  - previous and new path ECN state;
  - current ACK_ECN counters and previous counters;
  - reason (`probe`, `ack_ecn`, `no_ack_ecn`, counter regression, overflow,
    counter exceeds ACKed ECN packets, `mark_failed`, `ce`);
  - whether marking is now disabled.
- CE congestion response must produce a congestion state update with a closed
  `RecReason` value such as `ECNCE`; do not encode this as generic `Loss`.
- qlog-only values must be captured by value inside `ZquicLOG`. Do not read ECN
  diagnostics, build qlog structs, format strings, or map enums outside
  `ZquicLOG` unless the same value is already needed for protocol behavior.
- qlog enum/string mappings belong in `ZquicLog.hh` through `ZtEnumMap`, not
  ad hoc string literals at call sites.
- Release builds must erase qlog-only work. Debug builds with qlog disabled must
  not evaluate qlog-only ECN expressions.

Expose ECN path diagnostics through existing `PathDiag`/runtime diagnostics so
tests and applications can distinguish disabled, unsupported, failed, and
capable paths.

## Slicing

Implement ECN in slices that leave the tree buildable after each slice. Do not
start a later slice until the previous slice's acceptance gate passes.

### Slice 1: Endpoint TOS plumbing

Scope:

- Add `ecnBits()`, `ecnTOS()`, and `ecnMarkFromTOS()`.
- Preserve `EcnMark::T` in `Endpoint_::Cxn_::TxNode`, `m_txECN`, and all
  endpoint send/dequeue paths.
- Pass `ecnTOS(m_txECN)` to the UDP `ZiIOContext::init(...)` overload in
  `sendStart_()`.
- Decode `io.tos` in `recvDone_()` and pass it to `Datagram`.
- Do not modify `ZiMultiplex` unless tests show its existing TOS path is broken.

Acceptance gate:

- Existing behavior is unchanged when callers omit an ECN mark.
- A focused endpoint/helper test proves `ECT0` maps to TOS `0x02` on send and
  TOS `0x03` maps to `CE` on receive.
- `make -C zquic/src -j8` and the focused test build/run cleanly.

### Slice 2: Path ECN state

Scope:

- Add runtime `PathECNState` in `ZquicTypes.hh`.
- Add path state, counters, diagnostics, and accessors in `Path`.
- Add the explicit `EngineParams` ECN flag and initialize each new path from it.
- Reset ECN state on path replacement/migration.

Acceptance gate:

- ECN disabled by default leaves all sends `NotECT`.
- With ECN enabled, a new path enters `Testing` with active mark `ECT0`.
- Path state transition tests pass without requiring kernel ECN behavior.
- Slice 1 tests still pass.

### Slice 3: Outgoing packet marking and accounting

Scope:

- Add the `Path::txECN(...)` helper or equivalent link helper.
- Thread the selected mark through `sendPathPkt_()`, `sendPathProbePkt_()`,
  `sendPathPktApp_()`, and endpoint send callbacks.
- Pass `NotECT` explicitly for Initial, Handshake, 0-RTT, and PMTUD probes.
- Store the mark in `SentPkt`.
- Replace sent datagram/packet qlog `EcnMark::N` placeholders with the actual
  mark.

Acceptance gate:

- With ECN enabled, only ack-eliciting AppData packets are marked `ECT0`.
- Initial, Handshake, 0-RTT, and PMTUD probes remain `NotECT`.
- Sent packet qlog/runtime test coverage observes the selected mark.
- Slices 1-2 tests still pass.

### Slice 4: Probe policy and ACK_ECN validation

Scope:

- Move peer ACK_ECN validation counters into path ECN state.
- Extend `PktTxUpdate` with ECN-acked bytes/packets, CE delta, ECN sent time,
  probe-ACKed, and validation-failed fields.
- Aggregate ECN-marked acknowledged packets in `SentPktTracker::ack(...)`.
- Promote `Testing` to `Capable` only from valid ACK_ECN feedback covering
  marked packets.
- Fail to `Failed` and stop marking on invalid counters or missing ACK_ECN after
  the probe budget.

Acceptance gate:

- Valid ACK_ECN promotes `Testing` to `Capable`.
- Counter regression, overflow, impossible totals, and missing ACK_ECN after the
  probe budget fail the path to `Failed`.
- ACK processing still applies normal ACK/loss semantics when ECN validation
  fails.
- Slices 1-3 tests still pass.

### Slice 5: CE congestion response

Scope:

- Add `NewReno::congestionExperiencedAt(...)` or equivalent.
- Invoke it from ACK processing only after normal ACK accounting.
- Do not subtract bytes-in-flight in the CE method.
- Exclude PMTUD probes from CE response.
- Add qlog recovery reason and diagnostics for CE response.

Acceptance gate:

- CE halves/constrains `cwnd` once per recovery epoch.
- Bytes-in-flight is not decremented twice.
- PMTUD probe ACK/CE paths do not trigger CE congestion response.
- Slices 1-4 tests still pass.

### Slice 6: Runtime/qlog coverage and cleanup

Scope:

- Add runtime/qlog tests for ECN state updates, packet sent ECN, and CE recovery
  updates.
- Remove obsolete link-level ECN state such as `m_peerAckECN` after path-owned
  counters are in use.
- Audit logs and diagnostics for qlog gating and capture rules.
- Add negative qlog checks: failed ECN must stop subsequent sent packet events
  from showing `ECT0`; PMTUD probe events must remain `Not-ECT`; CE response
  must not appear as `loss`.

Acceptance gate:

- Runtime/qlog tests cover `Testing`, `Capable`, `Failed`, sent `ECT0`, sent
  `NotECT` after failure, and CE congestion response.
- No qlog-only work remains outside `ZquicLOG`.
- All targeted tests and verification commands pass.

## Tests

Coverage matrix:

- `ZiMxLoopTest` already exercises UDP TOS send/receive. Keep using that as the
  Zi-level proof that `ZiIOContext::tos` works.
- Add a zquic endpoint-level test or focused `ZquicSockTest` case that queues a
  datagram with `ECT0`, verifies `sendStart_()` passes `ZiTOS{0x02}` to the UDP
  `ZiIOContext::init(...)` overload, and verifies `recvDone_()` converts
  `io.tos = 0x03` to `Datagram::ecn == CE`.
  This can be done without depending on kernel ECN behavior by calling the
  endpoint/Cxn test seam or a local helper around the conversion functions.
- `ZquicPathTest` or `ZquicRecoveryTest` path-state coverage:
  - ECN disabled by default;
  - ECN enabled enters `Testing`;
  - migration/path replacement resets ECN state and counters;
  - failure switches active mark to `NotECT`;
  - probe budget is bounded and does not retry on the same failed path.
- `ZquicCodecTest`: ACK and ACK_ECN frame encode/decode remains stable,
  including zero ECN counters, non-zero ECT0/ECT1/CE counters, and no regression
  in ordinary ACK frame encoding.
- `ZquicRecoveryTest` or a focused ACK_ECN validation test:
  - valid ACK_ECN promotes to `Capable`;
  - missing ACK_ECN after probe budget fails to `Failed`;
  - ECT0, ECT1, and CE counter regression fail immediately;
  - ECT and CE overflow fail immediately;
  - ACK_ECN totals greater than ACKed ECN-marked packets fail;
  - CE deltas are counted once and only once across repeated ACK processing;
  - invalid ACK_ECN does not suppress normal ACK/loss processing.
- `ZquicCongestionTest`: NewReno CE response halves/constrains cwnd once per
  recovery epoch, does not subtract bytes-in-flight twice, and ignores PMTUD
  probe CE.
- `ZquicRuntimeTest` qlog coverage:
  - sent packet and datagram events carry `ECT0` during testing/capable AppData;
  - sent packet and datagram events carry `Not-ECT` after failure;
  - Initial, Handshake, 0-RTT, and PMTUD probe events carry `Not-ECT`;
  - ECN state updates are present for `Testing`, `Capable`, and `Failed`;
  - ACK_ECN validation failure logs the specific reason;
  - CE congestion response logs `RecReason::ECNCE` and does not log generic
    loss for the same signal;
  - release/qlog-disabled behavior does not evaluate qlog-only ECN expressions.

Verification commands after implementation:

```sh
make -C zi/test -j8
make -C zquic/src -j8
make -C zquic/test -j8
./libtool --mode=execute zi/test/ZiMxLoopTest
./libtool --mode=execute zquic/test/ZquicSockTest
./libtool --mode=execute zquic/test/ZquicCodecTest
./libtool --mode=execute zquic/test/ZquicCongestionTest
./libtool --mode=execute zquic/test/ZquicRecoveryTest
./libtool --mode=execute zquic/test/ZquicRuntimeTest
make -C zhttp/test test
```

Run top-level `make -j8` before merging if endpoint or Zi multiplex changes are
needed.

## Final Acceptance

ECN is complete only when all of the following are true:

- The implementation has been audited against `GUIDELINES.md` and
  `zquic/GUIDELINES.md`, and any deviations are either removed or explicitly
  justified in review notes.
- No qlog-only work is evaluated outside `ZquicLOG`; all qlog captures are by
  value and obey the zquic qlog capture/threading rules.
- ECN is path-owned: migration/replacement resets ECN state, ACK_ECN validation
  counters are no longer link-global, and failed paths stop marking without
  affecting future paths.
- Active marking uses `ZiIOContext::init(..., addr, ecnTOS(mark))`; there is no
  per-packet global socket TOS mutation.
- ACK_ECN validation is based on ECN-marked packets actually sent on the path,
  not only on largest ACKed packet number.
- CE response is congestion response for ACKed packets, not synthesized loss,
  and bytes-in-flight accounting remains correct.
- All slice acceptance gates pass in order.
- The verification commands above pass, including `make -C zhttp/test test`,
  plus top-level `make -j8` if shared Zi or build metadata changed.
- Test artifacts prove both protocol behavior and qlog behavior: ECN disabled,
  testing, capable, failed, CE response, qlog-enabled, and qlog-disabled paths.

## Open Decisions

- Default for the new `EngineParams` ECN flag. Keep it disabled by default for
  the first merge unless the owning application explicitly opts in.
- Exact probe threshold. Start with 10 marked ack-eliciting packets because it is
  simple, bounded, and matches common QUIC ECN validation guidance.
- Whether to expose ECT1 later for L4S; this plan deliberately keeps ECT0-only
  behavior.
