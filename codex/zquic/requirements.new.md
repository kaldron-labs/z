## Summary

The goal is to add a new `Zquic` library under `zquic/` that provides
QUIC v1, HTTP/3, and QPACK in the repository's existing style. The transport
API should feel familiar to `Ztls` users where that style still applies:
applications receive stream bytes through `ZiRxStream` and transmit stream
bytes through `ZiTxStream`. Unlike `Ztls`, QUIC has many concurrent streams, so
application byte callbacks and transmit streams live on a new `Zquic::Stream`
CRTP class rather than directly on `Zquic::Link`.

`Zquic` independently owns QUIC transport state: packet number spaces, packet
protection, stream state, flow control, loss recovery, congestion control,
path validation, PMTUD/DPLPMTUD, timers, packetization, and UDP endpoint
routing. It may use local `Z` components and zpicotls directly, and it may use
`../zngtcp2` only as a local reference for implementation logic and tests.
It must not depend on `../zngtcp2`, canonical online `ngtcp2`, quiche, MsQuic,
or any other QUIC implementation.

The primary first-release use case is HTTP/3 for REST-style clients and
servers. The required first-release protocol scope is:
- QUIC v1 transport sufficient for interoperable HTTP/3.
- HTTP/3 with ALPN `h3`.
- QPACK sufficient for interoperable request and response header blocks.
- PMTUD/DPLPMTUD per path.
- Basic RFC 9000 version negotiation behavior for v1-only endpoints.

The first release must explicitly exclude:
- QUIC v2.
- RFC 9368 compatible version negotiation.
- Multipath QUIC.
- WebTransport.
- QUIC DATAGRAM and HTTP/3 DATAGRAM.
- 0-RTT data send or accept.
- Active ECN marking and ECN validation.
- Connection migration beyond the path validation and CID machinery needed for
  the initial path and future extension.

The resolved buffer contract is:
- Tx packet encryption is not in-place. Plaintext stream buffers are retained
  for ACK/loss/retry handling, and packet encryption writes from retained
  stream/control plaintext ranges into the final UDP packet buffer.
- Rx packet decryption is in-place within the received UDP packet buffer.
  After frame parsing, STREAM payload slices are copied into stream-role
  buffers before delivery through each stream's `ZiRxStream`.
- The Rx packet-to-stream copy is a required ownership-transfer copy, not a
  fallback path. Other staging or fallback copies below the API boundary are
  forbidden unless this document later names them explicitly.
- On Tx, each UDP packet may contain at most one application STREAM frame, but
  QUIC control frames may piggyback with it when congestion, anti-amplification,
  and PMTU limits permit.

Key findings from the codebase and reference research:
- `Ztls` is a useful style, build, configuration, callback, and buffer
  ownership model, but its TLS record layer does not map to QUIC. `Zquic`
  should reuse `Ztls` backend patterns and zpicotls configuration where useful,
  but must use QUIC/TLS handshake messages, transport parameters, traffic
  secrets, AEAD, and header protection directly instead of `ptls_send()` /
  `ptls_receive()` record handling.
- `ZiMultiplex` already supports UDP sockets and per-send/receive peer
  addresses through `ZiIOContext::addr`, but QUIC needs its own datagram
  dispatch keyed by Destination Connection ID and path.
- `ZiRx` is TCP-message oriented and can copy trailing bytes when splitting
  framed input. UDP receive for QUIC must avoid that path and land directly in
  `Zquic` packet buffers.
- `ztls/buffers.md` sets the right standard: buffer ownership and copy rules
  must be explicit invariants, verified through assertions, counters, and tests.
- Local `../zngtcp2` should be used for reference behavior around packet
  writing, PMTUD, loss recovery, ACK/ECN structures, version negotiation, and
  HTTP/3 examples. Do not use online canonical `ngtcp2` code or docs, because
  the local fork is a divergent breaking-API fork.

Research basis:
- RFC 9000, QUIC Transport: https://www.rfc-editor.org/rfc/rfc9000.html
- RFC 9001, Using TLS to Secure QUIC:
  https://www.rfc-editor.org/rfc/rfc9001.html
- RFC 9002, QUIC Loss Detection and Congestion Control:
  https://www.rfc-editor.org/rfc/rfc9002.html
- RFC 9114, HTTP/3: https://www.rfc-editor.org/rfc/rfc9114.html
- RFC 9204, QPACK: Field Compression for HTTP/3:
  https://www.rfc-editor.org/rfc/rfc9204.html
- RFC 9368, Compatible Version Negotiation for QUIC:
  https://www.rfc-editor.org/rfc/rfc9368.html
- RFC 9369, QUIC Version 2: https://www.rfc-editor.org/rfc/rfc9369.html
- Local reference only: `../zngtcp2`, especially `lib/ngtcp2_conn.c`,
  `lib/ngtcp2_pmtud.c`, `lib/ngtcp2_rtb.c`,
  `doc/source/programmers-guide.rst`, and HTTP/3 examples.

## Product Requirements

### First-Release Scope

- Implement QUIC v1 as the only supported QUIC version.
- Implement HTTP/3 and QPACK as first-release product features, not future
  examples layered outside the requirements.
