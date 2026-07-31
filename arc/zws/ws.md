# WebSocket implementation plan

## Current baseline and scope

This is a dependent subsequent plan.  Its entry baseline is the current
checked-in transport/HTTP implementation **after `zhttp5.md` is complete**.
Do not start from the historical states in `zi.md`, `zhttp.md`, `h2.md`,
`zhttp3.md`, or `zhttp4.md`.  The required stream foundations at that handoff
are:

- `ZiRxStream` owns queued native input, while `ZiRxLayer<Impl>` provides the
  synchronous bounded/refillable application view.  `Zi::RxRefill` scopes its
  `Wait`, `Input`, `Final`, and `Error` states, and `Zi::RxEvent` is a
  `ZtFlags` mask exposing `Start()`, `Input()`, `Final()`, and `Error()`;
- `ZiTxStream` reports rollover versus final flush to `sendBuf_(buf, final)`,
  and `ZiTxLayer<Impl, Lower>` composes headroom and tailroom without moving
  payload;
- `Ztcp`, `Ztls`, and `Zquic::Stream` expose application-facing `txStream()`,
  owner-shard `txStream_()`, pooled `ZiIOBuf` storage, and continuation-ordered
  disconnect/teardown;
- `Zhttp` exposes normalized `H1TCP`, `H1TLS`, `H2TLS`, and `H3QUIC`
  client/server link profiles;
- H1 delivers decoded bodies through `Zhttp::BodyRx`/`ZiRxLayer`, treats `101`
  as a final response, and preserves unread native `ZiRxStream` bytes;
- H2 implements RFC 8441 Extended CONNECT, enabled by
  `H2Config::extendedConnect(true)`;
- H3 implements RFC 9220 Extended CONNECT, enabled by
  `QUICConfig::extendedConnect(true)`; and
- H1 Upgrade and H2/H3 Extended CONNECT expose the same non-owning
  `Zhttp::Stream<Link>` contract using the repository-wide
  `process(stream, rx)` and `txStream(fn)` convention.  Ordered input and
  exact-once peer terminal state are delivered through `Zi::RxEvent`;

There is deliberately no `Zhttp::Upgrade` or native-link transfer API.  H1
takeover is a link-local receive-mode change; H2/H3 use logical streams.
This plan consumes those existing contracts and must not reimplement them in
`libZws`.

The high-level `Zhttp::Agent` and `Zhttp::Service` are optimized for finite
HTTP request/response work.  The initial persistent WebSocket implementation
should compose the lower-level typed engines and links directly.  Reuse their
transport, ALPN, buffer, sharding, and lifecycle behavior without routing an
established WebSocket through the HTTP request coordinator.

The repository already contains an old, incomplete `zws` scaffold.  Its
`Zws::CliLink` derives directly from `Ztls::CliLink`, parses neither HTTP nor
WebSocket correctly, owns placeholder framing code, and predates the current
`Zhttp` profiles and logical-stream abstraction.  Replace that prototype; do
not preserve it as a compatibility layer and do not build the new design
beside it.

Complete the existing `libZws` module as generic RFC 6455 WebSocket
capability.
Public WebSocket APIs, including the Binance market-data and trading APIs, are
important use cases and initial integration targets, but neither trading nor
Binance defines the library API or architecture.  The protocol core must be
usable by unrelated low-latency messaging, telemetry, control, streaming, and
other WebSocket applications.  The client path needed to consume public
`wss://` APIs is the first vertical slice, but the initial HTTP/1 capability
includes both client and server bindings over plain TCP and TLS.

Interoperability with existing RFC 6455 `ws://` and `wss://` endpoints is the
first objective.  WebSocket over HTTP/2 and HTTP/3 are additional bindings of
the same WebSocket implementation; they do not change its protocol core.

The implementation must preserve the performance and ownership properties of
the current stack: compile-time composition, shard-affined state, pooled I/O
buffers, in-place TLS record protection, and no additional virtual dispatch,
type erasure, or avoidable allocation on the data path.  Apply
`GUIDELINES.md` throughout.

Optimize the common case for small, unfragmented messages with ultra-low
latency, no payload copy, and no heap allocation per message.  This workload
focus is a performance posture, not a protocol restriction: large and
fragmented messages must remain correct, incremental, bounded by configured
policy, and free of forced contiguous conversion.

## Protocol model

Model WebSocket as a framing and connection state machine over a reliable,
ordered, bidirectional byte stream.  HTTP establishes that stream:

```text
                         WebSocket
             framing, control and close state
                             |
                   ordered byte stream
              +--------------+--------------+
              |              |              |
       upgraded HTTP/1   HTTP/2 CONNECT   HTTP/3 CONNECT
        TCP/TLS link         stream              stream
```

For HTTP/1, a successful Upgrade changes the protocol mode of the same
`Zhttp::ClientLink` or `Zhttp::ServerLink`.  Its existing `Ztcp`/`Ztls`
connection, Rx stream, Tx path, and ownership remain in place; only the
link-local Rx dispatcher changes from the H1 handshake parser to the
WebSocket codec.  Returning positive progress from the handshake processor
lets the native receive loop call the new mode immediately when WebSocket
bytes follow the terminating headers in the same input queue.

