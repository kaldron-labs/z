`Zquic` qlog parity plan
========================

`ZquicLog` provides the debug-gated writer infrastructure and now has partial
typed qlog event coverage.  `Zquic_DEBUG` gating is intentional and should
remain.  Existing runtime qlog event families have been aligned with
the `# qlog` section of `zquic/GUIDELINES.md`: qlog-specific work must be
compiled out of non-`Zquic_DEBUG` builds, and must also be skipped before
lambda capture evaluation when qlog is disabled by configuration.

Current state
-------------

Implemented:

- `ZquicLogParams` exposes `qlog`, `qlogPath`, `qlogThread`, and
  `qlogRingSize` through engine parameters.
- `ZquicLogger` is the internal writer-thread object.  `ZquicLog(lambda)` is
  the qlog function, and `ZquicLOG((lambda))` is the source marker macro used
  by runtime qlog instrumentation.
- `ZquicLog` starts/stops with the engine and writes JSON-SEQ records through
  the qlog writer thread.
- diagnostics count records enqueued, written, dropped, ring pressure, writer
  failures, and bytes written.
- non-debug builds compile the qlog API to no-op stubs.
- qlog writer code uses `ZtJSON` / `ZtStruct` JSON mappings.
- arbitrary qlog string fields now own their data with `ZeString`; borrowed
  `ZuCSpan` is reserved for static event names and static header literals.
- qlog header emission now snapshots endpoint role and connection identity
  metadata.  `ZquicLogger` is the process-wide `ZmSingleton` initialized once
  by the owning app during application `init` and finalized after protocol
  machinery is drained during application `final`; after initialization,
  `ZquicLogger::enabled()` is the single runtime availability check.  Direct
  writer tests assert a configured `client` vantage point plus hex-encoded
  `common_fields` for ODCID, group id, DCID, and SCID.  Runtime server traces
  assert `"type":"server"` plus non-empty ODCID, group id, DCID, and SCID in
  the JSON-SEQ header.
- protocol-derived enum fields use `ZtEnumMap` JSON mappings instead of
  helper functions that convert enums to strings before serialization.
- typed event structs and writer helpers exist for datagram, packet/frame
  summary, ACK, recovery, ECN, security, path, and CID events.
- `ZquicLogFrameEvent` is marked as a Z POD/plain snapshot type so bounded
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
  `security:retry_sent`, `security:retry_validated`,
  `security:token_issued`, `security:token_validated`,
  `security:token_rejected`, `security:version_negotiation`, and
  `security:stateless_reset` events.  Packet protection failures now emit
  typed `security:packet_protection_failed` events with packet space, trigger,
  and owned/redacted reason data.  Runtime call sites use
  `ZquicLOG((...))` for server Retry send/validation, NEW_TOKEN issue and
  validation, Version Negotiation sent, stateless reset sent/detected, and
  client Retry acceptance/rejection, plus Tx/Rx missing-key, protect/unprotect,
  and invalid key phase failures.
- security event `kind`, `key_type`, `trigger`, and `reason` are now closed
  `ZtEnum` fields with `ZtEnumMap` JSON mappings.  Runtime call sites set the
  enums directly and the writer serializes the qlog spelling through `ZtJSON`,
  rather than storing strings only to output those strings again.  `key_type`
  currently maps `rx`, `rx_old`, and `tx`; `trigger` maps `sent`, `received`,
  `validated`, `local`, `remote`, `peer`, `selected`, `timer`,
  `handshake_complete`, `rx`, and `tx`; `reason` maps the current security
  status/failure vocabulary including token-validation statuses, Retry/VN/
  stateless-reset labels, packet-protection failures, and key/TLS lifecycle
  labels.  Each has a `None` value mapped to the existing empty JSON string for
  non-applicable events.
- security ALPN data remains an owned `ZeString` because ALPN is negotiated
  application protocol data, not a closed QUIC vocabulary.  The runtime ALPN
  qlog call site now captures `ZeString{m_crypto.negotiatedProtocol()}` by
  value inside `ZquicLOG((...))` and moves it into the event on the logger
  thread, so no borrowed `ZuCSpan` crosses the async boundary.
