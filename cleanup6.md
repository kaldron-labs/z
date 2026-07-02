`Zquic` qlog parity plan
========================

`ZquicLog` provides the debug-gated writer infrastructure and now has partial
typed qlog event coverage.  `Zquic_DEBUG` gating is intentional and should
remain.  Existing runtime qlog event families have been aligned with
the `# qlog` section of `zquic/GUIDELINES.md`: qlog-specific work must be
compiled out of non-`Zquic_DEBUG` builds, and must also be skipped before
lambda capture evaluation when qlog is disabled by configuration.

Scope target: mainstream qlog equivalence, not exhaustive or historical
coverage.  The implementation should match the current qlog schema and the
practical event/field coverage exposed by mainstream QUIC reference
implementations for comparable zquic features.  Equivalence means a normal
qlog parser/viewer and an engineer familiar with traces from implementations
such as quiche, ngtcp2, quic-go, msquic, or mvfst can inspect zquic traces for
the same mainstream scenarios without custom interpretation.  It does not mean
100% coverage of every draft field, every reference implementation extension,
or every internal zquic state transition.  When zquic needs diagnostic detail
that is outside the current schema and not backed by reference implementation
precedent, keep it under the private `zquic:*` schema or leave it out.

Current state
-------------

Implemented:

- `ZquicLogParams` exposes `qlog`, `qlogPath`, `qlogThread`, `qlogRingSize`,
  and `qlogAge` through engine parameters.
- `ZquicLogger` is the internal writer-thread object.  `ZquicLog(lambda)` is
  the qlog function, and `ZquicLOG((lambda))` is the source marker macro used
  by runtime qlog instrumentation.
- `ZquicLog` starts/stops with the engine and writes JSON-SEQ records through
  the qlog writer thread.
- qlog file output now follows the `ZiLog` file-sink aging precedent: an
  existing configured `.sqlog` path is archived through the configured age
  depth before the new sink is opened.  `ZquicLogTest::testQLogFileOutput`
  seeds an old qlog file and verifies it is moved to `.1`.
- diagnostics count records enqueued, written, dropped, ring pressure, writer
  failures, and bytes written.
- non-debug builds compile the qlog API to no-op stubs.
- qlog writer code uses `ZtJSON` / `ZtStruct` JSON mappings.
- arbitrary qlog string fields now own their data with `ZeString`; borrowed
  `ZuCSpan` is reserved for static non-event literals such as header metadata.
- qlog header emission now snapshots endpoint role and connection identity
  `LinkInfo`.  `ZquicLogger` is the process-wide `ZmSingleton` initialized once
  by the owning app during application `init` and finalized after protocol
  machinery is drained during application `final`; after initialization,
  `ZquicLogger::enabled()` is the single runtime availability check.  Direct
  writer tests assert a configured `client` vantage point plus hex-encoded
  `common_fields` for ODCID, group id, DCID, and SCID.  Header CID fields use
  sentinel-null omission when empty, so absent metadata is not emitted as empty
  JSON strings and is not removed by string post-processing.  Runtime server
  traces assert `"type":"server"` plus non-empty ODCID, group id, DCID, and
  SCID in event `common_fields`.
- protocol-derived enum fields use `ZtEnumMap` JSON mappings instead of
  helper functions that convert enums to strings before serialization.
- qlog event names now use the `EventName` closed vocabulary with a
  `ZtEnumMap` JSON mapping.  Runtime and direct writer call sites pass enum
  values, so event-name strings are confined to the map and output assertions.
- typed event structs and writer helpers exist for datagram, packet/frame
  summary, ACK, recovery, ECN, security, path, and CID events.
- `FrameEvent` is marked as a Z POD/plain snapshot type so bounded
  frame arrays survive by-value lambda/ring movement without invoking
  destructive string moves.
- runtime emission has been added for datagrams, packet sent/received/dropped,
  bounded packet frame summaries, packets ACKed, loss/retransmission marking,
  recovery metrics/timers/congestion/ECN, TLS/key/transport-parameter/ALPN
  events, path validation/PMTUD/path-admission events, and CID issue/retire/
  route-binding events.
- datagram sent/received, packet sent/received, and receive-side packet-drop
  qlog call sites now visibly use `ZquicLOG((...))` at the protocol decision
  point; the former `qlogDatagramRx_`, `qlogDatagramTx_`, `qlogPktDrop_`,
  `qlogPktRx_`, and `qlogPktTx_` helper wrappers have been removed.
- security qlog call sites for TLS failure, transport parameters, ALPN, key
  update, and key retirement now visibly use `ZquicLOG((...))` at the protocol
  decision point; the former `qlogKeyUpdated_`, `qlogKeyRetired_`,
  `qlogTransportParams_`, `qlogALPN_`, and `qlogTLSFailure_` helper wrappers
  have been removed.
- path, PMTUD, and CID qlog call sites now visibly use `ZquicLOG((...))` at
  the protocol decision point; the former `qlogPath_`,
  `qlogPathValidation_`, `qlogPMTUD_`, and `qlogCID_` helper wrappers have
  been removed.
- ACK, loss, recovery metric, congestion, ECN, and loss/PTO timer qlog call
  sites now visibly use `ZquicLOG((...))` at the protocol decision point; the
  former `qlogPacketsAcked_`, `qlogLoss_`, `qlogMetrics_`,
  `qlogCongestion_`, `qlogECNState_`, and `qlogTimer_` helper wrappers have
  been removed.
- security/address-validation qlog coverage now includes typed
  `zquic:retry_sent`, `zquic:retry_validated`,
  `zquic:token_issued`, `zquic:token_validated`,
  `zquic:token_rejected`, `quic:version_information`, and
  `zquic:stateless_reset` events.  Packet protection failures now emit
  typed `zquic:packet_protection_failed` events with enum-mapped
  `packet_number_space`, trigger, and owned/redacted reason data.  Runtime
  call sites use
  `ZquicLOG((...))` for server Retry send/validation, NEW_TOKEN issue and
  validation, Version Negotiation sent, stateless reset sent/detected, and
  client Retry acceptance/rejection, plus Tx/Rx missing-key, protect/unprotect,
  and invalid key phase failures.
- generic/private security event `kind`, local `key_type`, `trigger`, and
  `reason` fields are now closed `ZtEnum` fields with `ZtEnumMap` JSON
  mappings.  Runtime call sites set the enums directly and the writer
  serializes the qlog spelling through `ZtJSON`, rather than storing strings
  only to output those strings again.  The local non-standard `key_type` enum
  maps `rx`, `rx_old`, and `tx`; `trigger` maps `sent`, `received`,
  `validated`, `local`, `remote`, `peer`, `selected`, `timer`,
  `handshake_complete`, `rx`, and `tx`; `reason` maps the current security
  status/failure vocabulary including token-validation statuses, Retry/VN/
  stateless-reset labels, packet-protection failures, and key/TLS lifecycle
  labels.  Each has a `None` value mapped to the existing empty JSON string for
  non-applicable events.
- standard `quic:key_updated` and `quic:key_discarded` no longer use the
  generic security payload.  They emit the current draft key-event fields:
  enum-mapped `key_type` values from the qlog `$KeyType` vocabulary
  (`client_*_secret` / `server_*_secret`), optional `key_phase` for 1-RTT
  events, and enum-mapped qlog triggers (`tls`, `remote_update`,
  `local_update`).  The logger thread maps captured local RX/TX key direction
  plus packet space and trace vantage point into the qlog client/server secret
  vocabulary, so JSON spelling remains in the logger-thread `ZtJSON` path.
- standard `quic:parameters_set` no longer uses the generic security payload.
  It emits current draft field names including enum-mapped `initiator`,
  transport parameter CIDs/tokens when present, `max_idle_timeout`,
  `max_udp_payload_size`, ACK timing fields, connection/stream credit limits,
  and `disable_active_migration`.  Runtime call sites snapshot local and peer
  transport parameters separately inside `ZquicLOG((...))`; the peer event now
  uses `m_crypto.peerTransportParams()` rather than accidentally repeating
  local parameters.
- security ALPN data remains an owned `ZeString` because ALPN is negotiated
  application protocol data, not a closed QUIC vocabulary.  Standard
  `quic:alpn_information` now emits the current draft `chosen_alpn` object
  with `string_value`; the runtime ALPN qlog call site captures
  `ZeString{m_crypto.negotiatedProtocol()}` by value inside
  `ZquicLOG((...))` and moves it into the event on the logger thread, so no
  borrowed `ZuCSpan` crosses the async boundary.
- standard `quic:version_information` no longer uses the generic security
  payload for the server Version Negotiation path.  It emits the current draft
  `server_versions` and `client_versions` arrays as fixed-width lowercase qlog
  `QuicVersion` hex strings, omitting `chosen_version` for the unsupported
  client-version/no-overlap case.  The runtime call site captures only the
  attempted client version inside `ZquicLOG((...))`; supported server versions
  and JSON formatting are constructed on the logger thread.
- path, PMTUD, and CID event `kind`, `action`, and `reason` fields are now
  closed `ZtEnum` fields with `ZtEnumMap` JSON mappings.  Runtime call sites
  capture enum values, construct path/CID qlog event structs with aggregate
  initializers inside `ZquicLOG((...))`, and the writer serializes the qlog
  spelling through `ZtJSON` rather than storing short string literals in
  `ZeString`.
- standard `quic:mtu_updated` no longer uses the generic path payload.  It
  emits the current draft MTU object with required `new` and optional `done`;
  zquic-local PMTUD probe/loss reasons should move to private diagnostics if
  needed instead of leaking into the standard event.
- packet buffering/drop call sites capture the local closed
  `PktEvent::Reason` vocabulary inside `ZquicLOG((...))`, but
  standard `quic:packet_dropped` / `quic:packet_buffered` output now maps that
  internal classification to qlog `trigger` values on the logger thread.
  Standard packet output no longer emits the old zquic-local `reason` field or
  private strings such as `coalescing`, `parse_long`, and
  `anti_amplification` under the `quic:*` event.