- Support both client and server roles at the transport layer.
- Provide an HTTP/3 client path optimized for REST APIs: connection reuse,
  concurrent request streams, request cancellation, response header/body
  delivery, and clear error reporting.
- Provide enough HTTP/3 server support to exercise the same protocol machinery
  in deterministic local tests: request stream accept, response headers,
  response body, trailers where implemented, graceful close, and error paths.
- Do not implement QUIC v2 in the first release. Keep version constants and
  packet parsing structured so v2 can be added later without changing the
  public application API.
- Do not implement RFC 9368 compatible version negotiation in the first
  release. Implement ordinary RFC 9000 version negotiation behavior needed for
  v1-only clients and servers.
- Do not implement multipath, WebTransport, QUIC DATAGRAM, HTTP/3 DATAGRAM, or
  full connection migration in the first release. Avoid public API shapes that
  would make those features hard to add later.
- Do not implement 0-RTT send or accept in the first release. The transport
  must recognize 0-RTT packet types enough to drop or reject them safely.
- Do not implement active ECN marking or ECN validation in the first release.
  ECN-related state may exist only as disabled future-extension state.
- Implement PMTUD/DPLPMTUD in the first release. PMTU is per path, currently
  one active path per `Link`.

### Zquic Module and Build Integration

- Add a new `zquic` module with `zquic/Makefile.am`, `zquic/src/Makefile.am`,
  and `zquic/test/Makefile.am`, following the same autoconf/automake layout
  as `ztls`.
- Add `zquic` to the top-level `Makefile.am` `SUBDIRS`, to the symlink loop in
  `configure.ac`, and to `AC_CONFIG_FILES`.
- Build `libZquic.la` from `zquic/src`.
- Install public headers with the `Zquic` prefix, including at least
  `ZquicLib.hh` and `Zquic.hh`.
- Keep HTTP/3 and QPACK public types under the `Zquic` module. A nested
  namespace such as `Zquic::H3` is acceptable if it follows existing local
  style and keeps transport-only APIs clear.
- Link against the existing stack in repo order: `Zi`, `Ze`, `Zt`, `Zm`, `Zu`,
  plus `Ztls` and zpicotls.
- Do not link against, include headers from, copy source from, generate code
  from, or otherwise depend on `../zngtcp2`, canonical `ngtcp2`, quiche,
  MsQuic, nghttp3, or other external QUIC/HTTP3/QPACK implementations.
- Keep the default C++ style and naming: `.hh` headers, `.cc` sources,
  `Zquic` namespace, `ZquicAPI` / `ZquicExtern` export macros, short type
  names, `ZuDerive`, `ZmRef`, `ZtArray`, `ZtString`, `ZmHash`, `ZmList`, and
  related local primitives.
- STL use is a non-default exception. Any mandatory STL use must be isolated
  and documented in the requirement or implementation note that introduces it.
- This requirement defines the module boundary and ensures `Zquic` is a
  first-class peer of `Ztls`, not an external adapter.

### Public Transport API

- Provide `Zquic::Client`, `Zquic::Server`, `Zquic::Engine`,
  `Zquic::ClientParams`, `Zquic::ServerParams`, and `Zquic::EngineParams`.
- Use `Ztls` only as a style and structure guide. API compatibility with
  `Ztls` is not a requirement and must not drive awkward compatibility shims.
- Provide a `Zquic::Link` CRTP class for connection-level state and a
  `Zquic::Stream` CRTP class for stream-level application behavior.
- Move stream byte APIs from the connection surface to the stream surface:
  `Stream::txStream()` returns a `ZiTxStream` facade, and `Stream::Impl`
  receives bytes through `process(Zquic::RxStream &)`.
- `Stream`'s implementation type must be a template parameter to `Link`, so
  applications can bind connection and stream CRTP implementations together.
- `Stream::id()` must return the QUIC stream ID. Stream IDs must preserve QUIC
  semantics: initiator bit, direction bit, and monotonic issuance per stream
  type.
- `Link::stream(...)` must create a locally initiated stream, allocate a QUIC
  stream ID of the requested type, return `ZmRef<Stream>`, and schedule the
  stream for Rx-thread lookup.
- `Link::Impl` must expose `streamed(ZmRef<Stream>)` so the application can
  accept or reject peer-initiated streams.
- `Link` remains responsible for connection events such as `connected`,
  `disconnected`, `connectFailed`, listener callbacks, connection close, and
  transport-level errors.
- Stream close APIs must distinguish graceful FIN, reset send, stop receiving,
  peer reset, peer stop-sending, and complete teardown.
- Stream cancellation must release retained Tx buffers, queued Rx buffers, and
  stream table entries according to QUIC final-size rules.
- Preserve the intended CRTP and stream-table shape from `goal.md`:

```cpp
template <
  typename Impl, typename RxBufAlloc_, typename TxBufAlloc_>
class Stream ... {
  ...
  Stream(...) ... m_id(id) ... { ... }
  ...
  int64_t id() const { return m_id; }
  ...
  auto txStream();
  ...
private:
  int64_t   m_id;
};

template <typename Stream_>
inline int64_t Stream_IDAxor(const Stream_ &s) { return s.id(); }
template <typename Stream_>
ZuDerive(Streams_,
  (ZmHash<Stream_,
    ZmHashNode<Stream_,
      ZmHashKey<Stream_IDAxor<Stream_>,
        ZmHashHeapID<"Zquic.Stream">>>>));

template <
  typename App, typename Impl, typename RxBufAlloc_, typename TxBufAlloc_,
  typename Cxn_, typename CxnRef_, typename Stream_>
class Link ... {
  ...
  using Stream = Stream_;
  using Streams = Streams_<Stream>;
  ...
  StreamRef stream(...);
  ...
private:
  ...
  Streams   m_streams; // rx thread dedicated, used to dispatch received data
};
```

- This requirement connects to stream state, flow control, scheduler, and
  buffer-retention requirements because it defines which object owns each
  application-visible operation.

### HTTP/3 API

- Provide an HTTP/3 API over `Zquic::Link` and `Zquic::Stream`, with ALPN
  `h3` configured through the TLS/QUIC handshake.
- Provide an HTTP/3 client request API suitable for REST calls:
  method, scheme, authority, path, regular headers, optional request body,
  request FIN, cancellation, response headers, response body, trailers where
  supported, and completion/error callbacks.
- Provide an HTTP/3 server API sufficient for deterministic loopback and
  integration tests: peer request acceptance, header validation, body receive,
  response header/body send, response FIN, request cancellation, and connection
  shutdown.
- Map each HTTP request/response exchange to a client-initiated bidirectional
  QUIC stream.
- Support concurrent request streams over one QUIC connection.
- Surface HTTP/3 errors distinctly from QUIC transport errors and TLS errors.
- Enforce HTTP/3 pseudo-header rules, request/response header ordering rules,
  method/scheme/authority/path presence where required, and malformed-message
  error handling.
- Support request and response bodies as stream data without requiring the app
  to materialize the whole body in memory.
- Support graceful cancellation by mapping application cancellation to the
  correct combination of stream reset and stop-sending behavior.
- Keep connection reuse and preconnect possible without 0-RTT. 0-RTT is not a
  first-release latency feature.
- Alt-Svc discovery, HTTPS DNS record discovery, proxying, cache semantics,
  cookies, authentication policy, redirect policy, and general HTTP client
  features are outside `Zquic` first-release scope.
- This requirement connects the transport API to the primary product use case:
  HTTP/3 REST clients and servers.

### HTTP/3 Frames and State

- Implement HTTP/3 unidirectional stream types needed by RFC 9114 and
  RFC 9204: control streams, QPACK encoder streams, and QPACK decoder streams.
- Implement HTTP/3 request streams as client-initiated bidirectional QUIC
  streams.
- Each endpoint must open exactly one local HTTP/3 control stream and must
  reject invalid duplicate control streams according to HTTP/3 rules.
- The first frame on each control stream must be `SETTINGS`.
- Implement HTTP/3 frame parsing and serialization for `DATA`, `HEADERS`,
  `CANCEL_PUSH`, `SETTINGS`, `PUSH_PROMISE`, `GOAWAY`, and `MAX_PUSH_ID`.
- Implement frame type and length varint parsing independently from QUIC frame
  varint handling, while reusing shared safe varint primitives where practical.
- Implement unknown HTTP/3 frame skipping where the RFC requires it, with
  bounds checking and stream-type validation.
- Implement HTTP/3 connection error codes and stream error codes needed for
  malformed frames, closed critical streams, invalid settings, missing control
  stream setup, and cancelled requests.
- Server push is not required as a first-release feature. The client must not
  advertise push support unless implemented. If a peer sends forbidden push
  frames, close the connection or stream with the correct HTTP/3 error.
- The HTTP/3 layer must integrate with transport flow control without hidden
  body buffering. Flow-control backpressure should propagate to request and
  response body writers.
- This requirement connects to QUIC stream scheduling, QPACK, and tests.

### QPACK

- Implement QPACK for HTTP/3 header blocks.
- The first release may use a conservative QPACK profile that advertises
  `SETTINGS_QPACK_MAX_TABLE_CAPACITY = 0` and
  `SETTINGS_QPACK_BLOCKED_STREAMS = 0` by default. In that profile, the
  encoder emits static-table and literal representations only, and peers are
  not allowed to reference a dynamic table.
- Implement QPACK static table lookups, literal field-line encoding, field
  section prefix handling, required insert count handling for the zero-capacity
  profile, and base calculation validation.
- Implement enough decoder logic to reject dynamic table references when local
  settings advertise zero capacity.
- Support QPACK encoder and decoder stream types and enforce the one-stream
  limit for each type. Under the zero-capacity profile, `Zquic` may avoid
  creating its own encoder or decoder stream when it will not be used, but it
  must still allow the peer to create either stream and apply QPACK closure and
  duplicate-stream error rules.
- If nonzero dynamic table capacity or blocked streams are later enabled, the
  requirements expand to full dynamic table insertions, acknowledgements,
  stream cancellation instructions, duplicate handling, evictions, blocked
  stream accounting, and memory limits. That is a staged extension, not a
  first-release requirement.