- path, PMTUD, and CID event `kind`, `action`, and `reason` fields are now
  closed `ZtEnum` fields with `ZtEnumMap` JSON mappings.  Runtime call sites
  capture enum values, construct path/CID qlog event structs with aggregate
  initializers inside `ZquicLOG((...))`, and the writer serializes the qlog
  spelling through `ZtJSON` rather than storing short string literals in
  `ZeString`.
- packet event `reason` is now a closed `ZtEnumMap`-backed field for the
  current packet buffering/drop vocabulary, including `coalescing`,
  `parse_long`, `packet_length`, `prepare_long`, `unsupported_long_type`,
  `discarded_space`, `missing_keys`, `protection`, `duplicate`, `parse_short`,
  `invalid_key_phase`, `anti_amplification`, `probe_admission`, and
  `app_send`.  Runtime packet-buffer/drop call sites capture enum values and
  assign enum values inside `ZquicLOG((...))`; `ZtJSON` performs the JSON
  spelling conversion on the logger thread.
- transport packet buffering coverage now includes typed
  `transport:packet_buffered` events for protected Initial packets retained
  for long-header coalescing.  The runtime call site uses `ZquicLOG((...))`,
  captures only packet length and enum reason data by value, and constructs
  the packet qlog event on the logger thread.
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
  `local_close`, `peer_close_frame`, `drain_expired`, `idle`, `unknown`, and
  `no_error`.  Runtime close call sites capture only scalar close state and
  enum values; JSON spelling occurs in the logger thread.
- `ZquicLogTest` has focused typed writer coverage for transport, recovery,
  security, path, CID, and stream event JSON.
- `ZquicLogTest` now verifies packet frame summaries across ACK, CRYPTO,
  STREAM, RESET_STREAM, STOP_SENDING, MAX_STREAM_DATA, NEW_CONNECTION_ID,
  RETIRE_CONNECTION_ID, PATH_CHALLENGE, CONNECTION_CLOSE, ACK_ECN counters,
  enum-mapped packet reasons, truncation flags, and frame snapshot movement
  through the qlog ring path.
- `ZquicStreamTest::testRuntimeReceiveQLog` now provides end-to-end runtime
  receive-path qlog coverage for a protected short-header 1-RTT packet.  It
  verifies disabled qlog does not construct the receive accumulator, enabled
  qlog does construct it, and JSON-SEQ output contains
  `transport:packet_received` with packet number, PING frame data, frame count,
  and the frame truncation field.
- `ZquicLogTest` and `ZquicStreamTest` now use a stricter local JSON-SEQ
  scanner: every record must begin with RFC 7464 record separator, end with a
  newline, and be fully consumed by `ZtJSON::scan`.  This catches malformed
  JSON-SEQ records and trailing garbage instead of accepting a valid JSON
  prefix only.
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
  `ZquicLogTest` covers `transport:packet_buffered` writer output and
  `ZquicRuntimeTest` includes qlog assertions for Retry/NEW_TOKEN,
  missing-token, malformed-token, token-authentication-failure, expired-token,
  address-mismatch, invalid token kind, and NEW_TOKEN policy
  `security:token_rejected`, plus coalescing `transport:packet_buffered`
  runtime output in the current unrestricted runtime environment.
  `ZquicLogTest` also covers typed
  `transport:stream_state_updated` writer output, and
  `ZquicRuntimeTest::testRuntimeEndpointOpen` now asserts runtime stream-state
  qlog output for application stream open/close traffic.