- transport packet buffering coverage now includes typed
  `quic:packet_buffered` events for protected Initial packets retained
  for long-header coalescing.  The runtime call site uses `ZquicLOG((...))`,
  captures only packet length and local enum classification by value, and
  constructs the packet qlog event on the logger thread.  The standard
  buffered event omits a trigger when the internal classification has no
  current qlog trigger equivalent.
- stream state event `stream_type`, `old`, `new`, `stream_side`, and `reason`
  are now closed `ZtEnumMap`-backed fields.  Runtime stream open/reap call
  sites capture enum values by value inside `ZquicLOG((...))`, construct the
  stream-state qlog event with aggregate initializers on the logger thread,
  and leave qlog string spelling to `ZtJSON`.
- stream data movement `additional_info` is now a closed
  `ZtEnumMap`-backed field for the current `fin_set` vocabulary.  STREAM FIN
  state is captured as a scalar flag and mapped to the enum inside the
  logger-thread event initializer; qlog string spelling is left to `ZtJSON`.
- close event `reason`, `connection_error`, and `application_error` are now
  closed `ZtEnumMap`-backed fields for the current lifecycle/error vocabulary:
  `local_close`, `peer_close_frame`, `drain_expired`, `idle`, qlog transport
  error names such as `no_error` and `frame_encoding_error`, and application
  error `unknown`.  Runtime close call sites capture only scalar close state,
  enum values, and the numeric close code; JSON spelling and known
  `$TransportError` mapping occur in the logger thread.  The standard
  `quic:connection_closed` payload no longer carries the former zquic-local
  `application` and `frame` boolean fields, and it omits empty `reason` plus
  default `error_code:0`.
- `ZquicLogTest` has focused typed writer coverage for transport, recovery,
  security, path, CID, and stream event JSON.
- `ZquicLogTest` now verifies packet frame summaries across ACK, CRYPTO,
  STREAM, RESET_STREAM, STOP_SENDING, MAX_STREAM_DATA, NEW_CONNECTION_ID,
  RETIRE_CONNECTION_ID, PATH_CHALLENGE, PATH_RESPONSE, NEW_TOKEN,
  HANDSHAKE_DONE, CONNECTION_CLOSE, ACK_ECN counters, ACK frame
  `acked_ranges`, MAX_STREAMS / STREAMS_BLOCKED `stream_type`,
  NEW_CONNECTION_ID `connection_id` and `stateless_reset_token` for parsed Rx
  and direct writer frame data, enum-mapped packet reasons, current-draft
  `frame_type` field naming, omission of frame fields that are not applicable
  to the frame type, truncation flags, and frame snapshot movement through the
  qlog ring path.
- `ZquicStreamTest::testRuntimeReceiveQLog` now provides end-to-end runtime
  receive-path qlog coverage for a protected short-header 1-RTT packet.  It
  verifies disabled qlog does not construct the receive accumulator, enabled
  qlog does construct it, and JSON-SEQ output contains
  `quic:packet_received` with packet number, PING frame data, frame count,
  and the frame truncation field.
- `ZquicLogTest` and `ZquicStreamTest` now use a stricter local JSON-SEQ
  scanner: every record must begin with RFC 7464 record separator, end with a
  newline, and be fully consumed by `ZtJSON::scan`.  This catches malformed
  JSON-SEQ records and trailing garbage instead of accepting a valid JSON
  prefix only.
- `quic:connection_started` now carries typed endpoint data from
  `ZiSockAddr` values.  The qlog writer serializes endpoint objects through a
  `ZtStruct` mapping over `ZiIP` and port fields, exposing qlog `ip_v4` and
  `port_v4` while leaving IP address text to `ZiIP`'s existing print support;
  there is no qlog-local IP byte shifting, string parsing, or redundant span
  reconstruction for socket addresses.
- `find<"x">` / `match<"x">` support in `zt` has been restored for the qlog
  JSON work.
- current security enum verification passes:
  `make -C zquic/src -j8`,
  `make -C zquic/test -j8 ZquicLogTest ZquicRuntimeTest`,
  `libtool exec ./zquic/test/ZquicLogTest`, and standalone
  `timeout 240s libtool exec ./zquic/test/ZquicRuntimeTest`.  A parallel run
  of `ZquicLogTest` and `ZquicRuntimeTest` produced an idle-qlog miss once;
  rerunning `ZquicRuntimeTest` alone passed.
- `make -C zquic/src -j8` currently passes after the latest pause point.
  The `mkfifo /tmp/GMfifo2: File exists` line is the recurring GNU make
  jobserver FIFO warning, not a qlog build failure.
- focused qlog/resume-check verification passes after the frame relocation,
  writer coverage, runtime receive coverage, and datagram/packet/drop helper
  removal plus security/path/PMTUD/CID and ACK/loss/recovery/timer helper
  removal:
  `make -C zquic/test -j8 ZquicLogTest ZquicStreamTest ZquicHandshakeTest`,
  `make -C zquic/test -j8 ZquicLogTest ZquicStreamTest ZquicPMTUDTest
  ZquicCIDTest`, `ZquicLogTest`, `ZquicStreamTest`, `ZquicHandshakeTest`,
  `ZquicPMTUDTest`, `ZquicCIDTest`, `make -C zquic/test -j8 ZquicLogTest
  ZquicStreamTest ZquicRecoveryTest`, and `ZquicRecoveryTest`.
- handshake/recovery smoke verification now passes:
  `make -C zquic/test -j8 ZquicHandshakeTest ZquicRecoveryTest`,
  `ZquicHandshakeTest`, and `ZquicRecoveryTest`.
- current security/address-validation and runtime qlog verification passes:
  `make -C zquic/test -j8 ZquicLogTest ZquicAPITest ZquicRuntimeTest`,
  `ZquicLogTest`, `ZquicAPITest`, and
  `timeout 180s libtool exec ./zquic/test/ZquicRuntimeTest`.
  `ZquicLogTest` covers `quic:packet_buffered` writer output and
  `ZquicRuntimeTest` includes qlog assertions for Retry/NEW_TOKEN,
  missing-token, malformed-token, token-authentication-failure, expired-token,
  address-mismatch, invalid token kind, and NEW_TOKEN policy
  `zquic:token_rejected`, plus `quic:packet_buffered` runtime output for
  coalescing in the current unrestricted runtime environment.
  `ZquicLogTest` also covers typed
  `quic:stream_state_updated` writer output, and
  `ZquicRuntimeTest::testRuntimeEndpointOpen` now asserts runtime stream-state
  qlog output for application stream open/close traffic.
- runtime stream flow-control qlog coverage now uses spec-aligned
  `quic:connection_data_blocked_updated` and
  `quic:stream_data_blocked_updated` events for DATA_BLOCKED,
  STREAM_DATA_BLOCKED, STREAMS_BLOCKED, MAX_DATA unblocking of a blocked
  connection, and MAX_STREAM_DATA unblocking of a pending sender.  Blocked
  state and reason fields are `ZtEnumMap`-backed JSON enum values, not
  preformatted strings.  These call sites use `ZquicLOG((...))`;
  blocked/unblocked probes are in macro capture initializers so they are
  skipped when qlog is disabled or compiled out.  Event struct construction
  remains on the logger thread.  `ZquicStreamTest::testFlowControlQLog`
  verifies JSON-SEQ output for both blocked event names,
  `connection_flow_control`, `stream_flow_control`, `"new":"blocked"`, and
  `"old":"blocked"` / `"new":"unblocked"`.
- stream/application data movement now has typed
  `quic:stream_data_moved` coverage aligned with the qlog QUIC event
  definition: `stream_id`, `offset`, `from`, `to`, optional
  `additional_info`, and nested `raw.length`.  `from` and `to` are now
  `ZtEnumMap`-backed JSON enum values (`application`, `transport`,
  `network`), not preformatted strings.  Runtime call sites emit application
  -> transport when stream Tx buffers accept application data, transport ->
  network when STREAM frame data is packetized for send, network -> transport
  when received STREAM frame data is accepted by the receive path, and
  transport -> application when queued Rx stream slices are delivered to the
  application-facing `RxStream`.  All call sites use `ZquicLOG((...))` with
  scalar captures and logger-thread event construction; payload bytes are not
  logged.  `additional_info` is also enum-mapped for the current `fin_set`
  value.
- current owned-string/frame-summary verification passes after removing the
  packet post copy shim and changing the direct qlog post APIs to move event
  values into the single `ZquicLogger::post_()` ring path instead of
  copy-capturing `ZeString`/frame payloads:
  `make -C zquic/src -j8`,
  `make -C zquic/test -j8 ZquicLogTest`,
  `ZquicLogTest`,
  `make -C zquic/test -j8 ZquicAPITest ZquicRuntimeTest`,
  `ZquicAPITest`, and
  `timeout 180s libtool exec ./zquic/test/ZquicRuntimeTest`.
  `ZquicLogTest` verifies packet trigger mapping and all packet frame summaries
  survive by-value lambda/ring movement.
- current qlog-adjacent focused verification also passes:
  `make -C zquic/test -j8 ZquicStreamTest ZquicHandshakeTest
  ZquicPMTUDTest ZquicCIDTest ZquicRecoveryTest`, `ZquicStreamTest`,
  `ZquicHandshakeTest`, `ZquicPMTUDTest`, `ZquicCIDTest`, and
  `ZquicRecoveryTest`.
- current stream flow-control qlog verification passes:
  `make -C zquic/src -j8`,
  `make -C zquic/test -j8 ZquicStreamTest`,
  `ZquicStreamTest`,
  `make -C zquic/test -j8 ZquicLogTest`, and `ZquicLogTest`.
- current stream data movement qlog verification passes:
  `make -C zquic/src -j8`,
  `make -C zquic/test -j8 ZquicLogTest ZquicRuntimeTest ZquicStreamTest`,
  `ZquicLogTest`, `ZquicStreamTest`, and
  `timeout 240s libtool exec ./zquic/test/ZquicRuntimeTest`.  Runtime
  endpoint qlog assertions now require the transport -> application movement
  leg via `"from":"transport"` and `"to":"application"`, and now assert that
  the application payload strings used by the runtime transfer are not present
  in qlog output.
