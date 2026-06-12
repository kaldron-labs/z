## Summary

The goal is to add a first-class `Zquic` transport module and then use it from
`Zhttp` to deliver the primary product use case: HTTP/3 REST-style clients and
servers over QUIC v1.

This revision incorporates `plan.feedback.md`:

- `Zquic` is the QUIC transport library. It owns QUIC v1 connection state,
  packet number spaces, packet protection, streams, flow control, loss recovery,
  congestion control, timers, path validation, PMTUD/DPLPMTUD, UDP endpoint
  routing, and buffer ownership.
- Production HTTP/3 and QPACK types do not live in `libZquic`. They are added
  to `Zhttp` as the HTTP module's HTTP/3 implementation over `Zquic`.
- `Zquic` interop uses a lightweight HTTP/3-specific client/server harness
  under `zquic/test`. All HTTP-related code in the `Zquic` module is isolated
  to tests and is not installed, linked into `libZquic`, or exposed as example
  API.
- The original `goal.md` stream skeleton is preserved in this plan because it
  defines the required application-facing QUIC stream shape.
- Socket fragmentation and MTU behavior follows `mtu.md`: Zquic owns
  packetization and DPLPMTUD, disables fragmentation where the platform allows
  it, uses connected UDP on client sockets when kernel PMTU queries are useful,
  and keeps server sockets unconnected.
- Interop testing is no longer optional: first-release acceptance includes
  automated tests against `curl` and `caddy`. `Zquic` interop is a minimal
  HTTP/3 harness over raw `Zquic`; `Zhttp` interop is the full HTTP client and
  server with HTTP/3 plus HTTP/1.1 fallback.

First-release protocol scope:

- QUIC v1 transport only.
- HTTP/3 with ALPN `h3`, implemented in `Zhttp` over `Zquic`.
- QPACK implemented in `Zhttp`, with local encoder and decoder streams opened
  by default.
- Configurable QPACK indexing for application-selected header fields. Defaults
  may be conservative, but if nonzero dynamic table settings are advertised,
  the implementation must satisfy the corresponding QPACK dynamic table,
  blocked stream, acknowledgement, cancellation, eviction, and memory-limit
  rules.
- PMTUD/DPLPMTUD per path.
- Basic RFC 9000 Version Negotiation for v1-only endpoints.

First-release non-goals:

- QUIC v2.
- RFC 9368 compatible version negotiation.
- Multipath QUIC.
- WebTransport.
- QUIC DATAGRAM and HTTP/3 DATAGRAM.
- 0-RTT send or accept.
- Active ECN marking, ECN validation, and CE-based congestion response.
- Full connection migration beyond path/CID structure needed for future
  extension.
- Server push unless later explicitly enabled.
- API compatibility with `Ztls`.
- Dependency on `../zngtcp2`, canonical ngtcp2, nghttp3, quiche, MsQuic, or
  any other external QUIC/HTTP3/QPACK implementation.

Buffer contract:

- Packet buffers and stream buffers are distinct `ZiIOBuf` heaps with built-in
  size `1472`.
- Tx packet encryption is source-to-destination. Plaintext stream/control byte
  ranges are retained for ACK/loss/retry handling and encrypted directly into
  the final UDP packet buffer.
- Non-temporal cipher implementations can be used for Tx.
- No intermediate ciphertext staging buffer is allowed on Tx.
- Rx packet decryption is in-place within the received packet buffer.
- Non-temporal cipher implementations cannot be used for Rx.
- After frame parsing, each STREAM payload slice is copied once from the
  decrypted packet buffer into a stream buffer before delivery through
  `ZiRxStream`.
- That Rx packet-to-stream copy is required and separately counted. Other
  fallback copies below the public API boundary are forbidden unless explicitly
  added to `zquic/buffers.md`.
- On Tx, each UDP packet may contain at most one application STREAM frame, while
  QUIC control frames may piggyback when PMTU, congestion, anti-amplification,
  and packet-number-space rules permit.

Research basis:

- RFC 9000, QUIC Transport: https://www.rfc-editor.org/rfc/rfc9000.html
- RFC 9001, Using TLS to Secure QUIC:
  https://www.rfc-editor.org/rfc/rfc9001.html
- RFC 9002, QUIC Loss Detection and Congestion Control:
  https://www.rfc-editor.org/rfc/rfc9002.html
- RFC 9114, HTTP/3: https://www.rfc-editor.org/rfc/rfc9114.html
- RFC 9204, QPACK: https://www.rfc-editor.org/rfc/rfc9204.html
- RFC 8899, Datagram Packetization Layer PMTUD:
  https://www.rfc-editor.org/rfc/rfc8899.html
- RFC 9368, Compatible Version Negotiation for QUIC:
  https://www.rfc-editor.org/rfc/rfc9368.html
- RFC 9369, QUIC Version 2:
  https://www.rfc-editor.org/rfc/rfc9369.html
- Linux socket option documentation:
  https://www.man7.org/linux/man-pages/man2/IP_MTU_DISCOVER.2const.html
  and https://www.man7.org/linux/man-pages/man2/IP_RECVERR.2const.html
- Microsoft Winsock IP option documentation:
  https://learn.microsoft.com/en-us/windows/win32/winsock/ipproto-ip-socket-options
  and
  https://learn.microsoft.com/en-us/windows/win32/winsock/ipproto-ipv6-socket-options
- curl HTTP/3 documentation: https://curl.se/docs/http3.html
- Arch Linux `curl` package:
  https://archlinux.org/packages/core/x86_64/curl/
- Caddy protocol configuration documentation:
  https://caddyserver.com/docs/json/apps/http/servers/protocols
- Local reference only: `../zngtcp2`, especially
  `doc/source/programmers-guide.rst`,
  `doc/source/zngtcp2-buffer-contract.rst`,
  `crypto/zpicotls/zpicotls.c`, `lib/ngtcp2_pmtud.c`,
  `lib/ngtcp2_rtb.c`, and examples.
- Comparable open-source design reference only: MsQuic architecture and API
  docs, which separate registration/configuration/listener/connection/stream
  responsibilities behind a platform abstraction boundary:
  https://microsoft.github.io/msquic/msquicdocs/docs/Architecture.html and
  https://microsoft.github.io/msquic/msquicdocs/docs/API.html

## Architecture Documentation

### New Components

- `zquic/Makefile.am`, `zquic/src/Makefile.am`, and
  `zquic/test/Makefile.am` make `zquic` a peer transport module with tests.
- `zquic/src/ZquicLib.hh` and `zquic/src/ZquicLib.cc` define module export
  macros, logging subsystem names, and library version symbols following the
  `ZtlsLib` pattern.
- `zquic/src/Zquic.hh` is the public QUIC transport API. It contains
  `EngineParams`, `ClientParams`, `ServerParams`, `Engine`, `Client`, `Server`,
  `Link`, `Stream`, stream type/error enums, public buffer aliases, and generic
  ALPN configuration hooks.
- `zquic/src/ZquicTypes.hh` holds QUIC constants, transport errors,
  packet-number-space identifiers, stream ID helpers, version tables, frame
  type names, and compact printable names.
- All new `Zquic` and HTTP/3/QPACK enum-like protocol/state definitions must
  use `ZtEnum`, `ZtEnumMap`, or the existing `ZtEnum` flag helpers as
  appropriate. Do not add raw `enum` or `enum class` definitions for these
  values unless a C ABI or third-party API boundary requires one.
- `zquic/src/ZquicBuf.hh` defines `PacketBufAlloc`, `StreamBufAlloc`, retained
  Tx range descriptors, receive queues, copy counters, and buffer-contract
  assertions.
- Runtime invariant checks and defensive assertions in new `Zquic`,
  `Zhttp3`, and `ZhttpQPack` code must use `ZiAssert` with the relevant
  component name such as `"Zquic"` or `"Zhttp"`. Compile-time facts may still
  use `static_assert`.
- `zquic/src/ZquicPacket.hh` and `.cc` implement QUIC varints, long and short
  packet headers, packet number encoding/decoding, Retry, Version Negotiation,
  Stateless Reset recognition, and coalesced datagram iteration.
- `zquic/src/ZquicFrame.hh` and `.cc` implement typed QUIC frame structures,
  frame validation, ack-eliciting classification, and frame serialization.
- `zquic/src/ZquicCrypto.hh` and `.cc` own zpicotls QUIC integration: TLS
  context setup, transport parameter TLS extension handling, message-level QUIC
  handshake, traffic secret callbacks, Initial secret derivation, packet
  protection, header protection, key discard, key update, and async
  private-key integration.
- `zquic/src/ZquicTransportParams.hh` and `.cc` encode, decode, validate, and
  print QUIC transport parameters.
- `zquic/src/ZquicEndpoint.hh` and `.cc` own UDP socket objects and Destination
  Connection ID routing between `ZiConnection` and `Link`.
- `zquic/src/ZquicPath.hh` and `.cc` own local/remote addresses, client/server
  socket mode, validation state, anti-amplification accounting, disabled ECN
  placeholders, and PMTUD/DPLPMTUD state.
- `zquic/src/ZquicSock.hh` and `.cc` isolate platform socket behavior needed by
  QUIC: fragmentation controls, PMTU queries, Linux error-queue hints, and
  Winsock MTU/send-error mapping.
- `zquic/src/ZquicStream.hh` and `.cc` own stream send/receive state, stream ID
  allocation, final-size validation, reordering, reset/stop-sending, flow
  control, and teardown.
- `zquic/src/ZquicRecovery.hh` and `.cc` own sent-packet metadata, ACK range
  tracking, RTT estimation, PTO, packet/time threshold loss, persistent
  congestion, and NewReno state.
- `zquic/src/ZquicSched.hh` and `.cc` own frame queues and stream scheduling,
  including the first-release rule of at most one application STREAM frame per
  packet while allowing control-frame piggybacking.