- Header list size limits must be configurable and enforced before exposing a
  decoded header set to the application.
- QPACK parse and encode failures must surface as HTTP/3 errors with the
  correct error code class.
- This requirement keeps HTTP/3 interoperable while avoiding dynamic QPACK
  complexity that does not materially improve low-latency REST calls in the
  first release.

### Threading and Ownership Model

- `Zquic` must decouple protocol Rx and Tx as far as practical. Each side
  should run on a dedicated `ZmScheduler` thread or scheduler lane selected
  through `Zquic::EngineParams`.
- Rx-exclusive state includes UDP packet receive parsing, packet decryption,
  received packet-number tracking, ACK generation state, receive-side stream
  state, stream ID lookup, and `Link::m_streams`.
- Tx-exclusive state includes stream send queues, packetization, sent-packet
  metadata, loss recovery, congestion control, pacing, PTO handling, PMTUD
  probing, and UDP packet handoff.
- HTTP/3/QPACK state that follows received frames is Rx-owned until it hands
  application-visible events or bytes to the serialized callback path.
- HTTP/3/QPACK output state is Tx-owned where it produces HEADERS, DATA,
  QPACK, GOAWAY, SETTINGS, or cancellation frames.
- Shared state must be minimized. Any state shared between Rx, Tx, HTTP/3, and
  application threads must be explicitly listed in the implementation design
  and guarded with `ZmPLock` or `ZmAtomic`.
- `Link::m_streams` is Rx-thread dedicated. Adding or removing a stream must
  enqueue a job to the Rx thread; the application retains `ZmRef<Stream>` for
  Tx operations and must not need to look up streams by ID for sending.
- Application callbacks for a single connection and its streams must not run
  concurrently unless the API explicitly documents that behavior. The default
  must be serialized connection-local upcalls.
- No buffer may cross threads without an explicit owner transfer.
- This requirement connects to API requirements and to buffer ownership because
  thread ownership determines who may retain, release, parse, encrypt, or
  deliver each buffer.

### UDP Endpoint and Connection ID Routing

- Implement QUIC over `ZiMultiplex::udp()` using `ZiIOContext::addr` for
  per-datagram source and destination path data.
- Add a listener/endpoint layer that can own one UDP socket and dispatch
  incoming datagrams to `Link` objects by Destination Connection ID.
- Maintain connection ID tables for active Source Connection IDs issued by the
  local endpoint and for the initial client Destination Connection ID where
  required.
- Support server acceptance of new Initial packets, creation of a new `Link`,
  and association of the resulting connection IDs without relying on connected
  UDP sockets.
- Support client-originated connections with random initial Destination and
  Source Connection IDs, server address selection, and path storage.
- Implement ordinary QUIC v1 Version Negotiation handling:
  v1 servers send a Version Negotiation packet for unsupported versions on
  valid Initial packets, and v1 clients validate and abort safely if no offered
  version is supported.
- Implement Retry handling, Stateless Reset handling, and packet drop behavior
  for unknown or invalid connection IDs.
- Enforce server anti-amplification limits before address validation.
- Keep path state per connection: local address, remote address, validation
  state, PMTU/DPLPMTUD state, and disabled ECN state reserved for future use.
- Full connection migration is not required, but the path structure must not
  hard-code assumptions that prevent future migration support.
- This requirement connects to packet parsing, crypto, recovery, and timers
  because routing determines which keys and packet-number spaces can process a
  datagram.

### Packet and Frame Codec

- Implement QUIC v1 packet parsing and serialization for long headers, short
  headers, packet number encoding/decoding, coalesced UDP datagrams, Retry
  packets, Version Negotiation packets, and Stateless Reset detection.
- Implement packet type recognition for Initial, 0-RTT, Handshake, Retry, and
  1-RTT packets. Because 0-RTT is not supported in the first release, 0-RTT
  packets must be dropped or rejected safely without attempting early-data
  delivery.
- Implement QUIC varint encode/decode with bounds checking and tests.
- Implement frame parsing and serialization for the core QUIC transport frames:
  `PADDING`, `PING`, `ACK`, `RESET_STREAM`, `STOP_SENDING`, `CRYPTO`,
  `NEW_TOKEN`, `STREAM`, `MAX_DATA`, `MAX_STREAM_DATA`, `MAX_STREAMS`,
  `DATA_BLOCKED`, `STREAM_DATA_BLOCKED`, `STREAMS_BLOCKED`,
  `NEW_CONNECTION_ID`, `RETIRE_CONNECTION_ID`, `PATH_CHALLENGE`,
  `PATH_RESPONSE`, `CONNECTION_CLOSE`, and `HANDSHAKE_DONE`.
- ACK parsing must handle the ACK frame form with ECN counts, but first-release
  senders must not actively mark packets with ECN and must not rely on ECN for
  congestion behavior.
- Support ACK range encoding, ACK delay, ACK-eliciting frame classification,
  and frame validation by packet number space.
- Packetization must allow QUIC control frames to piggyback with stream data
  when the selected PMTU permits, but must not exceed the validated path MTU,
  peer `max_udp_payload_size`, congestion window, or anti-amplification limit.