- current stream-state enum verification passes:
  `make -C zquic/src -j8`,
  `make -C zquic/test -j8 ZquicLogTest ZquicStreamTest ZquicRuntimeTest`,
  `libtool exec ./zquic/test/ZquicLogTest`,
  `libtool exec ./zquic/test/ZquicStreamTest`, and
  `timeout 240s libtool exec ./zquic/test/ZquicRuntimeTest`.
- current stream-data-info and close enum verification passes:
  `make -C zquic/src -j8`,
  `make -C zquic/test -j8 ZquicLogTest ZquicStreamTest ZquicRuntimeTest`,
  `libtool exec ./zquic/test/ZquicLogTest`,
  `libtool exec ./zquic/test/ZquicStreamTest`, and
  `timeout 240s libtool exec ./zquic/test/ZquicRuntimeTest`.
- current ALPN ownership verification passes:
  `make -C zquic/src -j8`,
  `make -C zquic/test -j8 ZquicLogTest ZquicRuntimeTest`,
  `libtool exec ./zquic/test/ZquicLogTest`, and
  `timeout 240s libtool exec ./zquic/test/ZquicRuntimeTest`.
  `quic:alpn_information` now uses the current draft `chosen_alpn` /
  `string_value` data shape and no longer leaks generic security fields.
- current datagram sent/received verification passes:
  `make -C zquic/test -j8 ZquicLogTest ZquicRuntimeTest`,
  `libtool exec ./zquic/test/ZquicLogTest`, and
  `timeout 240s libtool exec ./zquic/test/ZquicRuntimeTest`.
  `ZquicLogTest::testQLogTypedTransportEvents` now asserts both
  `quic:udp_datagrams_received` and `quic:udp_datagrams_sent`, and
  `ZquicRuntimeTest::testRuntimeEndpointOpen` asserts runtime server-trace
  output for `quic:udp_datagrams_sent`.
- packet and UDP datagram writer data shapes have been moved closer to
  draft-ietf-quic-qlog-quic-events-12.  Packet events now serialize packet
  identity under `data.header` and byte lengths under `data.raw.length` /
  `data.raw.payload_length`; the explicit packet-number-space extension field
  now uses the draft spelling `packet_number_space` rather than the old local
  `packet_space`, including private packet-protection diagnostics; UDP datagram
  events now serialize
  `data.count`, `data.raw[]`, and enum-mapped `data.ecn[]`.  The old flat
  packet `packet_size`/`payload_size` fields and datagram `size` field are no
  longer emitted.  `ZquicLogTest::testQLogTypedTransportEvents` asserts the new
  packet/datagram fields, including ECN array spelling through `ZtJSON`.
- current path-admission packet-drop verification passes:
  `make -C zquic/src -j8`,
  `make -C zquic/test -j8 ZquicStreamTest ZquicLogTest`,
  `libtool exec ./zquic/test/ZquicStreamTest`, and
  `libtool exec ./zquic/test/ZquicLogTest`.  A broader runtime check also
  passes: `make -C zquic/test -j8 ZquicRuntimeTest` and
  `timeout 240s libtool exec ./zquic/test/ZquicRuntimeTest`.
  Path send rejection now logs `quic:packet_dropped` with the qlog
  `rejected` trigger for anti-amplification, probe-admission, and app-send
  rejection cases.  `quic:tuple_assigned` no longer carries blocked path
  reasons; it
  uses the current draft tuple-assignment payload.
  `ZquicStreamTest::testActivePathRuntimeBudget` now asserts
  `quic:packet_dropped` and the `rejected` trigger in runtime qlog output.
- current negative token/protection-failure runtime qlog verification passes:
  `make -C zquic/src -j8`,
  `make -C zquic/test -j8 ZquicRuntimeTest`,
  `timeout 240s libtool exec ./zquic/test/ZquicRuntimeTest`,
  `make -C zquic/test -j8 ZquicAPITest ZquicStreamTest`,
  `ZquicAPITest`, and `ZquicStreamTest`.
  `ZquicRuntimeTest::testRuntimeRetryAddressValidation` now asserts
  missing-token rejection output via `zquic:token_rejected` and
  `missing_token`.  `ZquicRuntimeTest::testRuntimeRejectedTokenQLog` now
  rewrites NEW_TOKEN-bearing Initial packets to assert malformed-token,
  token-authentication-failure, expired-token, address-mismatch, invalid token
  kind, and NEW_TOKEN policy rejection output via `malformed`, `auth`,
  `expired`, `address`, `kind`, and `new_token_policy`.  This required routing
  client path sends through the app `sendPkt` hook, matching the server-side
  app-gating pattern, so the runtime test can deterministically rewrite
  token-bearing Initial packets before UDP send.
  `ZquicStreamTest::testRuntimeReceiveQLog`
  now corrupts a protected short-header 1-RTT packet and asserts runtime output
  contains `quic:packet_dropped`, `zquic:packet_protection_failed`,
  and `protection`.
- current JSON-SEQ/redaction verification passes:
  `make -C zquic/test -j8 ZquicLogTest ZquicStreamTest ZquicRuntimeTest`,
  `ZquicLogTest`, `ZquicStreamTest`, and
  `timeout 240s libtool exec ./zquic/test/ZquicRuntimeTest`.
  The qlog test scanner now rejects trailing data after a parsed JSON value,
  and the endpoint runtime qlog test rejects leaked application payload strings
  such as `client-bidi`, `client-uni`, `server-bidi`, and `server-uni`.
- current role/header metadata verification passes:
  `make -C zquic/src -j8`,
  `make -C zquic/test -j8 ZquicLogTest ZquicRuntimeTest`,
  `ZquicLogTest`, and
  `timeout 240s libtool exec ./zquic/test/ZquicRuntimeTest`.
  `ZquicLogger::linkInfo()` records the configured endpoint role and owned
  header byte snapshots, rejects later opposite-role metadata for the same
  process-wide logger instance before the header is emitted, writes the role as
  `trace.vantage_point.type`, and writes connection identity bytes as hex
  through `ZtStruct`/`ZtJSON` `JSON::Hex` mappings rather than manual JSON.
  Per-event connection identity linkInfo is a CID-only `Zquic::LinkInfo`
  value (`origDCID`, `groupID`, `dcid`, `scid`) carried by events that have
  link identity available, serialized as a `ZtStruct` UDT under
  `common_fields`, and omitted by sentinel-null behavior when empty.  Event
  linkInfo is submitted with each event, so events from different linkInfo
  sources can be interleaved without relying on a frozen global event identity.
  `ZquicLogTest::testQLogFileOutput` asserts two event records carry distinct
  CID linkInfo snapshots.
  The server accept path now falls back from absent stored ODCID to the Initial
  header DCID before posting `connection_started`, so the first emitted header
  has non-empty ODCID/group/DCID/SCID in the endpoint runtime test.
- current spec-aligned blocked-event verification passes:
  `make -C zquic/src -j8`,
  `make -C zquic/test -j8 ZquicLogTest ZquicStreamTest`,
  `ZquicLogTest`, and `ZquicStreamTest`.
  `ZquicLogTest` covers writer JSON for
  `quic:connection_data_blocked_updated` and
  `quic:stream_data_blocked_updated`; `ZquicStreamTest` covers runtime
  flow-control qlog output for the same event names plus enum-mapped
  `blocked`/`unblocked` states and flow-control reasons.
- current close/drain lifecycle runtime verification passes:
  `make -C zquic/src -j8`,
  `make -C zquic/test -j8 ZquicLogTest ZquicRuntimeTest`,
  `ZquicLogTest`, and
  `timeout 240s libtool exec ./zquic/test/ZquicRuntimeTest`.
  `ZquicRuntimeTest::testRuntimeEndpointOpen` now forces the server close
  timer dispatch on the Tx thread after a peer close and asserts qlog output
  contains `quic:connection_closed`, peer/local close reason data,
  `drain_expired`, and `aborted`.  `connection_closed.initiator` and
  `connection_closed.trigger` are now `ZtEnumMap`-backed JSON enum values
  (`local`/`remote` and `application`/`error`/`idle_timeout`/`aborted`), not
  preformatted strings.  Close reason and error name fields are now also
  `ZtEnumMap`-backed JSON enum values for the current close vocabulary; known
  transport error codes map to qlog `$TransportError` strings, while
  `error_code` is retained only for unknown application/connection errors.
- current idle-timeout lifecycle runtime verification passes:
  `make -C zquic/test -j8 ZquicRuntimeTest` and
  `timeout 180s libtool exec ./zquic/test/ZquicRuntimeTest`.
  `ZquicRuntimeTest::testRuntimeIdleTimeoutQLog` drives the normal negotiated
  idle-timeout path and asserts qlog output contains
  `quic:connection_closed`, `idle_timeout`, `idle`, and `no_error`.
- current PTO qlog verification passes:
  `make -C zquic/src -j8`,
  `make -C zquic/test -j8 ZquicLogTest ZquicStreamTest`,
  `ZquicLogTest`, `ZquicStreamTest`,
  `make -C zquic/test -j8 ZquicRecoveryTest`, and
  `ZquicRecoveryTest`.  Runtime PTO paths now emit typed
  `quic:timer_updated` events for PTO expiry, PTO backoff change, and
  ping probe generation.  `quic:timer_updated` now uses the current draft
  schema fields (`timer_type`, `packet_number_space`, `event_type`, and
  `delta`) with `ZtEnumMap`-backed timer vocabularies serialized by `ZtJSON` on
  the logger thread.  Zquic-local PTO details such as backoff/probe counts are
  no longer emitted as ad hoc fields under the standard `quic:*` event.  Add a
  private `zquic:*` event later if those diagnostics are needed.  `ZquicLogTest`
  verifies the writer JSON shape for PTO and loss timers, and
  `ZquicStreamTest::testPTOQLog` drives the protected `Zquic.hh` PTO reclaim
  and probe-generation code paths and asserts runtime JSON-SEQ output contains
  `quic:timer_updated`, `timer_type: pto`, and `event_type: expired`.
