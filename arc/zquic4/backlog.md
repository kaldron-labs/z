# zquic Audit Work Stack Rank

This backlog is ordered by implementation priority.  Earlier items either fix correctness/interoperability risks, unlock multiple later areas, or prevent later feature work from being built on incomplete transport mechanics.

## P0. Recovery and timer foundation

### 1. Replace PTO-only timing with explicit QUIC timers

Scope:
- Add one `ZmScheduler::Timer` instance per independent QUIC expiry.  Do not multiplex unrelated expiry reasons through one timer object.
- Timer inventory and ownership:
  - ACK delay: `Cxn`-specific.  One per connection; armed from Rx ACK state and fires into ACK-only/control packet scheduling.
  - Loss time: `Cxn`-specific.  One per connection; covers time-threshold loss across packet number spaces.
  - PTO: `Cxn`-specific.  One per connection; covers probe timeout selection across Initial, Handshake, and 1-RTT packet spaces.
  - Idle timeout: `Cxn`-specific.  One per connection; closes the connection after negotiated idle expiry.
  - Draining/closing: `Cxn`-specific.  One per connection; retains closing/draining state for the required duration before teardown.
  - Key discard: `Cxn`-specific.  One per connection; schedules Initial/Handshake and old 1-RTT key discard windows.
  - PMTUD: `Link`-specific.  One per active path/link; schedules PMTUD probe cadence and black-hole recovery.
  - Path validation: `Link`-specific.  One per validating path/link; expires PATH_CHALLENGE validation and fallback.
- Keep Rx/Tx ownership explicit: Rx-owned receive state posts fixed snapshots to Tx; Tx owns send, loss, and path-probe timers.

Acceptance:
- Unit tests cover timer priority ordering and cancellation.
- Runtime tests show ACK delay, loss time, PTO, idle, draining/closing, key discard, PMTUD, and path-validation timers do not clobber each other.
- Tests verify each timer callback is scoped to the owning `Cxn` or `Link` and cannot fire after owner teardown.
- Existing runtime handshake/stream tests still pass.

### 2. Extend sent-packet tracking to all packet number spaces

Scope:
- Track PTO-eliciting, ack-eliciting, in-flight, and retransmittable state for Initial, Handshake, and 1-RTT.
- Process ACK effects for all spaces instead of returning early for non-1RTT.
- Add packet-space-specific discard for Initial and Handshake state after keys are no longer needed.

Acceptance:
- Lost Initial/Handshake CRYPTO data is retransmitted by PTO/loss recovery.
- ACK of Initial/Handshake packets updates sent-packet state and timer scheduling.
- Tests cover dropped Initial and dropped Handshake CRYPTO packet recovery.

### 3. Wire time-threshold loss and persistent congestion into runtime

Scope:
- Preserve the existing 1-RTT ACK-triggered packet-threshold retransmit path.
- Add RTT-derived time-threshold loss through the unified loss timer.
- Extend packet-threshold loss/retransmit coverage to any packet number space enabled by item 2.
- Detect persistent congestion and feed congestion state.

Acceptance:
- Tests cover ACK-triggered packet-threshold retransmit, time-threshold loss, and persistent-congestion collapse.
- Loss processing enqueues retransmittable frames exactly once.
- PTO and loss-time deadlines choose the same packet space ordering as ngtcp2.

### 4. Integrate congestion control into packet budgets

Scope:
- Make `NewReno` runtime-owned and feed it on packet send, ACK, loss, and persistent congestion.
- Replace `budget.congestion = app()->maxUDP()` with cwnd/bytes-in-flight driven allowance.
- Add pacing hooks only after cwnd gating is correct.

Acceptance:
- Congestion window gates normal stream/control sends.
- ACK increases cwnd; loss reduces cwnd; PMTUD probe loss does not reduce cwnd.
- Runtime tests include cwnd-limited send and loss recovery cases.

## P1. ACK behavior and packet assembly

### 5. Make ACK management timer-driven

