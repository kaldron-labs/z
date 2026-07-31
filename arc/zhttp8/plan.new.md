## Summary

Close the `zquic` functionality gaps identified against `../zngtcp2` for QUIC
v1 frame handling, stream limits, send/receive flow control, and
retransmittable control-frame behavior. The goal is ngtcp2-level protocol
coverage and edge-case handling while preserving Zquic's existing CRTP shape,
`Zu`/`Zm`/`Zt` containers, scheduler model, and compact protocol helpers.

The prior plan correctly identified the major missing pieces: runtime
`STREAM` receive processing bypasses `ReceiveFlow`; `MAX_*` and `*_BLOCKED`
frames are only partially validated; stream-count credit is not returned when
peer streams finish; and control frames such as `MAX_DATA`,
`MAX_STREAM_DATA`, `MAX_STREAMS`, `DATA_BLOCKED`, `STREAM_DATA_BLOCKED`,
`STREAMS_BLOCKED`, `PATH_RESPONSE`, and `HANDSHAKE_DONE` are not sent through a
single retransmittable scheduler path.

This revised plan sequences the work vertically. Each phase wires one behavior
from frame parse through connection state, packet assembly, recovery, and tests,
rather than building isolated layers that remain unused by runtime paths.

Research and comparison inputs:

- RFC 9000 requires receivers to close on stream or connection flow-control
  violations, and senders to ignore `MAX_DATA`/`MAX_STREAM_DATA` values that do
  not increase limits.
- The IANA QUIC frame registry confirms the v1 frame set from `0x00` through
  `0x1e`, plus extension `DATAGRAM` frames outside base QUIC.
- RFC 9308 notes the common implementation pattern of extending flow-control
  credit with `MAX_DATA` and `MAX_STREAM_DATA` as data is consumed.
- `../zngtcp2/lib/ngtcp2_conn.c` functions
  `conn_recv_max_stream_data`, `conn_recv_max_data`,
  `conn_recv_max_streams`, `conn_recv_stream_data_blocked`,
  `conn_recv_data_blocked`, and `conn_recv_streams_blocked_*` define the local
  comparison target.
- `../zngtcp2/lib/ngtcp2_strm.c` and `../zngtcp2/lib/ngtcp2_rtb.c` show that
  lost control frames are retransmitted only while their state predicate still
  requires them.
- `../zngtcp2/tests/ngtcp2_conn_test.c` contains the parity cases Zquic needs,
  especially receive `STREAM_DATA_BLOCKED`, receive `DATA_BLOCKED`, receive
  `STREAMS_BLOCKED`, receive `MAX_STREAMS`, receive `MAX_STREAM_DATA`, and send
  `DATA_BLOCKED`/`STREAM_DATA_BLOCKED`.

## Architecture Documentation

### New Or Changed Components

- `zquic/src/ZquicTypes.hh`
  - Add explicit protocol vocabulary only where runtime behavior is defined:
    `ApplicationClose` for frame `0x1d`.
  - Keep `Datagram`/`DatagramLen` out of the implemented runtime. Zquic must
    not advertise DATAGRAM support, and must decline peer DATAGRAM use in the
    RFC-compliant way: do not negotiate the `max_datagram_frame_size` transport
    parameter, and treat received DATAGRAM frames as a protocol violation
    because support was not negotiated.

- `zquic/src/ZquicFrame.hh` and `zquic/src/ZquicFrame.cc`
  - Preserve the compact `Frame` structure and existing codec style.
  - Extend parse/write/diagnostic support for `ApplicationClose`.
  - Add explicit unsupported DATAGRAM tests; do not add DATAGRAM application
    parse/write support in this gap-closure.

- `zquic/src/ZquicStream.hh`
  - Reuse and extend `FlowCredit`, `ReceiveFlow`, `FlowUpdate`,
    `StreamRxState`, and `StreamTxState`.
  - Add receive-flow state to stream objects instead of passing
    stack-allocated `ReceiveFlow` only in tests.
  - Add close lifecycle flags needed to return stream-count credit exactly once.