- runtime stream flow-control qlog coverage now uses spec-aligned
  `transport:connection_data_blocked_updated` and
  `transport:stream_data_blocked_updated` events for DATA_BLOCKED,
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
  `transport:stream_data_moved` coverage aligned with the qlog QUIC event
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
  `ZquicLogTest` verifies enum-mapped packet reasons and all packet frame
  summaries survive by-value lambda/ring movement.
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
- current datagram sent/received verification passes:
  `make -C zquic/test -j8 ZquicLogTest ZquicRuntimeTest`,
  `libtool exec ./zquic/test/ZquicLogTest`, and
  `timeout 240s libtool exec ./zquic/test/ZquicRuntimeTest`.
  `ZquicLogTest::testQLogTypedTransportEvents` now asserts both
  `transport:datagrams_received` and `transport:datagrams_sent`, and
  `ZquicRuntimeTest::testRuntimeEndpointOpen` asserts runtime server-trace
  output for `transport:datagrams_sent`.
- current path-admission packet-drop verification passes:
  `make -C zquic/src -j8`,
  `make -C zquic/test -j8 ZquicStreamTest ZquicLogTest`,
  `libtool exec ./zquic/test/ZquicStreamTest`, and
  `libtool exec ./zquic/test/ZquicLogTest`.  A broader runtime check also
  passes: `make -C zquic/test -j8 ZquicRuntimeTest` and
  `timeout 240s libtool exec ./zquic/test/ZquicRuntimeTest`.
  Path send rejection now logs both
  `transport:path_updated` with blocked path reason and
  `transport:packet_dropped` with enum-mapped packet-drop reason for
  anti-amplification, probe-admission, and app-send rejection cases.
  `ZquicStreamTest::testActivePathRuntimeBudget` now asserts
  `transport:packet_dropped` and `anti_amplification` in runtime qlog output.
- current negative token/protection-failure runtime qlog verification passes:
  `make -C zquic/src -j8`,
  `make -C zquic/test -j8 ZquicRuntimeTest`,
  `timeout 240s libtool exec ./zquic/test/ZquicRuntimeTest`,
  `make -C zquic/test -j8 ZquicAPITest ZquicStreamTest`,
  `ZquicAPITest`, and `ZquicStreamTest`.
  `ZquicRuntimeTest::testRuntimeRetryAddressValidation` now asserts
  missing-token rejection output via `security:token_rejected` and
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
  contains `transport:packet_dropped`, `security:packet_protection_failed`,
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
  `ZquicLogger::metadata()` records the configured endpoint role and owned
  byte snapshots, rejects later opposite-role metadata for the same
  process-wide logger instance, writes the role as
  `trace.vantage_point.type`, and writes connection identity bytes as hex
  through `ZtStruct`/`ZtJSON` `JSON::Hex` mappings rather than manual JSON.
  The server accept path now falls back from absent stored ODCID to the Initial
  header DCID before posting `connection_started`, so the first emitted header
  has non-empty ODCID/group/DCID/SCID in the endpoint runtime test.
- current spec-aligned blocked-event verification passes:
  `make -C zquic/src -j8`,
  `make -C zquic/test -j8 ZquicLogTest ZquicStreamTest`,
  `ZquicLogTest`, and `ZquicStreamTest`.
  `ZquicLogTest` covers writer JSON for
  `transport:connection_data_blocked_updated` and
  `transport:stream_data_blocked_updated`; `ZquicStreamTest` covers runtime
  flow-control qlog output for the same event names plus enum-mapped
  `blocked`/`unblocked` states and flow-control reasons.
- current close/drain lifecycle runtime verification passes:
  `make -C zquic/src -j8`,
  `make -C zquic/test -j8 ZquicLogTest ZquicRuntimeTest`,
  `ZquicLogTest`, and
  `timeout 240s libtool exec ./zquic/test/ZquicRuntimeTest`.
  `ZquicRuntimeTest::testRuntimeEndpointOpen` now forces the server close
  timer dispatch on the Tx thread after a peer close and asserts qlog output
  contains `connectivity:connection_closed`, peer/local close reason data,
  `drain_expired`, and `aborted`.  `connection_closed.initiator` and
  `connection_closed.trigger` are now `ZtEnumMap`-backed JSON enum values
  (`local`/`remote` and `application`/`error`/`idle_timeout`/`aborted`), not
  preformatted strings.  Close reason and error name fields are now also
  `ZtEnumMap`-backed JSON enum values for the current closed vocabulary.