Scope:
- Promote `AckManager` or equivalent into the runtime receive path.
- Track largest received packet timestamp per space.
- Generate ACK delay for 1-RTT and zero delay for Initial/Handshake.
- Support immediate ACK triggers and ACK-only packet scheduling.

Acceptance:
- Tests verify delayed ACK emission, immediate ACK emission, ACK delay encoding, and ACK state commit after send.
- ACK-only packets do not create retransmittable sent-packet entries.
- ACK ranges remain correct across duplicates and reordering.

### 6. Preserve and validate ACK_ECN counts

Scope:
- Extend `Frame` to retain ACK_ECN counters.
- Track received ECN marks per packet space.
- Add minimal ECN validation state and disable ECN on validation failure.

Acceptance:
- ACK_ECN parse/write tests cover ECT0, ECT1, and CE counts.
- ECN counters are surfaced to congestion/control logic.
- Non-ECN behavior remains unchanged when ECN is disabled.

### 7. Improve packet assembly/coalescing

Scope:
- Allow multiple control frames and multiple stream frames per packet where budget permits.
- Coalesce Initial/Handshake packets when useful during handshake.
- Use dynamic packet number length based on largest acknowledged packet.

Acceptance:
- Tests cover multiple frames in one protected packet.
- Packet number length shrinks/grows correctly from ACK state.
- Existing one-frame scheduling tests are updated to assert priority, not artificial packet count.

## P2. Path, PMTUD, and anti-amplification

### 8. Integrate `Path` into runtime send/receive budgets

Scope:
- Make active path state part of client/server link runtime.
- Record bytes received/sent into `Path`.
- Gate server sends by anti-amplification until address validation.
- Replace raw `app()->maxUDP()` budget use with active path maximum UDP payload.

Acceptance:
- Server cannot send more than 3x received bytes before validation.
- Active path MTU controls packet payload sizing.
- Tests cover unvalidated path budget exhaustion and validation unlock.

### 9. Implement path validation and migration basics

Scope:
- Add PATH_CHALLENGE generation, PATH_RESPONSE matching, validation timeout, and path promotion.
- Bind DCIDs/tokens to paths enough to distinguish current, validating, and retired paths.
- Handle NAT rebinding as a path validation event rather than a packet failure.

Acceptance:
- Tests cover successful PATH_CHALLENGE/PATH_RESPONSE validation.
- Tests cover validation timeout and retained old path.
- Short packets on a new address do not silently replace the active path before validation.

### 10. Wire PMTUD into runtime

Scope:
- Send PMTUD probes using `Path::startProbe`.
- Mark PMTUD probe packets so loss does not reduce cwnd.
- Handle probe ACK, loss, expiry, kernel/send-too-big hints, and blackhole fallback.

Acceptance:
- Tests cover successful probe growth, failed probe retry, blackhole fallback, and send-too-big hint handling.
- Active packet size follows PMTUD state.

## P3. Stream control and lifecycle completeness

### 11. Emit RESET_STREAM and STOP_SENDING frames

Scope:
- Add control-frame variants and scheduler paths for local reset and stop-sending requests.
- Track ACK/loss of these frames and retransmit while still valid.
- Update stream state transitions and callbacks.

Acceptance:
- Tests cover local RESET_STREAM send, peer RESET_STREAM receive, local STOP_SENDING send, peer STOP_SENDING receive, and retransmission after loss.
- Reset final-size validation remains intact.

### 12. Add closed-stream and suspicious-remote rate limiting

Scope:
- Track invalid duplicate/closed-stream activity enough to avoid unbounded work or repeated close storms.
- Map invalid stream activity to specific transport errors where applicable.
- Keep ordinary duplicate STREAM frames cheap and non-fatal.

Acceptance:
- Tests cover closed-stream MAX_STREAM_DATA, RESET_STREAM, STREAM_DATA_BLOCKED, and duplicate-final-size cases.
- Repeated invalid activity trips a deterministic internal/protocol error path.

### 13. Improve application callback/error surface