- `zquic/src/ZquicRecovery.hh`
  - Replace generic `SentFrameRef::control()` with enough identity to
    reconstruct a lost control frame or decide it no longer needs
    retransmission.
  - Use ngtcp2-style predicates: retransmit `MAX_STREAM_DATA` only if it still
    equals the current advertised receive limit and the read side is open;
    retransmit `STREAM_DATA_BLOCKED` only if the stream is still blocked at the
    same send limit; drop obsolete control retransmits.

- `zquic/src/ZquicSched.hh`
  - Keep `StreamScheduler`/`TxScheduler`; add a control-frame queue keyed by a
    small internal control ID or by frame identity.
  - Packet assembly must pull pending control frames before STREAM frames and
    record each transmitted control frame for recovery.

- `zquic/src/Zquic.hh`
  - Move runtime flow-control decisions into `Link`.
  - Add connection receive credit initialized from local transport parameters.
  - Add per-stream receive credit initialized from local stream transport
    parameters according to stream direction and initiator.
  - Add common validation helpers for `MAX_*` and `*_BLOCKED` frames used by
    both `CliLink` and `SrvLink`.
  - Replace direct `sendAckElicitingShortFrame_` calls for flow/path/control
    frames with queue-and-flush functions.

### New Or Changed Processes Or Threads

No new scheduler threads are required. The work should tighten ownership:

- Rx callbacks parse and validate frames on the existing Rx context.
- Any packet send or packet-protection operation must remain serialized on the
  Tx context or under the current `m_txLock` until all send call paths are
  proven single-threaded.
- Control-frame enqueuing may occur from Rx, but actual packet construction and
  protection should be routed through the same Tx-side helpers that send STREAM
  data.

### New Or Changed Interfaces

- Add `Link` helpers:
  - `receiveStreamFrame_(Frame, packet, diag)` using stream and connection
    receive-flow state.
  - `receiveResetStream_(Frame)` with final-size and credit-return accounting.
  - `queueControl_(ControlFrame)` and `flushControlAndStreams_(addr)`.
  - `validateMaxStreamData_(Frame)`, `validateStreamDataBlocked_(Frame)`,
    `validateDataBlocked_(Frame)`, and `validateStreamsBlocked_(Frame)`.
  - `maybeExtendMaxData_`, `maybeExtendMaxStreamData_`, and
    `maybeExtendMaxStreams_`.

- Add stream helpers:
  - `rxCreditAvailable`, `rxCreditLimit`, `extendRxCredit`.
  - `noteRxConsumed` or equivalent when application drains receive data.
  - `closeRead`, `closeWrite`, `closedForStreamCredit`, and
    `markStreamCreditReturned`.

- Add recovery helpers:
  - `SentFrameRef::flowUpdate(FlowUpdate)`.
  - `SentFrameRef::blocked(FrameType, streamID, limit, streamType)`.
  - `SentFrameRef::pathResponse(data)`.
  - `SentFrameRef::handshakeDone()`.

### New Or Changed Data Flows

- Inbound protected `STREAM`:
  1. Packet is parsed and decrypted.
  2. `FrameCodec::parse` yields `FrameType::Stream`.
  3. `Link` finds or accepts the stream, validates stream direction and stream
     count.
  4. `ReceiveFlow` computes novel stream bytes and checks both stream and
     connection limits.
  5. Stream data is queued to the existing `RxStream`.
  6. Consumed/discarded data triggers queued `MAX_DATA` and
     `MAX_STREAM_DATA` updates.

- Outbound STREAM:
  1. Application writes into stream Tx queues.
  2. Packetizer caps new bytes by PMTU, congestion, anti-amplification,
     connection send credit, and stream send credit.
  3. Newly transmitted bytes consume credit once.
  4. Retransmitted bytes do not consume credit again.
  5. Blocked streams queue `DATA_BLOCKED` or `STREAM_DATA_BLOCKED` once per
     observed limit.

- Control frames:
  1. Runtime state change enqueues a typed control item.
  2. Tx assembly writes pending ACK plus control frames, then STREAM data if
     space remains.
  3. Sent packets record typed control refs.
  4. Loss/PTO requeues only still-relevant control refs.

### New Or Changed Event-Driven Or Timer Processing

