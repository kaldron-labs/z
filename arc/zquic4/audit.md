# zquic QUIC Implementation Audit Against ../zngtcp2

This report audits `zquic` against the corresponding QUIC implementation areas in `../zngtcp2`.  The comparison is behavioral: it flags missing or divergent processing in `zquic`, not merely differences in structure or API style.

Overall, `zquic` is a compact QUIC runtime with real Initial/Handshake/1-RTT packet protection, TLS integration, streams, flow control, ACK ranges, packet-threshold loss, and 1-RTT PTO retransmission.  It is not yet behaviorally equivalent to ngtcp2.  The largest gaps are recovery across all packet number spaces, congestion-control integration, ACK scheduling, lifecycle timers, PMTUD/path validation integration, DATAGRAM, ECN, qlog, and several stream-control/lifecycle paths.

## Connection lifecycle

- `zquic` supports client/server handshake, version negotiation response, Retry parsing/validation on the client side, transport parameter validation, CONNECTION_CLOSE receive handling, and stateless reset detection.
- Gap: no idle timeout, close/draining duration management, key discard timers, or ngtcp2-style loss/ACK timer multiplexing.  ngtcp2 keeps these timers in the connection timer path; `zquic` has only a runtime PTO timer.
- Gap: Retry is not complete server-side behavior.  `zquic` can write and validate Retry packets, but the server accept path accepts Initials directly; there is no token generation/validation policy comparable to ngtcp2.
- Gap: no 0-RTT lifecycle.  `PktType::ZeroRTT` exists, but runtime long-header receive rejects anything other than Initial and Handshake.
- Divergence: close handling sends a single CONNECTION_CLOSE and tears endpoint state down quickly.  ngtcp2 maintains closing/draining behavior and timer-driven cleanup.

## Packet processing

- `zquic` parses and writes QUIC v1 long and short headers, packet numbers, varints, Retry, Version Negotiation, packet protection, and header protection.
- Gap: 0-RTT packets are not processed.
- Gap: ECN is not implemented.  ACK_ECN is parsed only to skip three counters; the counters are not retained or validated, and no ECN state influences congestion control.
- Gap: packet number length is fixed in runtime sends (`RuntimePNLength = 2`) rather than dynamically selected from largest acknowledged packet.
- Gap: coalesced datagram processing exists for long headers followed by a terminal short packet, but the send scheduler does not perform ngtcp2-style packet coalescing and budgeted aggregate packet generation.

## Streams

- Receive reassembly is implemented.  `zquic` uses `StreamRxPQueue`, `rxNovelSpans()`, and `ZmPQRx` to queue out-of-order packet-backed slices, suppress duplicates/overlap, and drain contiguous ranges.  Tests cover out-of-order delivery, duplicate pending frames, gap fill, interior overlap, and flow accounting across reordering.
- No gap versus ngtcp2 for ordinary STREAM receive reassembly.
- Gap: local RESET_STREAM and STOP_SENDING APIs set stream state, but the runtime scheduler does not visibly emit RESET_STREAM/STOP_SENDING frames as ngtcp2 does.
- Gap: closed-stream and suspicious-remote handling is much thinner.  ngtcp2 rate-limits and classifies repeated invalid activity; `zquic` generally returns failure or closes without an equivalent glitch-rate mechanism.
- Divergence: application callbacks and error reporting are less granular than ngtcp2's stream open/data/reset/stop callbacks and specific transport error propagation.

## Flow control

- `zquic` implements connection and stream receive credit, transmit credit, MAX_DATA, MAX_STREAM_DATA, MAX_STREAMS, and BLOCKED frame validation/queueing.
- Divergence: MAX_DATA and MAX_STREAM_DATA replenishment uses fixed half-window logic.  ngtcp2 can scale flow-control windows based on RTT and configured max-window policy.
- Divergence: stream-count replenishment increments by one when stream credit is returned.  ngtcp2 tracks unsent/max stream counts and exposes explicit extension APIs.
- Gap: flow-control callbacks and blocked/error reporting are less complete than ngtcp2.

## Congestion control

- `zquic` has a `NewReno` utility with cwnd, ssthresh, bytes-in-flight, loss, and persistent-congestion methods.
- Gap: runtime packetization does not integrate that congestion controller.  Send budgets generally set `budget.congestion = app()->maxUDP()`, so cwnd does not gate normal runtime sends.
- Gap: no CUBIC, BBR, pacing integration, ECN response, spurious loss response, or full congestion metrics path comparable to ngtcp2.

## Retransmission

### PTO

- `zquic` calculates PTO from smoothed RTT/rttvar/max_ack_delay and applies exponential backoff.
- Gap: runtime PTO is only armed for 1-RTT (`CryptoLevel::OneRTT`) packets.  ngtcp2 selects the earliest PTO across Initial, Handshake, and Application packet spaces.
- Gap: no anti-deadlock handshake probe behavior equivalent to ngtcp2's Initial/Handshake PTO selection.
- Divergence: PTO reclaims the newest outstanding 1-RTT ack-eliciting packets through `reclaimOnPTO(2)` and queues their frame refs.  ngtcp2 sets probe packet counts per packet number space and writes fresh probe packets from packet-space state.