For HTTP/2 and HTTP/3, Extended CONNECT creates one logical link within a
continuing multiplexed connection.  Its `Zhttp::Stream<Link>` facade supplies
control and Tx access while its ordered DATA Rx layer is passed separately to
`process()`.  H2 flow control remains in `Zhttp`; H3 stream and connection
flow control remains in `Zquic`.  For H1, the same abstraction adapts the
unread native TCP/TLS queue without copying it.  Once the opening handshake
succeeds, the WebSocket framing implementation must not depend on HTTP
version, HTTP frame types, HPACK, QPACK, QUIC, or a physical connection type.

Do not implement a non-standard mode that sends raw WebSocket frames directly
over TCP, TLS, or QUIC without the HTTP opening handshake.  Public WebSocket
servers do not expose such a protocol.

## Library boundary

Implement WebSocket in a separate `zws` module and `libZws` library layered on
`libZhttp`:

```text
Ztcp / Ztls / Zquic
          |
       libZhttp
          |
        libZws
          |
 WebSocket applications
```

WebSocket has its own framing, fragmentation, masking, control protocol,
persistent connection state, shutdown semantics, application callbacks,
extensions, and protocol test surface.  These do not belong in the HTTP
library, and HTTP-only applications should not acquire WebSocket code or
dependencies.

The dependency is strictly one-way.  `libZhttp` must not include or refer to
`Zws` types, WebSocket headers, opcodes, close codes, or extension semantics.
Use the protocol-independent facilities that already exist:

- H1 request/response parsers and builders;
- typed H1 TCP/TLS links whose native Rx loop preserves unread
  `ZiRxStream` bytes across `process()` calls;
- H2 capability discovery through `SETTINGS_ENABLE_CONNECT_PROTOCOL`;
- H3 capability discovery through the RFC 9220 setting;
- H2/H3 request/response stream state, callback-scoped `ZiRxLayer` DATA
  delivery, half-close, and reset; and
- the shard-affine `Zhttp::Stream<Link>` facade and separately passed Rx
  layer.

Do not invent a connection-reparenting API for H1 or a second logical-stream
adapter.  Buffer-preserving H2/H3 Rx and RFC 9220 are already owned by
`libZhttp`.

`libZws` owns:

- RFC 6455 opening-handshake generation, policy, and validation;
- WebSocket framing, masking, fragmentation, and message delivery;
- ping, pong, close, limits, and extension negotiation;
- the H1 Upgrade validation that authorizes `link.streamAccept()`;
- the profile-specific opening handshakes and one established-stream binding
  over `Zhttp::Stream<Link>` plus the passed Rx layer;
- generic client and server application contracts and WebSocket-specific
  diagnostics;
- `ws://` and `wss://` URI parsing and default ports, because the current
  `Zhttp::URL` accepts only `http://` and `https://`; and
- opening-handshake nonce and accept computation using existing
  `Ztls::Random`, `Ztls::MD<Ztls::SHA1>`, and `ZuBase64` facilities rather
  than direct OpenSSL calls or another crypto dependency.

The intended compile-time relationship is:

```text
H1 Upgrade / H2 CONNECT / H3 CONNECT
                  |
       Zhttp::Stream<Link> + Rx &
                  |
              Zws codec
```

Do not introduce a virtual or type-erased lowest-common-denominator transport
interface to enforce this library boundary.  Bind the codec to the common
logical stream with templates, preserving native pooled buffers, flow control,
shard ownership, and transport-specific fast paths.

### Two `zhttp` dependent stream families

Keep the two dependent-facing HTTP stream contracts distinct but stylistically
consistent:

1. **HTTP message body streams.**  These represent the entity body of one
   finite request or response.  `Zhttp::BodyRx` is a `ZiRxLayer` passed to the
   application's body callback; H1 body/chunked streams and H2/H3 DATA streams
   are `ZiTxLayer`s passed to body producers/builders.  HTTP framing,
   content-length/chunk completion, and ordinary message completion remain
   owned by `zhttp`.
2. **Upgraded binary logical streams.**  These represent opaque ordered bytes
   after a successful H1 Upgrade or H2/H3 Extended CONNECT.  The generic
   `Zhttp::Stream<Link>` supplies capability, Tx access, graceful end, and
   reset; its Rx layer is passed to `process(stream, rx)`.  Framing and message
   semantics above those bytes belong entirely to the dependent library such
   as `zws`.

Both families therefore layer on `ZiRxLayer`/`ZiTxLayer`, pass Rx downward by
reference, preserve explicit Tx `flush()` boundaries, and keep HTTP/transport
flow control below the dependent application.  Do not force an HTTP body
through the upgraded-stream API or model a persistent upgraded stream as an
unbounded HTTP body.

The upgraded-stream API is deliberately protocol-neutral.  Production
`libZhttp` must contain no WebSocket-specific ALPN token, H1 Upgrade token,
Extended CONNECT `:protocol` value, subprotocol, masking, opcode, close code,
or framing rule.  `zws` supplies and validates `Upgrade: websocket` for H1 and
`:protocol = websocket` for H2/H3.  Generic `zhttp` tests may use arbitrary
identifiers such as `opaque`; those values are test data, not library policy.

## Stream convention and established-stream boundary

Use one convention at every layer:

| Layer | Tx | Rx |
| --- | --- | --- |
| `Ztcp::Link` | caller obtains `txStream()` | link owns `RxStream` and passes it to `Impl::process(RxStream &)` |
| `Ztls::Link` | caller obtains `txStream()` | link owns plaintext `RxStream` and passes it to `Impl::process(RxStream &)` |
| `Zquic::Stream` | caller obtains `txStream()` | stream owns ordered `RxStream` and passes it to `Impl::process(RxStream &)` |
| H2 wire transport | H2 obtains the TLS link's `txStream()` | TLS passes `Ztls::RxStream &` to H2 `process()` |
| ordinary `Zhttp` link | caller obtains `txStream()` | transport passes its concrete Rx stream to the link/application `process()` |
| established `zws` link | caller obtains a WebSocket `txStream()` | `zws` passes a WebSocket `RxLayer &` to the dependent application's `process()` |

The `Zquic::Stream::rxStream()` member is useful to its owning QUIC/H3
implementation for explicit re-entry after FIN/reset notification.  It does
not change the application convention: normal delivery is still
`impl()->process(m_rx)`.  Do not add `rxStream()` accessors to TCP/TLS merely
to make the class surfaces textually identical.

At this plan's entry, `zhttp5.md` has already established:

- `Zhttp::Stream<Link>` is the borrowed logical-link/control facade for
  capability, graceful end, and reset;
- Rx is owned below and passed separately by reference to
  `process(Zhttp::Stream<Link>, Rx &)`;
- callback-scoped Tx access is named `txStream(fn)`, with a concrete
  `ZiTxStream`/`ZiTxLayer`-compatible callback argument; and
- H1, H2, and H3 enter the same `process(stream, rx)` overload through
  `Zhttp::StreamDispatch`.

The Tx callback remains necessary at the `Zhttp::Stream` boundary because the
H2 and H3 DATA `ZiTxLayer`s borrow a lower, method-local Tx object.  Calling
the accessor `txStream(fn)` preserves that lifetime honestly; it must not
return a dangling layer.  A later owning composite may permit a zero-argument
`txStream()`, but is not required for `zws`.

`Zhttp::Stream` is the sole HTTP transport interface used by the established
WebSocket codec.  Do not add H1, H2, or H3 specializations to the codec and do
not unwrap the facade to reach TCP, TLS, H2 DATA, H3 DATA, or QUIC stream
objects.

The logical operations map as follows:

| Operation | Meaning to `zws` | H1 mapping | H2 mapping | H3 mapping |
| --- | --- | --- | --- | --- |
| `localCap()` | this endpoint enabled the required HTTP stream mode | H1 binding called `streamEnable()` | local `SETTINGS_ENABLE_CONNECT_PROTOCOL` | local RFC 9220 setting |
| `peerCap()` | the peer is known to support the stream mode | true after accepted Upgrade | peer `SETTINGS_ENABLE_CONNECT_PROTOCOL` | peer RFC 9220 setting |
| `process(stream, rx)` | callback-scoped ordered byte input | no-copy unread TCP/TLS input layer | pooled H2 DATA layer | pooled H3 DATA layer |
| `txStream(fn)` | callback-scoped ordered byte output | native TCP/TLS Tx stream | H2 DATA `ZiTxLayer` | H3 DATA `ZiTxLayer` |
| `end()` | graceful local logical-stream completion, ordered after queued Tx | drained connection close | END_STREAM | QUIC FIN |
| `reset()` | abort this logical stream | abort the H1 connection | RST_STREAM | RESET_STREAM/STOP_SENDING |

`localCap()` and `peerCap()` are handshake policy facts, not WebSocket state.
For H1, call `streamEnable()` before parsing the opening handshake and
`streamAccept()` only after the Upgrade has been completely validated.  H2
and H3 do not call those H1 methods: their request/response parser calls
`parser.stream()` after a successful capable Extended CONNECT exchange.

The facade and every stream passed through it are borrowed:

- retain the typed HTTP link according to its existing ownership rules, not a
  `Zhttp::Stream` or its Rx/Tx objects;
- consume or copy every offered Rx span before `process()` returns;
- perform all Tx writes inside `txStream(fn)` and explicitly call `flush()` at
  each complete WebSocket frame/message latency boundary;
- never infer socket-send completion from return from `txStream(fn)`; and
- call `end()` or `reset()` once and wait for the normal link/application
  disconnect path before releasing ownership.

Use the existing dispatch adapter; do not duplicate it in `zws`:

- H1 `Zhttp::ClientLink`/`ServerLink` owns `H1::StreamBinding`, which changes
  receive mode after `streamAccept()` and invokes the dependent
  `process(stream, rx)`;
- an H2/H3 handshake parser owns
  `Zhttp::StreamDispatch<Link, ZwsCodec>`, initializes it with the logical
  link and codec before calling `parser.stream()`, and forwards its parser Rx
  callback to `dispatch.process(rx)`; and
- on terminal teardown, first prevent another parser dispatch with
  `dispatch.disable_()`, drain the owning link/parser work, and invoke
  `dispatch.final_()` only from that drained lifecycle point.