- Existing PTO processing must be extended to rebuild typed control frames, not
  only STREAM frames.
- ACK processing for lost packets must not requeue obsolete `MAX_*` updates
  after a newer limit has already been sent.
- After receiving `MAX_DATA`, `MAX_STREAM_DATA`, or `MAX_STREAMS`, schedule a
  Tx flush so blocked streams or queued stream opens resume without waiting for
  unrelated application writes.

### New Or Changed Network Programming

- `PATH_CHALLENGE` should enqueue a `PATH_RESPONSE` control frame and flush it
  through the protected Tx path.
- `PATH_RESPONSE` should remain accepted as a no-op until Zquic path validation
  state exists, but tests must verify it is not treated as `Unknown`.
- `HANDSHAKE_DONE` remains server-send/client-receive only. A server receiving
  it is a protocol error.

### New Or Changed Data Stores

- Add no external data store.
- Add in-memory Z containers:
  - a link-owned control-frame queue using `ZmQueue` or existing scheduler
    patterns, with explicit heap IDs;
  - optional stream ID keyed state in existing `Streams_` objects, not an STL
    map;
  - per-stream flags/counters for receive credit, blocked limit suppression,
    and stream-count credit return.

## Detailed Design and Implementation Plan

### Phase 1 - Frame Coverage Matrix And Vocabulary

- Add a maintained frame matrix to `zquic/test/ZquicFrameMatrixTest.cc` or a
  table comment in `ZquicCodecTest.cc`.
- Matrix columns:
  - frame code,
  - `FrameType`,
  - parse/write status,
  - runtime handler,
  - packet-space legality,
  - role legality,
  - ack-eliciting classification,
  - retransmittable when sent,
  - test coverage.
- Include all ngtcp2/RFC9000 base frames:
  - `PADDING`, `PING`, `ACK`, `ACK_ECN`, `RESET_STREAM`, `STOP_SENDING`,
    `CRYPTO`, `NEW_TOKEN`, `STREAM`, `MAX_DATA`, `MAX_STREAM_DATA`,
    `MAX_STREAMS_BIDI`, `MAX_STREAMS_UNI`, `DATA_BLOCKED`,
    `STREAM_DATA_BLOCKED`, `STREAMS_BLOCKED_BIDI`, `STREAMS_BLOCKED_UNI`,
    `NEW_CONNECTION_ID`, `RETIRE_CONNECTION_ID`, `PATH_CHALLENGE`,
    `PATH_RESPONSE`, `CONNECTION_CLOSE`, `CONNECTION_CLOSE_APP`,
    `HANDSHAKE_DONE`.
- Add `ApplicationClose` for frame `0x1d` and update parser, writer if needed,
  diagnostics, and tests.
- Document DATAGRAM explicitly as out of scope:
  - do not advertise or accept `max_datagram_frame_size`;
  - do not add DATAGRAM app callbacks or DATAGRAM packetization;
  - reject received DATAGRAM frames as a protocol violation because DATAGRAM was
    not negotiated, rather than silently ignoring them.

### Phase 2 - Runtime Receive Flow For STREAM And RESET_STREAM

- Add connection receive state to `Link`, initialized from local transport
  params in `configureLocalTransportParams_`/runtime reset:
  - advertised max data,
  - window size for update thresholds,
  - unsent/last-sent update limit.
- Add per-stream receive state when creating local or peer streams:
  - local bidi receives `initialMaxStreamDataBidiLocal`;
  - peer bidi receives `initialMaxStreamDataBidiRemote`;
  - peer uni receives `initialMaxStreamDataUni`;
  - local uni has no receive side.
- Change runtime `Link::receiveFrame(frame, packet)` so it no longer calls
  `stream->processFrame(frame, packet)` without `ReceiveFlow`.
- Preserve direct `Stream::receiveFrame(frame, flow, packet)` tests, but add
  runtime tests proving the connection path uses it.
- `RESET_STREAM` must account final size against connection receive data if the
  final size advances `last_offset`, matching ngtcp2's behavior.
- On flow-control violation, fail the packet with `TransportError::FlowControl`
  and close the runtime connection through existing close/error machinery.

### Phase 3 - Receive Credit Extension And MAX Updates