- `zquic/src/ZquicDiag.hh` and `.cc` hold counters, debug dump helpers,
  qlog-style hooks if added, and error-formatting helpers.
- `zquic/buffers.md` documents the packet/stream buffer contract before packet
  protection depends on it.
- `zquic/README.md` documents the transport API, non-goals, PMTUD, socket
  behavior, and buffer rules. It must not document HTTP/3 as a `Zquic` API.
- `zquic/test/ZquicH3Lite.hh`, `.cc`, and
  `zquic/test/ZquicH3InteropTest.cc` provide a lightweight test-only HTTP/3
  client/server harness for QUIC interop. The harness lives entirely under
  `zquic/test`, is not installed, and is not linked into `libZquic`.
- `zhttp/src/Zhttp3.hh` and `.cc` implement production HTTP/3 connection state,
  stream type handling, SETTINGS, request/response streams, GOAWAY,
  cancellation, frame parsing/serialization, body streaming, and error mapping
  over `Zquic`.
- `zhttp/src/ZhttpQPack.hh` and `.cc` implement QPACK for HTTP/3: static table,
  literals, field section prefix, required insert count handling, dynamic table
  capacity updates, insert/duplicate instructions, section acknowledgement,
  stream cancellation, blocked-stream accounting, evictions, configurable
  indexing policy, and memory limits.

### Changed Components

- `Makefile.am` adds `zquic` to `SUBDIRS` after `ztls` and before `zhttp`, so
  `Zhttp` can link against `Zquic`.
- `configure.ac` adds `zquic` to the module symlink loop and adds
  `zquic/Makefile`, `zquic/src/Makefile`, and `zquic/test/Makefile` to
  `AC_CONFIG_FILES`.
- `zhttp/src/Makefile.am` adds `-I$(top_srcdir)/zquic/src`, installs
  `Zhttp3.hh` and `ZhttpQPack.hh`, compiles the new `.cc` files, and links
  `libZhttp.la` against `libZquic.la`.
- `zhttp/test/Makefile.am` adds HTTP/3, QPACK, loopback, full HTTP interop,
  and HTTP/1.1 fallback test binaries or scripts, and makes the interop target
  fail when `curl` or `caddy` is unavailable.
- No existing module should be refactored as a prerequisite. Small additions to
  `Zi` are allowed only if required to expose socket handles or local-address
  information that cannot safely live in `ZquicSock`.

### Processes and Threads

`Zquic` follows the `Ztls` ownership style but splits QUIC protocol work more
explicitly:

- I/O Rx thread: `ZiMultiplex` receives UDP datagrams directly into
  `PacketBufAlloc` buffers. It does not parse QUIC beyond invoking the
  `ZiConnection` callback.
- Protocol Rx lane: endpoint routing by DCID, packet header parsing, header
  protection removal, in-place AEAD decryption, frame parsing, received packet
  number tracking, ACK generation, Rx flow control, stream lookup, and
  `Link::m_streams` ownership.
- Protocol Tx lane: stream send queues, control-frame queues, packetization,
  packet protection, sent-packet metadata, loss recovery, congestion control,
  pacing, PTO, PMTUD probes, and UDP packet handoff.
- Async private-key thread: the same isolation constraint as `Ztls`; it must
  not be the I/O, protocol Rx, or protocol Tx lane.
- Application callback path: connection-local callbacks are serialized by
  default. If a later implementation allows parallel callbacks, that behavior
  must be explicit API.

`EngineParams` accepts `rxThread`, `txThread`, and optional `asyncThread`.
Defaulting to `ZiMultiplex::rxThread()` and `ZiMultiplex::txThread()` is
acceptable, but tests must cover distinct Rx and Tx lanes.

`Zhttp` HTTP/3 state is layered over the same ownership boundaries:

- H3/QPACK input follows received `Zquic::Stream` bytes and is Rx-owned until
  application-visible events are serialized.
- H3/QPACK output is Tx-owned where it emits HEADERS, DATA, QPACK, SETTINGS,
  GOAWAY, reset, and stop-sending behavior through `Zquic::Stream::txStream()`.
- Shared H3/QPACK state, such as dynamic table counters visible to encoder and
  decoder paths, must be explicitly listed and guarded with `ZmPLock` or
  `ZmAtomic`.

### Interfaces

`Zquic` exposes transport streams, not HTTP/3 request objects:

```cpp
namespace Zquic {

struct EngineParams {
  EngineParams(ZiMultiplex *mx, ZuCSpan rxThread, ZuCSpan txThread);
  EngineParams &&caPath(ZuCSpan);
  EngineParams &&certPath(ZuCSpan);
  EngineParams &&keyPath(ZuCSpan);
  EngineParams &&asyncThread(ZuCSpan);
  EngineParams &&maxData(uint64_t);
  EngineParams &&maxStreamData(uint64_t);
  EngineParams &&maxStreamsBidi(uint64_t);
  EngineParams &&maxStreamsUni(uint64_t);
  EngineParams &&maxUDP(unsigned);
  EngineParams &&alpn(ZuSpan<const ptls_iovec_t>);
  EngineParams &&errorFn(ErrorFn);
};

template <typename Impl, typename RxBufAlloc, typename TxBufAlloc>
class Stream {
public:
  int64_t id() const;
  auto txStream();
  void fin();
  void reset(uint64_t appError);
  void stop(uint64_t appError);
};

template <
  typename App, typename Impl, typename RxBufAlloc, typename TxBufAlloc,
  typename Cxn, typename CxnRef, typename Stream>
class Link {
public:
  using StreamRef = ZmRef<Stream>;
  StreamRef stream(StreamType type = StreamType::Bidi);
  void close(uint64_t errorCode = 0);
};

} // namespace Zquic
```

The original skeleton from `goal.md` must remain the implementation guide for
stream ID storage and Rx-thread stream dispatch:

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

Production HTTP/3 lives in `Zhttp`, with an API shaped around existing
`Zhttp::Method`, header containers, and builder/receiver idioms where useful:

```cpp
namespace Zhttp::H3 {

struct Params {
  Params &&maxHeaderListSize(unsigned);
  Params &&qpackTableCapacity(unsigned);
  Params &&qpackBlockedStreams(unsigned);
  Params &&qpackIndex(ZuCSpan name);
  Params &&qpackNeverIndex(ZuCSpan name);
};

struct Header {
  ZuCSpan name;
  ZuCSpan value;
};

struct RequestParams {
  Method::T method;
  ZuCSpan scheme;
  ZuCSpan authority;
  ZuCSpan path;
  ZuSpan<Header> headers;
};

template <typename Impl, typename ZquicClient>
class Client {
public:
  RequestRef request(RequestParams);
};

template <typename Impl, typename ZquicServer>
class Server {
public:
  void respond(StreamRef, ResponseParams);
};

} // namespace Zhttp::H3
```

The HTTP/3 request object exposes body writers/readers backed by
`Zquic::Stream::txStream()` and `Zquic::Stream::Impl::process()`. It must not
materialize complete request or response bodies in hidden buffers.

### Data Flows

UDP Rx flow:

1. `ZiConnection::recv_` obtains a new `PacketBufAlloc` buffer and asks
   `ZiMultiplex` to receive one UDP datagram.
2. `ZiIOContext::addr` is populated by `ZiMultiplex` for UDP receive.
3. The I/O callback transfers the packet buffer and address to the protocol Rx
   lane.
4. `Endpoint` parses enough header to route by Destination Connection ID or to
   handle Version Negotiation, Retry, Stateless Reset, unknown CID, or new
   server Initial.
5. `Link` removes header protection and decrypts packet payload in place.
6. Parsed STREAM frame payload is copied once into `StreamBufAlloc`, queued on
   the stream's `ZiRxStream`, and delivered through `Stream::Impl::process`.
7. CRYPTO, ACK, flow-control, path, close, and stream frames update Rx-owned
   transport state and enqueue Tx work as needed.
8. When `Zhttp::H3` is in use, it consumes stream bytes from `Zquic` and
   produces HTTP/3 request/response, QPACK, body, and error events.

UDP Tx flow:

1. Application or `Zhttp::H3` code writes stream bytes through
   `Stream::txStream()`, producing `StreamBufAlloc` buffers.
2. Stream Tx submission transfers custody to the protocol Tx lane, which
   retains byte ranges until ACK, loss requeue, reset, cancellation, or
   teardown.
3. Packetization selects control frames and at most one application STREAM
   frame for each UDP packet, bounded by PMTU, peer `max_udp_payload_size`,
   congestion, pacing, and anti-amplification.
4. Packet headers and generated control plaintext are serialized directly into
   a `PacketBufAlloc` packet buffer. Retained stream/control plaintext ranges
   are gathered as read-only vectors.
5. zpicotls AEAD encrypts those vectors directly into the packet buffer, then
   header protection mutates protected header bytes.
6. The packet buffer is handed to `ZiConnection::send_`. Client sockets may be
   connected for PMTU query support; server sockets remain unconnected and use
   `ZiIOContext::addr` for each datagram.
7. Sent-packet metadata retains plaintext frame/range references needed for
   ACK, loss, PTO, and close handling.

HTTP/3 flow in `Zhttp`:

1. `Zhttp::H3` configures `Zquic` ALPN as `h3` only when HTTP/3 is enabled.
2. Each endpoint opens one local HTTP/3 control stream and sends SETTINGS
   first.
3. Each endpoint opens local QPACK encoder and decoder streams. Even with
   zero-capacity defaults, the streams exist so peer symmetry and future
   dynamic table settings are explicit.
4. Request/response exchanges use client-initiated bidirectional QUIC streams.
5. QPACK emits HEADERS field sections and receives acknowledgements,
   insert-count progress, and cancellation instructions through QPACK streams.
6. DATA frames map to streaming body bytes without hidden full-body buffering.

### Event-Driven and Timer Processing

All timers use `ZmScheduler::Timer` and a monotonic `Zm` clock:

- ACK delay timer per packet number space.
- Loss/PTO timer across packet number spaces.
- Pacing timer for Tx scheduling.
- Idle timeout timer.
- Handshake timeout timer.
- Closing/draining expiry timer, lasting at least three PTOs.
- Path validation timers.
- PMTUD probe and blackhole timers.
- Optional endpoint retry/rebind timers.

The Tx lane owns timers that can emit packets. Rx updates request timer changes
through explicit Tx jobs. Timer callbacks must not reach into Rx-owned stream
tables directly.

### Network Programming

`Zquic` uses `ZiMultiplex::udp()` with `ZiCxnOptions().udp(true)`.

- Client sockets pass a remote address to `ZiMultiplex::udp()`, which connects
  the UDP socket on supported paths. This enables Linux and Windows
  `IP_MTU`/`IPV6_MTU` queries, while the internal path model still stores the
  remote `ZiSockAddr`.
- Server sockets pass no remote address and remain unconnected because one UDP
  socket must serve many peers and many paths.
- All Tx paths retain explicit path addressing internally. On connected client
  sockets the address may be redundant, but keeping it in `ZquicPath` preserves
  one model for server, client, and future migration.
- DPLPMTUD does not require connected UDP sockets. Kernel PMTU and ICMP data is
  advisory input only; QUIC probe success/failure remains the authority for
  raising or lowering active PMTU.

Socket fragmentation controls from `mtu.md`:

- Linux IPv4: set `IP_MTU_DISCOVER` to `IP_PMTUDISC_DO` for normal
  no-fragment behavior or `IP_PMTUDISC_PROBE` when DPLPMTUD intentionally sends
  probes above the kernel cached PMTU.
- Linux IPv6: use `IPV6_MTU_DISCOVER` with the corresponding
  `IPV6_PMTUDISC_*` mode.
- Linux error hints: enable `IP_RECVERR`/`IPV6_RECVERR` and drain
  `MSG_ERRQUEUE` where practical, routing Packet Too Big hints by path. These
  hints may cap or lower PMTU, never raise it without QUIC probe success.
- Linux `IP_MTU`/`IPV6_MTU` queries require connected sockets, so use them only
  for client sockets and treat failure as advisory.
- Windows/MinGW IPv4: use Winsock `IP_DONTFRAGMENT`, `IP_MTU_DISCOVER`,
  `IP_MTU`, and `IP_USER_MTU` where available.
- Windows/MinGW IPv6: use `IPV6_MTU_DISCOVER`, `IPV6_MTU`,
  `IPV6_USER_MTU`, and `IPV6_DONTFRAG` where headers and runtime support
  permit.
- Windows enum values differ from Linux, so `ZquicSock` must never share raw
  numeric constants across platforms.
- Windows has no portable Linux-style `MSG_ERRQUEUE`; map `WSAEMSGSIZE` and
  available MTU queries into local path hints.
- Unsupported socket controls are logged and counted, not fatal, unless the
  platform would force IP fragmentation for datagrams above the safe minimum.

### Data Stores

No persistent data store is required.

In-memory transport structures:

- Endpoint CID hash tables.
- Per-link packet-number spaces.
- Sent-packet recovery table.
- ACK range tracker.
- Stream hash table keyed by stream ID.
- Stream reorder ranges.
- Flow-control windows.
- Path PMTUD/DPLPMTUD state.
- Diagnostics counters.

In-memory HTTP/3 and QPACK structures in `Zhttp`:

- H3 control stream state.
- QPACK encoder and decoder stream state.
- QPACK static table views.
- QPACK dynamic table entries, eviction state, insert count, blocked stream
  table, and memory accounting when nonzero dynamic capacity is configured.
- Header list limit accounting and decoded header containers.

TLS session tickets may be kept in memory like `Ztls::Ticket`, but 0-RTT data
must remain disabled.

### Dependency and API Behavior Review

- `ZiIOContext::addr` exists for UDP send and receive and is set by
  applications for send and by `ZiMultiplex` for receive
  (`zi/src/ZiIOContext.hh:24`).
- `ZiCxnOptions::udp(true)` selects UDP sockets (`zi/src/ZiMultiplex.hh:195`).
- `ZiMultiplex::udp()` creates a UDP socket and calls `connect()` when a remote
  IP is supplied (`zi/src/ZiMultiplex.cc:320`), matching the client-side PMTU
  query plan.
- `ZiConnection::recv_` uses `recvfrom`/`WSARecvFrom` for UDP and writes
  directly into the supplied buffer (`zi/src/ZiMultiplex.cc:1229` and
  `zi/src/ZiMultiplex.cc:1281`).
- `ZiConnection::send_` uses `sendto`/`WSASendTo` when UDP has an explicit
  address and normal `send`/`WSASend` otherwise
  (`zi/src/ZiMultiplex.cc:1448` and `zi/src/ZiMultiplex.cc:1481`).
- `ZiMultiplex::initSocket()` is the existing socket option hook
  (`zi/src/ZiMultiplex.cc:1570`). Prefer a `ZquicSock` helper first; add `Zi`
  API only if direct socket access cannot be kept local to `Zquic`.
- zpicotls `ptls_context_t::update_traffic_key` is the correct hook for QUIC
  traffic secrets (`/usr/include/zpicotls.h:1034`).
- zpicotls message-level APIs `ptls_handle_message`,
  `ptls_client_handle_message`, and `ptls_server_handle_message` are the QUIC
  handshake fit because they operate on handshake messages and epochs rather
  than TLS records (`/usr/include/zpicotls.h:1936`). `Zquic` must not use
  `ptls_send()`/`ptls_receive()` as a record layer.
- zpicotls `ptls_handshake_properties_t::additional_extensions`,
  `collect_extension`, and `collected_extensions` support QUIC transport
  parameter extension handling (`/usr/include/zpicotls.h:1184`).
- zpicotls `ptls_aead_encrypt_v_s` supports read-only plaintext vectors and
  explicitly requires those ranges not to overlap `output`
  (`/usr/include/zpicotls.h:1894`), matching Tx source-to-destination packet
  encryption.
- zpicotls `ptls_aead_decrypt` returns `SIZE_MAX` on invalid input
  (`/usr/include/zpicotls.h:1913`). In-place decrypt must be verified per
  selected AEAD in tests; reject non-temporal variants or other AEADs whose
  aliasing behavior violates exact in-place Rx.
- Existing `Ztls` disables server early data with `ctx->max_early_data_size =
  0` (`ztls/src/Ztls.hh:1767`) and client early data through
  `max_early_data_size` properties (`ztls/src/Ztls.hh:1046`). `Zquic` should
  duplicate that no-0-RTT posture.
- Linux headers on the current development host expose `IP_MTU_DISCOVER`,
  `IP_RECVERR`, `IP_MTU`, `IP_PMTUDISC_DO`, `IP_PMTUDISC_PROBE`,
  `IPV6_MTU_DISCOVER`, `IPV6_RECVERR`, and `IPV6_MTU`.
- Current mingw-w64 headers expose Winsock `IP_DONTFRAGMENT`,
  `IP_MTU_DISCOVER`, `IP_USER_MTU`, `IPV6_DONTFRAG`,
  `IPV6_MTU_DISCOVER`, `IPV6_USER_MTU`, and `PMTUD_STATE`.
- Current Arch Linux `curl` is in `core` and depends on `libngtcp2` and
  `libnghttp3`; the local package database reports `curl 8.20.0-7`.
- Current Arch Linux `caddy` is in `extra`; the local package database reports
  `caddy 2.11.3-1`.

### Complexity and Feasibility

- `Zquic` module/build integration is low complexity.
- Public CRTP transport API is medium complexity because stream ownership
  differs from `Ztls`, but `goal.md` gives the required shape.
- UDP endpoint and CID routing are medium complexity and feasible with
  `ZiMultiplex::udp()`.
- Packet/frame codec is medium-high complexity due strict varint, packet
  number, coalescing, and validation rules. It is isolated and heavily testable.
- zpicotls QUIC integration is high complexity. Feasible, but the plan must use
  message-level handshake APIs and traffic key callbacks, not TLS record APIs.
- Packet protection is high complexity because it must meet the buffer
  contract, exact nonce/AAD/sample rules, and timing-safe failure behavior.
- Stream state, flow control, loss recovery, congestion control, and timers are
  high complexity and should be implemented as transport vertical slices.
- PMTUD/DPLPMTUD is medium-high complexity. The local zngtcp2 PMTUD state
  machine is a useful reference; socket API variation is isolated in
  `ZquicSock`.
- Moving production HTTP/3 and QPACK into `Zhttp` reduces `Zquic` public API
  scope but expands `Zhttp`. This is the right module ownership because `Zhttp`
  already owns HTTP method/header vocabulary and HTTP/1.1 builders.
- QPACK dynamic support is high complexity once nonzero table capacity is
  enabled. Keeping defaults conservative while implementing a bounded,
  configured dynamic profile makes the requirement feasible without exposing
  partial protocol behavior.
- Automated curl/caddy interop is feasible on current Arch Linux because both
  packages are mainstream packages, but tests must isolate ports, certificates,
  process cleanup, timeouts, and diagnostics.

## Detailed Design and Implementation Plan

### Phase 1: Zquic Skeleton, Public Transport Shape, Build Wiring, and Buffer Contract

Focus: make `Zquic` a buildable, testable transport module with the public
stream shape locked before protocol behavior is added.

Add:

- `zquic/Makefile.am`
- `zquic/src/Makefile.am`
- `zquic/test/Makefile.am`
- `zquic/src/ZquicLib.hh`
- `zquic/src/ZquicLib.cc`
- `zquic/src/ZquicTypes.hh`
- `zquic/src/ZquicBuf.hh`
- `zquic/src/Zquic.hh`
- `zquic/test/ZquicAPITest.cc`
- `zquic/README.md`
- `zquic/buffers.md`

Modify:

- `Makefile.am`
- `configure.ac`