`StreamDispatch` is only two borrowed back-pointers and adds neither a queue
nor allocation.  It does not own or prolong the HTTP link or `zws` codec.
`StreamDispatch::process()` returns the dependent's `int` result and maps a
negative result to `Stream::reset()` for that logical stream.  When the
WebSocket peer can still be notified, the codec instead sends and flushes the
RFC close status, calls `Stream::end()`, and returns non-negative progress so
that the queued close frame drains before transport end.  Return negative only
when orderly close cannot be initiated; the codec must not also call
`reset()` from that callback.  Zero and positive results do not alter
transport flow control.

The `zws` codec implements that normalized HTTP-dependent callback.  It
consumes WebSocket framing from the outer Rx stream and drives an inner
`Zws::RxLayer`, which is a `ZiRxLayer` by construction:

```c++
template <typename HttpStream, typename HttpRx>
int process(HttpStream stream, HttpRx &httpRx)
{
  // Decode framing/control bytes from httpRx and expose only message payload
  // through m_rxLayer.
  return app()->process(*impl(), m_rxLayer);
}
```

The final `zws`-dependent application therefore follows the same contract as
the lower layers:

```c++
int process(Link &link, auto &rx)       // rx is Zws::RxLayer
{
  while (rx.input()) {
    auto events = rx.events();
    if (events & Zi::RxEvent::Error()) return -1;
    if (rx.consume(frame, consume) <= 0) break;
  }
  return rx.failed() ? -1 : 1;
}

link.txStream([&](auto &tx) {            // tx is Zws::TxLayer
  tx << payload;
  tx.flush();                            // message/latency boundary
});
```

There is no `streamProcess()` or `rxStream()` accessor in the
`zws`-dependent application contract.  Rx is passed to `process()` by
reference.  `RxLayer` and `TxLayer` are the correct type names because they
layer WebSocket framing over the HTTP logical Rx/Tx streams; reserve
`RxStream`/`TxStream` for owning/base stream implementations.

The outer HTTP Rx events describe the logical stream: `Start` means the HTTP
logical stream has started, `Input` means ordered bytes are available,
`Final` means clean peer end, and `Error` means reset or transport failure.
The inner WebSocket `RxLayer` events describe WebSocket message boundaries.
Do not forward outer events as inner events.  A clean outer `Final` is valid
only after the WebSocket close rules have been satisfied; otherwise it is an
abnormal WebSocket termination.

## Delivery sequence

### Entry gate

Begin only after `zhttp5.md` is complete and its normalized logical-stream
API, four-profile tests, and `GUIDELINES.md` audit are green.  This plan does
not modify `ztcp`, `ztls`, `zquic`, or `zhttp`.  If implementation appears to
require such a change, first demonstrate a defect or missing generic contract
with a focused failing test and revise the owning lower-layer plan explicitly;
do not hide transport work in `zws`.

1. Replace the existing direct-`Ztls` `zws` prototype with the
   transport-independent codec, layered Rx/Tx streams, handshake utilities,
   URI parser, and deterministic codec tests.
2. Add H1 client bindings over `Zhttp::H1TCP` and `Zhttp::H1TLS`; validate
   the opening response and delegate the mode switch to `streamAccept()`.
3. Add the corresponding H1 server links and opening-handshake policy.
4. Add focused lifecycle tests, generic client/server examples, and public-API
   integration clients, including reconnect and ping/pong behavior.
5. Add the WebSocket-over-H2 binding using
   `H2Config::extendedConnect(true)` and the common stream facade.
6. Add the WebSocket-over-H3 binding using
   `QUICConfig::extendedConnect(true)` and the same stream facade.
7. Run the common codec/lifecycle corpus over all four profiles, then add
   opt-in public-provider integration clients.

The H1 binding remains the public-API compatibility baseline.  HTTP/2 support
alone does not enable WebSocket: the peer must also advertise Extended CONNECT
support.  The same is true for H3: ALPN `h3` is not proof of RFC 9220
capability.  With `zhttp5.md` complete, H2/H3 have no remaining `libZhttp`
functionality prerequisite.

## Engine and link integration

Compose the bindings from the existing typed engines and links:

- H1 client: `Zhttp::Client<App, H1TCP/H1TLS>` plus
  `Zhttp::ClientLink<App, Link, Profile>`;
- H1 server: `Zhttp::Server<App, H1TCP/H1TLS>` plus
  `Zhttp::ServerLink<App, Link, Profile, Session>`, with a Zws session that
  owns handshake validation and the established WebSocket codec;
- H2: the existing `Zhttp::Client/Server<..., H2TLS>` specializations and
  their logical links; and
- H3: the existing `Zhttp::Client/Server<..., H3QUIC>` specializations and
  their logical request-stream links.

Keep one templated binding shape: `connected(link, info)`,
`process(stream, rx)`, `txStream(fn)`, WebSocket message delivery, and
`disconnected(link, peer)`.  The binding's HTTP-facing `process()` converts
the outer byte stream into the WebSocket `RxLayer` passed to the final
application's `process(link, rx)`.  Profile-specific opening-handshake code
calls `streamAccept()` for H1 or transitions the H2/H3 parser with
`parser.stream()` after Extended CONNECT succeeds; after that transition the
WebSocket codec receives only `Zhttp::Stream<Link>` plus the separately
passed Rx layer.  Concrete link types differ by profile; established
application control flow and codec behavior do not.  Do not add virtual
engines, virtual links, or a run-time transport switch.