- Use existing `ReceiveFlow::maxDataUpdate` and
  `ReceiveFlow::maxStreamDataUpdate` as the starting point.
- Trigger credit extension when:
  - application consumes receive bytes from `RxStream`;
  - data is discarded because of reset, stop-sending, or stream close;
  - `STREAM_DATA_BLOCKED` advances a stopped stream's receive offset and
    ngtcp2 would extend connection credit.
- Queue `MAX_DATA` and `MAX_STREAM_DATA` using `FlowUpdate`, not direct sends.
- Keep the initial threshold simple: update when available credit falls below
  half the window. Structure fields so adaptive BDP/window growth can be added
  later.
- Add duplicate suppression: if a MAX frame for the same or newer limit is
  already queued or sent, do not enqueue an older one.

### Phase 4 - Typed Retransmittable Control Frames

- Replace generic `SentFrameRef::control()` with typed refs sufficient to
  rebuild frames:
  - `FlowUpdate` for `MAX_DATA`, `MAX_STREAM_DATA`, `MAX_STREAMS`;
  - blocked refs for `DATA_BLOCKED`, `STREAM_DATA_BLOCKED`,
    `STREAMS_BLOCKED`;
  - fixed payload refs for `PATH_RESPONSE`;
  - singleton refs for `HANDSHAKE_DONE`.
- Add `ControlFrame` as a small POD-like struct in `ZquicSched.hh` or
  `ZquicRecovery.hh`, with a `write(uint8_t *, unsigned)` method mirroring
  `FlowUpdate::write`.
- Use a `ZmQueue` or the existing `TxScheduler` control lane with explicit
  heap IDs.
- Packet assembly must include pending ACK, then control frames, then at most
  one STREAM frame, matching the current `PacketAssembly` assumption.
- PTO/loss must requeue typed control refs only if still valid:
  - `MAX_DATA` only if it is the current advertised max data;
  - `MAX_STREAM_DATA` only if stream exists, read side is open, and the limit is
    still current;
  - `MAX_STREAMS` only if it is the current advertised stream limit;
  - blocked frames only if the connection/stream is still blocked at that
    limit;
  - `PATH_RESPONSE` and `HANDSHAKE_DONE` until acked or superseded by state.

### Phase 5 - Complete Send Credit Accounting

- Keep the current `FlowCredit` fields but finish accounting semantics:
  - connection send credit from peer `initialMaxData`;
  - local bidi stream credit from peer
    `initialMaxStreamDataBidiRemote`;
  - peer bidi stream credit from peer `initialMaxStreamDataBidiLocal`;
  - local uni stream credit from peer `initialMaxStreamDataUni`;
  - peer uni stream send side closed with zero send credit.
- Cap new STREAM packetization by both connection and stream credit.
- Consume connection and stream send credit only after a packet containing new
  data is successfully handed to the send path and recorded.
- Do not consume credit for retransmission.
- If send fails after stream data was dequeued, restore the stream scheduler
  state or retain the Tx range so the data is not lost.
- Track `lastDataBlocked`, per-stream `lastStreamDataBlocked`, and
  per-direction `lastStreamsBlocked` to avoid repeated BLOCKED frames for the
  same limit.
- On `MAX_DATA`/`MAX_STREAM_DATA`, monotonically extend credit and schedule a
  Tx flush of writable streams.

### Phase 6 - Validate Received MAX And BLOCKED Frames

- Implement ngtcp2-equivalent validation as common `Link` helpers.
- `MAX_DATA`:
  - ignore values that do not increase the connection send limit;
  - schedule writable stream flush on increase.
- `MAX_STREAM_DATA`:
  - reject uninitiated local streams;
  - reject local unidirectional streams and peer unidirectional cases that
    cannot send;
  - reject remote stream IDs beyond advertised stream limits;
  - create/open remote bidirectional streams only where ngtcp2 does;
  - tolerate stale frames for closed streams with a bounded diagnostic counter
    rather than unbounded work.
- `MAX_STREAMS`:
  - reject values above the QUIC max-varint/implementation stream cap;
  - monotonically extend local opening limits;
  - open queued local streams and notify the application.