- current retained path/CID/ECN/loss-PTO qlog verification passes:
  `make -C zquic/src -j8`,
  `make -C zquic/test -j8 ZquicLogTest ZquicAPITest ZquicRuntimeTest
  ZquicHandshakeTest ZquicRecoveryTest ZquicStreamTest ZquicPMTUDTest
  ZquicCIDTest`, `ZquicLogTest`, `ZquicAPITest`, `ZquicHandshakeTest`,
  `ZquicRecoveryTest`, `ZquicPMTUDTest`, `ZquicCIDTest`,
  `ZquicStreamTest`, and `timeout 240s libtool exec
  ./zquic/test/ZquicRuntimeTest` in an unrestricted local-network
  environment.  The restricted sandbox cannot bind the local UDP sockets needed
  by `ZquicRuntimeTest`, and fails before protocol/qlog assertions.
  `ZquicStreamTest::testPathValidationStateMachine` now proves both
  `success:true` and `success:false` `quic:path_validated` output,
  `ZquicStreamTest::testCIDQLog` proves standard
  `quic:connection_id_updated` output for local/remote CID issue and retire
  paths, `ZquicStreamTest::testAckECNValidationDisablesECN` proves standard
  `quic:ecn_state_updated` runtime output for ACK_ECN validation success and
  failure, and `ZquicStreamTest::testPTOQLog` proves
  `quic:packet_lost`, `quic:marked_for_retransmit`, and PTO timer output in a
  retained `.sqlog` file.
- current packet-number-space terminology and TLS epoch boundary cleanup passes:
  `make -C zquic/src -j8`,
  `make -C zquic/test -j8 ZquicCryptoTest ZquicHandshakeTest
  ZquicPacketProtectionTest ZquicLogTest ZquicStreamTest`,
  `ZquicCryptoTest`, `ZquicHandshakeTest`, `ZquicPacketProtectionTest`,
  `ZquicLogTest`, and `ZquicStreamTest`.  The obsolete `CryptoLevel` and
  `PktSpace` names are gone.  Runtime QUIC code uses `PktNumSpace` for the RFC
  packet number spaces; TLS `epoch` remains a TLS boundary value because 0-RTT
  and 1-RTT both map to the QUIC application-data packet number space.  The
  epoch/packet-number-space mappings are centralized in `ZquicTypes.hh`, with
  call sites no longer open-coding epoch ternaries.
- current end-to-end flow-control qlog verification passes:
  `make -C zquic/src -j8`,
  `make -C zquic/test -j8 ZquicStreamTest ZquicLogTest ZquicRecoveryTest`,
  `ZquicStreamTest`, `ZquicLogTest`, and `ZquicRecoveryTest`.
  `ZquicStreamTest::testFlowControlQLog` now drives the real send path to prove
  a stream with queued data first fails because connection data credit is
  exhausted and queues DATA_BLOCKED; applying MAX_DATA clears DATA_BLOCKED and
  the same stream then sends.  The same test drives the stream-credit blocked
  path through packetization, verifies MAX_STREAM_DATA clears the queued
  STREAM_DATA_BLOCKED control, and still asserts JSON-SEQ output contains the
  connection/stream blocked event names, enum-mapped flow-control reasons, and
  blocked/unblocked state transitions.
- final non-debug qlog no-op signoff passes in the current release
  configuration: `./z.config -L /usr`, top-level `make clean`, and top-level
  `make -j8`.  Generated makefiles show `-O3 -g -DNDEBUG` for `zquic` and
  downstream `zhttp`, with no `Zquic_DEBUG`, so all dependent Z libraries,
  generated dependency files, examples, and tests were rebuilt consistently.
- external qlog parser validation now passes for representative retained
  runtime traces.  `ZQUIC_TEST_KEEP=1 timeout 240s libtool exec
  ./zquic/test/ZquicRuntimeTest` preserves endpoint, Retry/token,
  token-rejection/policy, and idle-timeout `.sqlog` files under
  `/tmp/ZquicRuntimeTest.*`; `jq --seq -e .` accepts each retained file, and
  `blazingqlog <file> -p name` loads the same files and extracts event names.
  The same external parser signoff now covers the retained stream traces
  generated by `ZQUIC_TEST_KEEP=1 libtool exec ./zquic/test/ZquicStreamTest`:
  `ZquicStreamPathValidationQLog.sqlog`, `ZquicStreamPMTUDQLog.sqlog`,
  `ZquicStreamCIDQLog.sqlog`, `ZquicStreamECNQLog.sqlog`, and
  `ZquicStreamPTOQLog.sqlog` all pass
  `jq --seq -e .` and `blazingqlog <file> -p name`.
- extension event/field use has been tightened: event data outside the current
  qlog draft schema must match mainstream reference implementation precedent,
  not merely reuse a `quic:*` event name.  `quic:path_validated` now keeps a
  minimal reference-shape payload with `success` and `vantage`; the vantage point
  value is a closed `ZtEnumMap`-backed qlog enum serialized by `ZtJSON`, not
  owned arbitrary string event data.  zquic-local path action/reason/deadline
  details must stay on schema-defined events or move to private `zquic:*`
  diagnostics.
- `quic:packets_acked` now emits current draft field names
  `packet_number_space` and `packet_numbers`.  Runtime ACK processing now
  snapshots the bounded set of newly acknowledged packet numbers from
  `PktTxUpdate` inside `ZquicLOG((...))`, moves that bounded array into the
  qlog event, and serializes it on the logger thread.  Packet-number-space JSON
  now uses `application_data` for `PktNumSpace::AppData`, matching the current
  draft `$PacketNumberSpace` spelling.  ACK frame summaries now include
  bounded `acked_ranges` as draft-shaped nested numeric arrays, populated from
  parsed Rx ACK frames and from the Tx ACK snapshot inside the `ZquicLOG((...))`
  capture path.  Remaining ACK event work is richer detail only where it
  matches current qlog/reference expectations; do not reintroduce local byte
  counters into the standard `quic:packets_acked` payload.
- `quic:recovery_metrics_updated` now serializes a dedicated current-draft data
  object: RTT values use `latest_rtt`, `smoothed_rtt`, `rtt_variance`, and
  `min_rtt` in milliseconds, and congestion metrics use
  `congestion_window`, `ssthresh`, and `bytes_in_flight`.  The generic
  recovery writer no longer leaks the old `_us` RTT names or `cwnd` into
  unrelated loss/timer/congestion events.
- `quic:congestion_state_updated` now serializes a dedicated current-draft data
  object with enum-mapped NewReno state and trigger values: `new` is derived as
  `slow_start`, `congestion_avoidance`, or `recovery`, and `trigger` is derived
  from the ACK/loss/PMTUD reason.  Congestion metrics remain on
  `quic:recovery_metrics_updated`; the generic recovery object is no longer
  used for congestion-state records.
- `quic:marked_for_retransmit` now serializes the current-draft `frames` data
  shape.  Runtime loss call sites capture the bounded lost `SentFrameRef`
  snapshots by value inside `ZquicLOG((...))`, convert them to qlog frame
  summaries inside the lambda, and the logger thread serializes the frame list
  with `ZtJSON`.  Qlog frame objects now use current-draft field names such as
  `frame_type`, `raw.length`, `ack_delay`, `acked_ranges`, `maximum`,
  `limit`, `stream_type`, `final_size`, `sequence_number`,
  `retire_prior_to`, and `connection_id_length` rather than the old local
  `type`, `value`, range count, and `_us` spellings.  MAX_STREAMS and
  STREAMS_BLOCKED stream types are captured as protocol enum values and mapped
  to qlog JSON on the logger thread.  Parsed Rx and direct writer
  NEW_CONNECTION_ID summaries include `connection_id` and
  `stateless_reset_token`; sent-packet summaries still need Tx control-ref
  plumbing if those byte fields are required for locally sent NEW_CONNECTION_ID
  frames.  Frame summaries use a qlog-specific `ZtJSON` formatter on the logger
  thread so unset/default data for unrelated frame types is omitted instead of
  serialized as zero, false, empty-string, or empty-array placeholder fields.
- `quic:packet_lost` now serializes a dedicated current-draft data object with
  `header.packet_type`, `header.packet_number`, and enum-mapped `trigger`
  values.  The event name itself is a `ZtEnumMap`-backed enum value, not an
  arbitrary string passed to the writer.
- `quic:version_information` now serializes a dedicated current-draft data
  object for server-sent Version Negotiation on unsupported client versions:
  `server_versions` contains zquic's supported version list,
  `client_versions` contains the attempted client version, and
  `chosen_version` is absent because no overlap was found.  Version values are
  emitted as qlog `QuicVersion` hex strings by a logger-thread JSON formatter,
  and the generic security `kind` / `reason` / `value` fields no longer leak
  into this standard event.
- `quic:tuple_assigned` now serializes a dedicated current-draft data object
  with required `tuple_id`, emitted as the qlog `TupleID` string on the logger
  thread.  The old zquic path `kind` / `action` / `reason` / deadline fields
  no longer leak into this standard event; path diagnostics that do not fit the
  qlog draft tuple model should use private `zquic:*` events if needed.
- `quic:connection_id_updated` now serializes a dedicated current-draft data
  object with enum-mapped `initiator` and a hex `old` or `new` connection ID.
  CID qlog call sites snapshot the fixed-size `CxnID` by value inside
  `ZquicLOG((...))`; the logger thread chooses `old` for retired/tombstoned
  IDs and `new` for issued/updated/route-bound IDs.  The old zquic CID
  `kind` / `action` / `reason` / `sequence` / `reset_token` flag fields no
  longer leak into this standard event; richer route/reset-token diagnostics
  should use private `zquic:*` events if needed.
- `quic:ecn_state_updated` now serializes the current-draft `old` / `new` ECN
  state shape using enum-mapped qlog states (`unknown`, `capable`, `failed`).
  Zquic-local ECN validation reasons and counters no longer leak into the
  standard event; add a private `zquic:*` diagnostic event later if tooling
  needs those details.  `ZquicStreamTest::testAckECNValidationDisablesECN`
  now retains runtime qlog output for ACK_ECN validation success and failure,
  asserts `quic:ecn_state_updated` with `capable` and `failed` state updates,
  and asserts the private validation reason does not leak into the standard
  event.