Scope:
- Add explicit callbacks or CRTP hooks for stream open, data, reset, stop-sending, flow blocked, transport close, stateless reset, path update, and migration failure.
- Ensure default hooks are harmless but diagnostics make unhandled events visible.

Acceptance:
- Tests verify hooks fire on each control path.
- Existing applications compile with defaults.

## P4. Security and connection ID lifecycle

### 14. Harden key update handling

Scope:
- Retain old/new 1-RTT receive keys for the required window.
- Validate key phase transitions rather than installing next keys purely after decrypt success.
- Add key discard scheduling to the unified timer.

Acceptance:
- Tests cover peer key update, old-key packet tolerance, invalid key phase, and key discard.
- Existing packet protection tests still pass.

### 15. Complete stateless reset and CID token checks

Scope:
- Enforce token uniqueness for peer-issued CIDs.
- Associate reset tokens with path/DCID state.
- Retire/tombstone CIDs with timer-based cleanup.

Acceptance:
- Tests cover duplicate sequence/CID/token combinations, retired CID routing, and stateless reset on unknown short CID.

### 16. Add server Retry/token policy

Scope:
- Add configurable Retry policy and token generator/verifier.
- Validate Initial token before accepting when policy requires Retry.
- Carry original DCID/retry SCID transport parameter validation through the handshake.

Acceptance:
- Tests cover Retry required, valid retry, invalid token, and no-Retry policy.
- Client Retry behavior remains compatible with current tests.

## P5. Feature parity expansion

### 17. Add 0-RTT packet processing

Scope:
- Add 0-RTT packet protection state, transport parameter compatibility checks, early stream acceptance rules, and early data rejection cleanup.
- Ensure replay-sensitive behavior is application controlled.

Acceptance:
- Tests cover accepted 0-RTT, rejected 0-RTT, transport parameter incompatibility, and early packet discard.

### 18. Add qlog and structured metrics

Scope:
- Add qlog writer hooks for packet sent/received, frames, ACK/loss, congestion metrics, path validation, PMTUD, key update, close, and stateless reset.
- Keep existing counters, but make qlog optional and zero-cost when disabled.

Acceptance:
- Tests verify qlog emits parseable events for a handshake and stream transfer.
- Disabled qlog adds no allocations in the packet hot path.

### 19. Add CUBIC/BBR and pacing

Scope:
- Add congestion-controller abstraction after NewReno runtime integration is stable.
- Implement CUBIC first, then BBR if required.
- Wire `Pacer` to scheduler delays and packet bursts.

Acceptance:
- Tests cover controller selection, cwnd evolution, loss response, and pacing interval scheduling.

## P6. Polish and interop hardening

### 20. Expand interop/runtime tests

Scope:
- Add targeted packet-loss handshake tests, ACK delay tests, anti-amplification tests, migration tests, PMTUD tests, and close/draining tests.
- Add negative tests for malformed frames and transport parameter violations.

Acceptance:
- New tests run as standalone `zquic/test/*` binaries.
- Each P0-P5 feature has at least one regression test.

### 21. Review packet/frame storage lifetime

Scope:
- Audit `SentFrameRef` shallow range references and retransmit queue lifetime.
- Ensure retransmission never references freed packet/stream buffers.
- Add bounded queue/drop or stale-filter policy.

Acceptance:
- Tests cover retransmission after application buffer turnover and stream close.
- No unbounded retransmit growth under repeated loss.

### 22. Revisit flow-control window scaling

Scope:
- After RTT and timers are stable, add optional dynamic receive window growth for MAX_DATA/MAX_STREAM_DATA.
- Preserve current simple fixed-window mode as the default unless performance tests show benefit.

Acceptance:
- Tests cover fixed and dynamic window modes.
- Dynamic mode respects configured maximums and varint limits.

### 23. Documentation and API cleanup

Scope:
- Document supported QUIC feature level and deliberate non-goals.
- Update examples for new callbacks, Retry policy, qlog, and path events.
- Remove stale TODOs or dead stubs that no longer represent the runtime model.

Acceptance:
- README and module docs match implemented behavior.
- Examples build and exercise the current public API.
