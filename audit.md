# zquic QUIC Implementation Audit

This audit compares the current `zquic` implementation against QUIC v1
transport behavior, the live `zquic` code/tests, and mainstream production
stacks.  The primary reference is `../zngtcp2`; `../msquic`, `../quiche`, and
`../mvfst` were used as secondary checks for common production behavior.

`zquic` is no longer just a compact handshake/stream prototype: packet
tracking, multi-space PTO selection, ACK delay and loss timers, NewReno
integration, ECN receive accounting and ACK_ECN validation, reset/stop
retransmission, path validation, PMTUD, dynamic packet-number length, and
Initial/Handshake coalescing are now present.

`zquic` is still not feature-equivalent to the references.  The main remaining
gaps are richer migration/preferred-address handling, pacing and alternate
congestion controllers, a more complete ECN path response, production
application session-cache integrations for 0-RTT, and extension features such
as QUIC DATAGRAM/WebTransport.

## Connection lifecycle

`zquic` supports client and server handshakes, QUIC v1 packet protection, TLS
transport parameters, Version Negotiation response, client Retry parsing and
Retry integrity validation, stateless reset detection, and application/transport
close frames.

Current implemented behavior:

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

- 0-RTT packet processing now exists, but persistent session-ticket storage,
  cache eviction, and application-specific remembered-parameter policy remain
  outside zquic's core runtime hooks.

## Packet processing

`zquic` parses and writes QUIC v1 long/short headers, Retry, Version
Negotiation, packet numbers, varints, frame payloads, header protection, packet
protection, and coalesced datagrams.  It has separate Initial, Handshake, and
1-RTT packet spaces for ACK/recovery.

Closed or improved:

- Runtime packet-number length is selected from largest acknowledged state
  rather than remaining fixed.
- Initial/Handshake send coalescing now exists.
- ACK_ECN frames are parsed and written; received ECN marks are recorded, and
  peer ACK_ECN counters are validated for monotonicity and impossible totals.
- Long datagram receive walks coalesced long packets before a terminal short
  packet.

Remaining gaps:

- 0-RTT packets are protected and dispatched, but undecryptable/reordered 0-RTT
  received before usable Initial processing is still dropped rather than
  buffered.
- ECN is partial.  The implementation tracks counters, emits ACK_ECN, validates
  peer ACK_ECN growth, and disables ECN on invalid peer counters, but it does
  not yet expose the full reference behavior around active socket marking, ECN
  probing policy, and CE-driven congestion response.
- Packet builder policy is still simpler than the references: fewer packet
  composition modes and no pacing-driven write loop.
- QUIC DATAGRAM frames are intentionally unsupported unless a future extension
  negotiates them; WebTransport is outside the current `zquic` transport API.

## Streams

Stream creation, stream ID limits, send buffering, receive reassembly,
duplicate/overlap suppression, FIN handling, reset handling, stop-sending
handling, and closed-stream diagnostics are present.

Closed or improved:

- RESET_STREAM and STOP_SENDING are represented as retransmittable control
  frame refs and are queued, ACKed, lost, and retransmitted.
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

`zquic` integrates NewReno into send budgeting, bytes-in-flight accounting,
ACK processing, loss processing, and persistent congestion handling.

Remaining gaps:

- There is no CUBIC or BBR implementation.  zngtcp2, msquic, quiche, and mvfst
  all have richer congestion-controller selection.
- Pacing is not integrated into the runtime send loop.  `ZquicSched.hh` has a
  `Pacer` utility, but runtime sends are not paced like the production stacks.
- ECN congestion response is incomplete: invalid ACK_ECN disables ECN, but
  outgoing ECN marking and CE-driven congestion reaction are not integrated.

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
- Preferred address and path-bound CID lifecycle are not complete.
- `disable_active_migration` is parsed and advertised, but enforcement is not a
  full migration policy comparable to the reference stacks.
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
- qlog key-update/key-discard writer payloads now use the current draft key
  event shape instead of generic security fields: qlog `$KeyType`, optional
  AppData `key_phase`, and standard key triggers are serialized through
  `ZtEnumMap` / `ZtJSON` on the logger thread.

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

- No rich migration/path event API beyond validation and failure hooks.
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
  peer close, drain expiry, and idle timeout.  Qlog frame summary objects now
  use current-draft field names for the generic scalar data already captured
  by zquic, including bounded ACK frame `acked_ranges` emitted as nested
  numeric arrays, stream-type fields for MAX_STREAMS / STREAMS_BLOCKED, and
  NEW_CONNECTION_ID `connection_id` / `stateless_reset_token` fields for
  parsed Rx and direct writer frame data.  The logger-thread frame serializer
  now emits only fields applicable to each frame type instead of default-zero
  placeholders.  Remaining frame-detail work is limited to plumbing
  NEW_CONNECTION_ID CID/token bytes through Tx control frame references if
  locally sent NEW_CONNECTION_ID frames are added.