- `quic:mtu_updated` now serializes the current-draft MTU shape with
  `new` and `done`, rather than the old generic path `kind`/`action`/`reason`
  payload.  Keep PMTUD probe/loss detail under a private `zquic:*` event if
  later validation tooling needs it.
- current event-name enum verification passes:
  `make -C zquic/src -j8`,
  `make -C zquic/test -j8 ZquicLogTest ZquicRuntimeTest ZquicStreamTest`,
  `ZQUIC_TEST_KEEP=1 timeout 360s libtool exec ./zquic/test/ZquicLogTest`,
  `ZQUIC_TEST_KEEP=1 timeout 360s libtool exec ./zquic/test/ZquicRuntimeTest`,
  and
  `ZQUIC_TEST_KEEP=1 timeout 480s libtool exec ./zquic/test/ZquicStreamTest`.
  The stale-string audit no longer finds qlog writer event names passed as
  string arguments; remaining qlog event-name literals are the enum map and
  test/runtime output assertions.  The same pass verifies bounded ACK frame
  `acked_ranges` output as nested numeric arrays.

Paused implementation state:

- debug text logging is unrelated to qlog.  There must be no
  `Zquic_DEBUG_LOG*`, `Zquic_LOG*`, or private qlog-adjacent debug-text macro.
  Debug text logging should remain direct `ZiLOG(Debug, "Zquic", ...)` guarded
  by the existing runtime debug-log checks where needed.
- the current grep audit shows no `Zquic_DEBUG_LOG*`, `Zquic_LOG*`, or
  `ZquicLOG_*` debug-text macro in the qlog-touched files.  Keep this true.
- `Zquic_DEBUG` is the controlling conditional compile for qlog availability.
  `ZquicLOG` must perform the qlog runtime-enabled check before evaluating its
  macro argument, so lambda capture initializers are skipped when qlog is
  configured off.
- qlog output files must be aged before opening, matching the `ZiLog` file sink
  precedent.  Use `qlogAge` to configure the retained archive depth.  Automated
  tests may honor `ZQUIC_TEST_KEEP` to retain temporary qlog files for external
  parser/viewer checks; this is test artifact retention, not a library control.
- qlog macro arguments that contain lambda capture-list commas must be wrapped
  at call sites as `ZquicLOG(([...] { ... }))` or `ZquicLOG(([...](...) {
  ... }))`; do not make `ZquicLOG` variadic for this.
- the `# qlog` section of `zquic/GUIDELINES.md` is now the call-site authority
  for qlog instrumentation style and must be followed before further runtime
  qlog edits.
- receive packet frame-summary accumulation uses the documented
  `ZquicLogger::enabled()` exception and `ZuElem<PktEvent>` storage
  so disabled qlog avoids default-constructing the packet event accumulator.
  `ZquicLogger::enabled()` is the conditionally compiled API: a runtime qlog
  enabled check in `Zquic_DEBUG` builds and a `constexpr false` stub otherwise.
- existing focused stream/handshake/recovery tests pass with the receive
  accumulator path enabled.  Writer-level qlog-output assertions cover the
  required bounded frame-summary shapes and prove frame snapshots survive
  by-value ring movement; runtime stream-test assertions cover the
  disabled/enabled receive accumulator behavior.
- current qlog event lambdas use aggregate/member initialization where
  practical.  Avoid default-constructing an event just to overwrite it
  immediately.
- the former `qlogSet_` and enum-to-string helper layer has been removed.
  Remaining `qlog*` helpers in `Zquic.hh` are frame-summary support helpers:
  `qlogAddFrame_`, `qlogAddAckFrame_`, `qlogAddRxFrame_`, and
  `qlogAddTxFrame_`.  They must remain used only inside `ZquicLOG((...))`
  lambdas or the documented receive frame-summary accumulator exception.
- packet event posting no longer uses a bespoke post-payload copy shim; packet
  qlog events own their arbitrary strings and the direct qlog post APIs move
  event values into the single `ZquicLogger::post_()` enqueue path.  Frame
  summary data remains scalar POD snapshot data with JSON enum mapping for
  frame type.
- per-event CID linkInfo is now submitted with runtime events rather than
  depending on mutable process-wide event identity.  Runtime link-owned qlog
  call sites capture `linkInfo_()` by value inside `ZquicLOG((...))` and set
  the event `linkInfo` field; stream-owned runtime events use the public
  `Link::linkInfo()` wrapper from inside the macro argument for the same
  by-value snapshot.  The static audit for link-owned call sites at line 4500
  and later currently finds no standard qlog event construction without
  linkInfo.  The remaining no-linkInfo call sites are pre-link endpoint
  security/address-validation events that cannot have link CID linkInfo and
  should not capture empty linkInfo needlessly.
- retained qlog artifacts from the current direct and runtime tests validate
  with `jq --seq -e .` and `blazingqlog <file> -p name`: direct writer traces
  `ZquicLog*.sqlog`, stream runtime qlog traces `ZquicStream*QLog.sqlog`, and
  retained runtime endpoint/Retry/token/idle traces under
  `/tmp/ZquicRuntimeTest.*/*.sqlog`.

Still missing or incomplete:

- the runtime qlog call-site audit against `zquic/GUIDELINES.md` has removed
  the former event-wrapper helper layer for existing qlog families.  Remaining
  audit work is to keep frame-summary support helpers confined to
  `ZquicLOG((...))` lambdas or the receive accumulator exception, to keep
  per-event linkInfo captures only at call sites that can actually have link
  identity, and to apply the same style to all new qlog coverage.
- direct unit tests still call `ZquicLogger::typedEvent(...)` style APIs.  This
  is acceptable for writer tests, but runtime instrumentation should use
  `ZquicLOG`.
- `ZeString` use in qlog data structs has been narrowed substantially.  Very
  few qlog fields are genuinely arbitrary strings: most are QUIC fixed-size
  scalars, IP addresses, ports, protocol IDs, system/protocol error codes,
  enums, or other closed vocabularies.  Short qlog literals used as reasons,
  triggers, actions, states, labels, or kinds should remain `ZtEnum` values
  with `ZtEnumMap` JSON spellings.  Keep `ZeString` only for rare detailed
  arbitrary text that must be owned and moved across the logger boundary.
- remaining enum/string cleanup is now limited to any future qlog fields that
  carry short reason/trigger/action/state labels as strings.  Use `ZtEnumMap`
  for closed vocabularies so JSON string conversion occurs in `ZtJSON` on the
  logger thread; keep `ZeString` only for genuinely arbitrary detailed text or
  protocol strings, such as ALPN, that must cross the logger boundary.
- final non-debug/no-op verification now passes in the current release
  configuration: `./z.config -L /usr`, top-level `make clean`, and top-level
  `make -j8`.  Generated makefiles show `-O3 -g -DNDEBUG` for `zquic` and
  downstream `zhttp`, with no `Zquic_DEBUG`.
- stream/application-facing qlog coverage is partial relative to the full draft
  vocabulary, but it is close to mainstream-equivalent coverage for the zquic
  stream features that currently exist:
  `quic:stream_state_updated` now covers stream `idle` -> `open` creation
  and `open` -> `closed` reaping with stream id/type/side and enum-mapped
  reason values.  Spec-aligned flow-control blocked/unblocked coverage exists for
  the send-side BLOCKED/MAX_DATA/MAX_STREAM_DATA paths, including runtime
  JSON-SEQ tests for connection and stream blocked event names.
  `quic:stream_data_moved` now covers application data accepted into
  stream Tx buffers, packetized STREAM frame data moving toward the network,
  received STREAM frame data accepted into transport buffers, and delivery from
  transport buffers to application reads, with `raw.length` and without payload
  bytes.
  Connection-level unblocked coverage now has an end-to-end runtime scenario
  that reaches a genuinely blocked connection data-credit state before MAX_DATA
  is applied; the test also proves the blocked control is cleared and the
  stream send proceeds after the credit update.  Close/drain expiry runtime
  qlog coverage exists for the peer-close drain path, and idle-timeout runtime
  qlog coverage exists for the normal negotiated idle-timeout path.  Remaining
  in-scope work is not to model every internal stream substate; it is to add
  only application-visible state/data movement gaps that mainstream tools or
  reference traces would normally expose for the same QUIC behavior.
- Path validation now has retained runtime `.sqlog` assertions in
  `ZquicStreamTest::testPathValidationStateMachine`, covering
  `quic:path_validated` success and failure states plus the minimal
  `vantage` field shape.
- PMTUD now has retained runtime `.sqlog` assertions in
  `ZquicStreamTest::testPMTUDQLog`, covering `quic:mtu_updated` for ACKed and
  lost probe paths through the current draft `new` / `done` data shape.
- CID issue/retire/route-binding now has retained runtime `.sqlog` assertions
  in `ZquicStreamTest::testCIDQLog`, covering `quic:connection_id_updated`
  for locally issued and retired CIDs plus peer-issued and retire-prior-to CIDs
  through the standard `initiator`, `new`, and `old` data shape.
- Loss/PTO retransmission now has retained runtime `.sqlog` assertions in
  `ZquicStreamTest::testPTOQLog`, covering `quic:packet_lost`,
  `quic:marked_for_retransmit`, `quic:timer_updated`, reordering-threshold
  loss trigger, PTO timer type, and expired timer event output.
