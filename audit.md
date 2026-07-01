# zquic QUIC Implementation Audit

This audit compares the current `zquic` implementation against the functional
breakdown in `arc/zquic4/quic.md`.  The primary reference is `../zngtcp2`;
`../msquic`, `../quiche`, and `../mvfst` were used as secondary checks for
common production behavior.

The previous `arc/zquic4/audit.md` is now substantially stale.  `zquic` is no
longer just a compact handshake/stream prototype: packet tracking,
multi-space PTO selection, ACK delay and loss timers, NewReno integration,
ECN accounting, reset/stop retransmission, path validation, PMTUD, dynamic
packet-number length, and Initial/Handshake coalescing are now present.

`zquic` is still not feature-equivalent to the references.  The main remaining
gaps are 0-RTT, qlog tooling/schema signoff, richer migration/preferred-address
handling, pacing and alternate congestion controllers, and a more complete ECN
path response.

## Connection lifecycle

`zquic` supports client and server handshakes, QUIC v1 packet protection, TLS
transport parameters, Version Negotiation response, client Retry parsing and
Retry integrity validation, stateless reset detection, and application/transport
close frames.

Closed or improved relative to the old audit:

- Key discard, ACK delay, loss, PTO, PMTUD, and path-validation timers now exist
  in the runtime.
- CONNECTION_CLOSE/APPLICATION_CLOSE emission is integrated with disconnect and
  distinguishes application close while the engine is stopping.
- Active idle timeout and close/draining retention are now implemented.  Idle
  timeout is negotiated from transport parameters, peer and local
  ack-eliciting activity update the timer, and close/drain retention uses the
  RFC `3 * PTO` basis.
- The client can process Retry and rekey Initial protection from the Retry SCID.
- Connection-ID routing and stateless reset token handling are materially more
  complete than before.
- Server address validation now has a stateless token policy for Retry and
  NEW_TOKEN.  Tokens are authenticated, bounded, address-bound, expire by
  policy, and carry Retry ODCID/SCID state needed for transport parameter
  validation.
- NEW_TOKEN can be emitted after 1-RTT establishment, retained client-side, and
  reused on a later Initial to satisfy configured server address validation
  without an extra Retry.

Remaining gaps:

- 0-RTT is not implemented as a runtime packet space.  `PktType::ZeroRTT` and
  crypto configuration names exist, but protected long-packet receive accepts
  only Initial and Handshake.

## Packet processing

`zquic` parses and writes QUIC v1 long/short headers, Retry, Version
Negotiation, packet numbers, varints, frame payloads, header protection, packet
protection, and coalesced datagrams.  It has separate Initial, Handshake, and
1-RTT packet spaces for ACK/recovery.

Closed or improved:

- Runtime packet-number length is selected from largest acknowledged state
  rather than remaining fixed.
- Initial/Handshake send coalescing now exists.
- ACK_ECN frames are parsed and written; received ECN marks are recorded.
- Long datagram receive walks coalesced long packets before a terminal short
  packet.

Remaining gaps:

- 0-RTT packets are parsed at header level but not decrypted or dispatched.
- ECN is partial.  The implementation tracks counters and validates monotonic
  ACK_ECN growth, but it does not yet expose the full reference behavior around
  path ECN validation, marking policy, fallback disablement, and congestion
  response.
- Packet builder policy is still simpler than the references: fewer packet
  composition modes and no pacing-driven write loop.

## Streams

Stream creation, stream ID limits, send buffering, receive reassembly,
duplicate/overlap suppression, FIN handling, reset handling, stop-sending
handling, and closed-stream diagnostics are present.

Closed or improved:

- The old audit's reset/stop emission gap is closed.  RESET_STREAM and
  STOP_SENDING are represented as retransmittable control frame refs and are
  queued, ACKed, lost, and retransmitted.
- Closed-stream control/data accounting is now explicit in diagnostics.
- Receive reassembly remains one of the stronger areas of the implementation.

