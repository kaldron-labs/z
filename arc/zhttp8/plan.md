# Zquic gap-closure plan vs ../zngtcp2

This plan closes the gaps found while comparing the current `zquic` work with
`../zngtcp2`'s QUIC connection, frame, flow-control, and stream-limit handling.
The goal is not to clone ngtcp2 internals, but to bring `zquic` behavior to the
same protocol coverage and edge-case standard using existing Zquic/Z framework
patterns.

## 1. Establish a frame-handling coverage matrix

- Build a source-level matrix from `../zngtcp2/lib/ngtcp2_pkt.h`,
  `../zngtcp2/lib/ngtcp2_pkt.c`, and `../zngtcp2/lib/ngtcp2_conn.c`.
- For every frame code, mark Zquic status as:
  - parsed/written,
  - semantically processed,
  - validated for illegal packet spaces and endpoint roles,
  - ack-eliciting classified,
  - retransmittable when sent,
  - covered by connection-level tests.
- Put the matrix in a `zquic/test` fixture or test comment so future frame
  additions have an obvious parity checklist.

Known current gaps:
- `DATAGRAM` and `DATAGRAM_LEN` are implemented by ngtcp2 but not represented in
  `Zquic::FrameType`.
- Application `CONNECTION_CLOSE` is parsed as generic `ConnectionClose`; decide
  whether Zquic needs a distinct enum value or a flag/error namespace field.
- `DATA_BLOCKED`, `STREAM_DATA_BLOCKED`, and `STREAMS_BLOCKED` are parsed but
  not semantically validated.
- `NEW_TOKEN`, `PING`, and `PATH_RESPONSE` have limited or no meaningful runtime
  processing.

## 2. Add missing protocol vocabulary deliberately

- Extend `FrameType` only where Zquic actually intends to support runtime
  behavior:
  - add `Datagram` if QUIC datagrams are in scope,
  - add `ApplicationClose` or an equivalent discriminator for frame `0x1d`,
  - preserve compact enum style and existing `Frame` storage.
- Update `FrameCodec::parse`, writers, `Diag::frameTypeName`, and codec tests.
- If DATAGRAM is explicitly out of scope, make unsupported DATAGRAM frames fail
  with protocol handling consistent with QUIC transport rules rather than being
  silently ignored as `Unknown`.

## 3. Introduce runtime receive-flow state in `Link`

- Reuse `ReceiveFlow` and `FlowUpdate`; do not create an unrelated flow-control
  abstraction.
- Add connection-level receive credit to `Link`, initialized from local transport
  parameters:
  - initial max data,
  - per-stream bidi local/remote and uni receive limits.
- Add per-stream receive-flow state or embed enough state in `Stream` to track:
  - receive max offset,
  - unsent max offset,
  - receive window,
  - consumed/delivered offset,
  - final size and reset interaction.
- Route protected `STREAM` and `RESET_STREAM` processing through runtime flow
  accounting. The current direct `receiveFrame(frame, packet)` path must not
  bypass `ReceiveFlow`.

## 4. Implement receive-side credit extension

Mirror ngtcp2's behavior at a Zquic level:

- Extend connection `MAX_DATA` as application data is consumed or unread data is
  discarded during reset/stop/close.
- Extend stream `MAX_STREAM_DATA` as application data is consumed.
- Queue `MAX_DATA` and `MAX_STREAM_DATA` as retransmittable control frames when
  update thresholds are crossed.
- Keep the existing simple threshold initially, but structure it so adaptive
  window growth can be added later.
- Do not send MAX updates as fire-and-forget one-shot packets; put them through
  the same packet assembly/recovery path as other retransmittable control
  frames.

## 5. Implement send-side credit accounting completely

- Keep `FlowCredit` for connection and stream send credit.
- Initialize send credit from peer transport parameters exactly as ngtcp2 does:
  - local bidi stream: peer `initial_max_stream_data_bidi_remote`,
  - peer bidi stream: peer `initial_max_stream_data_bidi_local`,
  - local uni stream: peer `initial_max_stream_data_uni`,
  - peer uni stream: send side closed, zero credit.
- Enforce both connection and stream send credit before STREAM packetization.
- Consume send credit only for newly sent stream bytes, not retransmissions.
- Restore or retain scheduler state when a stream is blocked.
- Track last blocked offset for connection and stream so `DATA_BLOCKED` and
  `STREAM_DATA_BLOCKED` are not spammed for the same limit.
- Queue blocked frames through retransmittable control-frame machinery.

## 6. Validate received MAX and BLOCKED frames

Add runtime handlers equivalent to ngtcp2's validation:

- `MAX_DATA`
  - monotonically extend connection send credit.
  - flush pending writable streams when credit increases.
- `MAX_STREAM_DATA`
  - reject impossible local-stream state.
  - reject illegal uni-directional cases.
  - reject remote stream IDs beyond allowed stream limits.
  - tolerate closed local streams with bounded/glitch-style behavior or a
    simpler Zquic diagnostic counter.
  - extend stream send credit and reschedule if pending.