- Retry/VN/stateless reset/token/protection-failure qlog coverage is partial
  but already covers the main runtime outcomes needed for mainstream
  equivalence:
  key runtime decision points are instrumented and writer-tested, and
  Retry/NEW_TOKEN plus missing-token, malformed-token,
  token-authentication-failure, expired-token, address-mismatch, invalid token
  kind, and NEW_TOKEN policy rejection runtime output is covered by
  `ZquicRuntimeTest`.  Server-sent Version Negotiation now uses the current
  draft `quic:version_information` data shape; direct writer coverage asserts
  `server_versions`, `client_versions`, and absence of the old generic
  `unsupported_version` reason.
  Stateless reset detection now has runtime `.sqlog` assertions in
  `ZquicAPITest::testStatelessResetDetection`, covering
  `zquic:stateless_reset`, received trigger, and token-match reason.
  0-RTT remains unsupported as a runtime packet space, but the current
  rejection posture now emits private `zquic:zero_rtt_rejected` qlog records
  from `Crypto::init()` when 0-RTT is requested and from
  `Crypto::rejectZeroRTT()` when a 0-RTT packet/rejection path is exercised.
  The event uses enum-mapped `0rtt` reason data and is covered by direct writer
  assertions in `ZquicLogTest` plus runtime crypto-path output assertions in
  `ZquicHandshakeTest::testZeroRTTPktDrop`.
  Corrupt protected short-header packet runtime output is covered by
  `ZquicStreamTest`.  External qlog-tool validation now passes for the current
  retained runtime trace set; remaining in-scope work is reference-shape
  comparison for those traces, plus TLS alert/handshake failure details only
  where zquic has bounded scalar state and mainstream references expose
  equivalent events.
- top-level connection metadata now uses the current JSON-SEQ `QlogFileSeq`
  header shape at the writer layer and is covered by runtime endpoint tests for
  the server trace.  Event identity is carried with each event as by-value
  CID linkInfo when link identity is available, so interleaved events from
  different linkInfo sources do not depend on a process-wide mutable event
  identity.  Remaining gaps are reference-shape review for the runtime traces
  zquic can currently generate, and any future
  receive-side packet buffering event only if the implementation begins buffering undecryptable
  packets instead of dropping them.  Local JSON-SEQ scanning is stricter now,
  and representative traces also load in `jq --seq` plus the `blazingqlog`
  JSON-SEQ qlog parser.  The current implementation drops
  missing-key packets instead of buffering them, and logs those decisions as
  packet drops plus packet-protection failure events.  Remaining schema work is
  limited to deviations that affect mainstream qlog consumers or differ from
  the current schema/reference implementations for events zquic actually
  emits; non-mainstream zquic detail should stay private or be omitted.

Completed mainstream-parity signoff:

1. Representative zquic `.sqlog` traces have been produced and retained for the
   mainstream scenarios zquic currently implements: handshake/endpoint open,
   Retry/NEW_TOKEN, token rejection and policy rejection, 1-RTT stream transfer
   with ACK ranges and stream data movement, packet protection failure, packet
   loss plus PTO retransmission, path validation success/failure, PMTUD
   success/loss, CID issue/retire/route binding, stateless reset detection,
   idle timeout, and close/drain expiry.
2. Event/data-shape alignment for currently emitted `quic:*` events is complete
   to the cleanup scope: zquic-local fields on standard events have been mapped
   to the current draft qlog shape, matched to a documented mainstream extension
   precedent, moved to `zquic:*`, or omitted.  New standard events should not be
   added only to expose local diagnostics.
3. Trace identity expectations are validated for JSON-SEQ header fields,
   `trace.event_schemas`, connection/CID metadata, vantage point, group/
   connection identity, and per-event CID linkInfo.  Direct tests prove event
   linkInfo can differ per record, runtime tests prove non-empty per-event CID
   fields are emitted for link-owned traces, and static audit confirms
   pre-link endpoint-only events are the intentional no-linkInfo exception.
4. External qlog-tool validation currently passes for retained direct writer,
   stream runtime, API stateless-reset, and runtime endpoint/Retry/token/idle
   traces with `jq --seq -e .` and `blazingqlog <file> -p name`.
   Rerun this signoff when event shape or trace identity changes.  Add another
   mainstream parser/viewer only if it catches real compatibility issues rather
   than draft-completeness gaps.
5. TLS alert/handshake failure detail, ECN path-marking/congestion-response
   diagnostics, peer address migration, and preferred-address qlog remain
   conditional future items.  They should be added only when zquic exposes
   bounded scalar state for the behavior and mainstream references emit
   equivalent information.
6. Private diagnostics remain intentionally private.  Retry validation, token
   policy, packet-protection reasons, unsupported 0-RTT rejection, and zquic
   path/CID details that are not in the current schema remain under `zquic:*`
   unless reference implementations establish compatible `quic:*` extension
   fields.

Explicit non-goals for this cleanup:

- historical qlog schema compatibility;
- full coverage of every current draft event and field;
- instrumentation of unsupported zquic features;
- qlog records for every internal stream, path, CID, crypto, or recovery
  substate;
- moving private diagnostics into `quic:*` without current schema support or
  mainstream reference precedent.

Goals
-----

1. Keep qlog entirely debug-gated, low-overhead when disabled, and nonblocking
   when enabled.
2. Emit qlog-mainline compatible event names and data objects for the events
   mainstream QUIC implementations expose for comparable interop, recovery,
   migration, ECN, and packet-protection behavior.
3. Place event generation at ownership-correct Rx/Tx sites without introducing
   locks, cross-shard reads, heap churn, or hidden packet/frame copies.
4. Redact payload bytes, secrets, tokens, and application data by default while
   retaining enough structural metadata for analysis tools.
5. Preserve current diagnostics and add enough tests that qlog output remains a
   stable debugging artifact.

Guideline constraints
---------------------

- Read and follow `GUIDELINES.md` and `zquic/GUIDELINES.md` before
  implementation.  Their Z Framework idioms, low-latency posture, ownership
  rules, naming/layout conventions, and test/build expectations are
  authoritative over generic C++ preferences.
- Keep `Zquic_DEBUG` gating.  Do not make qlog a production fast-path feature.
- Follow the `# qlog` section of `zquic/GUIDELINES.md` for every qlog call
  site.  In particular, all qlog-only
  runtime reads, derived values, conversions, and event struct construction must
  be inside the `ZquicLOG` macro argument.
- `ZquicLOG` must check qlog runtime enabled state before evaluating its
  argument, matching the `ZiLOG` severity-filter precedent.
- Use `ZquicLOG(([...](auto &o, ZuTime time) { ... }))` at qlog call sites so
  lambda capture-list commas remain a single macro argument.
- Do not introduce qlog-specific text logging macros.  Debug text logging is
  unrelated and should use `ZiLOG(Debug, ...)` directly.
- Use Z containers and formatting; do not introduce STL JSON helpers or generic
  logging dependencies.
- Do not call into the writer thread with references to packet/frame buffers or
  mutable runtime state.  Snapshot only small fixed metadata by value.
- Avoid heap allocation in send/receive hot paths.  Capture fixed metadata by
  value and move the lambda by value through the ring; construct qlog-specific
  structs and run JSON helpers on the qlog writer thread.
- Qlog event structs should be plain data structs initialized directly from
  captured values where practical.  Avoid default construction followed by
  immediate field overwrite.
- Do not add locks around Rx/Tx state to make tracing easier.  Emit from the
  owning shard or pass a small snapshot to the owner that can emit.
- Keep qlog calls shallow and locally auditable; no deep formatting work in the
  packet protection, ACK, or scheduler critical path.
- Rebuild changed library code before dependent tests, following the
  `GUIDELINES.md` build guidance (`make -C zquic/src -j8` before focused
  `zquic/test` binaries when source code changes).

Target event model
------------------

Replace the current generic event object with typed event data structs in
`ZquicLog.cc` and small public/static emit helpers in `ZquicLog.hh`.
The lists below define the parity target for zquic features that exist and are
observable in mainstream QUIC qlog traces.  They are not a demand to invent
events for unsupported features or to emit every possible internal state
transition.

Parity target for top-level trace/header metadata:

- JSON-SEQ `QlogFileSeq` header with `file_schema`,
  `serialization_format`, `trace.common_fields`, and
  `trace.event_schemas`;
- current draft `urn:ietf:params:qlog:events:quic-12` event schema for
  `quic:*` events plus the private `urn:zlib:zquic:qlog:events:zquic` schema
  when zquic-specific diagnostic events are emitted;
- trace title, vantage point type (`client`, `server`, or `unknown`), original
  destination CID when available, group id or connection id, and implementation
  name/version;
- per-event relative or absolute time with a consistent unit matching qlog
  expectations.

Parity target for transport events:

- `quic:packet_sent`;
- `quic:packet_received`;
- `quic:packet_dropped`;
- `quic:packet_lost`;
- `quic:packet_buffered`;
- `quic:packets_acked`;
- `quic:udp_datagrams_sent`;
- `quic:udp_datagrams_received`.

Packet events should include `header.packet_type`, packet number when known,
byte lengths under `raw.length` / `raw.payload_length`, DCID/SCID where useful,
ECN mark, frames, ACK ranges, ACK delay, coalescing/datagram context,
drop/loss reason, and bytes in flight.  `packet_number_space` is currently
kept as an explicit scalar extension field where useful, even though the qlog
draft can infer the packet number space from `header.packet_type`.

Parity target for frame coverage:

- CRYPTO offset and length;
- ACK and ACK_ECN ranges, ACK delay, and ECN counters;
- STREAM id, offset, length, and FIN;
- RESET_STREAM and STOP_SENDING;
- MAX_DATA, MAX_STREAM_DATA, MAX_STREAMS, DATA_BLOCKED,
  STREAM_DATA_BLOCKED, and STREAMS_BLOCKED;
- NEW_CONNECTION_ID and RETIRE_CONNECTION_ID;
- PATH_CHALLENGE and PATH_RESPONSE;
- NEW_TOKEN;
- HANDSHAKE_DONE;
- CONNECTION_CLOSE and APPLICATION_CLOSE with error code and reason length,
  but not unbounded reason text unless an explicit redaction policy allows it.

Parity target for recovery/congestion events:

- `quic:recovery_metrics_updated` for RTT sample, smoothed RTT, RTT variance,
  min RTT, latest RTT, congestion window, ssthresh, and bytes-in-flight
  context;
- `quic:timer_updated` for Initial, Handshake, and 1-RTT timers,
  including timer type, packet number space, event type, and delta;
- `quic:packet_lost` with loss trigger and packet metadata;
- `quic:marked_for_retransmit` with frame-ref kind and crypto/stream
  offsets;
- `quic:congestion_state_updated` with NewReno state and trigger; keep
  congestion window, ssthresh, and bytes-in-flight on
  `quic:recovery_metrics_updated`, and put recovery-start/persistent-
  congestion/controller diagnostics in a private `zquic:*` event unless a
  mainstream reference-backed `quic:*` extension shape is adopted;