Follow the current engine lifecycle exactly:
`init()` -> `start()` -> process links -> stop accepting ->
`stop(done)` -> `final()`.  A stop request is asynchronous; only its
continuation proves that ingress, links, and pending Rx/Tx work have drained.
Use `Zhttp::Engines` when one endpoint owns several profile engines.  The
blocking wrappers are for the main/control thread only.

## Initial HTTP/1 bindings

The initial client `wss://` connection path is:

```text
resolve
  -> TCP connect
  -> TLS handshake through Zhttp::H1TLS (ALPN http/1.1)
  -> RFC 6455 HTTP/1.1 opening handshake
  -> validate the 101 response
  -> switch the existing link's Rx mode to WebSocket
  -> exchange WebSocket frames
```

Use `Zhttp::H1TLS` with `Zhttp::TLSConfig`; its current transport traits
advertise only `http/1.1`.  Do not use the mixed H1/H2 TLS engine for this
binding.  Negotiating `h2` does not imply RFC 8441 support and prevents use of
the HTTP/1 Upgrade handshake.

The `ws://` path is the same without TLS.  The server bindings accept the
corresponding HTTP/1 Upgrade request, validate it, return the `101` response,
and switch the same H1 link to the server-side WebSocket state machine.
Client links are persistent objects owning transient native connections;
server links remain transient objects owned by their native connections.
Client and server bindings share the codec but retain their distinct masking
and handshake validation rules.

The opening handshake must generate and validate the RFC 6455 headers,
including `Upgrade`, `Connection`, `Sec-WebSocket-Key`,
`Sec-WebSocket-Version`, and `Sec-WebSocket-Accept`.  Support optional
subprotocol negotiation without making it mandatory for APIs that do not use
it.  Extension negotiation, including per-message compression, is separate
policy and should not complicate the initial uncompressed data path.  Validate
HTTP version, status/method, case-insensitive token lists, unique singleton
fields, the version value, the selected subprotocol, and the absence of an
unexpected HTTP body before changing modes.

Build the handshake with `Zhttp::H1::Builder` and parse it with
`Zhttp::H1::Parser` configured for the required WebSocket headers.  The
current response parser already treats `101` as final rather than interim.
Generate exactly 16 random nonce bytes with `Ztls::Random`, encode them with
`ZuBase64`, and compute the accept value as the base64 encoding of
the digest produced by updating `Ztls::MD<Ztls::SHA1>` with the key and then
the RFC 6455 GUID.  Initialize the `Ztls` backend once during library/engine
initialization.  A server must reject a key that does not decode to exactly 16
bytes.

Use a dedicated handshake session rather than the normal
`Zhttp::ClientMessage`/`Zhttp::ServerSession` request-lifecycle adapters.  In
particular, `Zhttp::H1::Server::process()` unconditionally resets its parser
after `request()` returns; a takeover result must switch the link mode and
bypass all subsequent HTTP request dispatch.

Before parsing the handshake, call the H1 link's `streamEnable()`.  On
success, emit and explicitly flush the final opening-handshake bytes, then
call `streamAccept()`.  `Zhttp::H1::StreamBinding` changes its Rx-owned mode
and immediately calls `process(stream, rx)` with `Start`, plus `Input` when
bytes remain after the headers in the same native `ZiRxStream`.  Do not
dispatch another HTTP message, copy pending bytes, replace the native
connection, or introduce another receive queue.  A rejected handshake remains
in HTTP mode until the binding sends the rejection and closes through the
normal link lifecycle.

## WebSocket core

The transport-independent core owns:

- client masking and efficient in-place mask application;
- frame header parsing and generation;
- text and binary messages;
- incremental UTF-8 validation for text messages;
- fragmented messages and continuation frames;
- ping, pong, and close control frames;
- protocol validation and close status reporting;
- partial input and output across arbitrary I/O buffer boundaries;
- message and connection size limits supplied by configuration;
- orderly shutdown and transport-failure propagation.

Reject non-canonical payload-length encodings, invalid or reserved opcodes,
unexpected RSV bits, fragmented control frames, control payloads over 125
bytes, invalid continuation sequences, a one-byte close payload, invalid close
codes, and invalid UTF-8 in text messages or close reasons.  Perform all
length arithmetic with overflow-safe checks before comparing configured
limits or allocating storage.

Keep frame processing incremental.  Parse directly from pooled receive buffers
and write headers and payloads directly into pooled transmit buffers.  Avoid
temporary contiguous payloads, mandatory message assembly, and heap allocation
per frame.  The codec owns its parser/refill state and presents each logical
message through an inner `ZiRxLayer`, backed directly by the current
outer Rx span passed to `process(stream, rx)` regardless of profile.
Applications requiring a complete contiguous message may explicitly copy
from that layer into owned storage; the protocol layer must not do so
implicitly.

The wire decoder owns framing state and implements the inner `ZiRxLayer`
refill contract with `Zi::RxRefill`.  The layer exposes payload bytes only: it
must not expose WebSocket headers, interleaved control frames, or bytes from
the following message.  Refill parses continuation headers and handles
interleaved control frames, allowing application consumption to cross
WebSocket fragmentation transparently.  At each message callback the
application reads and clears this inner layer's `Zi::RxEvent` mask:
`Start()` identifies a new WebSocket message, `Input()` identifies newly
offered payload, `Final()` identifies the consumed FIN-bearing message end,
and `Error()` is terminal for that connection.  A fragmented text message
retains one incremental UTF-8 validator across all invocations.