- First-release packetization may include at most one application STREAM frame
  per UDP packet. Control frames, ACKs where valid, and CRYPTO frames may share
  the packet when permitted by packet number space and size constraints.
- Packet payloads must contain complete frames. Partial frame staging is an
  internal Tx concern and must not leak into packet buffers.
- This requirement connects to recovery, stream state, flow control, crypto,
  HTTP/3, and buffer requirements because encoded frames are what must be
  retained, acknowledged, retransmitted, or retired.

### TLS and zpicotls Integration

- Use zpicotls as the TLS 1.3 handshake and key-schedule provider for QUIC.
  `Zquic` must not use the TLS record-layer path from `Ztls::Link`.
- Reuse `Ztls` backend initialization, certificate, key, CA, ALPN, random,
  session ticket, asynchronous private-key, and error-formatting patterns where
  possible.
- Configure zpicotls for QUIC handshake message handling, traffic secret
  export/update callbacks, ALPN negotiation, certificate verification, and
  transport parameter extension handling.
- Implement QUIC transport parameter encode/decode for local and remote limits,
  idle timeout, max UDP payload size, active connection ID limit, stateless
  reset token, original Destination Connection ID, initial Source Connection
  ID, retry Source Connection ID, max data, per-stream data, max streams,
  ack delay exponent, max ack delay, disable active migration, and preferred
  address only when future migration support requires it.
- Do not advertise HTTP/3 ALPN unless the HTTP/3 layer is enabled and able to
  open/send/receive control streams and QPACK streams.
- Derive and install Initial, Handshake, and 1-RTT packet protection keys as
  required by QUIC v1.
- Do not derive or install 0-RTT keys in the first release unless a later
  explicit requirement enables 0-RTT. Peers that send 0-RTT data receive safe
  drop/reject behavior.
- Support key discard rules for Initial and Handshake spaces and packet key
  update rules for 1-RTT.
- Duplicate `Ztls` asynchronous private-key support for QUIC handshakes. Async
  signing must integrate with scheduler ownership and must not block Rx or Tx
  protocol threads.
- If a configured zpicotls async path cannot be represented safely in `Zquic`,
  configuration must fail with a clear diagnostic.
- This requirement connects to packet protection, recovery, connection close,
  HTTP/3 ALPN, and API events because handshake progress gates stream creation
  and data transmission.

### Packet Protection and Header Protection

- Implement QUIC packet protection directly with zpicotls AEAD and cipher
  primitives, using the TLS-selected cipher suite and QUIC labels.
- Tx packet encryption is source-to-destination, not in-place:
  retained plaintext stream/control ranges are encrypted directly into the
  final `Zquic` UDP packet buffer.
- No intermediate staging ciphertext buffer is allowed on Tx.
- Rx packet decryption is in-place within the received `PacketBufAlloc`
  buffer. Header protection is removed first, then AEAD protection decrypts
  the QUIC packet payload in that same packet buffer.
- After successful in-place Rx packet decryption and frame parsing, STREAM
  payload slices are copied into `StreamBufAlloc` buffers before delivery to
  the stream receive queue. This specific copy is required by the buffer
  lifetime contract.
- Header protection must mutate protected packet header bytes in the packet
  buffer after AEAD encryption on Tx and before AEAD decryption on Rx.
- Packet protection must enforce nonce construction from packet number,
  associated data over the unprotected header, AEAD tag sizing, packet number
  reconstruction, duplicate packet number handling after unprotection, and
  timing-side-channel-safe error behavior where required.
- Support zpicotls fusion and non-temporal AEAD variants only where their
  source/destination aliasing rules match the `Zquic` buffer contract.
- This requirement connects to the buffer model and recovery requirements
  because sent packet metadata must reference the exact plaintext ranges and
  packet number space used for encryption.

### Buffer Model and Copy Contract

- Define separate packet and stream buffer heaps:
  `Zquic::PacketBufAlloc` and `Zquic::StreamBufAlloc` or similarly succinct
  names. Both must use built-in size `1472`, but they must have distinct heap
  IDs and semantic roles.
- UDP receive must land directly in `Zquic` packet `ZiIOBuf` instances owned
  by the QUIC endpoint or link. The receive path must not route through
  TCP-style `ZiRx` framing that copies trailing bytes.
- Application stream Tx starts as `StreamBufAlloc` buffers obtained through
  `Stream::txStream()`. Accepted Tx buffers are retained until their stream
  byte ranges are acknowledged, reset, canceled, or destroyed with the stream
  or connection.
- UDP packet Tx uses `PacketBufAlloc` buffers. Packet construction serializes
  headers and control frames directly into the packet buffer and encrypts
  retained plaintext stream ranges directly into that packet buffer.
- On Tx, each packet may include at most one application STREAM frame plus any
  permitted control frames that fit the current PMTU and congestion budget.
- Rx stream delivery must provide application-visible bytes through
  `ZiRxStream` backed by `StreamBufAlloc` buffers.
- The required Rx STREAM payload copy is:
  decrypted packet payload in `PacketBufAlloc` -> stream-owned plaintext bytes
  in `StreamBufAlloc`.