- Header metadata now uses the current JSON-SEQ `QlogFileSeq` shape:
  `file_schema`, `serialization_format`, `trace.common_fields`,
  `trace.vantage_point`, and `trace.event_schemas`.  It advertises the
  current draft QUIC event schema URI plus a private zquic extension schema URI
  and includes implementation identity, vantage point, relative time metadata,
  and connection identity/CID data where available.  Runtime tests cover server
  trace metadata and redaction of application payload data.
- Local tests validate JSON-SEQ framing strictly enough to reject malformed
  records and trailing data.  Writer tests cover typed transport, recovery,
  security, path/CID, stream, and close events; runtime tests cover representative
  endpoint, stream, token, retry, packet-protection, close, idle, path-admission,
  flow-control, and PTO traces.

Completed qlog signoff:

- Final non-debug/no-op signoff has passed.  The tree was configured with
  `./z.config -L /usr`; generated makefiles show `-O3 -g -DNDEBUG` for
  `zquic` and downstream `zhttp`, with no `Zquic_DEBUG`; top-level
  `make clean` followed by top-level `make -j8` completed successfully.
- External qlog-tool validation has been performed on representative retained
  runtime traces generated with `ZQUIC_TEST_KEEP=1`.  Endpoint, Retry/token,
  token-rejection/policy, and idle-timeout `.sqlog` files parse with
  `jq --seq`; `blazingqlog` also loads those JSON-SEQ files and extracts event
  names from them.  Retained `ZquicStreamTest` qlog traces for path validation,
  PMTUD, CID issue/retire, ECN validation, and loss/PTO also pass `jq --seq -e .` and
  `blazingqlog <file> -p name`.  The same signoff now covers direct writer
  traces and `ZquicAPITest` stateless-reset qlog output.  Event/data-shape
  alignment for currently emitted `quic:*` events is complete to the
  mainstream-equivalence scope: current draft QUIC events use the qlog field
  model where zquic has comparable scalar state, and event/field data extending
  beyond that model either follows mainstream reference implementation
  precedent or remains under the private `zquic:*` event schema.  The
  `quic:path_validated` extension now keeps a minimal `success` / `vantage`
  payload shape rather than carrying zquic-local path detail
  fields under the `quic:*` namespace; `vantage` is enum-mapped on the
  logger thread rather than carried as arbitrary string event data.
- Packet and UDP datagram writer payloads now use the current qlog
  `header`/`raw` and datagram `count`/`raw[]`/`ecn[]` structure; where packet
  events keep an explicit packet-number-space scalar, it uses the
  `packet_number_space` spelling and qlog `$PacketNumberSpace` enum values;
  private packet-protection diagnostics use the same field spelling.  Standard
  packet drop/buffer output maps zquic's internal packet classifications to
  qlog `trigger` values and no longer emits private `reason` strings such as
  `parse_long` or `anti_amplification` under `quic:*` packet events.
  Recovery metrics, congestion-state, loss, timer, and ECN events now have
  dedicated standard-shape writers, and standard `quic:key_updated` /
  `quic:key_discarded` events now emit qlog `$KeyType`, optional AppData
  `key_phase`, and qlog key-update triggers rather than the generic zquic
  security payload.  `quic:parameters_set` now emits bounded current-draft
  transport parameter fields with `initiator`, and `quic:alpn_information`
  emits `chosen_alpn.string_value` instead of generic security fields.
  `quic:version_information` now emits the current draft version-array payload
  (`server_versions`, `client_versions`, optional `chosen_version`) for the
  server Version Negotiation no-overlap case, with versions serialized as qlog
  `QuicVersion` hex strings instead of generic security `kind` / `reason` /
  `value` fields.  Qlog event names are enum-mapped through
  `EventName`.  `quic:connection_closed` no longer carries the former
  zquic-local `application` and `frame` boolean fields in its standard payload;
  known zquic transport close codes now map to qlog `$TransportError` strings
  such as `frame_encoding_error`, empty close reasons are omitted, and
  `error_code` is retained only for unknown close errors.
  `quic:mtu_updated` now emits the draft `new` / `done` payload instead of the
  generic zquic path event object.  `quic:tuple_assigned` now emits the draft
  `tuple_id` payload instead of zquic-local path `kind` / `action` / `reason`
  and deadline fields.  `quic:connection_id_updated` now emits the draft
  `initiator` plus `old` or `new` CID payload instead of zquic-local
  `kind` / `action` / `reason` / sequence / reset-token flag fields.
  Private `zquic:zero_rtt_rejected` coverage now records runtime 0-RTT
  rejection causes from TLS, transport-parameter, frame-policy, missing-key, and
  late-after-1RTT paths using enum-mapped security reason data.
  Future event data-shape work should be limited to behavior zquic exposes but
  does not yet implement in comparable form, such as TLS alert/handshake failure
  details, client-side Version Negotiation received coverage if implemented,
  richer migration/preferred-address events, and optional packet metadata such
  as datagram IDs where zquic later plumbs those scalar snapshots.