Use the outer stream without gathering payload.  Strip frame headers, unmask
client input in place on a server, bound application consumption to the
current data frame, and resume framing when that payload is consumed.  Parse
and act on interleaved control frames before resuming the same logical data
message.  Do not build a contiguous message or allocate one wrapper object per
payload span.  This exact codec path applies to H1, H2, and H3; the only
difference is the concrete outer Rx layer passed by `zhttp`.

For every profile, consume the callback-scoped pooled outer Rx view in
`process(stream, rx)`.  Drive `rx` with `input()` followed by the
read-and-clear `events()`, then consume accepted input in place.  The
WebSocket decoder must fully account for each offered region before
returning: payload is consumed by the application, while partial framing
bytes are consumed into fixed parser state.

Rx frame/message/parser state is Rx-owned.  Tx construction occurs
synchronously inside `Zhttp::Stream::txStream(fn)`: H1 supplies its native
pooled Tx stream, H2 supplies its DATA layer, and H3 supplies
`H3::dataStream()` over the selected QUIC request stream.  DATA framing,
scheduling, and flow control remain below `libZws`.  Do not access Rx state
while constructing Tx frames.  A control response originating on Rx must
snapshot its RFC-bounded payload and enqueue Tx work; it must not reach into
Tx-owned state or make either shard wait.

Client payloads must be masked on transmission with a fresh unpredictable
32-bit key per frame.  Structure the output path so masking occurs in place
before the existing TCP send or in-place TLS record protection.  Track the
mask offset across receive and transmit spans.  Reject masked server-to-client
frames and unmasked client-to-server frames.

### Message streams

#### Receive

Treat a complete unfragmented message contained in one native input buffer as
the primary receive fast path.  On the Rx owner:

- parse and remove the frame header in place;
- unmask client-to-server input in place for a server binding;
- present the payload as a bounded `ZiRxLayer` message to the application;
- let the application consume it with normal `input()`/`consume()` calls;
- complete delivery without allocating a message object, gathering the
  payload, copying it, or scheduling work onto a non-owning shard.

The message layer is a synchronous borrowed view.  It remains valid only for
the application callback, and the application must consume or copy the
offered payload before returning.  For a fragmented message, later callbacks
continue the same logical message until the FIN-bearing continuation frame
has been consumed.  `Zi::RxEvent` supplies message start, more input, final,
and error state without exposing WebSocket frame boundaries.  Reset the layer
only when beginning the next logical message; never reset it between
continuation frames.

#### Transmit

Expose one callback-scoped `txStream(fn)` operation over
`Zhttp::Stream::txStream(fn)` for all bindings.  Inside the lower callback
construct a message-scoped `Zws::TxLayer` (`ZiTxLayer`) and pass it to the
application callback.  All concrete streams are borrowed and must not be
retained.  Application writes append payload to the current pooled `ZiIOBuf`;
ordinary rollover emits a full non-final WebSocket fragment.  Explicitly call
the WebSocket layer's `tx.flush()` after each complete WebSocket
message/frame production boundary.  That flush finalizes the WebSocket
FIN/header/mask and propagates a flush to the HTTP stream.  Neither facade nor
either callback adds a flush.  Scope-exit destruction only flushes residual
output and must not be the latency or message-boundary mechanism.

The first emitted frame uses the selected text or binary opcode.  Every later
buffer belonging to that message uses the continuation opcode.  Rollover
frames have FIN clear; the frame emitted by message-ending flush has FIN set.
An empty message must still emit one zero-length FIN frame.  A message whose
payload exactly fills one buffer must remain pending until flush so it is
emitted once with FIN set rather than as a non-final frame followed by an
empty continuation.  Track “message begun” separately from buffered payload;
an empty-message flush must allocate/finalize a WebSocket header before the
lower stream's normal empty-buffer filtering.

Use the existing Tx boundary signal: rollover arrives at
`prepareBuf_(buf, false)` and message-ending flush arrives with `final = true`.
The WebSocket `ZiTxLayer` uses that signal to prepare each buffer with the
correct opcode and FIN bit.  Destruction may flush an active message, but must
not create an unsolicited empty message when no message was begun.

`ZiTxStream::flush()` intentionally filters an absent/empty buffer, so the
WebSocket binding must implement the RFC-required empty-message case
explicitly.  Do this in `libZws` by allocating and submitting one header-only
FIN frame through the same lower stream; do not change foundation empty-buffer
filtering or synthesize an empty continuation after an exact-capacity payload.

Ping, pong, and close use the same buffer-finalization machinery but remain
single-frame control messages.  Reject a control payload that would roll over
the stream rather than fragmenting it.

Reserve the maximum WebSocket header headroom for every buffer in the message
stream, in addition to lower-layer headroom and tailroom.  At send time the
payload length is known, so prepend the RFC-mandated minimal 2-, 4-, or
10-byte server header, or 6-, 8-, or 14-byte client header, without moving the
payload.  Unused reserved headroom remains outside the active span.  Lower
layers prepend their own headers from that active span and preserve their
tailroom.

The existing composed-headroom contract permits the unused reserved prefix to
pass through TCP, TLS, H2/H3 DATA framing, and nested Tx layers without moving
the payload.  Apply a fresh unpredictable client mask key to each emitted
fragment and mask its payload in place before the same buffer continues
through H1 TCP/TLS or the H2/H3 DATA layer.