- `MAX_STREAMS`
  - reject values above QUIC max-varint/implementation cap.
  - monotonically extend local stream opening limit.
  - open queued local streams and notify the application.
- `DATA_BLOCKED`
  - reject values greater than current advertised receive max data.
- `STREAM_DATA_BLOCKED`
  - validate stream existence/state/direction/limit.
  - reject values greater than advertised stream receive credit.
  - create peer streams only where ngtcp2 would and only within limits.
- `STREAMS_BLOCKED`
  - reject values greater than current advertised stream count limits.

## 7. Fix stream-count lifecycle and MAX_STREAMS emission

The Caddy stall around 40 completed H3 requests strongly suggests peer stream
count credit is not returned.

- Add explicit stream close lifecycle:
  - read side closed,
  - write side closed,
  - reset received/sent,
  - fin acked or all tx data complete,
  - application has consumed/discarded receive data.
- When a peer-initiated stream is fully closed, extend the advertised peer
  stream limit for the relevant direction.
- Queue `MAX_STREAMS` as a retransmittable control frame after stream close,
  matching ngtcp2's "write MAX_STREAMS after RESET_STREAM" ordering where
  applicable.
- Add protection against double-extending stream credit for the same closed
  stream.

## 8. Make control-frame sending go through one scheduler path

- Add a control-frame queue owned by `Link`, using existing Z containers.
- Represent queued control frames with existing `FlowUpdate` where possible and
  a small local union/struct for non-flow control frames.
- Packet assembly should pull, encode, record, and retransmit control frames in
  the same path used for STREAM/CRYPTO retransmission.
- Avoid direct `sendShortPacket_` calls from arbitrary receive callbacks except
  for cases that are intentionally immediate and safe.
- Keep the packet-protection lock or replace it with stricter tx-thread
  serialization after all call paths are audited.

## 9. Audit packet-space and role legality

Use ngtcp2's packet-space checks as the reference.

- 0-RTT must only accept the frame types permitted by QUIC.
- Server must reject `HANDSHAKE_DONE`.
- Client must process `HANDSHAKE_DONE` only once and only in valid state.
- ACK validation should reject impossible ACK ranges and ACKs for unsent packet
  numbers.
- CRYPTO and STREAM frame handling must reject invalid packet spaces.
- PATH_CHALLENGE should enqueue PATH_RESPONSE safely and PATH_RESPONSE should
  participate in path validation state rather than being ignored.

## 10. Add ngtcp2-parity connection tests

Add focused Zquic tests modeled on the ngtcp2 connection tests, not only codec
tests.

Required tests:
- stream rx flow control,
- stream rx flow-control error,
- stream tx flow control,
- connection rx flow control,
- connection rx flow-control error,
- connection tx flow control,
- receive `MAX_STREAM_DATA`,
- send `MAX_STREAM_DATA`,
- receive `DATA_BLOCKED`,
- receive `STREAM_DATA_BLOCKED`,
- receive `STREAMS_BLOCKED`,
- receive `MAX_STREAMS`,
- peer stream close emits `MAX_STREAMS`,
- reset/stop-sending returns receive credit correctly,
- blocked STREAM retransmission does not consume credit twice,
- blocked frame duplicate suppression,
- concurrent rx ACK/control send vs tx stream send does not race packet
  protection or packet numbers.

Interop tests:
- `zhttp --http3 -n 100 -j 10` against Caddy must complete.
- Scale after that:
  - `-n 1000 -j 10`,
  - `-n 10 -j 1000`,
  - `-n 1000 -j 10`.
- Compare H1/H3 timing only after correctness is stable.

## 11. Rework current partial patch before committing

Do not commit the current partial flow-control work as complete.

Keep or refine:
- `FlowCredit::set`,
- stream send-credit fields,
- connection send-credit field,
- packetizer capping by stream and connection credit,
- queued H3 requests while `MAX_STREAMS` blocks stream creation,
- packet-protection serialization if the tx/rx call graph still permits
  concurrent protected sends.

Rework:
- direct BLOCKED frame sends,
- direct PATH_RESPONSE sends if they bypass recovery/serialization,
- lack of received BLOCKED validation,
- missing receive-flow integration,
- missing stream-close credit return,
- lack of retransmittable MAX/BLOCKED control-frame queue.

## 12. Completion criteria

The work is complete when:

- The frame matrix shows every ngtcp2-supported QUIC v1 frame is either
  implemented or explicitly documented as out of scope with correct protocol
  rejection behavior.
- Runtime `Zquic` handles `MAX_DATA`, `MAX_STREAM_DATA`, `MAX_STREAMS`,
  `DATA_BLOCKED`, `STREAM_DATA_BLOCKED`, and `STREAMS_BLOCKED` with ngtcp2-level
  validation.
- Receive and send flow control are enforced in protected packet processing.
- Stream close returns stream-count credit and emits retransmittable
  `MAX_STREAMS`.
- The new Zquic connection tests cover the ngtcp2 test categories listed above.
- `make -C zquic test` and `make -C zhttp test` pass.
- Caddy H3 interop passes at the agreed workloads without stalls or AEAD
  assertions.