- The required Rx STREAM payload copy must be counted separately from forbidden
  fallback copies, so diagnostics can prove that no extra staging path is being
  used.
- The application must never observe bytes whose lifetime depends on a reusable
  UDP packet buffer.
- Copy-based fallback paths are not acceptable for packet Rx, packet Tx,
  stream Tx retention, encryption, packet parsing, HTTP/3 frame parsing, or
  QPACK parsing.
- Any future allowed copy must be explicitly named in `zquic/buffers.md` with
  source heap, destination heap, owner transfer, and reason.
- Add `zquic/buffers.md`, modeled after `ztls/buffers.md`, documenting packet
  buffer roles, stream buffer roles, owner transfer, retain/release rules,
  permitted source/destination overlap, zpicotls AEAD aliasing requirements,
  required Rx packet-to-stream copies, forbidden fallback copies, diagnostics,
  and fatal buffer-contract violations.
- This requirement is the main acceptance contract for the buffer ownership
  goal.

### Stream State and Scheduling

- Implement QUIC stream state machines for bidirectional and unidirectional
  streams, including local/remote initiation, send states, receive states, FIN,
  `RESET_STREAM`, `STOP_SENDING`, and final-size validation.
- Implement ordered byte delivery through each stream's `ZiRxStream`.
  Out-of-order STREAM frames must be retained or represented until missing
  offsets arrive. Overlap and duplicate data must be handled without exposing
  duplicates to the application.
- Implement stream send queues over retained `ZiIOBuf` ranges.
- The stream scheduler must choose sendable streams subject to stream flow
  control, connection flow control, congestion window, PMTU, packet number
  space, and stream priority policy.
- Initial priority policy may be round-robin or FIFO, but it must avoid
  starving active streams and must be isolated so future priority rules can be
  added without changing the public API.
- HTTP/3 request streams should be scheduled fairly with control streams, but
  control streams and QPACK streams must not be starved behind large body
  transfers.
- Stream creation must enforce local and remote stream limits by stream type.
  Limit extensions must generate `MAX_STREAMS` frames and unblock queued local
  stream creation when peer limits increase.
- Stream teardown must release retained Tx buffers, pending Rx buffers, reorder
  state, HTTP/3 stream state, QPACK references if any, and the Rx-thread stream
  table entry exactly once.
- This requirement connects to flow control, buffer retention, HTTP/3, and API
  callbacks because stream state determines what the application may send or
  receive at any time.

### Flow Control

- Implement QUIC connection-level flow control with `MAX_DATA` and
  `DATA_BLOCKED`.
- Implement stream-level flow control with `MAX_STREAM_DATA` and
  `STREAM_DATA_BLOCKED`.
- Implement stream-count flow control with `MAX_STREAMS` and
  `STREAMS_BLOCKED` for bidirectional and unidirectional stream types.
- Provide `Zquic::EngineParams` or connection params for initial connection
  data limits, initial per-stream data limits, initial stream-count limits,
  and receive-window update thresholds.
- Flow-control violations must close the connection with the correct transport
  error rather than relying on assertions.
- Flow-control backpressure must surface at the stream Tx and HTTP/3 body
  writer layers in a way that does not force applications to copy or abandon
  retained buffers.
- This requirement connects to packetization, stream scheduling, transport
  parameter requirements, and HTTP/3 body streaming.

### Loss Recovery, ACKs, and Congestion Control

- Implement ACK tracking and ACK generation for each packet number space.
  ACK-only packets must be distinguishable from ack-eliciting packets.
- Track every sent ack-eliciting packet until it is acknowledged, declared
  lost, or discarded with its packet number space.
- Sent-packet metadata must include packet number, packet number space, sent
  time, encoded size, in-flight status, ack-eliciting status, frames or frame
  references, retained stream/control buffer references, PMTUD probe flag, and
  path.
- Implement RFC 9002 loss detection: packet-threshold loss, time-threshold
  loss, PTO, exponential PTO backoff, and per-space loss timers.
- Implement initial NewReno congestion control with bytes-in-flight,
  congestion window, slow start, recovery, congestion avoidance, minimum
  window, persistent congestion, and per-path state.
- Implement RTT estimation from ACK frames, including ACK delay handling only
  when allowed for the packet number space.
- Implement retransmission by re-queuing lost frame data or regenerating
  current frames as QUIC requires. Do not retransmit packets byte-for-byte
  except where the protocol specifically calls for retained terminal close
  behavior.
- Packet send pacing must be represented in Tx scheduling. The first release
  may use scheduler timers rather than platform GSO or `SO_TXTIME`.
- Loss of PMTUD-only probe packets must not incorrectly reduce congestion
  window or report lost application bytes.
- Because ECN is not enabled in the first release, congestion response is based
  on loss and RTT behavior, not CE marks.
- This requirement connects to timers, packetization, PMTUD, stream retention,
  and connection close.

### PMTUD, Packet Size, and Datagram Output

- Use `1472` as the built-in packet and stream buffer size requested by the
  goal, while respecting QUIC's required minimum UDP payload handling and the
  actual path maximum.
- Implement PMTU state separately from buffer capacity. Buffer capacity is not
  permission to send a datagram of that size.