### ACK

- `zquic` tracks ACK ranges and writes ACK frames.
- Divergence: runtime ACK scheduling is snapshot/immediate-flush oriented.  `AckManager` exists as a utility, but it is not the runtime path.  ngtcp2 has an ACK tracker with active/immediate ACK flags, delayed ACK expiry, largest-packet timestamps, ACK_ECN counts, and commit semantics after ACK emission.
- Gap: ACK delay generation in runtime snapshots is effectively absent for locally generated ACKs; incoming ACK delay is used for RTT sampling.
- Gap: ACK-only packets are not integrated with a dedicated ACK delay timer.

## Loss recovery

- `zquic` tracks sent packets and supports ACK processing, packet-threshold loss, retransmit queueing, RTT sampling for 1-RTT ACKs, and PTO backoff reset after ACK.
- Gap: time-threshold loss and persistent congestion are utility methods, but they are not wired into runtime timers.
- Gap: Initial and Handshake CRYPTO loss recovery is incomplete.  ACK processing returns early for non-1RTT levels, and PTO is not scheduled for those packet spaces.
- Gap: retransmission stores shallow frame refs and an unbounded retransmit queue.  There is no ngtcp2-style frame-chain lifecycle, stale retransmit filtering depth, or retransmit drop policy beyond `controlStillValid_()`.
- Gap: congestion-control side effects of ACK/loss are not wired into runtime send behavior.

## Frame scheduling

- `zquic` prioritizes queued control frames before streams and can append pending ACKs to outgoing packets.
- Divergence: it sends one queued control frame per packet and at most one stream frame per packet.  ngtcp2 builds richer packets with multiple eligible frames under packet, congestion, flow, and path budgets.
- Gap: no full scheduler for CRYPTO, ACK, control, stream, path validation, DATAGRAM, PMTUD, and retransmission priorities comparable to ngtcp2.

## Path management

- `zquic` has a `Path` utility with anti-amplification accounting, PMTUD state, probe attempts, hints, and blackhole handling.
- Gap: runtime send/receive paths do not integrate `Path` state.  Budgets use `app()->maxUDP()` directly rather than active path MTU, anti-amplification allowance, or PMTUD probe state.
- Gap: PATH_CHALLENGE receive queues PATH_RESPONSE, but there is no full path validation state machine, no migration, no preferred-address handling, and no path-bound DCID lifecycle comparable to ngtcp2.
- Gap: PMTUD probes are represented in `Path`, but runtime PMTUD send/ACK/loss/expiry behavior is not wired into packet sending.

## Security

- `zquic` supports TLS integration through zpicotls, Initial secret derivation, Handshake/1-RTT traffic secrets, packet protection, key updates, Retry integrity, and stateless reset verification.
- Gap: key update handling is optimistic: on short-packet decrypt failure it tries the next receive secret and installs a peer key update if decrypt succeeds.  There is no full old/new key retention window or ngtcp2-style key phase validation behavior.
- Gap: stateless reset token management is simpler than ngtcp2's path/DCID tracker and token uniqueness checks.
- Gap: anti-amplification exists in `Path`, but is not integrated into runtime server send budgets.

## Datagram support

- Gap: no QUIC DATAGRAM frame support.  `FrameType` has no DATAGRAM value, and frame parse/write paths do not include DATAGRAM.
- Gap: no `max_datagram_frame_size` transport parameter behavior, DATAGRAM send API, or DATAGRAM receive callback comparable to ngtcp2.

## Application interface

- `zquic` exposes stream creation/send, endpoint callbacks, connection callbacks, diagnostics, and CRTP hooks.
- Gap: no public QUIC DATAGRAM API, no migration/path callbacks, no rich transport error surface, no congestion/path/qlog stats comparable to ngtcp2.
- Divergence: many base callbacks are no-ops, so unsupported or unhandled control behavior can disappear unless an application override is present.

## Timers

- Runtime timer coverage is effectively PTO-only.
- Gap: no ACK delay timer, loss-time timer, idle timeout, draining timer, key discard timer, PMTUD timer, or path validation timer wired into runtime.
- ngtcp2 centralizes these decisions through connection expiry/loss-detection paths and chooses the next deadline across ACK delay, loss, PTO, path validation, stale CID retirement, and handshake/early-data discard.

## Instrumentation

- `zquic` has runtime counters and debug log points.
- Gap: no qlog event stream, packet/frame qlog emission, congestion metrics qlog, or comparable structured tracing.

## Principal corrections from review

- The earlier claim that stream receive reassembly is partial was incorrect.  `zquic`'s ordinary STREAM receive reassembly is handled by `ZmPQueue`/`ZmPQRx` and is covered by targeted tests.
- The real stream gaps are not basic reassembly; they are stream-control frame emission, lifecycle edge cases, invalid-activity handling, and callback/error granularity compared with ngtcp2.