Design and implementation details:

- Mirror `ztls/src/Makefile.am` include order and link order, adding
  `@PTLS_CFLAGS@` and `@PTLS_LIBS@`.
- Put `zquic` after `ztls` and before `zhttp` in top-level `SUBDIRS`.
- Define `ZquicAPI`, `ZquicExtern`, and `ZquicExplicit`.
- Define `PacketBufAlloc` and `StreamBufAlloc` with built-in size `1472`,
  separate heap IDs such as `"Zquic.Packet"` and `"Zquic.StreamBuf"`, and
  separate diagnostics.
- Define `EngineParams`, `ClientParams`, and `ServerParams` with validation
  stubs and `Ztls`-style fluent setters.
- Define generic ALPN configuration in `Zquic` params. `Zquic` must not expose
  HTTP/3 request/response types.
- Use `ZtEnum`/`ZtEnumMap` for all new enum-like protocol, packet,
  stream-state, error, and configuration value sets. Use `ZiAssert` for all
  new runtime assertions, including buffer-contract and state-machine
  invariants.
- Define the `Stream` and `Link` CRTP skeleton in `Zquic.hh`, preserving the
  exact `goal.md` stream ID and `m_streams` dispatch shape.
- Add a `test` target and one API compile test that instantiates minimal
  client link, server link, and stream types without opening sockets.
- Write `zquic/buffers.md` before packet protection code exists. It must name
  permitted source/destination overlap, retained Tx ranges, required Rx
  packet-to-stream copy, and forbidden fallback copies.

How it connects:

- Later phases fill implementation files without changing the public transport
  template shape.
- `Zhttp` phases depend on the stream API and buffer contract, not on any
  HTTP API surface in `Zquic`.

Acceptance for this phase:

- `make -C zquic/test test` builds and runs `ZquicAPITest`.
- `make -j` includes the new module.
- `zquic/buffers.md` names the required Rx packet-to-stream copy and forbids
  hidden fallback copies.

### Phase 2: UDP Endpoint Loopback, Path Model, and Socket Fragmentation Controls

Focus: prove direct UDP datagram ownership, per-datagram address handling, and
platform socket setup before QUIC parsing.

Add:

- `zquic/src/ZquicEndpoint.hh`
- `zquic/src/ZquicEndpoint.cc`
- `zquic/src/ZquicPath.hh`
- `zquic/src/ZquicPath.cc`
- `zquic/src/ZquicSock.hh`
- `zquic/src/ZquicSock.cc`
- `zquic/test/ZquicEndpointTest.cc`
- `zquic/test/ZquicSockTest.cc`

Design and implementation details:

- Add an endpoint-owned `ZiConnection` subtype for UDP sockets.
- The receive callback allocates a fresh `PacketBufAlloc`, initializes
  `ZiIOContext` with the raw packet buffer, and transfers completed datagrams
  plus `ZiIOContext::addr` to the protocol Rx lane.
- The send path accepts a packet buffer plus `ZquicPath` and calls
  `ZiConnection::send_` on the I/O Tx thread.
- Client endpoints pass remote address to `ZiMultiplex::udp()` to get connected
  UDP where possible. Server endpoints leave remote address empty and remain
  unconnected.
- Implement `ZquicSock::initUDP()` and `ZquicSock::pathHint()` helpers.
- On Linux, configure `IP_MTU_DISCOVER`/`IPV6_MTU_DISCOVER` and optionally
  `IP_RECVERR`/`IPV6_RECVERR`.
- On Windows/MinGW, configure `IP_DONTFRAGMENT`,
  `IP_MTU_DISCOVER`/`IPV6_MTU_DISCOVER`, and family-specific MTU/user-MTU
  controls where available.
- Query `IP_MTU`/`IPV6_MTU` only on connected client sockets.
- Treat unsupported controls as diagnostics unless they imply forced
  fragmentation.
- Add minimal endpoint lifecycle: `openUDP`, `closeUDP`, `listening`,
  `listenFailed`, and datagram/byte counters.
- Do not parse QUIC yet. The loopback test sends a raw datagram and checks
  packet buffer ownership and address preservation.

How it connects:

- QUIC packet parsing receives datagrams from this endpoint.
- Path objects start as address/socket-mode holders and later gain
  anti-amplification, validation, ECN-disabled fields, and PMTUD state.

Acceptance for this phase:

- UDP loopback test passes or degrades consistently with existing `Zi` loopback
  behavior if the environment denies sockets.
- Packet buffer allocation counters prove UDP Rx did not use `ZiRx`.
- Socket tests verify Linux/Windows option selection through compile-time and
  platform-specific branches without sharing raw option values.

### Phase 3: QUIC Codec, Transport Parameters, zpicotls Message Handshake, and Initial Protection

Focus: build the smallest end-to-end QUIC handshake path over UDP, including
packet/frame codecs, transport parameters, CRYPTO frames, and Initial/Handshake
packet protection.

Add:

- `zquic/src/ZquicPacket.hh`
- `zquic/src/ZquicPacket.cc`
- `zquic/src/ZquicFrame.hh`
- `zquic/src/ZquicFrame.cc`
- `zquic/src/ZquicTransportParams.hh`
- `zquic/src/ZquicTransportParams.cc`
- `zquic/src/ZquicCrypto.hh`
- `zquic/src/ZquicCrypto.cc`
- `zquic/test/ZquicCodecTest.cc`
- `zquic/test/ZquicCryptoTest.cc`
- `zquic/test/ZquicPacketProtectionTest.cc`
- `zquic/test/ZquicHandshakeTest.cc`

Design and implementation details:

- Implement QUIC varint encode/decode with bounds checks and no overreads.
- Implement connection ID value type and hash/accessor helpers using local
  `Zu`/`Zm` containers.
- Implement stream ID helpers for initiator bit, direction bit, ordinal, and
  next-ID allocation.
- Implement long-header and short-header parse/write helpers, including
  Initial, 0-RTT recognition, Handshake, Retry, Version Negotiation, and 1-RTT
  packet types.
- Implement packet number encoding and reconstruction.
- Implement frame parse/write for PADDING, PING, ACK, CRYPTO,
  CONNECTION_CLOSE, and enough transport frames to complete handshake.
- Implement transport parameter encode/decode and validation for local and
  remote limits, idle timeout, max UDP payload size, active connection ID
  limit, stateless reset token, original DCID, initial SCID, retry SCID,
  initial max data/stream data, max streams, ACK delay exponent, max ACK delay,
  and disable active migration.
- Use zpicotls message-level `ptls_client_handle_message` and
  `ptls_server_handle_message` with epoch offsets. Do not use TLS record-layer
  `ptls_send()` or `ptls_receive()`.
- Configure `ptls_context_t::update_traffic_key` to collect Initial,
  Handshake, and 1-RTT traffic secrets.
- Configure transport parameters through `additional_extensions`,
  `collect_extension`, and `collected_extensions`.
- Set server `max_early_data_size` to `0`. Client early-data output pointers
  must result in no early data being emitted.
- Implement QUIC v1 Initial key derivation and RFC 9001 Initial packet vectors.
- Implement AEAD and header-protection context setup from the negotiated
  zpicotls cipher suite.
- Tx protection uses `ptls_aead_encrypt_v_s` with non-overlapping retained
  plaintext vectors and packet-buffer output.
- Rx removes header protection, reconstructs packet number, and decrypts in
  place in the packet buffer. Tests must prove exact in-place decrypt for each
  enabled AEAD.
- Reject non-temporal or other AEAD variants whose decrypt aliasing violates
  the buffer contract.
- Duplicate `Ztls` async private-key support and thread validation.

How it connects:

- This phase creates the first UDP client/server handshake and installs 1-RTT
  keys. Later phases broaden routing, streams, recovery, and HTTP.
- The codec and crypto tests become the guardrail for every later packetizing
  phase.

Acceptance for this phase:

- RFC 9001 Initial secrets and packet protection vectors pass.
- Deterministic loopback client/server handshake reaches 1-RTT key
  availability with a configured ALPN value.
- 0-RTT packets are recognized and safely dropped or rejected; no early-data
  API exists.
- Packet protection tests prove Tx source-to-destination encryption and Rx
  in-place decryption.

### Phase 4: Connection Bootstrap Hardening, CID Routing, Version Negotiation, Retry, and Close

Focus: turn the handshake slice into a routed QUIC connection that handles the
mandatory edge cases around CIDs, versions, Retry, anti-amplification, and
connection lifecycle.

Modify:

- `ZquicEndpoint.hh/.cc`
- `ZquicPath.hh/.cc`
- `ZquicPacket.hh/.cc`
- `ZquicFrame.hh/.cc`
- `ZquicCrypto.hh/.cc`
- `Zquic.hh`

Add:

- `zquic/test/ZquicVersionTest.cc`
- `zquic/test/ZquicCIDTest.cc`
- `zquic/test/ZquicCloseTest.cc`

Design and implementation details:

- Implement client connection start with random initial DCID and SCID of at
  least 8 bytes.
- Implement server new-Initial acceptance, Initial destination/source CID
  validation, and association of local SCIDs and initial client DCIDs to
  `Link`.
- Maintain active local Source Connection ID table, initial client DCID table,
  retired CID tombstones, and unknown-CID behavior.
- Implement ordinary RFC 9000 Version Negotiation for unsupported versions.
  Do not implement RFC 9368 compatible version negotiation or version
  information transport parameters.
- Implement Retry packet generation and validation, including original DCID and
  retry SCID transport parameter checks.
- Implement Stateless Reset token generation/storage and unknown-CID reset
  behavior only when safe.
- Enforce server anti-amplification byte accounting before address validation.
- Implement states: starting, handshaking, established, closing, draining, and
  closed.
- Implement graceful connection close, abrupt drop cases, idle close, and
  draining for at least three PTOs.
- Discard Initial and Handshake keys at the correct transitions.

How it connects:

- Later stream and HTTP phases can assume established connections are routed by
  CID and have correct close behavior.
- Version handling remains v1-only but does not ossify around a single literal.

Acceptance for this phase:

- Loopback handshake works over `ZiMultiplex::udp()` with CID routing.
- Unsupported version packets produce valid Version Negotiation or safe abort.
- Retry, unknown-CID, stateless-reset, anti-amplification, close, and draining
  tests are deterministic.

### Phase 5: Stream API Vertical Slice, One Bidi Stream, FIN, and Buffer Contract

Focus: deliver the first application-visible QUIC stream end to end over an
established connection.

Modify:

- `Zquic.hh`
- `ZquicBuf.hh`
- `ZquicStream.hh/.cc`
- `ZquicSched.hh/.cc`
- `ZquicFrame.hh/.cc`

Add:

- `zquic/src/ZquicStream.hh`
- `zquic/src/ZquicStream.cc`
- `zquic/src/ZquicSched.hh`
- `zquic/src/ZquicSched.cc`
- `zquic/test/ZquicStreamTest.cc`
- `zquic/test/ZquicBufferTest.cc`

Design and implementation details:

- Implement `Stream::id()`, `Stream::txStream()`, `Link::stream(...)`, and
  `Link::Impl::streamed(ZmRef<Stream>)`.
- Implement Rx-thread-owned `Link::m_streams` using the hash table shape from
  `goal.md`. Stream additions and removals are Rx jobs.
- Implement client-initiated bidirectional stream IDs and server acceptance.
- Implement STREAM frame Tx from retained `StreamBufAlloc` ranges.
- Implement STREAM frame Rx with the required copy from decrypted
  `PacketBufAlloc` payload to `StreamBufAlloc` delivery buffer.
- Implement in-order byte delivery in this phase. Out-of-order ranges can be
  represented but full overlap handling lands in Phase 7.
- Implement FIN send/receive and simple stream close callbacks.
- Enforce at most one application STREAM frame per packet. Permit control-frame
  piggyback only if size limits allow.
- Add counters for required Rx packet-to-stream copies and forbidden copy
  attempts.

How it connects:

- This phase validates the public transport API before recovery and flow
  control add complexity.
- `Zhttp::H3` later maps request streams to this same object without changing
  `Zquic`.

Acceptance for this phase:

- Loopback test sends one bidirectional stream from client to server and one
  response from server to client.
- FIN close is observed by both sides.
- Buffer tests prove packet and stream heap separation, stream Tx buffer
  retention, direct packet encryption output, in-place Rx decrypt, and exactly
  one required Rx STREAM payload copy.

### Phase 6: ACKs, Recovery, NewReno, PTO, Retransmission, and Tx Scheduling

Focus: make stream data reliable under loss and paced under congestion.

Add:

- `zquic/src/ZquicRecovery.hh`
- `zquic/src/ZquicRecovery.cc`
- `zquic/test/ZquicRecoveryTest.cc`
- `zquic/test/ZquicCongestionTest.cc`

Design and implementation details:

- Implement ACK generation and ACK delay timers per packet number space.
- Implement sent-packet metadata with packet number, PN space, sent time,
  encoded size, ack-eliciting flag, in-flight flag, PMTUD probe flag, path,
  frame references, and retained buffer references.
- Implement RTT estimation, ACK delay handling only where allowed, packet
  threshold loss, time threshold loss, PTO, exponential backoff, and persistent
  congestion.
- Implement NewReno with slow start, congestion avoidance, recovery start time,
  minimum congestion window, bytes-in-flight, and per-path state.
- Implement retransmission by requeueing lost frame data or regenerating
  current control frames, not by retransmitting lost packets byte-for-byte.
- Implement packet pacing as a Tx scheduler timer. Do not require GSO,
  `SO_TXTIME`, or platform batching.
- Ensure PMTUD-only probe loss does not report lost application bytes or reduce
  cwnd as normal data loss.

How it connects:

- Phase 7 stream reordering and flow control depend on reliable ACK/loss
  signals.
- Phase 9 HTTP/3 large bodies need retransmission, PTO, and fair pacing.

Acceptance for this phase:

- Unit tests cover ACK ranges, ACK-only vs ack-eliciting packets, RTT updates,
  packet/time loss thresholds, PTO backoff, persistent congestion, NewReno cwnd
  transitions, and retransmission queue behavior.
- Loopback tests with simulated loss and reordering complete one stream without
  duplicate delivery.

### Phase 7: Full Stream State, Flow Control, Reset/Stop, and Fair Scheduling

Focus: complete transport stream behavior required by HTTP/3 concurrency.

Modify:

- `ZquicStream.hh/.cc`
- `ZquicSched.hh/.cc`
- `ZquicFrame.hh/.cc`
- `ZquicTransportParams.hh/.cc`

Add:

- `zquic/test/ZquicFlowTest.cc`
- `zquic/test/ZquicStreamStateTest.cc`

Design and implementation details:

- Implement bidirectional and unidirectional send/receive state machines.
- Implement out-of-order STREAM frame retention, overlap/duplicate handling,
  final-size validation, and ordered `ZiRxStream` delivery.
- Implement connection-level flow control with `MAX_DATA` and `DATA_BLOCKED`.
- Implement stream-level flow control with `MAX_STREAM_DATA` and
  `STREAM_DATA_BLOCKED`.
- Implement stream-count flow control with `MAX_STREAMS` and `STREAMS_BLOCKED`
  for bidi and uni streams.
- Implement queued local stream creation when peer stream-count limits block
  immediate opening.
- Implement stream reset, stop sending, peer reset, peer stop-sending, final
  size checks, and final teardown.
- Implement fair scheduling across sendable streams with a small isolated
  policy object. Control streams used by higher layers must not be starved by
  body data.
- Surface Tx backpressure through `Stream::txStream()` without copying or
  abandoning retained buffers.

How it connects:

- HTTP/3 control streams and QPACK streams require unidirectional stream
  support.
- Request cancellation maps directly to reset and stop-sending behavior.

Acceptance for this phase:

- Tests cover every stream state transition, stream limit extension, queued
  local stream creation, final-size violations, flow-control violations, reset,
  stop-sending, overlap, duplicate data, and teardown release exactly once.
- Concurrent stream loopback tests complete without starvation.

### Phase 8: PMTUD/DPLPMTUD, Path Validation, and Datagram Output Limits

Focus: implement required per-path PMTUD while keeping one active path in the
first release.

Modify:

- `ZquicPath.hh/.cc`
- `ZquicSock.hh/.cc`
- `ZquicSched.hh/.cc`
- `ZquicRecovery.hh/.cc`
- `ZquicEndpoint.hh/.cc`

Add:

- `zquic/test/ZquicPMTUDTest.cc`

Design and implementation details:

- Keep buffer capacity separate from active max UDP payload size. `1472` is
  buffer size, not permission to send 1472-byte datagrams.
- Start active max UDP payload at the QUIC minimum/validated default: no larger
  than 1200 before discovery and configured caps.
- Implement DPLPMTUD probe state per path with candidate sizes derived from
  configured caps, peer `max_udp_payload_size`, local address family, and
  administrator maximum.
- Send probes as QUIC probe packets subject to anti-amplification, congestion,
  pacing, and handshake safety.
- Track probe count, success, expiry, failure floor, and blackhole fallback.
  The local zngtcp2 PMTUD state machine is a suitable reference for retry and
  candidate advancement.
- Treat ICMP Packet Too Big only as a hint to lower or cap PMTU, not as
  authority to raise it.
- Use Linux error queue hints where available and validated.
- Use connected-client `IP_MTU`/`IPV6_MTU` or Winsock MTU queries as advisory
  hints.
- Reduce to a safe payload size after blackhole detection and continue the
  connection where QUIC permits.
- Keep disabled ECN state in the path object, but do not mark outgoing packets
  or validate ECN.

How it connects:

- Packetization now uses the path's active max UDP payload size.
- Recovery marks PMTUD probes so their loss does not corrupt application-loss
  accounting or congestion response.

Acceptance for this phase:

- Tests cover initial 1200-byte behavior, successful DPLPMTUD growth, probe
  loss, blackhole fallback, peer max UDP payload limit, configured cap, kernel
  PMTU hint lowering, and independent state per link/path.
- Packetization never exceeds PMTU, peer limit, congestion budget,
  anti-amplification budget, or packet buffer capacity.

### Phase 9: Zhttp HTTP/3 Control Streams, SETTINGS, QPACK Streams, and One REST Exchange

Focus: implement the primary product use case as a narrow vertical slice in
`Zhttp` over `Zquic`.

Add:

- `zhttp/src/Zhttp3.hh`
- `zhttp/src/Zhttp3.cc`
- `zhttp/src/ZhttpQPack.hh`
- `zhttp/src/ZhttpQPack.cc`
- `zhttp/test/Zhttp3Test.cc`
- `zhttp/test/ZhttpQPackTest.cc`
- `zhttp/test/Zhttp3LoopTest.cc`

Modify:

- `zhttp/src/Makefile.am`
- `zhttp/test/Makefile.am`
- `zhttp/src/Zhttp.hh` only if shared method/header helpers need small
  public additions.

Design and implementation details:

- Configure ALPN `h3` through `Zquic` only when `Zhttp::H3` is active and able
  to send/receive control streams and QPACK streams.
- Open exactly one local HTTP/3 control stream and send SETTINGS first.
- Accept peer control stream and enforce single-control-stream rules.
- Open local QPACK encoder and decoder streams by default, even when dynamic
  table capacity starts at zero.
- Accept peer QPACK encoder and decoder streams, enforce one-stream limits, and
  handle closure/duplicate stream errors.
- Implement HTTP/3 frame parse/write for DATA, HEADERS, SETTINGS, GOAWAY,
  CANCEL_PUSH, PUSH_PROMISE, and MAX_PUSH_ID, including unknown-frame skipping
  where allowed.