- `DATA_BLOCKED`:
  - reject offsets greater than current advertised receive max data.
- `STREAM_DATA_BLOCKED`:
  - validate stream initiator, direction, stream limit, and final size;
  - reject offsets above advertised stream receive credit;
  - advance receive offset only when it increases;
  - account novel blocked offset against connection receive credit, like
    ngtcp2;
  - create peer streams only where ngtcp2 would.
- `STREAMS_BLOCKED`:
  - reject counts greater than the currently advertised peer stream limit for
    that direction.

### Phase 7 - Stream Close Lifecycle And MAX_STREAMS Return

- Add explicit stream lifecycle state:
  - read side closed,
  - write side closed,
  - reset received/sent,
  - FIN sent/acked,
  - receive data consumed or discarded,
  - stream-count credit returned.
- When a peer-initiated stream is fully closed, extend advertised peer stream
  limit once for the direction and queue `MAX_STREAMS`.
- Ensure reset/stop-sending paths return receive data credit and stream count
  in the same ordering ngtcp2 tests expect.
- This phase is the likely fix for the Caddy H3 stall around 40 completed
  requests, so include an interop checkpoint after unit tests pass.

### Phase 8 - Packet-Space And Role Legality Audit

- Add a single helper that validates frame legality by packet space and role
  before semantic processing.
- Enforce:
  - 0-RTT frame set restrictions;
  - `HANDSHAKE_DONE` server-send/client-receive only;
  - `NEW_TOKEN` client-receive only and non-empty;
  - `CRYPTO` only in Initial/Handshake/1-RTT crypto contexts where valid;
  - `STREAM` only in application data packet spaces;
  - ACK ranges must not acknowledge unsent packet numbers;
  - unknown/unsupported extension frames must fail according to negotiated
    support.
- Keep `PING` as ack-eliciting no-op.
- Keep `PATH_RESPONSE` as recognized no-op until path validation is implemented.

### Phase 9 - Client And Server Runtime Integration

- Remove direct flow/path control sends from both `CliLink` and `SrvLink`:
  - `dataBlocked_`,
  - `streamDataBlocked_`,
  - `streamsBlocked_`,
  - direct `PATH_RESPONSE`,
  - direct `HANDSHAKE_DONE` where possible.
- Replace them with base `Link` queue helpers and a single
  `flushControlAndStreams_` path.
- Keep CRTP defaults side-effect-safe in the base, consistent with
  `GUIDELINES.md`.
- Preserve the current packet-protection lock until the Tx/Rx send call graph is
  audited and all protected sends are serialized.

### Phase 10 - NgTCP2-Parity Tests

- Add connection-level tests, not only codec/data-structure tests.
- Extend `ZquicFlowTest` or add `ZquicFlowRuntimeTest` for:
  - runtime STREAM receive enforces stream max data;
  - runtime STREAM receive enforces connection max data;
  - duplicate STREAM frames do not consume receive credit twice;
  - `RESET_STREAM` final size accounts for receive credit.
- Extend `ZquicStreamTest` for:
  - `MAX_STREAM_DATA` local uninitiated stream error;
  - `MAX_STREAM_DATA` remote stream over limit error;
  - valid remote stream creation from `MAX_STREAM_DATA`;
  - local unidirectional invalid cases.
- Add blocked-frame tests modeled on `ngtcp2_conn_test.c`:
  - receive `DATA_BLOCKED` at limit succeeds;
  - receive `DATA_BLOCKED` above limit fails;
  - receive `STREAM_DATA_BLOCKED` for local open stream advances rx offset;
  - unopened local stream fails;
  - valid unopened remote stream creates stream;
  - remote stream over limit fails;
  - stream limit violation fails;
  - connection limit violation fails;
  - after `RESET_STREAM`, same offset succeeds and larger offset fails;
  - after `STOP_SENDING`, blocked offset can advance and returns credit;
  - decreasing blocked offset is ignored.
- Add send-side tests:
  - stream send is capped by stream credit;
  - stream send is capped by connection credit;
  - retransmission does not consume credit twice;
  - blocked frame duplicate suppression;
  - `MAX_DATA`/`MAX_STREAM_DATA` resume blocked stream sends.