- `quic:packets_acked` now uses the draft `packet_number_space` and
  `packet_numbers` field names, and runtime ACK processing now snapshots the
  bounded set of newly acknowledged packet numbers from `PktTxUpdate` inside
  `ZquicLOG((...))` instead of fabricating a singleton from largest ACKed.
  ACK frame summaries now include bounded `acked_ranges` as draft-shaped nested
  numeric arrays, populated from parsed Rx ACK frames and from the Tx ACK
  snapshot inside the `ZquicLOG((...))` capture path.  Richer ACK event detail
  remains to be added only where it matches current qlog or reference-
  implementation expectations; any local counters such as acked/lost byte
  totals should be private extension data if they are retained.  Packet-
  number space JSON now uses the current `$PacketNumberSpace` value
  `application_data` for the QUIC application-data packet number space.
- `quic:recovery_metrics_updated` now uses the current draft field names and
  units for RTT/congestion snapshots: `latest_rtt`, `smoothed_rtt`,
  `rtt_variance`, `min_rtt` in milliseconds, plus `congestion_window`,
  `ssthresh`, and `bytes_in_flight`.  The generic recovery writer no longer
  emits the old `_us` RTT fields or `cwnd` default fields into unrelated
  recovery events.
- `quic:timer_updated` now uses the current draft timer data shape:
  `timer_type`, `packet_number_space`, `event_type`, and `delta`.  Zquic-local
  PTO backoff/probe details are not emitted as extra fields on this standard
  event; if those diagnostics are kept, they should be represented as a
  reference-backed `quic:*` extension or a private `zquic:*` event.
- `quic:packet_lost` now uses a dedicated current-draft data object with
  `header.packet_type`, `header.packet_number`, and enum-mapped `trigger`
  values (`reordering_threshold`, `time_threshold`, `pto_expired`).  The event
  name itself is also enum-mapped through `ZtJSON`; the generic recovery writer
  is no longer used for packet-lost records.
- `quic:ecn_state_updated` now uses the current draft `old` / `new` ECN state
  shape with qlog ECN states (`unknown`, `capable`, `failed`).  Zquic-local ECN
  validation reasons and counters are not emitted as extra fields on this
  standard event; add a private `zquic:*` diagnostic event if those details are
  needed by tooling.  Runtime qlog coverage now drives ACK_ECN validation
  success and failure and asserts the standard state transition output.
- Connection-level MAX_DATA unblocked coverage now has an end-to-end runtime
  scenario in `ZquicStreamTest::testFlowControlQLog`: a real stream send first
  fails on exhausted connection data credit and queues DATA_BLOCKED, then
  MAX_DATA clears the queued control and allows the stream send to proceed.
- Multi-connection trace identity no longer relies on a mutable global event
  linkInfo slot.  The trace header keeps its configured role/header identity,
  while event identity is carried as by-value `Zquic::LinkInfo`
  (`origDCID`, `groupID`, `dcid`, `scid`) on each event that has link identity
  available.  The qlog writer serializes that linkInfo as a `ZtStruct` UDT
  under `common_fields` and omits it when the sentinel-null value is empty.
  `ZquicLogTest` covers distinct per-event CID linkInfo snapshots and runtime
  tests cover the server endpoint trace metadata.
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
3. 0-RTT packet processing is implemented: separate early traffic-key state,
   transport-parameter compatibility checks, default-deny application hooks,
   conservative zhttp request policy, early stream marking, rejection cleanup,
   and qlog diagnostics are covered by focused zquic/zhttp tests.

## Priority work

1. Integrate pacing and consider CUBIC/BBR selection if production WAN behavior
   matters.
2. Finish ECN as a path feature: active socket marking, probing policy, and
   CE-driven congestion response.
3. Extend migration/preferred-address/CID lifecycle if mobile/NAT-rebinding
   behavior is a target.
4. Add production 0-RTT session-cache integrations in applications that need
   persisted resumption tickets, remembered HTTP/3 settings, and cache eviction.
5. Add QUIC DATAGRAM/WebTransport extension support only if those extension
   use cases become targets.