- Implement QPACK static table and literal encoders/decoders.
- Implement field section prefix validation, required insert count handling,
  base validation, malformed integer handling, and header-list-size enforcement.
- Default QPACK dynamic table capacity and blocked streams may be zero in this
  phase, but the stream plumbing must match the dynamic-capable design.
- Reject dynamic references when local settings advertise zero capacity.
- Implement pseudo-header validation for REST-style request/response:
  `:method`, `:scheme`, `:authority`, `:path`, and response `:status`.
- Implement one client request and one server response over a client-initiated
  bidirectional stream.

How it connects:

- Uses completed unidirectional stream support and reliable transport from
  earlier phases.
- Provides first demonstrable HTTP/3 REST behavior before dynamic QPACK,
  cancellation, and interop hardening.

Acceptance for this phase:

- Loopback HTTP/3 client sends GET/POST headers and optional body; server
  decodes and responds; client receives response headers and body.
- QPACK tests cover static table, literals, zero dynamic settings, dynamic
  reference rejection, malformed integer handling, base validation, and header
  list limits.
- No HTTP/3 or QPACK production public types are added to `Zquic`.

### Phase 10: QPACK Dynamic Configuration and HTTP/3 Completeness

Focus: finish the first-release HTTP/3 behavior in `Zhttp`, including the
feedback-required ability to configure additional QPACK-indexed headers.

Modify:

- `Zhttp3.hh/.cc`
- `ZhttpQPack.hh/.cc`
- `ZquicStream.hh/.cc`
- `ZquicSched.hh/.cc`

Add:

- `zhttp/test/ZhttpQPackDynamicTest.cc`
- `zhttp/test/Zhttp3StateTest.cc`

Design and implementation details:

- Add `Zhttp::H3::Params` for QPACK table capacity, blocked streams, maximum
  header list size, indexed-header allowlist, and never-index rules.
- Implement QPACK dynamic table capacity negotiation and local capacity update
  instructions.
- Implement encoder instructions for insert with name reference, insert without
  name reference, duplicate, and set dynamic table capacity.
- Implement decoder stream instructions for section acknowledgement, stream
  cancellation, and insert count increment.
- Implement Required Insert Count, Delta Base, Base, blocked-stream accounting,
  unblocking, and stream cancellation semantics.
- Enforce memory limits, eviction rules, and header list size before exposing
  decoded headers.
- Implement policy so only configured safe header fields are indexed. Never
  index sensitive fields such as authorization/cookie-like headers by default.
- Support concurrent request streams over one connection.
- Support streaming request and response bodies without materializing complete
  bodies.
- Implement receive-side trailers. Stage send-side trailer API only if it does
  not delay core REST request/response behavior.
- Map application cancellation to reset send and stop receiving.
- Implement HTTP/3 GOAWAY and graceful shutdown distinct from QUIC
  CONNECTION_CLOSE.
- Enforce malformed frame, closed critical stream, duplicate stream, invalid
  setting, forbidden push, and invalid frame-on-stream-type errors with correct
  HTTP/3 error classes.
- Keep server push disabled unless fully implemented. Do not advertise push
  support.
- Surface HTTP/3, QPACK, QUIC transport, TLS, and path errors distinctly.

How it connects:

- Completes the production HTTP/3 product surface in `Zhttp`.
- Leaves DATAGRAM, WebTransport, push, 0-RTT, and QUIC v2 out of the public
  contract.

Acceptance for this phase:

- HTTP/3 tests cover concurrent REST requests, response body streaming,
  cancellation, malformed pseudo-headers, unknown frame skipping, GOAWAY,
  duplicate control streams, QPACK stream closure rules, and error mapping.
- Dynamic QPACK tests cover configured indexing, eviction, blocked stream
  limits, section acknowledgement, stream cancellation, insert count increment,
  memory caps, never-index policy, and peer dynamic table errors.

### Phase 11: Runtime Integration, Diagnostics, Automated Interop, and Hardening

Focus: make the component-complete transport and HTTP/3 slices into runnable
client/server stacks, then prove them against mainstream tools. The phase is
not complete if tests only exercise local codecs or curl-to-Caddy fixtures that
do not route traffic through `Zquic` or `Zhttp`.

Modify:

- `Zquic.hh`
- `ZquicEndpoint.hh/.cc`
- `ZquicConn.hh`
- `ZquicCrypto.hh/.cc`
- `ZquicRecovery.hh/.cc`
- `ZquicSched.hh/.cc`
- `ZquicStream.hh/.cc`
- `ZquicDiag.hh/.cc`
- `zquic/README.md`
- `zquic/buffers.md`
- `zquic/test/Makefile.am`
- `zhttp/src/Zhttp3.hh`
- `zhttp/src/Zhttp3.cc`
- `zhttp/src/ZhttpQPack.hh`
- `zhttp/src/ZhttpQPack.cc`
- `zhttp/src/Makefile.am`
- `zhttp/test/Makefile.am`

Add:

- `zquic/test/ZquicH3Lite.hh`
- `zquic/test/ZquicH3Lite.cc`
- `zquic/test/ZquicH3InteropTest.cc`
- `zquic/test/ZquicRuntimeTest.cc` or equivalent runnable transport loop test.
- `zhttp/test/Zhttp3InteropTest.cc` or equivalent test driver.
- `zhttp/test/ZhttpFallbackTest.cc` or equivalent test driver.
- `zhttp/test/caddy-h3.json` or generated temporary config.
- `zhttp/test/curl-h3.sh` only if a shell wrapper is cleaner than C++ process
  control.

Implementation path:

1. Complete the runnable `Zquic` transport API.
   - Add `Client::connect()` and `Server::listen()` entry points that open UDP
     endpoints, start the QUIC handshake, create routed `Link` instances, and
     drive connection lifecycle callbacks.
   - Join the existing endpoint, packet codec, crypto, transport-parameter,
     CID routing, close, recovery, scheduler, PMTUD, flow-control, and stream
     pieces into one Rx/Tx packet pump.
   - Ensure the packet pump uses `ZiMultiplex::udp()`, connected client UDP
     sockets where configured, unconnected server sockets, per-datagram
     `ZiIOContext::addr`, and the packet/stream buffer contract already
     documented in `zquic/buffers.md`.
   - Add deterministic loopback tests proving a real UDP client/server
     handshake, one bidirectional stream exchange, one unidirectional stream
     exchange, retransmission after simulated loss, idle/graceful close,
     PMTUD output limiting, and no forbidden buffer copies.

2. Make the `Zquic` lightweight HTTP/3 interop harness real.
   - Keep all HTTP code under `zquic/test`; it must not be installed, exported,
     or linked into `libZquic`.
   - Implement ALPN `h3`, SETTINGS, one control stream, QPACK
     static/literal/zero-capacity field sections, HEADERS, DATA, GOAWAY, and
     graceful close over actual `Zquic` streams.
   - The harness must use raw `Zquic` streams and must not use `Zhttp`.
   - Replace any prerequisite-only or local-codec-only `ZquicH3InteropTest`
     checks with real local interop:
     - `curl --http3-only -k` -> local lightweight `zquic/test` HTTP/3 server.
     - local lightweight `zquic/test` HTTP/3 client -> local Caddy HTTP/3
       server.

3. Complete the production `Zhttp::H3` client/server layer over `Zquic`.
   - Extend `Zhttp::H3::Client` and `Zhttp::H3::Server` beyond local
     `Connection` wrappers so they open required control and QPACK
     unidirectional streams, create request streams, stream bodies, receive
     trailers, apply cancellation, emit GOAWAY, and map H3/QPACK/QUIC/TLS/path
     errors distinctly.
   - Keep production HTTP/3 and QPACK code in `Zhttp`; do not add public H3
     request/response API to `Zquic`.
   - Implement the full REST client/server path using existing `Zhttp` method,
     header, builder, body, and receiver idioms where they fit. The path must
     not materialize full request or response bodies unless the existing
     `Zhttp` API explicitly asks for that behavior.

4. Implement HTTP/3 plus HTTP/1.1 fallback behavior in `Zhttp`.
   - Provide a configured client policy that attempts HTTP/3 over `Zquic` when
     ALPN and endpoint settings permit it, then falls back to existing
     HTTP/1.1 over `Ztls` when HTTP/3 is unavailable, rejected, or disabled.
   - Preserve secure production TLS defaults. Test-only self-signed trust and
     `curl -k` usage are allowed only inside the interop harness.
   - Fallback tests must prove selected protocol, response status, headers,
     body, error mapping, and cleanup.

5. Add diagnostics and documentation needed to debug the runtime stack.
   - Add `ZiLog` subsystem names for `Zquic` endpoint, crypto, recovery,
     stream, and PMTUD paths, plus `Zhttp` HTTP/3 and QPACK paths.
   - Add counters for packet Rx/Tx, bytes Rx/Tx, stream bytes Rx/Tx,
     header/body bytes, packets lost, PTO count, retransmitted frames, cwnd,
     bytes in flight, handshake state, stream counts, PMTUD probes,
     PMTUD success/failure, required Rx packet-to-stream copies, and
     buffer-contract violations.
   - Add debug dump helpers for packet headers, frames, CIDs, PN spaces,
     stream state, recovery state, HTTP/3 frames, QPACK field sections, and
     PMTUD state.
   - Add disabled-by-default key log or qlog-style hooks only if they do not
     expose secrets by default.
   - Finalize `Zquic` docs with transport client/server examples, buffer rules,
     PMTUD behavior, socket behavior, errors, and unsupported features. Do not
     add HTTP/3 examples to `zquic/example`.
   - Finalize `Zhttp` docs with HTTP/1.1 via `Ztls`, HTTP/3 via `Zquic`,
     fallback behavior, REST client/server examples, cancellation, connection
     reuse, QPACK configuration, and error mapping.