- current idle-timeout lifecycle runtime verification passes:
  `make -C zquic/test -j8 ZquicRuntimeTest` and
  `timeout 180s libtool exec ./zquic/test/ZquicRuntimeTest`.
  `ZquicRuntimeTest::testRuntimeIdleTimeoutQLog` drives the normal negotiated
  idle-timeout path and asserts qlog output contains
  `connectivity:connection_closed`, `idle_timeout`, `idle`, and `no_error`.
- current PTO qlog verification passes:
  `make -C zquic/src -j8`,
  `make -C zquic/test -j8 ZquicLogTest ZquicStreamTest`,
  `ZquicLogTest`, `ZquicStreamTest`,
  `make -C zquic/test -j8 ZquicRecoveryTest`, and
  `ZquicRecoveryTest`.  Runtime PTO paths now emit typed
  `recovery:loss_timer_updated` events for PTO expiry, PTO backoff change, and
  ping probe generation.  The new `expired`, `backoff`, and `probe` reasons
  are `ZtEnumMap`-backed `ZquicLogRecoveryReason` values, so JSON spelling is
  performed by `ZtJSON` on the logger thread.  `ZquicLogTest` verifies the
  writer JSON shape for these reasons, and
  `ZquicStreamTest::testPTOQLog` drives the protected `Zquic.hh` PTO reclaim
  and probe-generation code paths and asserts runtime JSON-SEQ output contains
  `recovery:loss_timer_updated`, `expired`, `backoff`, and `probe`.
- prior focused non-debug qlog no-op checks in a disposable release build are
  useful smoke evidence only.  They are not final release verification.  Final
  release signoff must be configured through `z.config` and then use top-level
  `make clean` followed by top-level `make -j8` so all dependent Z libraries,
  generated dependency files, and tests are rebuilt consistently.

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
- qlog macro arguments that contain lambda capture-list commas must be wrapped
  at call sites as `ZquicLOG(([...] { ... }))` or `ZquicLOG(([...](...) {
  ... }))`; do not make `ZquicLOG` variadic for this.
- the `# qlog` section of `zquic/GUIDELINES.md` is now the call-site authority
  for qlog instrumentation style and must be followed before further runtime
  qlog edits.
- receive packet frame-summary accumulation uses the documented
  `ZquicLogger::enabled()` exception and `ZuElem<ZquicLogPacketEvent>` storage
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

Still missing or incomplete:

- the runtime qlog call-site audit against `zquic/GUIDELINES.md` has removed
  the former
  event-wrapper helper layer for existing qlog families.  Remaining audit work
  is to keep frame-summary support helpers confined to `ZquicLOG((...))`
  lambdas or the receive accumulator exception, and to apply the same style to
  all new qlog coverage.
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
- focused non-debug library/test rebuilds from the earlier disposable release
  tree are smoke evidence only.  They do not satisfy final release
  verification.  A final non-debug/no-op pass must be configured through
  `z.config` and then rebuilt with top-level `make clean` followed by
  top-level `make -j8`.
- stream/application-facing qlog coverage is partial:
  `transport:stream_state_updated` now covers stream `idle` -> `open` creation
  and `open` -> `closed` reaping with stream id/type/side and enum-mapped
  reason values.  Spec-aligned flow-control blocked/unblocked coverage exists for
  the send-side BLOCKED/MAX_DATA/MAX_STREAM_DATA paths, including runtime
  JSON-SEQ tests for connection and stream blocked event names.
  `transport:stream_data_moved` now covers application data accepted into
  stream Tx buffers, packetized STREAM frame data moving toward the network,
  received STREAM frame data accepted into transport buffers, and delivery from
  transport buffers to application reads, with `raw.length` and without payload
  bytes.
  Connection-level unblocked coverage still needs an end-to-end scenario that
  reaches a genuinely blocked connection data-credit state before MAX_DATA is
  applied; the current MAX_DATA unblocked emission is instrumented and covered
  indirectly through focused flow-control tests.  Close/drain expiry runtime
  qlog coverage exists for the peer-close drain path, and idle-timeout runtime
  qlog coverage exists for the normal negotiated idle-timeout path.