The WebSocket layer must not add a coalescing timer or implicit delay.
Application writes are batched only within the current message stream, and
flush is the precise latency and message-boundary control.  Buffer sizing
remains transport policy; do not encode a trading-specific message size or
capacity in the generic library.

Compression is opt-in policy.  Per-message compression can add state,
allocation, copying, and latency disproportionate to small messages, so the
uncompressed path must remain independent and minimal.

### Close, teardown, and receive pressure

Keep WebSocket close-handshake state and its deadline timer on Rx.  Public
`close()` dispatches to Rx, validates and snapshots the optional status/reason,
and enqueues one Tx close frame.  A peer close is parsed on Rx; if no close was
sent, enqueue the bounded echo response before transport teardown.  Stop
delivering data messages after the close state begins while still processing
the control exchange required by RFC 6455.

Do not treat `txRun()` completion or return from `Stream::txStream(fn)` as
socket-send completion.  Send and explicitly flush the close frame through
`txStream(fn)`.  Once the WebSocket close state permits transport closure
(normally after both close frames, or immediately after flushing the required
peer-close response), call `Stream::end()`.  `libZhttp` orders the end behind
queued Tx and maps it to a drained H1 connection close, H2 END_STREAM, or H3
FIN.  Do not reach below `Zhttp::Stream` for `ZiTx::sent`, native
`disconnect()`, RST_STREAM, or QUIC FIN merely to implement ordinary
WebSocket closure.

On a protocol error detected inside `process(stream, rx)`, send and explicitly
flush the appropriate RFC close code, call `Stream::end()`, and return
non-negative progress so the close frame is ordered ahead of transport end.
If the close frame cannot be produced, return a negative value and let
`StreamDispatch` reset the stream exactly once.  On a timeout or other
asynchronous failure outside that callback, call `Stream::reset()` directly.
This aborts the H1 connection but only the affected logical stream for H2/H3.
Never disconnect the shared H2 session or QUIC connection merely to close one
WebSocket.  Treat the outer stream's
`Zi::RxEvent::Final()` and `Error()` as mutually exclusive, exact-once peer
terminal events.  Interpret them through WebSocket close state: a `Final`
without a valid close exchange is still an abnormal WebSocket closure.

All bindings follow the repository teardown order: prevent new ingress,
timer, and I/O work and post an Rx continuation; from that continuation post a
Tx continuation; from the Tx continuation complete teardown and release
ownership.  Each continuation drains work already queued on its owning shard.
Do not block, poll, or infer completion from a `run()`/`invoke()` call.

The current `ZiRxLayer` contract is synchronous.  Consume or copy all payload
offered in the callback before returning.  Enforce frame, message, and
queued-input limits and fail the connection deterministically on overload.

## Endpoint and application policy

Connections remain endpoint-driven.  Parse `ws://` and `wss://` in `libZws`
with default ports 80 and 443, retaining the authority and request target for
the opening handshake.  Do not pass these schemes directly to the current
`Zhttp::URL` parser.  An H1 endpoint selects `Zhttp::H1TCP` or
`Zhttp::H1TLS`; explicitly configured H2/H3 endpoints use Extended CONNECT
only after capability is known.  Do not probe HTTP/2 or HTTP/3 before H1 for a
latency-sensitive connection when the provider has not documented support.
Reject fragments and userinfo, normalize an empty path to `/`, preserve the
query, format IPv6 authorities correctly, and retain whether the port was
explicit when generating `Host`.

Provide generic policy hooks for:

- reconnect and backoff after transport failure or server-directed closure;
- connection and handshake deadlines;
- ping scheduling, pong deadlines, and idle detection;
- maximum inbound message size and receive-buffer pressure;
- actions to run when the connection becomes ready or reconnects.

Provider-specific connection replacement, subscription replay,
authentication, rate limits, and message formats are application policies
layered above the WebSocket protocol state machine.  Binance-specific JSON,
SBE, authentication, subscriptions, limits, and connection-lifetime rules do
not belong in the generic WebSocket library.

The `Ztcp`/`Ztls` `reconnFreq()` hook retries transient
connection-establishment failures only.  Reconnecting an established
WebSocket after EOF, reset, close, or timeout remains endpoint/application
policy and must schedule a new `connect()` after the old link's disconnect
callback has completed.

## HTTP/2 and HTTP/3 bindings

The H2 binding may use RFC 8441 only after the peer advertises
`SETTINGS_ENABLE_CONNECT_PROTOCOL`.  Configure the local setting with
`H2Config::extendedConnect(true)` and query the peer through
`Zhttp::Stream{link}.peerCap()` before the request builder emits
`:protocol = websocket`.  On the server, initialize the H2 parser's Extended
CONNECT semantics from `Zhttp::Stream{link}.localCap()`.  The server parser
calls `parser.stream()` only after validating a capable Extended CONNECT
request with an open request body; the client parser calls it only after
validating the successful non-final 2xx response.  Initialize the
`StreamDispatch` before either transition.  Subsequent ordered input, remote
half-close, and reset use the common pooled stream contract.  Follow RFC 8441
handshake fields: do not emit H1 `Connection`, `Upgrade`, `Sec-WebSocket-Key`,
or `Sec-WebSocket-Accept` fields on H2.