Remaining gaps:

- Stream lifecycle callbacks and error surfacing are intentionally thinner than
  ngtcp2 and the larger stacks.
- Invalid repeated peer activity is counted, but there is no reference-style
  glitch rate limiter or broad policy framework.

## Flow control

Connection-level and stream-level limits, MAX_DATA, MAX_STREAM_DATA,
MAX_STREAMS, DATA_BLOCKED, STREAM_DATA_BLOCKED, STREAMS_BLOCKED, credit
advertisement, and blocked-state queuing are implemented.

Remaining gaps:

- Receive-window growth remains a simple local policy.  References commonly
  expose autotuning or more configuration around target windows and RTT.
- Application callbacks for blocked/unblocked and transport error reporting are
  less granular than in the reference implementations.

## Congestion control

`zquic` now integrates NewReno into send budgeting, bytes-in-flight accounting,
ACK processing, loss processing, and persistent congestion handling.  This
closes the earlier audit's "utility only, not runtime integrated" finding.

Remaining gaps:

- There is no CUBIC or BBR implementation.  zngtcp2, msquic, quiche, and mvfst
  all have richer congestion-controller selection.
- Pacing is not integrated into the runtime send loop.  `ZquicSched.hh` has a
  `Pacer` utility, but runtime sends are not paced like the production stacks.
- ECN congestion response is incomplete because ECN itself is only partially
  integrated.

## Retransmission

PTO calculation uses smoothed RTT, RTT variance, max ACK delay, and exponential
backoff.  PTO level selection now considers Initial, Handshake, and 1-RTT
packet spaces, and PTO reclaim can drive retransmission.  ACK processing records
RTT samples, clears PTO backoff, and feeds congestion state.

Closed or improved:

- PTO is no longer 1-RTT-only.
- ACK delay timer and delayed ACK emission are wired into runtime.
- Retransmittable frame refs cover CRYPTO, STREAM, flow/control frames,
  PATH_CHALLENGE/PATH_RESPONSE, HANDSHAKE_DONE, RESET_STREAM, and STOP_SENDING.

Remaining gaps:

- Probe generation and anti-deadlock behavior are simpler than ngtcp2's full
  packet-space expiry model.
- Retransmission storage is compact frame-ref based rather than the richer
  frame-chain/lifetime machinery in the reference implementations.

## Loss recovery

`zquic` has packet tracking, ACK range processing, packet-threshold loss,
time-threshold loss, loss timer scheduling, in-flight byte accounting, crypto
frame recovery, stream/control retransmission, and persistent congestion
detection.

Closed or improved:

- Time-threshold loss and persistent congestion are now runtime paths, not just
  unused helpers.
- Initial and Handshake packet spaces participate in loss/PTO machinery.
- ACK/loss side effects update NewReno and send diagnostics.

Remaining gaps:

- Recovery scanning is intentionally batched and simpler than mature reference
  implementations.
- There is no pacing interaction, ECN reaction, or alternate congestion
  controller integration.

## Frame scheduling

The scheduler prioritizes ACKs, retransmits, queued control frames, stream data,
path validation, close frames, and PMTUD probes under packet/path/congestion
budget constraints.

Closed or improved:

- Packets can now carry multiple recorded frame refs up to `SentPkt::MaxFrames`;
  the old "one control plus one stream" limitation is no longer accurate.
- Initial/Handshake coalescing and ACK/control coalescing are present.
- Path validation and PMTUD probes are part of send scheduling.

Remaining gaps:

- The packet builder is still less general than zngtcp2/quiche/mvfst schedulers,
  especially around pacing, 0-RTT, and broad priority policy.
- Server retransmit/send ref handling is more constrained than client handling
  because server packet emission goes through path-aware endpoint routing.

## Path management

`zquic` now has active path state, anti-amplification accounting, validation
challenges, PATH_RESPONSE handling, PMTUD probe state, blackhole/retry handling,
path hints from sockets, and path diagnostics.