- `quic:ecn_state_updated` with the standard validation state shape; add
  monotonicity failure, CE reaction, and fallback disablement details only as
  private diagnostics or reference-backed extension fields if mainstream
  tooling needs them;
- PTO expiry, probe generation, and PTO backoff changes where they are visible
  in zquic's recovery state.  Add pacer delay only when pacing is integrated.

Parity target for TLS/security events:

- `quic:parameters_set` for transport parameters sent and received, with
  bounded/redacted values;
- `quic:alpn_information` for ALPN selection;
- `quic:key_updated` for key update start/completion/rejection,
  old-key acceptance, and invalid key phase where it maps cleanly;
- `quic:key_discarded` for Initial, Handshake, and old 1-RTT key discard;
- `quic:version_information` for Version Negotiation sent/received;
- zquic extension events for Retry validation, Retry integrity failure,
  stateless reset diagnostics, token issuance/validation/rejection, TLS alert
  or handshake failure details, and packet protection failure reason when those
  diagnostics do not map cleanly to current draft QUIC qlog events or
  mainstream reference implementation extension practice;
- no secrets, plaintext, or raw tokens.

Parity target for path/CID events:

- `quic:tuple_assigned` for active path tuple assignment/update;
- peer address change or NAT rebinding candidate when zquic has an observable
  candidate/promotion event matching mainstream traces;
- anti-amplification limit blocking and unblocking where those decisions affect
  packet send/drop output;
- `quic:path_validated` for path validation challenge sent, response received,
  validation success, and validation failure/timeout, following mvfst's
  reference-implementation precedent for this extension event.  Because this is
  outside the current draft schema, the `quic:*` payload should remain limited
  to `success` and `vantage`; additional zquic-specific
  diagnostics require a private `zquic:*` event;
- `quic:mtu_updated` for PMTUD probe sent, ACKed, lost, blackhole detected, and
  MTU updated;
- `quic:connection_id_updated` for connection ID issued, retired, and
  route-bound; stateless reset token association details should remain private
  diagnostics unless a mainstream reference-backed qlog extension shape is
  adopted;
- preferred-address events only if that feature is implemented.

Parity target for stream/application-facing events:

- stream opened and closed;
- stream state transitions that matter to applications and are visible in
  mainstream traces;
- flow-control blocked and unblocked;
- application data accepted, queued, and sent at frame granularity;
- local close, peer close, idle timeout, and final close/drain expiry.

Implementation plan
-------------------

Resume checklist before adding more event coverage:

1. Confirm there are no `Zquic_DEBUG_LOG*`, `Zquic_LOG*`, or `ZquicLOG_*`
   debug-text macros.  Debug text logging must be direct `ZiLOG(Debug, ...)`.
   Current grep audit passes this check.
2. Confirm all qlog runtime instrumentation uses `ZquicLOG((lambda))`, and
   that `ZquicLOG` performs the qlog runtime-enabled check before evaluating
   the lambda expression.  Current grep audit passes the malformed
   `ZquicLOG([` check.
3. Finish auditing the existing qlog helper block against `zquic/GUIDELINES.md`:
   move
   qlog-only reads and derived values into lambda capture initializers, move
   qlog-specific struct construction into lambda bodies, and convert event
   structs to aggregate/member initialization where practical.  Scalar
   aggregate/member initialization is mostly complete.  Datagram sent/received,
   packet sent/received, receive-side packet-drop, security, path, PMTUD, and
   CID wrappers have been inlined at runtime call sites.  ACK/loss/recovery/
   timer wrappers have also been inlined at runtime call sites.  Security
   `kind` is now enum-mapped through `ZtJSON`; continue converting remaining
   enum-like fields before treating the data-shape audit as complete.
   Remaining helper audit is limited to the frame-summary support helpers and
   the receive accumulator exception.
4. Review and test the receive packet/frame-summary path that now uses
   `ZquicLogger::enabled()` and `ZuElem<PktEvent>`; confirm disabled
   qlog does not allocate, default-construct, or populate qlog event state,
   while enabled qlog still records bounded frame summaries.  This now has
   direct runtime coverage in `ZquicStreamTest::testRuntimeReceiveQLog`.
5. Rebuild `zquic/src`, rebuild focused tests, and run `ZquicLogTest`,
   `ZquicStreamTest`, `ZquicPMTUDTest`, `ZquicCIDTest`, plus handshake/recovery
   smoke tests before continuing to new stream/security/header coverage.
   Current debug build/test pass is complete for this checklist item.
6. Non-debug compile/no-op verification for `Zquic_DEBUG`-off builds is
   complete: `./z.config -L /usr`, top-level `make clean`, and top-level
   `make -j8` pass with generated `zquic` and downstream `zhttp` makefiles
   using `-O3 -g -DNDEBUG` and no `Zquic_DEBUG`.

1. Introduce typed qlog data structs.
   - Add compact structs for packet, datagram, frame, recovery, congestion,
     security, path, CID, stream, and close event data.
   - Use typed scalars, fixed-size data, system/protocol error codes, and
     `ZtEnumMap`-backed enums for almost all qlog fields.  Use owned
     `ZeString` only for rare free-form detailed text; use scalar snapshots for
     small frame summaries.
   - Add `ZtStruct` JSON mappings that use qlog-mainline field names.
   - Keep the old generic `event()` helper only for tests or remove it after
     typed emitters replace all callers.

2. Keep the qlog linkInfo snapshot layer event-owned.
   - Closed qlog vocabularies should remain `ZtEnumMap`-backed enums for
     packet space, packet type, frame type, drop reason, loss trigger, timer
     type, redaction category, and similar short labels.
   - Snapshot connection identity as by-value `Zquic::LinkInfo`
     (`origDCID`, `groupID`, `dcid`, `scid`) in `ZquicLOG` capture
     initializers wherever link identity is available.
   - Make event helpers accept already-owned linkInfo; they should not inspect
     non-owner state from the writer side or rely on global current linkInfo.

3. Instrument datagram and packet receive.
   - Emit `quic:udp_datagrams_received` when a UDP datagram enters the QUIC
     receive path.
   - Emit `quic:packet_received` after successful long/short packet
     protection and frame parse, with packet number, space, size, ECN, and frame
     summaries.
   - Emit `quic:packet_dropped` for parse failure, unsupported 0-RTT,
     missing keys, duplicate packet, discarded packet space, packet protection
     failure, stateless reset, version mismatch, token/address validation
     failure, and frame legality failure.
   - Keep drop reasons as enum/string constants, not ad hoc text.

4. Instrument datagram and packet send.
   - Emit `quic:packet_sent` after a datagram/packet is accepted by path
     and app gating, not merely after packet protection succeeds.
   - Include protected packet number, packet type/space, bytes, frames, ACK
     ranges, ACK delay, ECN marking once available, coalesced datagram context,
     and bytes in flight after accounting.
   - Emit `quic:udp_datagrams_sent` once per actual UDP send.
   - Emit `quic:packet_buffered` when a packet is retained for later
   emission or processing.  Initial long-header coalescing is now covered by
   a `quic:packet_buffered` event; because the current draft trigger
   vocabulary has no coalescing trigger, the standard event omits a trigger.
   Add receive-side buffered-packet events if the implementation later buffers
   undecryptable packets instead of dropping them.
   - Emit `quic:packet_dropped` for path budget rejection,
     anti-amplification blocking, congestion blocking, PMTUD/probe admission
     failure, app send hook rejection, and missing traffic secrets.

5. Add frame summary builders.
   - Reuse parsed `Frame` data where available on Rx.
   - Reuse `TxPktRefs`, ACK metadata, CRYPTO offsets, and stream/control refs on
     Tx instead of reparsing protected buffers.
   - Bound the number of frame summaries per packet to `SentPkt::MaxFrames` or
     another named qlog cap; set a truncation flag if exceeded.
   - Plumb NEW_CONNECTION_ID CID/token bytes through Tx control frame
     references if locally sent NEW_CONNECTION_ID summaries need
     `connection_id` and `stateless_reset_token`; parsed Rx and direct writer
     frame summaries already emit those fields.
   - Never copy frame payload bytes into qlog events.

6. Instrument ACK, loss, and recovery.
   - `quic:packets_acked` now includes bounded newly acknowledged packet
     numbers captured from ACK processing, using the current draft
     `packet_number_space` and `packet_numbers` shape.  ACK frame summaries
     now include bounded `acked_ranges`; continue only with richer ACK event
     detail where it matches current qlog/reference expectations; keep local
     counters out of the standard payload.
   - Emit `quic:packet_lost` and `quic:marked_for_retransmit` from the existing
     loss decision point with loss trigger and packet space.
   - Emit `quic:marked_for_retransmit` when frame refs are queued for
     retransmission.
   - `quic:recovery_metrics_updated`, `quic:packet_lost`,
     `quic:marked_for_retransmit`, and `quic:congestion_state_updated` now use
     current draft data shapes.
   - `quic:timer_updated` now uses the current draft data shape.  Keep
     zquic-only PTO backoff/probe counters out of this standard event unless a
     mainstream reference implementation establishes a compatible `quic:*`
     extension field; otherwise emit such diagnostics under `zquic:*`.
   - Emit `quic:timer_updated` whenever loss/PTO timer state is armed,
     canceled, or moved.
   - Emit PTO expiry/probe events from the PTO timeout path.  PTO expiry,
     backoff, and probe-generation qlog events are now emitted and tested via
     standard `quic:timer_updated` state transitions; extend this with private
     diagnostics only if reference-tool validation shows value.

7. Instrument congestion and ECN.
   - `quic:congestion_state_updated` now emits dedicated NewReno `new` and
     `trigger` fields from ACK-driven window growth and loss reactions, without
     embedding congestion metrics in the state-change event.
   - `quic:ecn_state_updated` now uses the current draft state shape.  Continue
     by adding private `zquic:*` diagnostics for ACK_ECN monotonic validation,
     peer ECN counter growth, validation failure reason, CE reaction, and
     fallback disablement if those details are required by validation tooling.
   - Keep event emission close to the state change so the qlog trace does not
     require reconstructing state from counters.