Pass the pooled callback-scoped H2 Rx view to
`process(Zhttp::Stream{link}, rx)` without another queue.  On Tx,
`Stream::txStream(fn)` supplies the native pooled H2 DATA layer only for the
callback.  Explicitly flush each complete WebSocket message before returning;
neither facade nor body stream may be retained.  `end()` is a local
HTTP-stream half-close and `reset()` resets the logical link.  HTTP settings,
HPACK, flow control, stream scheduling, and connection shutdown remain in
`libZhttp`.

The H3 binding uses the identical application contract.  Enable local RFC
9220 support with `QUICConfig::extendedConnect(true)` and require
`Stream::peerCap()` before emitting `:protocol = websocket`; ALPN `h3` alone
is insufficient.  Initialize server request semantics from
`Stream::localCap()`.  The current H3 parser delivers ordered DATA through
the same `StreamDispatch`, maps QUIC FIN to the outer
`Zi::RxEvent::Final()`, and maps stream reset/stop to the outer
`Zi::RxEvent::Error()`.  QPACK, H3 framing, QUIC flow control, migration, and
stream scheduling remain in `libZhttp`/`libZquic`.

Prefer an already established capable H2/H3 connection over opening one
solely to attempt WebSocket Extended CONNECT.  Capability results may be
cached per origin with bounded lifetime, but failure must fall back cleanly to
a new H1 WebSocket connection unless endpoint policy forbids fallback.  Do
not attempt to reuse a failed H2/H3 physical session as an H1 connection.

WebSocket over one HTTP/3 stream remains reliable and ordered.  It gains QUIC
connection reuse, migration, and isolation from loss on other streams, but it
does not provide unordered messages, multiple application streams, or
unreliable datagrams.  Do not expose those properties through the WebSocket
API.

## Verification

Add standalone protocol tests covering:

- one common established-codec fixture instantiated over H1 TCP, H1 TLS, H2
  TLS, and H3 QUIC, with no profile branch inside the codec;
- exact outer-stream `Start`/`Input`/`Final`/`Error` handling, including proof
  that those events are not exposed as WebSocket message boundaries;
- `StreamDispatch` bind/disable/final ordering and no callback after terminal
  teardown, plus exactly one logical-stream reset after a negative dependent
  return;
- `ws://`/`wss://` parsing, default and explicit ports, IPv6 authority,
  query preservation, and rejected URI forms;
- opening-handshake success and every mandatory validation failure;
- partial and coalesced handshake input, including WebSocket bytes received
  with the final HTTP response bytes;
- H1 TLS negotiation restricted to `http/1.1`;
- all payload-length encodings and masked/unmasked direction rules;
- Tx flush at zero length, exact buffer capacity, and either side of every
  payload-length encoding boundary;
- exactly one prompt lower-layer flush per explicit WebSocket message flush,
  no facade-generated flush, and `end()` ordered behind the last flush;
- automatic Tx rollover with first/continuation opcodes, FIN only on flush,
  one mask key per fragment, and preserved nested headroom/tailroom;
- Rx message-stream consumption across frames and buffers, including
  fragmented data with interleaved control frames;
- proof that an Rx message stream cannot consume a control frame, framing
  bytes, or bytes from the following message;
- partial frame headers and payloads across I/O buffers;
- ping/pong payload reflection and timeout policy;
- send completion ordering for a locally or remotely initiated close;
- normal close, simultaneous close, malformed close, EOF, reset, and timeout;
- H1 drained-close, H2 END_STREAM/RST_STREAM, and H3 FIN/RESET mappings
  exercised only through `Stream::end()`/`reset()`;
- configured frame/message/queued-input limits and deterministic overload;
- reconnect without leaked links, timers, buffers, or subscriptions.

Add a source-boundary test that rejects references from the established codec
to `Ztcp`, `Ztls`, `Zquic`, H2/H3 frame types, HPACK/QPACK, native
`disconnect()`, or transport-specific flow-control APIs.  Profile-specific
opening-handshake files may name their HTTP parser/builder and configuration;
the codec and message streams may name only `Zhttp::Stream`,
`Zhttp::StreamDispatch`, and protocol-neutral `Zi` layers.

Run Autobahn WebSocket protocol tests against both H1 client and server
implementations in addition to in-tree deterministic tests.  Add an opt-in
Binance integration test that connects, subscribes to a public market-data
stream, validates ping/pong behavior, receives messages, and disconnects
cleanly.  It must not be part of an offline default test run.

Add small-message benchmarks and allocation/copy instrumentation covering
plain and TLS connections, representative text and binary payload sizes,
ping/pong, and bidirectional traffic.  The unfragmented in-buffer fast path
must demonstrate no allocation beyond the existing pooled transport buffer and
no payload copy.  Track latency distributions rather than reporting
throughput alone, and report caller-thread `txStream()` and on-Tx
`txStream_()` paths separately.

Run the same codec and lifecycle corpus over H1 TCP, H1 TLS, H2 TLS, and H3
QUIC.  Binding-specific tests should concentrate on capability negotiation,
fallback, flow control, stream reset, QUIC FIN/RESET/STOP mapping, and proving
that one WebSocket cannot close or corrupt a shared HTTP connection.