- Retry/VN/stateless reset/token/protection-failure qlog coverage is partial:
  key runtime decision points are instrumented and writer-tested, and
  Retry/NEW_TOKEN plus missing-token, malformed-token,
  token-authentication-failure, expired-token, address-mismatch, invalid token
  kind, and NEW_TOKEN policy rejection runtime output is covered by
  `ZquicRuntimeTest`.
  Corrupt protected short-header packet runtime output is covered by
  `ZquicStreamTest`.
- top-level connection metadata is now schema/API-complete at the writer layer
  and covered by runtime endpoint tests for the server trace.  Remaining gaps
  are broader stream/application-facing coverage, any future receive-side
  packet buffering for undecryptable packets, multi-connection trace identity
  expectations for mainstream qlog consumers, and external qlog-tool
  validation.  Local JSON-SEQ scanning is stricter now, but it is not a
  substitute for loading representative traces in mainstream qlog tooling.  The
  current implementation drops missing-key packets instead of buffering them,
  and logs those decisions as packet drops plus packet-protection failure
  events.

Goals
-----

1. Keep qlog entirely debug-gated, low-overhead when disabled, and nonblocking
   when enabled.
2. Emit qlog-mainline compatible event names and data objects for the events
   needed to debug interop, recovery, migration, ECN, and packet protection.
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

Required top-level trace/header metadata:

- qlog version and format remain JSON-SEQ;
- trace title, vantage point type (`client`, `server`, or `unknown`), original
  destination CID when available, group id or connection id, and implementation
  name/version;
- per-event relative or absolute time with a consistent unit matching qlog
  expectations.

Required transport events:

- `transport:packet_sent`;
- `transport:packet_received`;
- `transport:packet_dropped`;
- `transport:packet_lost`;
- `transport:packet_buffered`;
- `transport:packets_acked`;
- `transport:datagrams_sent`;
- `transport:datagrams_received`.

Packet events should include packet type, packet space, packet number when
known, datagram size, packet size, header and payload length when known,
DCID/SCID where useful, ECN mark, frames, ACK ranges, ACK delay,
coalescing/datagram context, drop/loss reason, and bytes in flight.

Required frame coverage:

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

Required recovery/congestion events:

- `recovery:metrics_updated` for RTT sample, smoothed RTT, RTT variance, min
  RTT, latest RTT, and max ACK delay context;
- `recovery:loss_timer_updated` for Initial, Handshake, and 1-RTT timers,
  including timer type and deadline;
- `recovery:packet_lost` with loss trigger and packet metadata;
- `recovery:marked_for_retransmit` with frame-ref kind and crypto/stream
  offsets;
- `recovery:congestion_state_updated` with cwnd, ssthresh, bytes in flight,
  recovery start, persistent congestion, and controller name;
- `recovery:ecn_state_updated` with validation state, monotonicity failure,
  CE reaction, and fallback disablement;
- PTO expiry, probe generation, and PTO backoff changes.  Add pacer delay when
  pacing is integrated.

Required TLS/security events:

- transport parameters sent and received, with bounded/redacted values;
- ALPN selection;
- TLS alert or handshake failure;
- key update start, completion, rejection, old-key acceptance, and invalid key
  phase;
- key discard for Initial, Handshake, and old 1-RTT keys;
- Retry validation result and Retry integrity failure;
- Version Negotiation sent/received;
- stateless reset detection;
- token issuance, token validation, and token rejection reason;
- packet protection failure reason, without logging key material or plaintext.

Required path/CID events:

- active path creation and update;
- peer address change or NAT rebinding candidate;
- anti-amplification limit blocking and unblocking;
- path validation challenge sent, response received, validation success, and
  validation failure/timeout;