- Add recovery tests:
  - lost `MAX_STREAM_DATA` retransmits only while still current;
  - obsolete `MAX_STREAM_DATA` is dropped;
  - lost `STREAM_DATA_BLOCKED` retransmits only while still blocked;
  - obsolete blocked frames are dropped.
- Add role/space tests:
  - server rejects `HANDSHAKE_DONE`;
  - client accepts it once in valid state;
  - `NEW_TOKEN` server receive fails;
  - `PING` is ack-eliciting no-op;
  - unsupported DATAGRAM handling is explicit.

### Phase 11 - Interop And Performance Checkpoints

- Run unit tests first:
  - `make -C zquic test`
  - `make -C zhttp test`
- Then run Caddy H3 interop:
  - `zhttp --http3 -j 10 -n 100`
  - `zhttp --http3 -j 10 -n 1000`
  - `zhttp --http3 -j 10 -n 1000`
- Compare H1/H3 timing only after H3 correctness is stable. H3 should not stall
  or complete fewer bodies than requested before performance is interpreted.

### Phase 12 - Reconcile Current Partial Patch Before Commit

- Keep or refine:
  - `FlowCredit::set`;
  - stream send-credit fields;
  - connection send-credit field;
  - packetizer capping by stream and connection credit;
  - queued H3 requests while stream credit blocks creation;
  - packet-protection serialization if protected sends can still race.
- Rework before committing:
  - direct BLOCKED frame sends;
  - direct PATH_RESPONSE sends;
  - direct HANDSHAKE_DONE sends if they bypass recovery;
  - missing receive-flow integration in runtime `Link`;
  - missing receive BLOCKED validation;
  - missing stream-close credit return;
  - generic `SentFrameRef::control()`;
  - lack of retransmittable typed control-frame queue.

## Code References to Impacted Code

- `zquic/src/ZquicTypes.hh:92` - Extend `FrameType` for application close and
  document DATAGRAM as deliberately unsupported.
- `zquic/src/ZquicFrame.hh:28` - Extend `Frame` only as needed for
  application close; DATAGRAM stays unsupported.
- `zquic/src/ZquicFrame.cc:25` - Update parser and ack-eliciting behavior for
  the complete supported frame matrix.
- `zquic/src/ZquicStream.hh:33` - Reuse `FlowCredit`; add fields only where
  stream runtime state needs them.
- `zquic/src/ZquicStream.hh:73` - Reuse `FlowUpdate` for queued MAX frames.
- `zquic/src/ZquicStream.hh:92` - Wire `ReceiveFlow` into runtime `Link`
  processing.
- `zquic/src/ZquicSched.hh:13` - Add compact control-frame queue and packet
  assembly helpers using Z containers.
- `zquic/src/ZquicRecovery.hh:314` - Replace generic control refs with typed
  retransmittable refs.
- `zquic/src/Zquic.hh:1416` - Change stream/reset receive path to enforce
  runtime receive flow.
- `zquic/src/Zquic.hh:1668` - Strengthen `MAX_DATA`, `MAX_STREAM_DATA`, and
  `MAX_STREAMS` handling.
- `zquic/src/Zquic.hh:2114` - Finish send-credit accounting and blocked-frame
  queuing.
- `zquic/src/Zquic.hh:2587` - Apply packet-space/role legality checks and
  dispatch all control frames to common handlers.
- `zquic/src/Zquic.hh:3290` - Remove client direct control sends in favor of
  queued base helpers.
- `zquic/src/Zquic.hh:3779` - Remove server direct control sends in favor of
  queued base helpers.
- `zquic/test/ZquicFlowTest.cc:92` - Keep direct `ReceiveFlow` tests and add
  runtime receive-flow coverage.
- `zquic/test/ZquicStreamTest.cc:518` - Extend stream limit and blocked-frame
  tests.
- `zquic/test/ZquicRecoveryTest.cc` - Add typed control retransmission tests.
- `zquic/test/ZquicRuntimeTest.cc` - Add end-to-end protected packet flow tests
  for credit exhaustion/resumption.