Closed or improved:

- PATH_CHALLENGE/PATH_RESPONSE scheduling and validation are runtime behavior.
- PMTUD probes are sent and are acknowledged/lost through recovery.
- Send budgets use active path maximum UDP size and path allowance, not just the
  application maximum UDP size.

Remaining gaps:

- NAT rebinding and migration handling are limited.  Path observation exists,
  but there is no full active migration policy comparable to the references.
- Preferred address, disable-active-migration policy, and path-bound CID
  lifecycle are not complete.
- Multipath is not supported.

## Security

TLS integration, Initial secret derivation, Handshake/1-RTT traffic secrets,
header/payload protection, Retry integrity validation, stateless reset,
transport parameters, peer key update receive handling, old-key acceptance, and
key discard timers are implemented.

Closed or improved:

- Key update handling is no longer just optimistic "try next key and install";
  old key state and invalid key phase accounting are present.
- Stateless reset token generation/verification and route lookup are integrated.
- Retry SCID transport parameter handling is present.
- Server-side Retry and NEW_TOKEN address-validation tokens are generated and
  validated against authenticated payloads, peer address, token kind, expiry,
  and original destination CID.

Remaining gaps:

- Key update behavior is still simpler than the reference stacks' complete
  packet-number/key-phase validation and retention policy.
- Address validation remains intentionally compact compared with the reference
  stacks: the default policy is stateless, opt-in, and does not provide a
  broader server token-service framework or one-shot token replay database.

## Application interface

`zquic` exposes CRTP callbacks, stream APIs, disconnect/close hooks, diagnostics,
path diagnostics, active path MTU inspection, and test hooks.

Remaining gaps:

- No rich migration/path event API.
- Error reporting and callback coverage are thinner than the reference stacks.
- Several base callbacks are no-ops by design, so applications must override the
  hooks they care about.

## Timers

Implemented runtime timers include ACK delay, loss, PTO, idle timeout, close
retention, key discard, PMTUD, and path validation.  Existing `3 * PTO`
calculations cover key discard, persistent congestion, path validation
deadlines, and close/draining retention.

Closed or improved:

- The old "PTO-only runtime timer coverage" finding is closed.
- Loss/PTO scheduling now selects across packet spaces.
- ACK delay expiry flushes pending 1-RTT ACKs.
- Idle timeout is actively enforced after handshake establishment.
- Local application close uses the minimum close timer; peer-initiated close
  enters draining and retains state for the RFC-derived close/drain period.
- PMTUD and path-validation expiries are wired.

Remaining gaps:

- Timer selection remains distributed and narrower than the reference stacks'
  central expiry decisions.

## Instrumentation

`zquic` has structured runtime diagnostics, counters for packet/frame/control
activity, RTT/loss/congestion/path metrics, ECN counters, debug logs, and a
debug-gated qlog writer.  The qlog path is configured from `EngineParams`
(`qlog`, `qlogPath`, `qlogThread`, `qlogRingSize`, `qlogAge`), starts/stops
with the engine, writes JSON-SEQ records, ages existing output files before
opening the new sink, maintains enqueue/write/drop diagnostics, and has tests
for file output, archive aging, and ring back-pressure.  `Zquic_DEBUG` gating
is intentional: non-debug builds compile this path to no-op stubs.

Current qlog state:

- `ZquicLogger` owns the writer thread and ring; `ZquicLOG((lambda))` is the
  runtime call-site marker and checks qlog runtime availability before lambda
  capture evaluation.  Debug text logging is separate and uses `ZiLOG(Debug, ...)`.
- Writer output uses `ZtJSON`/`ZtStruct`.  Closed qlog vocabularies are modeled
  as `ZtEnumMap`-backed enum fields rather than preformatted strings.  Runtime
  qlog call sites capture small scalar metadata by value and construct
  qlog-specific structs on the logger thread.