6. Implement deterministic automated interop.
   - Use local loopback only. Do not depend on public Internet endpoints.
   - Use ephemeral ports, temporary directories, bounded timeouts, and reliable
     child-process cleanup.
   - Fail fast if `curl` or `caddy` is missing.
   - Fail if `curl --version` lacks HTTP3 plus ngtcp2/nghttp3 support.
   - Generate temporary certificates/configs as needed; keep insecure trust
     local to the tests.
   - Required interop matrix:
     - `curl --http3-only -k` -> local lightweight `zquic/test` H3 server.
     - local lightweight `zquic/test` H3 client -> local Caddy H3 server.
     - `curl --http3-only -k` -> local full `Zhttp` H3 server.
     - local full `Zhttp` H3 client -> local Caddy H3 server.
     - `curl --http1.1 -k` -> local full `Zhttp` HTTP/1.1 fallback route.
     - local full `Zhttp` client fallback -> local HTTP/1.1-only endpoint
       after an HTTP/3-unavailable or ALPN-rejected case.

How it connects:

- The runtime packet pump turns the earlier codec, crypto, recovery, scheduler,
  stream, PMTUD, and close slices into the actual transport product.
- The lightweight `Zquic` H3 harness proves transport interoperability without
  leaking HTTP into `libZquic`.
- The full `Zhttp` H3 and fallback tests prove the primary REST client/server
  product path.
- Diagnostics prove the buffer contract and make the large protocol surface
  debuggable.
- Documentation prevents accidental user reliance on excluded features.

Acceptance for this phase:

- `make -j` succeeds.
- `make -C zquic/test test` passes.
- `make -C zhttp/test test` passes.
- `ZquicRuntimeTest` or equivalent proves a real UDP-backed `Zquic`
  client/server connection, not only local component calls.
- `ZquicH3InteropTest` passes the curl/Caddy matrix through the lightweight
  raw-`Zquic` H3 harness and fails if the test can pass without traffic entering
  `Zquic`.
- `Zhttp3InteropTest` passes the curl/Caddy matrix through the full production
  `Zhttp` HTTP/3 client/server path and fails if the test can pass with only
  curl-to-Caddy traffic.
- `ZhttpFallbackTest` proves HTTP/3-to-HTTP/1.1 fallback through `Zhttp` and
  fails if fallback is only a Caddy protocol selection fixture.
- Interop tests fail when `curl` or `caddy` is unavailable or when curl lacks
  HTTP/3/ngtcp2/nghttp3 support.
- Buffer diagnostics prove no forbidden target-path copies are used.
- Unsupported features are documented: QUIC v2, compatible version negotiation,
  multipath, WebTransport, DATAGRAM, 0-RTT, active ECN, and full migration.

## Code References to Impacted Code

- `Makefile.am:3` - Add `zquic` to top-level `SUBDIRS` after `ztls` and before
  `zhttp`.
- `configure.ac:196` - Add `zquic` to the module symlink loop.
- `configure.ac:209` - Add `zquic/Makefile`, `zquic/src/Makefile`, and
  `zquic/test/Makefile` to `AC_CONFIG_FILES`.
- `ztls/src/Makefile.am:1` - Use as the build and link-order model for
  `zquic/src/Makefile.am`.
- `ztls/test/Makefile.am:20` - Use as the standalone binary and `prove`
  test-target model.
- `ztls/src/Ztls.hh:77` - Reuse parameter object and fluent setter style.
- `ztls/src/Ztls.hh:138` - Reuse explicit thread-ownership documentation
  style, while splitting QUIC Rx and Tx.
- `ztls/src/Ztls.hh:544` - Reuse the `Zi::txStream` facade idea, moving it to
  `Zquic::Stream`.
- `ztls/src/Ztls.hh:1046` - Reuse client no-early-data configuration posture.
- `ztls/src/Ztls.hh:1154` - Reuse `Engine::init_`, validation, `ZmBlock`, and
  scheduler invocation patterns.
- `ztls/src/Ztls.hh:1176` - Reuse async private-key thread validation rules.
- `ztls/src/Ztls.hh:1226` - Reuse zpicotls context initialization, random,
  cipher, ALPN, CA, cert, and key setup patterns.
- `ztls/src/Ztls.hh:1767` - Reuse server no-early-data configuration posture.
- `ztls/buffers.md:1` - Use as the documentation quality bar for
  `zquic/buffers.md`.
- `ztls/buffers.md:55` - Reuse AEAD aliasing scrutiny, especially excluding
  unsafe non-temporal decrypt variants.
- `ztls/buffers.md:199` - Reuse buffer-hook contract checks where zpicotls
  needs origin-backed output.
- `zi/src/ZiIOContext.hh:24` - Use `ZiIOContext::addr` for UDP path
  information.
- `zi/src/ZiIOContext.hh:56` - Use UDP send initialization that carries an
  explicit address.
- `zi/src/ZiMultiplex.hh:195` - Use `ZiCxnOptions::udp`.
- `zi/src/ZiMultiplex.hh:472` - Use direct `ZiConnection::recv` and `send`
  callbacks instead of `ZiRx`.
- `zi/src/ZiMultiplex.hh:859` - Use `ZiMultiplex::udp()` for endpoint sockets.
- `zi/src/ZiMultiplex.hh:873` - Use `rxRun`, `rxInvoke`, `txRun`, and
  `txInvoke` to keep owner transitions explicit.
- `zi/src/ZiMultiplex.cc:320` - Client UDP sockets become connected when a
  remote IP is supplied.
- `zi/src/ZiMultiplex.cc:1229` - UDP receive uses `WSARecvFrom`.
- `zi/src/ZiMultiplex.cc:1281` - UDP receive uses `recvfrom`.
- `zi/src/ZiMultiplex.cc:1448` - UDP send uses `WSASendTo` when an address is
  provided.
- `zi/src/ZiMultiplex.cc:1481` - UDP send uses `sendto`.
- `zi/src/ZiMultiplex.cc:1570` - Socket initialization is the local pattern to
  follow if `ZquicSock` needs a `Zi` helper.
- `zi/src/ZiIOBuf.hh:58` - Use `ZiIOBuf` owner, data base, skip, length, and
  aligned allocation model for packet/stream buffers.
- `zi/src/ZiIOBuf.hh:190` - Account for `ensure()` preserving existing data;
  do not rely on it where packet protection requires pointer stability.
- `zi/src/ZiTxStream.hh:40` - Use as the stream Tx facade building block.
- `zi/src/ZiRxStream.hh:34` - Use as the stream Rx delivery facade.
- `zhttp/src/Makefile.am:1` - Add `Zquic` include path, sources, installed
  headers, and `libZquic.la` link dependency.
- `zhttp/src/Zhttp.hh:31` - Reuse HTTP method vocabulary for HTTP/3.
- `zhttp/src/Zhttp.hh:161` - Reuse header container/matcher ideas after QPACK
  decoding, not HTTP/1.1 wire parsing.
- `zhttp/src/Zhttp.hh:729` - Reuse builder idioms where they fit HTTP/3
  request/response construction.
- `zhttp/test/Makefile.am:1` - Add HTTP/3/QPACK/full interop and HTTP/1.1
  fallback test binaries and target behavior.
- `zrest/src/Zrest.hh:352` - Later REST clients may layer over
  `Zhttp::H3`/`Zquic`, but this plan does not require changing `Zrest`.
- `/usr/include/zpicotls.h:1034` - Use `update_traffic_key` for QUIC traffic
  secrets.
- `/usr/include/zpicotls.h:1184` - Use raw extension hooks for QUIC transport
  parameters.
- `/usr/include/zpicotls.h:1894` - Use `ptls_aead_encrypt_v_s` for retained
  plaintext vector encryption into packet buffers.
- `/usr/include/zpicotls.h:1913` - Use `ptls_aead_decrypt` and verify exact
  in-place decrypt behavior by test.
- `/usr/include/zpicotls.h:1936` - Use message-level handshake APIs for QUIC.
- `../zngtcp2/doc/source/programmers-guide.rst:9` - Local reference confirming
  QUIC needs TLS integration different from a normal TLS record stack.
- `../zngtcp2/doc/source/programmers-guide.rst:222` - Local reference for
  packet read/write handoff sequencing.
- `../zngtcp2/doc/source/programmers-guide.rst:284` - Local reference for PMTUD
  probe configuration and default 1200-byte start.
- `../zngtcp2/doc/source/programmers-guide.rst:295` - Local reference for
  server packet routing and Version Negotiation flow.
- `../zngtcp2/doc/source/programmers-guide.rst:320` - Local reference for CID
  association and cleanup.
- `../zngtcp2/doc/source/programmers-guide.rst:344` - Local reference for why
  0-RTT remains out of first release.
- `../zngtcp2/doc/source/programmers-guide.rst:385` - Local reference for
  stream payload custody and retention.
- `../zngtcp2/doc/source/programmers-guide.rst:395` - Local reference for
  monotonic timer integration.
- `../zngtcp2/doc/source/programmers-guide.rst:484` - Local reference for
  version negotiation behavior and why compatible version negotiation is staged
  out.
- `../zngtcp2/doc/source/zngtcp2-buffer-contract.rst:14` - Local reference for
  semantic buffer roles and ownership.
- `../zngtcp2/doc/source/zngtcp2-buffer-contract.rst:42` - Local reference for
  source-to-destination packet protection design.
- `../zngtcp2/doc/source/zngtcp2-buffer-contract.rst:103` - Local reference for
  retained stream Tx payload lifetime.
- `../zngtcp2/doc/source/zngtcp2-buffer-contract.rst:125` - Local reference for
  Tx packet encryption into packet handoff buffers.
- `../zngtcp2/lib/ngtcp2_pmtud.c:32` - Local reference for PMTUD probe retry
  and candidate state.
- `../zngtcp2/lib/ngtcp2_rtb.c:160` - Local reference for excluding PMTUD probe
  loss from normal lost-byte reporting.

## Detailed Test Plan