- PMTUD probe sent, ACKed, lost, blackhole detected, and MTU updated;
- connection ID issued, retired, route-bound, and stateless reset token
  associated;
- preferred-address events when that feature is implemented.

Required stream/application-facing events:

- stream opened and closed;
- stream state transitions that matter to applications;
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
   `ZquicLogger::enabled()` and `ZuElem<ZquicLogPacketEvent>`; confirm disabled
   qlog does not allocate, default-construct, or populate qlog event state,
   while enabled qlog still records bounded frame summaries.  This now has
   direct runtime coverage in `ZquicStreamTest::testRuntimeReceiveQLog`.
5. Rebuild `zquic/src`, rebuild focused tests, and run `ZquicLogTest`,
   `ZquicStreamTest`, `ZquicPMTUDTest`, `ZquicCIDTest`, plus handshake/recovery
   smoke tests before continuing to new stream/security/header coverage.
   Current debug build/test pass is complete for this checklist item.
6. Add a non-debug compile/no-op verification pass for `Zquic_DEBUG`-off
   builds before treating macro/API cleanup as complete.  Earlier disposable
   `-DNDEBUG` checks are smoke evidence only; final release verification must
   use `z.config`, top-level `make clean`, and top-level `make -j8`.

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

2. Add a qlog metadata snapshot layer.
   - Define small helper enums/functions for qlog packet space, packet type,
     frame type, drop reason, loss trigger, timer type, and redaction category.
   - Snapshot connection identity once when the engine/link starts: vantage
     point, ODCID/DCID/SCID, and local role.
   - Make event helpers accept already-owned metadata; they should not inspect
     non-owner state from the writer side.

3. Instrument datagram and packet receive.
   - Emit `transport:datagrams_received` when a UDP datagram enters the QUIC
     receive path.
   - Emit `transport:packet_received` after successful long/short packet
     protection and frame parse, with packet number, space, size, ECN, and frame
     summaries.
   - Emit `transport:packet_dropped` for parse failure, unsupported 0-RTT,
     missing keys, duplicate packet, discarded packet space, packet protection
     failure, stateless reset, version mismatch, token/address validation
     failure, and frame legality failure.
   - Keep drop reasons as enum/string constants, not ad hoc text.

4. Instrument datagram and packet send.
   - Emit `transport:packet_sent` after a datagram/packet is accepted by path
     and app gating, not merely after packet protection succeeds.
   - Include protected packet number, packet type/space, bytes, frames, ACK
     ranges, ACK delay, ECN marking once available, coalesced datagram context,
     and bytes in flight after accounting.
   - Emit `transport:datagrams_sent` once per actual UDP send.
   - Emit `transport:packet_buffered` when a packet is retained for later
     emission or processing.  Initial long-header coalescing is now covered
     with reason `coalescing`; add receive-side buffered-packet events if the
     implementation later buffers undecryptable packets instead of dropping
     them.
   - Emit `transport:packet_dropped` for path budget rejection,
     anti-amplification blocking, congestion blocking, PMTUD/probe admission
     failure, app send hook rejection, and missing traffic secrets.

5. Add frame summary builders.
   - Reuse parsed `Frame` data where available on Rx.
   - Reuse `TxPktRefs`, ACK metadata, CRYPTO offsets, and stream/control refs on
     Tx instead of reparsing protected buffers.
   - Bound the number of frame summaries per packet to `SentPkt::MaxFrames` or
     another named qlog cap; set a truncation flag if exceeded.
   - Never copy frame payload bytes into qlog events.

6. Instrument ACK, loss, and recovery.
   - Emit `transport:packets_acked` from ACK processing with acknowledged
     ranges and largest ACKed metadata.
   - Emit `transport:packet_lost` and `recovery:packet_lost` from the existing
     loss decision point with loss trigger and packet space.
   - Emit `recovery:marked_for_retransmit` when frame refs are queued for
     retransmission.
   - Emit `recovery:metrics_updated` when RTT samples update recovery state.
   - Emit `recovery:loss_timer_updated` whenever loss/PTO timer state is armed,
     canceled, or moved.
   - Emit PTO expiry/probe events from the PTO timeout path.  PTO expiry,
     backoff, and probe-generation qlog events are now emitted and tested;
     extend this when pacing introduces pacer-delay state.