- Runtime emission now covers datagrams sent/received, packet sent/received/
  buffered/dropped/lost, packets ACKed, bounded frame summaries, recovery RTT/
  loss/PTO timer/congestion/ECN events, PTO expiry/backoff/probe generation,
  TLS/security events, Retry/NEW_TOKEN/token-rejection/protection failures,
  path validation/PMTUD/path-admission events, CID issue/retire/route binding,
  stream state, stream data movement, flow-control blocked/unblocked, local and
  peer close, drain expiry, and idle timeout.
- Header metadata now includes qlog version/format, implementation identity,
  vantage point, and connection identity/CID data where available.  Runtime
  tests cover server trace metadata and redaction of application payload data.
  This remains a known schema-shape deviation from the latest qlog main draft,
  which uses `file_schema`, `serialization_format`, `trace.event_schemas`, and
  `trace.common_fields` in `QlogFileSeq`.
- Local tests validate JSON-SEQ framing strictly enough to reject malformed
  records and trailing data.  Writer tests cover typed transport, recovery,
  security, path/CID, stream, and close events; runtime tests cover representative
  endpoint, stream, token, retry, packet-protection, close, idle, path-admission,
  flow-control, and PTO traces.

Remaining gaps:

- Final non-debug/no-op signoff has passed.  The tree was configured with
  `./z.config -L /usr`; generated makefiles show `-O3 -g -DNDEBUG` for
  `zquic` and downstream `zhttp`, with no `Zquic_DEBUG`; top-level
  `make clean` followed by top-level `make -j8` completed successfully.
- External qlog-tool validation has been performed on representative retained
  runtime traces generated with `ZQUIC_TEST_KEEP=1`.  Endpoint, Retry/token,
  token-rejection/policy, and idle-timeout `.sqlog` files parse with
  `jq --seq`; `blazingqlog` also loads those JSON-SEQ files and extracts event
  names from them.  Remaining mainstream-parity deviations are schema shape, not
  parseability: the latest qlog main/QUIC drafts use `QlogFileSeq` header fields
  such as `file_schema` and `serialization_format`, and the registered QUIC
  event namespace is `quic:*`; current zquic output still uses the older
  `qlog_version`/`qlog_format` header style and category event names such as
  `transport:*`, `recovery:*`, `security:*`, `path:*`, and `connectivity:*`.
- Connection-level MAX_DATA unblocked coverage now has an end-to-end runtime
  scenario in `ZquicStreamTest::testFlowControlQLog`: a real stream send first
  fails on exhausted connection data credit and queues DATA_BLOCKED, then
  MAX_DATA clears the queued control and allows the stream send to proceed.
- Multi-connection trace identity expectations for mainstream qlog consumers are
  not yet validated.  Current tests cover single-process metadata and a server
  endpoint trace.
- Receive-side buffered-packet qlog coverage is not applicable to the current
  implementation because undecryptable/missing-key packets are dropped and
  logged as packet drops plus packet-protection failures, rather than buffered.
- Pacer delay is not represented in qlog yet because pacing is not integrated.

## Completed priority work

1. Active idle timeout and closing/draining timer behavior are implemented and
   covered by `make -C zquic/test test` and `make -C zhttp/test test`.
2. Server address-validation policy is implemented: Retry token
   generation/validation, NEW_TOKEN emission, client token reuse, address/ODCID
   binding, diagnostics, and codec/runtime tests are present.

## Priority work

1. Finish qlog schema parity: migrate the JSON-SEQ header to the latest
   `QlogFileSeq` shape and either adopt the registered `quic:*` event namespace
   or explicitly retain/document the current category namespaces as a deliberate
   compatibility target.
2. Integrate pacing and consider CUBIC/BBR selection if production WAN behavior
   matters.
3. Finish ECN as a path feature: socket marking, validation state, fallback, and
   congestion response.
4. Extend migration/preferred-address/CID lifecycle if mobile/NAT-rebinding
   behavior is a target.