- `zhttp/example/zhttp.cc` - No primary QUIC transport fix belongs here, but
  keep H3 request queuing aligned with `MAX_STREAMS` behavior.

## Detailed Test Plan

- Build/codec tests:
  - frame matrix test covers all base QUIC v1 frame types;
  - application close parse/write tests;
  - unsupported DATAGRAM behavior test.
- Flow-control unit tests:
  - direct `ReceiveFlow` cases remain;
  - runtime `Link` receive path rejects stream and connection overrun;
  - receive credit update thresholds enqueue correct `FlowUpdate`.
- MAX/BLOCKED tests:
  - port ngtcp2 receive cases listed in Phase 10;
  - add stale/duplicate frame handling cases.
- Send-side tests:
  - credit caps packet payload;
  - blocked frames are queued once per limit;
  - MAX frames unblock and flush pending streams;
  - retransmits do not consume credit again.
- Stream lifecycle tests:
  - peer stream FIN returns `MAX_STREAMS` once;
  - `RESET_STREAM` returns stream count and receive credit;
  - duplicate close events do not double-extend limits.
- Recovery tests:
  - typed control refs are recorded, lost, requeued, rebuilt, and suppressed
    when obsolete.
- Role/packet-space tests:
  - illegal frame/role combinations close or reject as required.
- Interop tests:
  - Caddy H3 runs at the agreed workloads without stalls, partial body counts,
    or packet-protection assertions.

## Acceptance Criteria

- Every base QUIC v1 frame implemented by ngtcp2 is represented in the Zquic
  frame matrix as implemented, intentionally no-op, or unsupported with correct
  protocol behavior.
- Runtime `Link` enforces receive flow control for protected `STREAM` and
  `RESET_STREAM`, not only direct unit-test calls.
- Runtime `Link` enforces send flow control and does not consume send credit on
  retransmission.
- `MAX_DATA`, `MAX_STREAM_DATA`, `MAX_STREAMS`, `DATA_BLOCKED`,
  `STREAM_DATA_BLOCKED`, and `STREAMS_BLOCKED` are processed with ngtcp2-level
  validation.
- Control frames that affect correctness are queued, recorded, and
  retransmitted through one protected packet path.
- Peer stream close returns stream-count credit and emits retransmittable
  `MAX_STREAMS` once.
- Client `zhttp` and server `zhttpd` use cases both work because the common
  `CliLink`/`SrvLink` base behavior is fixed.
- `make -C zquic test` and `make -C zhttp test` pass.
- Caddy H3 interop completes `zhttp --http3 -j 10 -n 100` and then the agreed
  scaled workloads without stalls.

## Non-goals

- Do not clone ngtcp2 internals or public APIs.
- Do not add STL containers where existing `Zu`, `Zm`, `Zt`, or `Zi` structures
  fit.
- Do not add QUIC DATAGRAM application support. A peer can request DATAGRAM,
  but Zquic should decline by not negotiating it and should reject actual
  DATAGRAM frames if the peer sends them anyway.
- Do not add adaptive BDP flow-control growth in the first correctness pass.
- Do not replace the scheduler/thread model unless a specific race remains
  after protected sends are serialized.
- Do not treat H1/H3 performance comparison as meaningful until correctness and
  interop are stable.

## Options and Open Questions

- Closed-stream stale frame handling:
  - Recommended: bounded diagnostic counter similar in spirit to ngtcp2's
    glitch rate limit, without adding a full rate limiter unless existing Zm
    utilities make that cheap.
  - Alternative: strict failure on every stale frame. This is simpler but can
    be less tolerant of reordered packets than ngtcp2.
- Control-frame storage:
  - Recommended: a compact tagged struct with explicit payload fields and
    `write()` method.
  - Alternative: retain encoded bytes. This is easier initially but weakens
    obsolete-frame suppression and risks heap/copy churn.
- Tx serialization:
  - Recommended: keep `m_txLock` during this work, then remove only after a
    call-graph audit proves all protected sends run on one Tx context.
  - Alternative: force all receive-triggered control sends onto Tx scheduler
    immediately and drop the lock later. This is cleaner long-term but should
    follow, not precede, the control queue.

No blocking open questions remain for the first implementation pass.