7. Instrument congestion and ECN.
   - Emit `recovery:congestion_state_updated` from NewReno state transitions,
     loss reaction, persistent congestion, and ACK-driven window growth.
   - Emit `recovery:ecn_state_updated` for ACK_ECN monotonic validation,
     peer ECN counter growth, validation failure, CE reaction, and fallback
     disablement once implemented.
   - Keep event emission close to the state change so the qlog trace does not
     require reconstructing state from counters.

8. Instrument TLS/security.
   - Emit transport parameter sent/received events after decode/encode with
     bounded values and explicit redaction.
   - Emit ALPN selected, handshake failure, TLS alert, and 0-RTT rejection from
     the crypto/TLS callback paths.
   - Emit key install/update/discard events from traffic secret update,
     key-update, and discard paths.
   - Retry, Version Negotiation, stateless reset, token issue/validate/reject,
     and packet protection failure now have typed qlog event names and runtime
     call sites.  Security event `kind`, `key_type`, `trigger`, and `reason`
     are enum-mapped.  Missing-token, malformed-token,
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
      open and final reaping now emit `transport:stream_state_updated`; add
      finer sending/receiving state transitions as they become explicit in the
      stream state machine.
    - Emit flow-control blocked/unblocked where blocked state changes.  The
      send-side BLOCKED/MAX_DATA/MAX_STREAM_DATA paths now emit and test
      spec-aligned connection/stream blocked qlog; add a connection-level
      MAX_DATA unblocked end-to-end runtime scenario that proves the connection
      data-credit state was genuinely blocked before MAX_DATA arrived, plus
      any receive/application-visible flow state transitions that are made
      explicit later.
    - Emit `transport:stream_data_moved` for application data movement.
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
    - Add tests for per-connection metadata: vantage point, connection/CID
      fields, qlog version/format, and monotonically valid event timestamps.
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
    - Confirm the output can be loaded by mainstream qlog viewers without
      custom parsing.
    - Keep any known schema deviations documented in `audit.md` until fixed.

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
   remain no-op stubs and compile cleanly.
3. qlog disabled is a cheap branch.  Disabled qlog does not build frame-summary
   arrays, format JSON, allocate, or copy packet payloads.
4. The trace header includes qlog version/format, implementation identity,
   vantage point, and per-connection identity/CID metadata where available.
5. The required transport, frame, recovery/congestion, TLS/security, path/CID,
   and stream/application-facing event families listed above are emitted from
   runtime code, not only from direct `ZquicLog` unit-test calls.
6. Packet send/receive/loss/drop events include enough metadata to reconstruct
   packet space, type, packet number, size, frames, ACK ranges, ECN mark, drop or
   loss reason, coalescing context, and bytes-in-flight where applicable.
7. Recovery traces expose RTT updates, loss/PTO timer changes, PTO firing,
   retransmission marking, congestion state, and ECN state/fallback decisions.
8. Security traces expose transport parameters, ALPN, key install/update/discard,
   Retry/Version Negotiation/stateless reset/token outcomes, and protection
   failures without logging secrets, plaintext, or raw tokens.
9. Path/CID traces expose anti-amplification blocking, path validation, PMTUD,
   active path changes, CID issue/retire/route binding, and reset-token
   association.
10. Tests cover unit-level writer behavior, disabled/no-op behavior, JSON-SEQ
    validity, typed event schemas, runtime event emission for each required
    event family, redaction, back-pressure/drop diagnostics, and frame-summary
    truncation.
11. The focused `zquic` test binaries listed in the verification target pass in
    the relevant debug build.
12. Representative generated `.sqlog` files load in mainstream qlog tooling, or
    any remaining tooling/schema deviation is documented in `audit.md` with a
    follow-up item.