- The active max UDP payload size starts conservatively. Before DPLPMTUD
  raises it, send no datagram larger than QUIC's 1200-byte minimum Initial
  requirement and the configured path limit.
- Implement DPLPMTUD with QUIC probe packets per path. It is strongly
  preferred to classic ICMP-based PMTUD and is required for the first release.
- PMTUD state is per `Link` path. The first release has one active path, but
  the path object must own its own PMTU, probe state, blackhole/loss state, and
  max UDP payload size.
- PMTUD probe packets must fit `PacketBufAlloc`, peer `max_udp_payload_size`,
  local address-family constraints, and configured administrator caps.
- PMTUD should run only after the handshake state is safe for probe traffic and
  must not violate anti-amplification limits.
- Packetization must choose frames that fit the current PMTU. Control-frame
  piggybacking with stream data is required when it fits and should be skipped
  when it would force fragmentation or exceed the congestion/PMTU budget.
- Configure UDP sockets to avoid IP fragmentation where the platform supports
  it. ICMP Packet Too Big information may be consumed as a hint, but successful
  DPLPMTUD probes are the authority for raising PMTU.
- If DPLPMTUD fails or blackhole behavior is detected, reduce back to a safe
  payload size and continue the connection where QUIC permits.
- Add a future extension point for UDP GSO batching, but do not require GSO for
  the first usable release.
- This requirement connects to buffer allocation, packetization, pacing, and
  congestion control.

### Timers and Connection Lifecycle

- Use `ZmScheduler::Timer` for QUIC timers: loss detection/PTO, idle timeout,
  ACK delay, pacing, draining/closing state expiry, path validation, PMTUD
  probes, PMTUD blackhole detection, and optional handshake timeout.
- All timers must use a monotonic clock through existing `Zm` time facilities.
- Implement QUIC states for starting, handshaking, established, closing,
  draining, and closed.
- Implement graceful connection close with `CONNECTION_CLOSE`, abrupt drop for
  protocol cases that require no close packet, idle close, and draining state
  lasting at least three PTOs.
- Expose connection lifecycle callbacks to the application in the `Ztls` style,
  but make stream-level lifecycle visible through `Stream` callbacks or state
  accessors.
- HTTP/3 GOAWAY must be distinct from QUIC CONNECTION_CLOSE. HTTP/3 graceful
  shutdown should send GOAWAY before transport close when possible.
- This requirement connects to recovery, endpoint routing, HTTP/3 shutdown, and
  application API because timers drive most autonomous QUIC behavior.

### ECN

- ECN is not a first-release feature.
- Do not actively set ECT(0) or ECT(1) on outgoing packets by default.
- Do not require socket control-message support for ECN in the first release.
- Packet and ACK parsers should be structured so ACK frames with ECN counts can
  be accepted or rejected according to protocol rules without destabilizing the
  connection.
- Reserve per-path ECN state fields only where doing so avoids future API or
  storage churn. Reserved ECN state must remain disabled and must not affect
  congestion control.
- Future ECN support must include per-path validation, receiver-side ECN count
  reporting, sender-side marking policy, fallback to non-ECT on validation
  failure, and congestion response to CE counts. That future work is not part
  of this requirement set.

### 0-RTT

- 0-RTT is not required and must not be enabled in the first release.
- Clients must not send 0-RTT packets or early HTTP requests.
- Servers must not accept 0-RTT stream data.
- The packet parser must recognize 0-RTT packet type enough to drop or reject
  it safely, preserve connection state, and avoid confusing loss recovery or
  stream state.
- TLS session resumption without early data may be supported if it follows the
  existing `Ztls` configuration model, but it must not imply early-data
  support.
- Future 0-RTT support would require replay-risk controls, remembered
  transport parameters, remembered HTTP/3 SETTINGS, ALPN compatibility,
  rejection handling, idempotency policy, and stream/application rollback.
  Those are explicitly staged out of the first release.

### Versioning and Future Extension Points

- The first release is QUIC v1-only.
- Implement robust handling of unsupported versions and reserved versions so
  the implementation does not ossify around a single literal.
- RFC 9368 compatible version negotiation is a non-goal for the first release,
  but version tables, transport parameters, and packet parsing should not make
  it impossible to add later.
- QUIC v2 is a non-goal for the first release because it is not required for
  HTTP/3 and does not add application capability needed by the target use case.
- WebTransport, DATAGRAM, multipath, and full migration are future extension
  points. Do not expose public claims or configuration flags that imply support
  for them.

### Diagnostics, Telemetry, and Debuggability

- Provide `Zquic` log messages through `ZiLog` with clear subsystem names and
  transport, TLS, HTTP/3, and QPACK error details.
- Add counters for packet Rx/Tx, bytes Rx/Tx, stream bytes Rx/Tx, HTTP/3
  header bytes, HTTP/3 body bytes, packets lost, PTO count, retransmitted
  frames, congestion window, bytes in flight, handshake state, stream counts,
  PMTUD probes, PMTUD success/failure, required Rx packet-to-stream copies, and
  buffer-contract violations.
- Add explicit forbidden-copy counters or assertions for any path where a copy
  would violate the target buffer contract.