Add `zquic/test` standalone binaries and include them in
`zquic/test/Makefile.am`. Add `zhttp/test` binaries for production HTTP/3 and
QPACK behavior.

`Zquic` transport tests:

- `ZquicAPITest`: public template instantiation, params setters, invalid thread
  validation, buffer aliases, stream ID helper compile coverage, and absence of
  public HTTP/3 request/response types in `Zquic`.
- `ZquicEndpointTest`: UDP loopback, direct packet buffer receive,
  `ZiIOContext::addr` preservation, endpoint close, datagram counters, and no
  `ZiRx` path.
- `ZquicSockTest`: Linux and Windows option selection, unsupported option
  diagnostics, connected-client PMTU query branch, unconnected-server branch,
  `EMSGSIZE`/`WSAEMSGSIZE` path hints, and no shared numeric PMTUD constants
  across platforms.
- `ZquicCodecTest`: varints, packet number encode/decode/reconstruct, long and
  short header parsing, Retry, Version Negotiation, Stateless Reset detection,
  frame parse/write, malformed bounds, and safe 0-RTT recognition without data
  delivery.
- `ZquicCryptoTest`: transport parameter encode/decode, zpicotls raw extension
  collection, message-level handshake epochs, ALPN passthrough, no 0-RTT
  configuration, and async private-key validation.
- `ZquicPacketProtectionTest`: RFC 9001 Initial vectors,
  Initial/Handshake/1-RTT key install, header protection, AEAD success/failure,
  in-place decrypt aliasing, key update, malformed packet rejection, and safe
  0-RTT packet drop/reject.
- `ZquicHandshakeTest`: client/server UDP loopback handshake, CID routing,
  anti-amplification, key discard, close, and drain.
- `ZquicVersionTest`: unsupported version handling, RFC 9000 Version
  Negotiation, reserved version drop behavior, and v1-only tables.
- `ZquicCIDTest`: client initial CID, server SCID issuance, active CID table,
  retired CID tombstones, unknown CID drops/resets, and cleanup.
- `ZquicCloseTest`: graceful close, protocol close, idle timeout, draining, and
  no late callbacks after final close.
- `ZquicStreamTest`: one stream, peer-initiated stream, concurrent streams,
  FIN, reset, stop-sending, ordered delivery, and stream teardown.
- `ZquicBufferTest`: heap separation, retained Tx buffer lifetime, direct
  packet encryption output, in-place Rx packet decrypt, required Rx
  packet-to-stream copy, forbidden-copy assertions/counters, and packet buffer
  lifetime isolation.
- `ZquicRecoveryTest`: ACK ranges, ACK delay, ack-only behavior, packet/time
  threshold loss, PTO, exponential backoff, persistent congestion, and
  retransmit requeue.
- `ZquicCongestionTest`: NewReno slow start, congestion avoidance, recovery,
  minimum window, bytes-in-flight, and PMTUD probe loss exclusion.
- `ZquicFlowTest`: connection data, stream data, stream count limits,
  blocking/unblocking, MAX_* emission, and flow-control violation close.
- `ZquicStreamStateTest`: bidirectional/unidirectional state machines,
  final-size validation, overlap/duplicate data, queued stream creation, and
  exact-once release.
- `ZquicPMTUDTest`: initial 1200 behavior, successful DPLPMTUD growth, probe
  loss, blackhole fallback, peer max UDP payload limit, configured caps, kernel
  MTU hint lowering, and path state isolation.
- `ZquicLoopTest`: component-integrated client/server handshake, streams,
  concurrent streams, large payloads split across packets, loss, reordering,
  flow-control block/unblock, PMTUD probing, and graceful close.
- `ZquicRuntimeTest` or equivalent: real UDP-backed `Zquic::Client` and
  `Zquic::Server` over `ZiMultiplex::udp()`, proving handshake, stream
  creation, bidirectional and unidirectional stream bytes, retransmission,
  PMTUD output limiting, close/drain, and diagnostics through the public
  runtime API.
- `ZquicH3InteropTest`: test-only lightweight HTTP/3 client/server over raw
  `Zquic` runtime streams, with all HTTP parsing, QPACK static/literal
  handling, SETTINGS, HEADERS, DATA, and close behavior isolated under
  `zquic/test`. The test must fail if it can pass using only local codecs or
  without packet traffic entering `Zquic`.

`Zhttp` HTTP/3 and QPACK tests:

- `ZhttpQPackTest`: static table encode/decode, literal fields, field section
  prefix, zero dynamic settings, dynamic reference rejection under zero
  capacity, required insert count validation, base validation, header list
  size, and malformed integer handling.
- `ZhttpQPackDynamicTest`: dynamic table capacity update, configured indexed
  headers, insert with name reference, insert without name reference,
  duplicate, section acknowledgement, stream cancellation, insert count
  increment, blocked stream accounting, eviction, memory caps, and never-index
  policy.
- `Zhttp3Test`: SETTINGS exchange, control stream setup, QPACK stream setup,
  duplicate critical streams, HEADERS/DATA, unknown frame skipping,
  pseudo-header validation, forbidden push frames, GOAWAY, and error mapping.
- `Zhttp3StateTest`: concurrent requests, cancellation, response body
  streaming, receive-side trailers, send-side trailers if enabled, closed
  critical streams, invalid settings, and stream-type frame validation.
- `Zhttp3LoopTest`: full `Zhttp::H3` client/server loopback over the runnable
  `Zquic` stream API, REST request/response, concurrent requests, large body
  split across packets, QPACK dynamic configuration, cancellation, graceful
  shutdown, loss, reordering, flow control, and PMTUD.
- `Zhttp3InteropTest`: local automated interop with curl and caddy using the
  full `Zhttp` HTTP client/server stack. The test must fail if it can pass
  using only curl-to-Caddy traffic.
- `ZhttpFallbackTest`: HTTP/3-to-HTTP/1.1 fallback behavior using `Zhttp`'s
  HTTP/3-over-`Zquic` path and existing HTTP/1.1-over-`Ztls` path. The test
  must prove `Zhttp` selected the fallback, not merely that Caddy can serve
  HTTP/1.1.

Interop details:

- Zquic lightweight HTTP/3 interop:
  - start a local `zquic/test` lightweight HTTP/3 server backed by
    `Zquic::Server`, with a temporary self-signed cert;
  - run `curl --http3-only -k` against it;
  - verify status, body, that curl reports HTTP/3, and that `Zquic`
    diagnostics report the connection, packets, stream bytes, and graceful
    shutdown;
  - fail if `curl` is missing or `curl --version` lacks `HTTP3` plus
    `ngtcp2`/`nghttp3`.
  - start local `caddy` with HTTP/3 enabled;
  - connect using the `zquic/test` lightweight HTTP/3 client backed by
    `Zquic::Client`;
  - verify response headers, body, selected ALPN, stream bytes, and clean
    shutdown.
- Zhttp full client/server interop:
  - start a full `Zhttp` server exposing the same route through HTTP/3 over
    `Zquic` and HTTP/1.1 over `Ztls`;
  - run curl against the HTTP/3 endpoint and verify the full `Zhttp` request
    path, selected protocol, response headers, body, and shutdown;
  - generate a temporary Caddy config with HTTP/3 enabled on an ephemeral local
    port;
  - run `caddy` as a child process;
  - connect with the full `Zhttp` client over HTTP/3;
  - force an HTTP/3-unavailable case by using an HTTP/1.1-only endpoint or by
    rejecting `h3` ALPN, then verify fallback through the existing `Zhttp`
    HTTP/1.1/TLS path;
  - verify response headers, body, selected protocol, shutdown, and process
    cleanup;
  - fail if `caddy` is missing.
- Interop tests must use local loopback only, bounded timeouts, ephemeral
  directories, reliable child process cleanup, and stack-specific diagnostics
  proving that traffic passed through the intended `Zquic` or `Zhttp` code.

Required commands at completion:

```sh
make -j
make -C zquic/test test
make -C zhttp/test test
```

## Options and Open Questions

No blocking open questions remain. The following decisions resolve the legacy
options from `plan.md`:

- HTTP/3 placement: production HTTP/3 and QPACK are implemented in `Zhttp`
  using `Zquic`; `Zquic` remains the QUIC transport. Any HTTP/3 code in the
  `zquic` module is limited to lightweight interop tests under `zquic/test`.
  There is no public `Zquic::H3` API and no HTTP-related code under
  `zquic/src` or `zquic/example`.
- Protocol threads: mimic `Ztls` parameter style while allowing separate
  `rxThread` and `txThread`; default to `ZiMultiplex` Rx/Tx lanes if omitted
  and test distinct lanes.
- Client UDP socket mode: use connected UDP for client sockets when a remote
  address is known so kernel PMTU queries work; keep servers unconnected; keep
  explicit `ZquicPath` addressing internally for both.
- QPACK streams: open local encoder and decoder streams by default. Defaults
  may advertise conservative capacities, but `Zhttp::H3::Params` must allow
  applications to configure additional QPACK-indexed headers, which requires
  bounded dynamic table support when nonzero capacity is advertised.
- HTTP/3 trailers: implement receive-side trailer parsing. Add send-side
  trailers only if it fits without delaying core REST request/response
  behavior.
- Socket fragmentation controls: implement `ZquicSock` according to `mtu.md`;
  Linux uses PMTUD/DF and error-queue hints, Windows/MinGW uses Winsock PMTUD
  and DF controls, and DPLPMTUD remains authoritative.
- Interop: automated curl/caddy interop is a required first-release acceptance
  path, not an optional manual target. `Zquic` interop uses a lightweight
  HTTP/3-only test harness under `zquic/test`; `Zhttp` interop uses the full
  HTTP client/server and verifies HTTP/1.1 fallback. On current Arch Linux,
  require the `curl` package from `core` and `caddy` from `extra`; fail tests
  if either is unavailable or curl lacks HTTP/3 support.