8. Instrument TLS/security.
  - `quic:parameters_set` now emits bounded local and peer transport
    parameter snapshots using current draft field names and enum-mapped
    `initiator`; continue by adding any missing TLS-level settings such as
    resumption/early-data/cipher only when zquic has scalar state for them and
    a redaction policy.
  - `quic:alpn_information` now emits selected ALPN as
    `chosen_alpn.string_value`; private `zquic:zero_rtt_rejected` now covers
    zquic's unsupported-0-RTT rejection posture from crypto paths.  Continue
    with richer handshake failure and TLS alert coverage from the crypto/TLS
    callback paths.
  - Emit key install/update/discard events from traffic secret update,
     key-update, and discard paths.
    `quic:key_updated` and `quic:key_discarded` now use standard qlog
     key-event payloads with qlog `$KeyType`, optional AppData `key_phase`, and
     qlog trigger mappings; continue by adding runtime assertions for full
     client/server role mapping and by extending key-phase tracking if zquic
     later stores the full key generation instead of only the key phase bit.
  - Retry, Version Negotiation, stateless reset, token issue/validate/reject,
     and packet protection failure now have typed qlog event names and runtime
     call sites.  Security event `kind`, `key_type`, `trigger`, and `reason`
     are enum-mapped where those events still use the private security payload;
     standard Version Negotiation now uses the current draft
     `quic:version_information` version-array payload.  Missing-token,
     malformed-token,
     token-authentication-failure, expired-token, address-mismatch, invalid
     token kind, NEW_TOKEN policy rejection, and corrupt 1-RTT short-packet
     protection failure now have runtime qlog trace assertions; continue by
     adding qlog-tool validation.

9. Instrument path and CID lifecycle.
   - Emit active path updates, peer address observations, and
     anti-amplification blocking where path admission decisions happen.
   - Emit path validation challenge/response lifecycle events from the
     PATH_CHALLENGE/PATH_RESPONSE scheduler and receive handling.
   - Emit PMTUD probe and MTU update events from PMTUD state transitions.
   - Path, PMTUD, and CID event `kind`, `action`, and `reason` are now
     enum-mapped through `ZtJSON`; continue by adding qlog-tool validation.
   - Emit CID issue/retire/route-bind/reset-token events where CIDs enter or
     leave routing tables.

10. Instrument stream/application-facing events.
    - Emit stream opened/closed and relevant state transition events from stream
      lifecycle transitions, not from application callback wrappers.  Stream
      open and final reaping now emit `quic:stream_state_updated`; add
      finer sending/receiving state transitions as they become explicit in the
      stream state machine.
    - Emit flow-control blocked/unblocked where blocked state changes.  The
      send-side BLOCKED/MAX_DATA/MAX_STREAM_DATA paths now emit and test
      spec-aligned connection/stream blocked qlog; the connection-level
      MAX_DATA unblocked test now proves the connection data-credit state was
      genuinely blocked before MAX_DATA arrived.  Add any receive/application-
      visible flow state transitions that are made explicit later.
    - Emit `quic:stream_data_moved` for application data movement.
      Application -> transport, transport -> network, network -> transport,
      and transport -> application are now implemented and tested.
    - Emit application close, transport close, idle timeout, and close/drain
      expiry with close source and error code.  Runtime qlog coverage now
      asserts peer/local close, drain expiry, and the normal negotiated
      idle-timeout path; tighten close error-code/source assertions where
      needed.

11. Add qlog tests in layers.
    - Extend `ZquicLogTest` for typed JSON shape, header metadata, redaction,
      and back-pressure behavior.
    - Add focused runtime tests that drive packet sent/received, packet dropped,
      ACK, loss, PTO, close, Retry/token, path validation, and PMTUD events.
    - Add coverage for qlog disabled/no-op behavior in non-debug builds where
      the build mode permits it.
    - Add tests that assert no payload bytes, TLS secrets, or raw tokens appear
      in qlog output.
    - Add tests for truncated frame-summary behavior when a packet carries more
      frames than the qlog per-packet summary cap.
    - Add tests for trace and event linkInfo: vantage point, header
      connection/CID fields, per-event CID linkInfo, `file_schema`,
      `serialization_format`, `trace.event_schemas`, and monotonically valid
      event timestamps.
    - Keep tests tolerant of event ordering where scheduling legitimately
      races, but strict about event presence and required fields.
    - Keep extending the local JSON-SEQ scanner/helper so it validates all
      records are complete JSON values and checks event names/categories without
      depending on field order.  `ZquicLogTest` and `ZquicStreamTest` now
      reject malformed JSON-SEQ framing and trailing data after each parsed
      JSON value.

12. Validate with qlog tooling.
    - Generate traces from handshake, stream transfer, loss/retransmission,
      Retry/token reuse, close, and PMTUD scenarios.
    - Confirm the output can be loaded by mainstream qlog parsers/viewers
      without custom parsing.  Representative endpoint, Retry/token,
      token-rejection/policy, and idle-timeout traces now pass `jq --seq` and
      `blazingqlog -p name`.
    - Keep the remaining latest-draft event type/data-shape deviations
      documented in `audit.md` until they are implemented, aligned with a
      mainstream reference implementation extension precedent, or explicitly
      moved to a private zquic event schema.

Review points
-------------

- qlog disabled must remain a cheap branch and must not format strings or build
  frame arrays on hot paths.
- writer closures must not capture references, spans into packet buffers, or
  pointers to runtime-owned mutable state.
- events emitted from Rx and Tx must not force cross-shard reads; if both sides
  need context, snapshot it at the existing handoff.
- drop/loss/security reasons should be stable enums or named constants so tests
  and tooling can key on them.
- redaction policy should be explicit and conservative: no plaintext payload,
  no secrets, no raw tokens by default.
- qlog records should remain useful under back-pressure.  Dropped qlog records
  are acceptable, but the drop counters and ring pressure counters must remain
  accurate.

Verification target
-------------------

Do not rebuild or rerun the full focused test set after every small qlog
coverage edit.  Batch related qlog schema/event/call-site changes first, then
run the focused build and tests once for that batch.  Rebuild `zquic/src` before
dependent tests when source code changes, but avoid using a rebuild as a
progress checkpoint for each individual assertion or documentation update.

Focused build and tests:

```
make -C zquic/src -j8
make -C zquic/test -j8
libtool exec ./zquic/test/ZquicLogTest
libtool exec ./zquic/test/ZquicHandshakeTest
libtool exec ./zquic/test/ZquicRecoveryTest
libtool exec ./zquic/test/ZquicStreamTest
libtool exec ./zquic/test/ZquicPMTUDTest
libtool exec ./zquic/test/ZquicCIDTest
```

Runtime trace scenarios:

- client/server handshake with Initial and Handshake coalescing;
- 1-RTT stream transfer with ACK ranges and delayed ACK;
- forced packet loss and PTO retransmission;
- Retry plus NEW_TOKEN reuse;
- stateless reset detection;
- path validation and PMTUD probe success/loss;
- local application close, peer close, idle timeout, and close/drain expiry;
  peer/local close and drain expiry are currently covered by the endpoint qlog
  runtime trace, while idle timeout is covered by the dedicated idle qlog
  runtime trace.

Each generated `.sqlog` should be JSON-SEQ valid, contain qlog-compatible event
names, include connection/vantage metadata, redact payload/secrets/tokens, and
load in mainstream qlog viewers without custom interpretation.

Acceptance criteria
-------------------

The qlog implementation is complete for this cleanup when all of the following
are true:

1. `GUIDELINES.md` has been applied to the implementation: no new STL-heavy
   helpers, no virtual/concepts-based tracing abstraction, no hot-path heap
   churn for disabled qlog, no cross-shard locking to read trace metadata, and
   no writer closures capturing references into live packet/runtime state.
2. `Zquic_DEBUG` remains the only active qlog build mode.  Non-debug qlog calls
   remain no-op stubs and compile cleanly; current release verification is
   `./z.config -L /usr`, top-level `make clean`, and top-level `make -j8`.
3. qlog disabled is a cheap branch.  Disabled qlog does not build frame-summary
   arrays, format JSON, allocate, or copy packet payloads.
4. The trace header includes `file_schema`, `serialization_format`,
   implementation identity, `trace.event_schemas`, vantage point, relative time
   metadata, and per-connection identity/CID linkInfo where available.
5. The mainstream-equivalent transport, frame, recovery/congestion,
   TLS/security, path/CID, and stream/application-facing event families for
   implemented zquic behavior are emitted from runtime code, not only from
   direct `ZquicLog` unit-test calls.
6. Packet send/receive/loss/drop events include enough metadata to reconstruct
   packet space, type, packet number, size, frames, ACK ranges, ECN mark, drop or
   loss reason, coalescing context, and bytes-in-flight where applicable.
7. Recovery traces expose the RTT updates, loss/PTO timer changes, PTO firing,
   retransmission marking, congestion state, and ECN state transitions that
   mainstream reference traces expose for comparable recovery behavior.
8. Security traces expose transport parameters, ALPN, key install/update/discard,
   Retry/Version Negotiation/stateless reset/token outcomes, and protection
   failures without logging secrets, plaintext, or raw tokens.
9. Path/CID traces expose mainstream-equivalent anti-amplification blocking,
   path validation, PMTUD, active path changes, and CID issue/retire/route
   binding for zquic's implemented path/CID behavior.  Reset-token association
   detail remains private unless reference implementations expose an equivalent
   field shape.
10. Tests cover unit-level writer behavior, disabled/no-op behavior, JSON-SEQ
    validity, typed event schemas, runtime emission for each mainstream-parity
    event family zquic implements, redaction, back-pressure/drop diagnostics,
    and frame-summary truncation.
11. The focused `zquic` test binaries listed in the verification target pass in
    the relevant debug build.
12. Representative generated `.sqlog` files load in mainstream qlog tooling,
    and any remaining schema/reference deviation that affects mainstream
    equivalence is documented in `audit.md` with a follow-up item.