- Provide optional debug dump helpers for packet headers, frames, connection
  IDs, packet number spaces, stream state, recovery state, HTTP/3 frames, QPACK
  header blocks, and PMTUD state. These helpers must not be required in
  optimized data paths.
- Add key-log or qlog-style hooks only if they can be disabled by default and
  do not compromise production safety.
- Diagnostics must distinguish QUIC transport errors, TLS alerts or verify
  failures, HTTP/3 errors, QPACK errors, local configuration errors, and
  network/path failures.
- This requirement connects to testing and buffer contracts because the
  implementation must be inspectable enough to prove it is not silently using
  fallback copies.

### Tests and Acceptance Criteria

- Add `zquic/test` standalone binaries and a `test` make target consistent
  with existing module tests.
- Unit tests must cover QUIC varint encoding, packet number
  encoding/decoding, packet header parsing, frame parsing/serialization,
  transport parameter encoding/decoding, stream ID allocation, stream state
  transitions, flow control, ACK range behavior, loss detection timers, and
  congestion window transitions.
- Crypto tests must include RFC 9001 Initial packet/key test vectors, header
  protection application/removal, AEAD success/failure, key update, malformed
  packet rejection, and safe 0-RTT packet drop/reject behavior.
- Buffer tests must verify packet and stream heap separation, direct UDP Rx
  allocation, retained stream Tx buffer lifetime, direct packet encryption
  output, in-place Rx packet decryption, required Rx packet-to-stream copy,
  stream-owned delivery lifetime, and forbidden-copy counter/assertion
  behavior.
- PMTUD tests must cover initial 1200-byte behavior, successful DPLPMTUD probe
  growth, probe loss, blackhole fallback, peer `max_udp_payload_size` limits,
  configured caps, and PMTU state isolation by path/link.
- HTTP/3 tests must cover ALPN `h3`, SETTINGS exchange, control stream setup,
  QPACK stream type handling, request header encode/decode, response header
  encode/decode, body streaming, trailers where supported, cancellation,
  malformed pseudo-headers, unknown frame skipping, GOAWAY, and HTTP/3 error
  mapping.
- QPACK tests must cover static table encoding/decoding, literal
  encoding/decoding, zero dynamic table settings, dynamic reference rejection
  under zero capacity, header list size enforcement, malformed integer
  handling, and QPACK stream lifecycle.
- Loopback integration tests must cover client/server handshake, ALPN, one
  bidirectional stream, peer-initiated stream, multiple concurrent streams,
  stream FIN, stream reset, connection close, HTTP/3 request/response,
  packet loss simulation, reordering simulation, flow-control
  blocking/unblocking, PMTUD probing, and large payloads split across multiple
  packets.
- Interop tests against external QUIC/HTTP3 endpoints are desirable but should
  be an optional manual target unless the repository already has reliable
  tooling for them. The core acceptance target is deterministic local tests.
- No implementation is complete unless it builds with `make -j` and its
  `zquic/test` binaries pass through `make -C zquic/test test`.
- This requirement connects to all other requirements by defining how product
  completeness is demonstrated.

### Documentation and Migration Notes

- Document the public `Zquic` transport API in `zquic/README.md` or an
  equivalent module document, including minimal client/server examples and
  stream lifecycle.
- Document the HTTP/3 API, including REST-style client examples, server
  examples, connection reuse, request cancellation, response body streaming,
  and error mapping.
- Document the QPACK first-release profile, especially default zero dynamic
  table capacity and blocked-stream count.
- Document the buffer contract in `zquic/buffers.md` before or alongside
  implementation of packet protection.
- Document any deliberate API differences from `Ztls`, especially why
  `txStream()` and `process(rxStream)` live on `Stream` instead of `Link`.
- Document unsupported first-release features clearly: QUIC v2, compatible
  version negotiation, multipath, WebTransport, DATAGRAM, 0-RTT, active ECN,
  and full migration.
- Document PMTUD/DPLPMTUD behavior, defaults, caps, and diagnostics.
- This requirement connects to API, build, and tests because the new module is
  large enough that maintainers need a stable map of supported behavior.

## Resolved Decisions

- `Zquic` implements QUIC v1, HTTP/3, and QPACK for the first release.
- `Ztls` is an implementation style guide, not an API compatibility contract.
- `../zngtcp2` is a local reference only. Do not depend on it and do not use
  online canonical `ngtcp2` docs or code as the controlling reference.
- Tx encryption is source-to-destination from retained stream/control plaintext
  into the final packet buffer.
- Rx packet decryption is in-place in the packet buffer, followed by an
  explicitly required copy of STREAM payload bytes into stream buffers.
- First-release Tx packetization allows at most one application STREAM frame
  per UDP packet, with control-frame piggybacking when it fits.
- 0-RTT is not required and must not be enabled.
- ECN is not required and must not be active by default.
- PMTUD/DPLPMTUD is required, per path/link.
- Async private-key support should duplicate the `Ztls` async-thread pattern.
- QUIC v2, compatible version negotiation, multipath, WebTransport, DATAGRAM,
  and full migration are future extensions, not first-release requirements.
