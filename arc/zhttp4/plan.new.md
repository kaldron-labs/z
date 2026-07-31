## Summary
The goal is to make `zhttp` a reusable HTTP messaging and transport framework
for both client and server applications. `zhttpclient` remains the immediate
proving ground, but the library API must not assume that every application is a
client, that every inbound message is a response, or that every outbound
message is a request.

Applications should focus on transport-agnostic message concerns:

- building requests;
- parsing requests;
- building responses;
- parsing responses;
- handling headers, bodies, completion, and application policy.

`zhttp` should own protocol and transport mechanics:

- HTTP/1 parsing, framing, chunking, and connection-close completion;
- HTTP/3 frame parsing and emission on request streams;
- HTTP/3 control stream parsing and SETTINGS/GOAWAY state;
- QPACK encoder and decoder stream wiring;
- QPACK dynamic table ownership and instruction processing;
- request/response stream routing;
- reusable TCP/TLS/QUIC link/session glue.

The current codebase already has a useful low-level split:

- `zhttp/src/ZhttpHPack.hh` and `zhttp/src/ZhttpHPack.cc`: HPACK Huffman
  support used by QPACK and H3 field parsing.
- `zhttp/src/ZhttpQPack.hh` and `zhttp/src/ZhttpQPack.cc`: QPACK static table,
  dynamic table state, field section encode/decode, and encoder/decoder
  instruction streams.
- `zhttp/src/ZhttpUtil.hh`: HTTP parser utilities shared by higher layers.
- `zhttp/src/Zhttp.hh`: public aggregate header containing HTTP message APIs
  plus both HTTP/1 and HTTP/3 protocol machinery.
- `zhttp/example/zhttpclient.cc`: working client example, currently mixing
  application policy, message callbacks, connection/link plumbing, HTTP/3
  control stream creation, QPACK stream writers, fallback, DNS probing,
  redirects, logging, and file I/O.
- `zhttp/test/Zhttp3InteropTest.cc`: already has both client and server H3
  plumbing, including duplicated local control/QPACK stream setup, peer stream
  uniqueness checks, and QPACK table ownership.

The revised design preserves the low-level codec split, first moves the
existing H1/H3 message code out of `Zhttp.hh` without behavior change, then
extracts reusable session code by vertical slices:

1. behavior-preserving header split;
2. role-neutral request/response message facades over existing builders and
   parsers;
3. reusable H3 connection bootstrap and QPACK stream handling used by both the
   example client and the existing H3 interop server/client test;
4. reusable H3 request/response stream routing and message send/parse paths;
5. reusable H1 TCP/TLS request/response session handling;
6. `zhttpclient` reduction to client policy and a maintained `zhttpserver`
   example proving the server role.

Research findings from comparable implementations support this split:

- nghttp3 uses a per-HTTP/3-connection object and keeps QPACK operations behind
  the HTTP/3 layer, while the application provides stream I/O and callbacks.
  See https://nghttp2.org/nghttp3/programmers-guide.html.
- nghttp3's QPACK guide shows that encoding can produce request-stream bytes
  and encoder-stream bytes, while decoding request-stream bytes may need
  connection-owned decoder state and encoder-stream progress. See
  https://nghttp2.org/nghttp3/qpack-howto.html.
- quiche exposes QUIC as low-level stream I/O and HTTP/3 as a higher-level
  request/response API on top of it. See https://docs.rs/quiche/latest/quiche/.
- lsquic separates engine role selection from HTTP functionality; HTTP mode is
  enabled at engine level and then stream callbacks carry application data. See
  https://lsquic.readthedocs.io/en/latest/apiref.html.
- RFC 9204 requires at most one encoder stream and one decoder stream per
  endpoint and treats duplicate or closed QPACK streams as connection errors.
  See https://datatracker.ietf.org/doc/rfc9204/.

## Architecture Documentation

### Layer 1: Codec and Protocol Primitives
Owned by existing low-level headers:

- `ZhttpHPack.hh`
- `ZhttpQPack.hh`
- `ZhttpUtil.hh`

These remain independent protocol primitives. They must not know about TCP,
TLS, QUIC, DNS, redirects, files, CLI options, authentication, routing, or
application policy.

Actual API behavior reviewed:

- `Zhttp::H3::Params` defaults to `maxHeaderListSize = 1<<16`,
  `qpackTableCapacity = 0`, and `qpackBlockedStreams = 0`. This matches the
  current conservative profile documented in `zhttp/README.md`.
- `Zhttp::H3::QPackTxTable` is connection-affine Tx state. Its own comment says
  callers must serialize access from the owning transmit path. Session code
  must keep QPACK table mutation on the same link/Tx path that emits encoder
  stream bytes.
- `Zhttp::H3::QPackEncoderTx` is currently a virtual write boundary. The
  selected plan is to retain it as an internal session bridge for the current
  builder/parser contracts, while removing direct `QPackEncoderTx` wiring from
  application and test policy code.
- `Zhttp::H3::QPack::decodeFieldSection()` uses `QPackRxTable` when provided to
  derive insert count and max capacity. Non-zero required insert count without
  a table is rejected by `H3::Parser`.
- `Zhttp::H3::Builder` uses dynamic QPACK only if `qpackTx()` is non-null and
  the peer advertised capacity. It writes encoder-stream bytes through
  `qpackEncoderTx()` and commits table mutations only after successful emission.

### Layer 2: Transport-Agnostic Message Layer
Owned by `Zhttp.hh` as the public application-facing aggregate, plus new
protocol-specific headers included by it:

- `ZhttpHeaders(...)`
- `Method`, parser states, connection states, and HTTP enums.
- CRTP message parser and builder contracts.
- Common request/response message callback vocabulary.
- Inclusion of protocol-specific message headers:
  - `ZhttpH1.hh`
  - `ZhttpH3.hh`

`Zhttp.hh` should be the header applications include when they want to build or
parse HTTP messages without caring whether the underlying transport is HTTP/1
over TCP/TLS or HTTP/3 over QUIC.

Application callbacks should be message-shaped and role-neutral.

Inbound message parser callbacks:

```c++
void reset();
void operation(Method::T method, ZuBSpan path); // request
void status(unsigned);                          // response
template <typename Key> void header(ZuBSpan value);
template <typename Key, typename Value> void header();
void contentLength(uint64_t);
void xferCompression(XferCompression::T);       // H1 transfer-encoding only
void chunked();                                 // H1 transfer-encoding only
void body(ZuBSpan);
void complete(ParserState::T);
```

Outbound message builder callbacks:

```c++
void reset();
bool request();
template <typename L> void operation(L &&l);
template <typename L> void host(L &&l);
unsigned status();
template <typename L> void reason(L &&l);
template <typename Key, typename L> void header(L &&l);
uint64_t contentLength();
void complete(ParserState::T);
```

For applications, `complete()` means end of message, not end of transport
stream. HTTP/3 stream lifecycle, QPACK section acknowledgements, RESET_STREAM,
STOP_SENDING, and critical unidirectional stream errors are session-layer
details surfaced as structured completion/failure state.

The concrete low-level CRTP builders may continue using the current efficient
callback shape, such as `operation(L &&)`, `host(L &&)`, and
`header<Key>(L &&)`. The higher-level application contract should remain
message-shaped and symmetric across client/server roles.

### Layer 3: Protocol-Specific Message Implementations
Split current namespace bodies out of `Zhttp.hh`:

- `zhttp/src/ZhttpH1.hh`
  - `Zhttp::H1::ParserState`
  - `Zhttp::H1::Parser`
  - `Zhttp::H1::BodyStream`
  - `Zhttp::H1::ChunkedStream`
  - `Zhttp::H1::Builder`
  - HTTP/1 chunking and content-length handling.
- `zhttp/src/ZhttpH3.hh`
  - `Zhttp::H3::CxnState`
  - `Zhttp::H3::ParserState`
  - `Zhttp::H3::FrameState`
  - `Zhttp::H3::CxnStreamState`
  - `Zhttp::H3::CxnParser`
  - `Zhttp::H3::Parser`
  - `Zhttp::H3::DataStream`
  - `Zhttp::H3::Builder`
  - H3 varint/frame helpers currently in `Zhttp.hh`.

This keeps the public API stable while making ownership clear. `Zhttp.hh`
becomes an aggregate over `ZhttpUtil.hh`, `ZhttpHPack.hh`, `ZhttpQPack.hh`,
`ZhttpH1.hh`, and `ZhttpH3.hh`.

### Layer 4: Transport Session Layer
Add reusable transport/session adapters after the message split is complete.
These adapters should be role-neutral where possible and specialized by CRTP
policy where client and server behavior differs.

HTTP/3 session mechanics:

- local control stream creation;
- local QPACK encoder and decoder stream creation;
- peer control/QPACK stream uniqueness checks;
- `CxnParser` dispatch for unidirectional streams;
- request/response stream routing;
- QPACK Rx/Tx table ownership;
- message emission through `H3::Builder`;
- message parsing through `H3::Parser`;
- H3 stream completion and cancellation mapping to message completion/failure.

HTTP/1 session mechanics:

- TCP/TLS connection callbacks;
- request or response stream creation/use;
- message emission through `H1::Builder`;
- message parsing through `H1::Parser`;
- HTTP/1 connection close, content-length, and chunked completion behavior;
- error and disconnect propagation.

Transport adapters may live in `ZhttpH1.hh` and `ZhttpH3.hh` if template only.
If the implementation becomes large, add `ZhttpH1Session.hh` and
`ZhttpH3Session.hh` rather than bloating the message headers. If non-template
implementation is introduced, add corresponding `.cc` files and update
`zhttp/src/Makefile.am`.

The H3 session layer may depend on `Zquic`. The H1 session layer may depend on
`Ztcp` and `Ztls`. Keep those dependencies out of `ZhttpHPack.hh`,
`ZhttpQPack.hh`, `ZhttpUtil.hh`, and out of the core codec portions of
`ZhttpH1.hh` and `ZhttpH3.hh`.

Actual transport API behavior reviewed:

- `Zquic::Link::stream(Zi::StreamType::Simplex)` opens local unidirectional
  streams subject to peer stream limits and can return `nullptr` when blocked.
  H3 bootstrap must handle failure and allow the application/session to retry
  or fail cleanly.
- `Zquic::CliLink::send()` and `Zquic::SrvLink::send()` marshal payloads to the
  Tx thread when necessary; `send_()` requires Tx-thread invocation. H3 session
  code should use public `send()` unless already inside a documented Tx path.
- `Zquic::Stream::txStream()` is app-thread safe and marshals buffers; 
  `txStream_()` requires Tx-thread invocation. Existing test code uses
  `txInvoke()` for H3 response/request builders that write directly to
  `txStream_()`.
- `Ztcp` and `Ztls` expose a single duplex stream through `stream()`, while
  simplex streams are not supported. H1 session templates should not pretend
  there are multiple independent HTTP/1 streams on one link in the first
  extraction.

### Layer 5: Application Layer
The application layer is any HTTP application, not specifically a client.
Examples include:

- command-line clients;
- HTTP servers;
- proxies and gateways;
- streaming APIs;
- RPC-style services;
- file transfer tools;
- protocol test tools;
- higher-level REST frameworks.

Application-owned concerns:

- request/response payload semantics;
- selected request and response headers;
- body source/sink policy;
- URL, route, redirect, or service policy;
- authentication and authorization policy;
- logging and metrics policy;
- CLI/configuration;
- lifecycle decisions after message completion/failure;
- `ZiMultiplex` configuration;
- `Ztls` CA/certificate configuration;
- Alt-Svc interpretation and retry/fallback policy.

`zhttpclient` should keep only example-specific policy and payload behavior:

- CLI options and usage text;
- URL parsing and redirect policy;
- Alt-Svc parsing and upgrade policy;
- DNS-first and Alt-Svc-first orchestration;
- CA/output file configuration;
- response body sink and logging;
- request header values such as `user-agent` and `accept`;
- overall run orchestration.

`zhttpclient` should not own generic transport machinery such as local H3
control/QPACK stream setup, peer QPACK stream parsing, response stream routing,
or reusable link glue.

### State Ownership
`State` in `zhttpclient` should be split by ownership as an example of the
general boundary.

`zhttpclient`-owned state:

```c++
URL             url;
Options         options;
ZtString<>      location;
unsigned        status = 0;
ZiFile          bodyFile;
int64_t         contentLength = -1;
uint64_t        bodyBytes = 0;
unsigned        bodyChunks = 0;
bool            bodyFileOpen = false;
bool            chunked = false;
bool            redirect = false;
bool            framingLogged = false;
bool            done = false;
bool            failed = false;
```

Application policy may retain Alt-Svc metadata because upgrade policy is not a
protocol invariant:

```c++
ZtString<>      altSvcHost;
uint16_t        altSvcPort = 0;
bool            altSvcH3 = false;
```

Transport/session-owned H3 state:

```c++
Zhttp::H3::CxnState::T h3State;
bool                    peerControl;
bool                    peerEncoder;
bool                    peerDecoder;
Zhttp::H3::QPackRxTable qpackRxTable;
Zhttp::H3::QPackTxTable qpackTxTable;
```

The response/request stream ID is session routing state while a request is in
flight. The application may observe it for diagnostics, but should not own the
routing decision:

```c++
int64_t activeStreamID = -1;
```

Stream refs, parser pointers, and QPACK encoder/decoder stream writers are
also transport/session state and should move out of the example.

For a server, analogous transport-owned state includes listener/session state,
accepted connection state, active request stream IDs, response stream routing,
and per-connection QPACK tables. Application state remains request/response
payload state and service policy.

## Detailed Design and Implementation Plan

### Phase 1: Split H1/H3 Message Headers Without Behavior Change
- Area of focus: reduce `Zhttp.hh` from implementation home to aggregate
  header while preserving every current public type name and CRTP contract.
- Add files:
  - `zhttp/src/ZhttpH1.hh`
  - `zhttp/src/ZhttpH3.hh`
- Modify files:
  - `zhttp/src/Zhttp.hh`
  - `zhttp/src/Makefile.am`
- Move current `namespace Zhttp::H1` parser, body stream, chunked stream, and
  builder code from `Zhttp.hh` into `ZhttpH1.hh`.
- Move current `namespace Zhttp::H3` connection state, frame state, connection
  stream parser, message parser, frame helpers, data stream, and builder code
  from `Zhttp.hh` into `ZhttpH3.hh`.
- Keep `ZhttpHeaders(...)`, `DefltMaxHdr`, `DefltMaxBody`, `Method`, and
  `XferCompression` in `Zhttp.hh` unless moving them would create a direct
  include cycle.
- Update `Zhttp.hh` to include:

```c++
#include <zlib/ZhttpUtil.hh>
#include <zlib/ZhttpHPack.hh>
#include <zlib/ZhttpQPack.hh>
#include <zlib/ZhttpH1.hh>
#include <zlib/ZhttpH3.hh>
```

- Ensure new headers include only their direct dependencies. For example,
  `ZhttpH3.hh` needs `ZhttpQPack.hh`, `ZhttpHPack.hh`, `ZtLocalArray.hh`,
  `ZiTxStream.hh`, `ZiLog.hh`, and core `Zu*` helpers it actually uses.
- Add `ZhttpH1.hh` and `ZhttpH3.hh` to `pkginclude_HEADERS`.
- Do not add compatibility aliases or forwarders. This phase should preserve
  names because namespaces stay the same, not because aliases are introduced.
- Acceptance for this phase:
  - `make -C zhttp` passes.
  - `make -C zhttp/test ZhttpParserTest ZhttpHPackTest ZhttpQPackTest ZhttpQPackDynamicTest` passes.
  - `make -C zhttp/example zhttpclient` passes.
  - `git diff -- zhttp/src/Zhttp.hh zhttp/src/ZhttpH1.hh zhttp/src/ZhttpH3.hh`
    shows moved code and aggregate includes, not behavior edits.

### Phase 2: Add Role-Neutral Message Facades Over Existing Parsers and Builders
- Area of focus: give applications a message-shaped vocabulary while still
  delegating to the proven H1/H3 parser/builder implementations.
- Add file if the facade code is large:
  - `zhttp/src/ZhttpMsg.hh`
- Otherwise keep small aliases/wrappers in `Zhttp.hh` after the H1/H3 includes.
- The facades should express four concepts:
  - request parser;
  - response parser;
  - request builder;
  - response builder.
- The initial implementation can be template aliases or thin CRTP wrappers
  around the existing protocol templates. Avoid virtual dispatch and avoid C++
  concepts.
- Candidate shape:

```c++
namespace Zhttp {

template <typename Impl, typename Headers = ZuTypeList<>,
  uint64_t MaxBody = DefltMaxBody>
using H1ReqParser = H1::Parser<Impl, true, Headers, MaxBody>;

template <typename Impl, typename Headers = ZuTypeList<>,
  uint64_t MaxBody = DefltMaxBody>
using H1RespParser = H1::Parser<Impl, false, Headers, MaxBody>;

template <typename Impl, typename Headers = ZuTypeList<>,
  uint64_t MaxBody = DefltMaxBody>
using H3ReqParser = H3::Parser<Impl, true, Headers, MaxBody>;

template <typename Impl, typename Headers = ZuTypeList<>,
  uint64_t MaxBody = DefltMaxBody>
using H3RespParser = H3::Parser<Impl, false, Headers, MaxBody>;

}
```

- If builders get aliases, keep names short and explicit:

```c++
template <typename Impl, typename Headers = ZuTypeList<>,
  typename Trailers = ZuTypeList<>, bool HasBody = false, bool Chunked = false>
using H1ReqBuilder = H1::Builder<Impl, Headers, Trailers, HasBody, Chunked>;
```

- Do not hide the protocol choice yet. A runtime protocol-erasing builder/parser
  would either allocate or require type erasure and is not needed for the
  current goal. Protocol selection can remain at compile time inside session
  templates.
- Add or update tests in `zhttp/test/ZhttpParserTest.cc` and
  `zhttp/test/Zhttp3InteropTest.cc` to use at least one facade alias for each
  direction:
  - H1 request parse;
  - H1 response parse;
  - H3 request parse;
  - H3 response parse;
  - H1/H3 request build;
  - H1/H3 response build.
- Acceptance for this phase:
  - Existing low-level H1/H3 parser and builder tests continue to pass.
  - Application-like test code can express "parse request", "parse response",
    "build request", and "build response" without manually passing `true` or
    `false` request booleans at each use site.
  - QPACK table, stream ID, H3 connection state, and peer control stream
    callbacks remain out of ordinary message callback types.

### Phase 3: Extract H3 Connection Bootstrap and QPACK Stream Handling
- Area of focus: one complete reusable H3 connection bootstrap slice, shared by
  client and server code before request/response routing is moved.
- Add file:
  - `zhttp/src/ZhttpH3Session.hh`
- Modify files:
  - `zhttp/src/Zhttp.hh` or `zhttp/src/ZhttpH3.hh` to include/export the new
    session template only if it does not pull `Zquic` into the codec layer.
  - `zhttp/src/Makefile.am`
  - `zhttp/example/zhttpclient.cc`
  - `zhttp/test/Zhttp3InteropTest.cc`
- Start from duplicated code in:
  - `zhttp/example/zhttpclient.cc:openH3LocalStreams`
  - `zhttp/example/zhttpclient.cc:CliLink::QPackStreamTx`
  - `zhttp/example/zhttpclient.cc:QUICClient::Stream`
  - `zhttp/test/Zhttp3InteropTest.cc:sendH3ControlStreams`
  - `zhttp/test/Zhttp3InteropTest.cc:H3ServerLink::QPackStreamTx`
  - `zhttp/test/Zhttp3InteropTest.cc:H3Client::Link::QPackStreamTx`
  - `zhttp/test/Zhttp3InteropTest.cc:H3ServerStream::peer*Stream`
  - `zhttp/test/Zhttp3InteropTest.cc:H3Client::Stream::peer*Stream`
- Introduce a role-neutral H3 connection state holder. Keep names short:

```c++
namespace Zhttp { namespace H3 {

template <typename Link, typename StreamRef>
struct Cxn {
  using State = CxnState;

  bool openLocal(Link &link, const Params &params = Params{});
  bool peerControlStream();
  bool peerEncoderStream();
  bool peerDecoderStream();
  QPackRxTable *qpackRx();
  QPackTxTable *qpackTx();

  State::T         state = State::Init;
  StreamRef        control;
  StreamRef        enc;
  StreamRef        dec;
  bool             peerControl = false;
  bool             peerEncoder = false;
  bool             peerDecoder = false;
  QPackRxTable     qpackRxTable;
  QPackTxTable     qpackTxTable;
};

}}
```

- The actual implementation may use CRTP if `Link`/`StreamRef` needs local
  policy hooks. Do not use concepts or `requires`; if capability detection is
  needed, use `typename = void` detector structs and `ZuIfT`.
- Factor a stream writer for existing builder/parser contracts:

```c++
template <typename Link, typename StreamRef>
struct QPackStreamTx : public QPackEncoderTx {
  QPackStreamTx(Link *link_, StreamRef *stream_);
  bool write(ZuBSpan) override;
};
```

- Keep the virtual `QPackEncoderTx` override because `H3::Builder` and
  `H3::Parser` currently depend on that API, but hide it behind the reusable H3
  session. Application code should no longer expose `qpackEncoderTx_`,
  `qpackDecoderTx_`, or local subclasses of `QPackEncoderTx`.
- `openLocal()` should open exactly one local control stream, one local encoder
  stream, and one local decoder stream. It should emit:
  - unidirectional stream type `0x00` for the control stream;
  - SETTINGS frame type `0x04`;
  - `SETTINGS_QPACK_MAX_TABLE_CAPACITY` (`0x01`);
  - `SETTINGS_MAX_FIELD_SECTION_SIZE` (`0x06`);
  - `SETTINGS_QPACK_BLOCKED_STREAMS` (`0x07`);
  - unidirectional stream type `0x02` for QPACK encoder;
  - unidirectional stream type `0x03` for QPACK decoder.
- Default settings should match current conservative behavior:
  - QPACK table capacity: `params.qpackTableCapacity()`, default zero;
  - max field section size: `params.maxHeaderListSize()`, default `1<<16`;
  - blocked streams: `params.qpackBlockedStreams()`, default zero.
- If any local stream cannot be opened because QUIC peer stream limits block it,
  return failure and let the app/session fail or retry. Do not silently continue
  without critical streams when dynamic QPACK may be enabled.
- Preserve RFC 9204 behavior: receiving a second peer control, encoder, or
  decoder stream is a connection error. Current duplicated `peer*Stream()`
  booleans should move into `H3::Cxn`.
- Add a small adapter base for unidirectional stream parsing:

```c++
template <typename Impl, typename Cxn>
struct CxnStream :
  public CxnParser<Impl> {
  CxnState::T h3State() const;
  void h3State(CxnState::T);
  bool peerControlStream();
  bool peerEncoderStream();
  bool peerDecoderStream();
  QPackRxTable *qpackRx();
  QPackTxTable *qpackTx();
};
```

- Wire `zhttpclient` and `Zhttp3InteropTest` to use `H3::Cxn` for:
  - local control/QPACK stream setup;
  - QPACK stream writers;
  - peer stream uniqueness;
  - QPACK Rx/Tx table access;
  - H3 connection state storage.
- Acceptance for this phase:
  - `zhttpclient` no longer defines `openH3LocalStreams`.
  - `zhttpclient` no longer owns peer control/encoder/decoder booleans
    directly.
  - `zhttpclient` and application-like tests no longer define local
    `QPackEncoderTx` subclasses; they use the H3 session's internal writer.
  - `Zhttp3InteropTest` no longer has separate client/server copies of
    `sendH3ControlStreams` or QPACK stream writer logic.
  - `ZhttpQPackDynamicTest` still passes.
  - `Zhttp3InteropTest` still passes where Caddy/curl prerequisites are
    available, and still compiles when those runtime tools are not available.

### Phase 4: Extract H3 Request/Response Stream Routing and Message Send/Parse
- Area of focus: one full H3 request/response exchange through reusable session
  code, with both client and server directions covered.
- Extend `ZhttpH3Session.hh`.
- Extract duplicated message send path from:
  - `zhttp/example/zhttpclient.cc:sendH3Request`
  - `zhttp/test/Zhttp3InteropTest.cc:sendH3Request`
  - `zhttp/test/Zhttp3InteropTest.cc:sendH3Response`
- Extract duplicated message parse/routing path from:
  - `zhttp/example/zhttpclient.cc:processResponse`
  - `zhttp/example/zhttpclient.cc:QUICClient::Stream::process`
  - `zhttp/test/Zhttp3InteropTest.cc:H3ServerStream::process`
  - `zhttp/test/Zhttp3InteropTest.cc:H3Client::Stream::process`
- Introduce a stream-role adapter that distinguishes unidirectional H3
  connection streams from bidirectional request streams:

```c++
namespace Zhttp { namespace H3 {

template <typename Impl, typename Link, typename Stream>
struct SessionStream {
  int process(Stream &stream);
};

}}
```

- The concrete shape can be CRTP. Required hooks should be simple and
  message-focused:
  - `Impl::onReq(Stream &)`;
  - `Impl::onResp(Stream &)`;
  - `Impl::onH3Error(CxnState::T)`;
  - `Impl::done(Stream &, ParserState::T)`;
  - optional `Impl::acceptStream(uint64_t)` for proxies or multiplexed clients.
- For the initial client slice, keep one active response stream because
  `zhttpclient` sends one request at a time. The session state owns
  `activeStreamID`.
- For the server slice, route each bidirectional peer stream to a request parser
  and response builder. `Zhttp3InteropTest` already proves this shape with
  `H3ServerStream::parser` and `sendH3Response`.
- Session send helpers should provide:

```c++
template <typename Stream, typename Builder>
bool sendReq(Stream &stream, Builder &builder, bool fin = true);

template <typename Stream, typename Builder>
bool sendResp(Stream &stream, Builder &builder, bool fin = true);
```

- The helpers must set `builder.qpackTx_`, `builder.qpackEncoderTx_`, and
  `builder.streamID_` through a small adapter or documented CRTP hook, not by
  making application code reach into link internals.
- Preserve Tx-thread constraints:
  - Use `link.send(ref, payload, fin)` for simple payloads.
  - Use `app()->txInvoke()` before using `txStream_()` and `send_()`.
  - Do not mutate `QPackTxTable` concurrently from Rx and Tx paths.
- Completion semantics:
  - H3 parser `Complete` maps to message completion.
  - H3 parser `Cancelled` maps to message/session cancellation.
  - H3 parser `Error` maps to message/session failure.
  - H3 connection parser `Error` maps to connection/session failure.
- Acceptance for this phase:
  - `zhttpclient` no longer has H3-specific response stream routing in
    `QUICClient::Stream::process`; it delegates to the reusable H3 session.
  - `zhttpclient` request/response callbacks remain application policy.
  - H3 request/response builders and parsers receive QPACK writer access from
    the session adapter, not from application-owned fields.
  - `Zhttp3InteropTest` uses the same H3 session code for its client and
    server sides.
  - H3 dynamic QPACK tests still pass.
  - H3 interop tests compile and pass where runtime prerequisites are present.

### Phase 5: Extract H1 TCP/TLS Session Mechanics
- Area of focus: one reusable H1 request/response exchange over the existing
  single-stream TCP/TLS link APIs.
- Add file if needed:
  - `zhttp/src/ZhttpH1Session.hh`
- Modify:
  - `zhttp/example/zhttpclient.cc`
  - `zhttp/test/ZhttpFallbackTest.cc`
  - `zhttp/test/Zhttp3InteropTest.cc`
- Extract duplicated H1 send/parse patterns:
  - `zhttp/example/zhttpclient.cc:sendH1Request`
  - `zhttp/example/zhttpclient.cc:processResponse<false>`
  - `zhttp/test/Zhttp3InteropTest.cc:sendH1Request`
  - `zhttp/test/Zhttp3InteropTest.cc:sendH1Response`
  - `zhttp/test/Zhttp3InteropTest.cc:H1ClientLinkOps`
  - `zhttp/test/Zhttp3InteropTest.cc:H1ServerLinkOps`
- The H1 session should be role-neutral but explicit about first-release
  limits:
  - one in-flight request/response per TCP/TLS link;
  - no HTTP/1 pipelining;
  - no connection pooling;
  - no automatic retry;
  - keep-alive may be retained only if the current link lifetime already
    supports it without new policy.
- Candidate hooks:

```c++
namespace Zhttp { namespace H1 {

template <typename Impl, typename Link>
struct Session {
  void connected(Link &);
  void disconnected(Link &);
  template <typename Rx> int process(Link &, Rx &);
};

}}
```

- The concrete implementation should call `H1::Builder::request()` or
  `H1::Builder::response()` and then `finish()`. If `finish()` already flushes,
  remove the current `zhttpclient` FIXME and avoid an extra explicit flush.
- H1 disconnect handling must preserve current behavior:
  - incomplete response on disconnect is failure;
  - parser error is failure;
  - parser complete is message completion;
  - content-length and chunked state remain parser/application callbacks.
- Acceptance for this phase:
  - `zhttpclient` no longer defines generic H1 `CliLink` logic.
  - H1 session API does not use client-only names for server-capable behavior.
  - `ZhttpParserTest`, `ZhttpFallbackTest`, and the H1 portions of
    `Zhttp3InteropTest` pass.
  - `zhttpclient` still supports TCP and TLS H1 paths.

### Phase 6: Rebuild `zhttpclient` Around Policy
- Area of focus: reduce `zhttpclient` to example policy after reusable H1/H3
  sessions exist.
- Keep in `zhttpclient`:
  - CLI parsing and usage text;
  - URL parsing;
  - redirect policy;
  - Alt-Svc parsing and upgrade policy;
  - CA/output file configuration;
  - response body sink;
  - example logging;
  - request headers `user-agent` and `accept`;
  - high-level run selection and fallback orchestration.
- Move out of `zhttpclient`:
  - H3 local control/QPACK stream setup;
  - peer QPACK stream parsing;
  - H3 response stream routing;
  - H3 QPACK table ownership;
  - reusable H1 link callback mechanics;
  - reusable H3 link/stream callback mechanics.
- `runH3DNSFirst` and `runH1AltSvcFirst` are client policy orchestration. They
  may remain in `zhttpclient`, or become example-level helpers if multiple
  client examples need them. They should not be part of the core protocol or
  role-neutral session layer.
- The final `State` should visibly separate:
  - application policy and response summary fields;
  - current run lifecycle flags;
  - no QPACK tables;
  - no peer critical stream booleans;
  - no H3 parser state unless only diagnostic.
- Acceptance for this phase:
  - `zhttpclient` is materially smaller.
  - H3-specific code in `zhttpclient` reads as policy and message callbacks,
    not protocol mechanics.
  - HTTP/3 fallback behavior is unchanged.
  - Output file behavior, redirect behavior, status/header logging, and
    Alt-Svc interpretation are unchanged.

### Phase 7: Add `zhttpserver.cc` As A Server-Side Proving Example
- Area of focus: prevent the API from drifting back into a client-only shape by
  adding a maintained example that uses the same reusable sessions from the
  server role.
- Add:
  - `zhttp/example/zhttpserver.cc`
  - `zhttp/test/ZhttpClientServerTest.cc`
- Modify:
  - `zhttp/example/Makefile.am`
  - `zhttp/test/Makefile.am`
  - `zhttp/README.md` or `zhttp/example/README.md` if an example README exists
- Keep strengthening `zhttp/test/Zhttp3InteropTest.cc`, but do not rely on a
  dense interop test as the only server-facing API demonstration.
- Update `zhttp/example/Makefile.am`:

```make
noinst_PROGRAMS = zhttpclient zhttpserver
zhttpclient_SOURCES = zhttpclient.cc
zhttpserver_SOURCES = zhttpserver.cc
```

- Update `zhttp/test/Makefile.am`:

```make
noinst_PROGRAMS = ... ZhttpClientServerTest
ZhttpClientServerTest_SOURCES = ZhttpClientServerTest.cc

test: $(noinst_PROGRAMS)
	prove ... ZhttpClientServerTest
```

- `zhttpserver.cc` should be a compact echo/static-response server, not a
  production web server. Its purpose is to demonstrate server ownership
  boundaries:
  - parse inbound requests;
  - build outbound responses;
  - handle request bodies and completion as message events;
  - avoid direct QPACK, H3 connection-stream, or HTTP/1 framing code;
  - keep TLS certificate/key and QUIC transport setup as example policy.
- Supported CLI:

```text
Usage: zhttpserver [OPTION]...

Options:
  -a, --addr=IP        listen address, default 127.0.0.1
  -p, --port=PORT      listen port, default 8080
  -c, --cert=PATH      TLS certificate path for HTTPS/H3
  -k, --key=PATH       TLS private key path for HTTPS/H3
      --http           enable HTTP/1.1 over plain TCP
      --https          enable HTTP/1.1 over TLS
      --http3          enable HTTP/3 over QUIC
      --body=TEXT      response body, default zhttp-ok
  -h, --help           show help
```

- Example state should stay policy-only:

```c++
struct Options {
  ZiIP          addr{"127.0.0.1"};
  uint16_t      port = 8080;
  ZuCSpan       cert;
  ZuCSpan       key;
  ZuCSpan       body{"zhttp-ok"};
  bool          http = true;
  bool          https = false;
  bool          http3 = false;
  bool          help = false;
};

struct State {
  Options       options;
  ZmSemaphore  done;
  ZtString<>    body;
  ZmAtomic<unsigned> requests = 0;
  ZmAtomic<unsigned> errors = 0;
};
```

- Request parser callback shape:

```c++
struct ReqSink {
  void operation(Zhttp::Method::T method, ZuBSpan path);
  template <typename Key> void header(ZuBSpan value);
  void contentLength(uint64_t);
  void body(ZuBSpan);
  template <typename ParserState>
  void complete(typename ParserState::T state);

  Zhttp::Method::T method = -1;
  ZtString<>       path;
  ZtString<>       requestBody;
  bool             complete = false;
  bool             failed = false;
};
```

- Response builder callback shape:

```c++
struct RespOps {
  unsigned status() const { return 200; }
  template <typename L> void reason(L &&l) const { l("OK"); }
  uint64_t contentLength() const { return body.length(); }
  template <typename Key, typename L> void header(L &&l) const;

  ZuCSpan body;
};
```

- The server link/stream code should be mostly glue to the reusable sessions:
  - H1 TCP/TLS accepted link delegates request parsing and response sending to
    `Zhttp::H1::Session`.
  - H3 QUIC accepted stream delegates unidirectional connection streams and
    bidirectional request streams to `Zhttp::H3::Session`.
  - The example should not define local `QPackEncoderTx` subclasses, QPACK
    tables, peer critical stream booleans, or H3 control stream setup.
- The response body can be constant text from `--body`. Do not add filesystem
  serving, routing tables, MIME detection, directory traversal handling, or
  authentication in this example.
- The default mode should enable `--http` without requiring TLS certificates.
  `--https` and `--http3` require certificate/key options or a documented
  self-signed test path because both use TLS.
- Add integration coverage in `ZhttpClientServerTest.cc` that starts
  `zhttpserver`, runs `zhttpclient`, verifies the output body, and then shuts
  the server down. Use loopback ports and temporary directories following the
  existing interop-test style in `ZhttpCaddyInterop.hh` / `ZquicInteropTest.hh`.
- The integration matrix must cover:
  - HTTP over TCP with HTTP/1.1:

```sh
zhttpserver --http --addr 127.0.0.1 --port PORT --body zhttp-ok
zhttpclient -o TMP/body http://127.0.0.1:PORT/zhttp-interop
```

  - HTTPS over TLS with HTTP/1.1:

```sh
zhttpserver --https --cert TMP/cert.pem --key TMP/key.pem \
  --addr 127.0.0.1 --port PORT --body zhttp-ok
zhttpclient -c TMP/cert.pem -o TMP/body https://localhost:PORT/zhttp-interop
```

  - HTTPS over QUIC with HTTP/3:

```sh
zhttpserver --http3 --cert TMP/cert.pem --key TMP/key.pem \
  --addr 127.0.0.1 --port PORT --body zhttp-ok
zhttpclient -c TMP/cert.pem --http3-only -o TMP/body \
  https://localhost:PORT/zhttp-interop
```

- If `zhttpclient` does not currently support enough URL/CA behavior for these
  invocations, extend `zhttpclient` in Phase 6 rather than weakening the test
  matrix. The point of this test is to prove the example client and example
  server interoperate through the public application-facing surfaces.
- `ZhttpClientServerTest.cc` should verify for every matrix row:
  - client exit status is success;
  - output body exactly matches `zhttp-ok`;
  - server request counter increments;
  - no server error counter increments;
  - protocol-specific logs or status files confirm the expected path where the
    examples expose that detail.
- Acceptance for this phase:
  - `make -C zhttp/example zhttpserver` passes.
  - `make -C zhttp/test ZhttpClientServerTest` passes.
  - `zhttpserver --http --port PORT --body zhttp-ok` responds to a simple
    HTTP/1 request with status 200, `content-length`, `content-type:
    text/plain`, and body `zhttp-ok`.
  - `zhttpserver --http3 --cert CERT --key KEY --port PORT` exercises the H3
    server session where local test certificates are available.
  - `zhttpserver.cc` does not duplicate H3 client/server session internals from
    `Zhttp3InteropTest`.
  - `ZhttpClientServerTest` passes for HTTP/TCP/H1, HTTPS/TLS/H1, and
    HTTPS/QUIC/H3 when local TLS and QUIC prerequisites are available.
  - Tests cover both "client builds request/parses response" and "server parses
    request/builds response".

## Code References to Impacted Code
- `zhttp/src/Zhttp.hh:14` - Convert to compact aggregate header after moving
  H1/H3 implementation bodies.
- `zhttp/src/Zhttp.hh:117` - Current `namespace H1` parser begins here; move to
  `ZhttpH1.hh`.
- `zhttp/src/Zhttp.hh:459` - Current `namespace H3` state and connection parser
  begins here; move to `ZhttpH3.hh`.
- `zhttp/src/Zhttp.hh:1791` - Current `Zhttp::H1::Builder`; move to
  `ZhttpH1.hh`.
- `zhttp/src/Zhttp.hh:1916` - Current `Zhttp::H3::Builder`; move to
  `ZhttpH3.hh`.
- `zhttp/src/ZhttpQPack.hh:84` - `Zhttp::H3::Params`; use for H3 SETTINGS
  defaults and QPACK policy.
- `zhttp/src/ZhttpQPack.hh:259` - `QPackEncoderTx` virtual write API currently
  required by H3 parser/builder dynamic QPACK paths.
- `zhttp/src/ZhttpQPack.hh:264` - `QPackTxTable` connection-affine Tx state;
  session code must serialize mutation.
- `zhttp/src/Makefile.am:10` - Add installed headers and any new session source
  files.
- `zhttp/example/zhttpclient.cc:309` - Request builder currently wires QPACK
  pointers manually.
- `zhttp/example/zhttpclient.cc:445` - Response parser currently reads QPACK
  state from link internals.
- `zhttp/example/zhttpclient.cc:532` - `openH3LocalStreams`; replace with H3
  session bootstrap.
- `zhttp/example/zhttpclient.cc:570` - `CliLink::QPackStreamTx`; replace with
  reusable H3 QPACK stream writer.
- `zhttp/example/zhttpclient.cc:693` - `QUICClient::Stream` currently combines
  Zquic stream handling with H3 connection parser glue; delegate to H3 session.
- `zhttp/test/Zhttp3InteropTest.cc:127` - H3 response send path; extract common
  H3 send helper.
- `zhttp/test/Zhttp3InteropTest.cc:276` - H3 request send path; extract common
  H3 send helper.
- `zhttp/test/Zhttp3InteropTest.cc:310` - H3 local control/QPACK stream setup;
  replace with H3 session bootstrap.
- `zhttp/test/Zhttp3InteropTest.cc:475` - Server H3 stream shows the server-side
  extraction source.
- `zhttp/test/Zhttp3InteropTest.cc:720` - Client H3 stream shows the client-side
  extraction source.
- `zhttp/example/Makefile.am:20` - Add `zhttpserver` to `noinst_PROGRAMS` and
  add `zhttpserver_SOURCES = zhttpserver.cc`.
- `zhttp/example/zhttpserver.cc:1` - New server example using reusable H1/H3
  sessions from the server role.
- `zhttp/test/Makefile.am:15` - Add `ZhttpClientServerTest` to
  `noinst_PROGRAMS`, source list, and `test` target.
- `zhttp/test/ZhttpClientServerTest.cc:1` - New integration test that runs
  `zhttpclient` against `zhttpserver` across HTTP/TCP/H1, HTTPS/TLS/H1, and
  HTTPS/QUIC/H3.
- `zhttp/test/ZhttpFallbackTest.cc:123` - Existing H1 builder/parser usage for
  fallback tests; update to message facades/session helpers where useful.
- `zquic/src/Zquic.hh:1661` - `Link::stream()` opens local duplex/simplex
  streams and may return null when limited.
- `zquic/src/Zquic.hh:2800` - client `send()` marshals to Tx path.
- `zquic/src/Zquic.hh:3261` - server `send()` mirrors client send semantics.
- `ztcp/src/Ztcp.hh:155` - TCP exposes only a duplex stream.
- `ztls/src/Ztls.hh:281` - TLS exposes only a duplex stream.

## Detailed Test Plan
- After Phase 1:
  - Build `zhttp`.
  - Run `zhttp/test/ZhttpParserTest`.
  - Run `zhttp/test/ZhttpHPackTest`.
  - Run `zhttp/test/ZhttpQPackTest`.
  - Run `zhttp/test/ZhttpQPackDynamicTest`.
  - Build `zhttp/example/zhttpclient`.
- After Phase 2:
  - Add compile-time use of request/response facades in existing parser and
    interop tests.
  - Verify the old direct `H1::Parser`, `H3::Parser`, `H1::Builder`, and
    `H3::Builder` tests still compile and pass.
- After Phase 3:
  - Add unit coverage for H3 local SETTINGS encoding with default `Params`.
  - Add a peer duplicate stream test: second control, encoder, or decoder
    stream should put the H3 connection in `CxnState::Error`.
  - Run `ZhttpQPackDynamicTest`, especially tests around encoder/decoder
    instructions and capacity handling.
- After Phase 4:
  - Update `Zhttp3InteropTest` to use the new H3 session for both client and
    server.
  - Ensure H3 request parse and response build are covered on the server side.
  - Ensure H3 request build and response parse are covered on the client side.
  - Run runtime interop if Caddy and curl HTTP/3 are available; otherwise the
    test should skip runtime dependency paths without compile failures.
- After Phase 5:
  - Run `ZhttpParserTest` for content-length, chunked body, trailers, and error
    paths.
  - Run `ZhttpFallbackTest` to preserve H3-to-H1 fallback behavior.
  - Run H1 portions of `Zhttp3InteropTest`.
- After Phase 6:
  - Build and run `zhttpclient` smoke cases for:
    - plain TCP H1 where available;
    - TLS H1;
    - H3;
    - fallback from failed H3 to H1;
    - redirect policy;
    - output file write failure.
- After Phase 7:
  - Build `zhttp/example/zhttpserver`.
  - Build `zhttp/test/ZhttpClientServerTest`.
  - Run an HTTP/1 smoke test against `zhttpserver --http --port PORT --body
    zhttp-ok` and verify status 200, `content-length`, `content-type:
    text/plain`, and body `zhttp-ok`.
  - Run an HTTP/3 smoke test against `zhttpserver --http3 --cert CERT --key KEY
    --port PORT` where local certificate and HTTP/3 client prerequisites are
    available.
  - Ensure server-side tests continue to cover request body parse and response
    body build.
  - Run `ZhttpClientServerTest` and verify it covers:
    - `http://` over TCP using HTTP/1.1;
    - `https://` over TLS using HTTP/1.1;
    - `https://` over QUIC using HTTP/3 with `zhttpclient --http3-only`.
  - Document `zhttpserver` invocation in `zhttp/README.md` or the example
    directory.

## Acceptance Criteria
- `Zhttp.hh` is a compact public aggregate over protocol-specific headers.
- H1 and H3 message code live in separate protocol headers.
- Application-facing parser and builder surfaces cover request and response
  messages for both client and server roles.
- H3 session adapters own reusable control stream, QPACK stream, table, and
  stream-routing mechanics.
- H1 session adapters own reusable TCP/TLS link callback, send, parse, and
  disconnect mechanics.
- Applications implement message callbacks and policy, not protocol mechanics.
- `zhttpclient` is materially smaller and mostly reads as an example of client
  request/response policy.
- `zhttpserver.cc` proves the same abstractions work for inbound requests and
  outbound responses.
- `ZhttpClientServerTest` proves `zhttpclient` and `zhttpserver` interoperate
  over HTTP/TCP/H1, HTTPS/TLS/H1, and HTTPS/QUIC/H3.
- Existing zhttp unit tests and example builds pass.
- Dynamic QPACK remains optional and defaults to zero-capacity behavior.
- No codec-layer header depends on `Ztcp`, `Ztls`, or `Zquic`.
- `QPackEncoderTx` remains only as an internal H3 session bridge; ordinary
  application code does not subclass it or manually pass it to parsers/builders.

## Non-goals
- Do not merge HPACK, QPACK, H1, and H3 into one header again.
- Do not add compatibility aliases for old internal names.
- Do not make dynamic QPACK mandatory.
- Do not make `zhttp` own application redirect, URL, CLI, filesystem,
  authentication, authorization, routing, DNS preference, or service policy.
- Do not hide protocol errors from the application; surface them as structured
  message/session completion or failure callbacks.
- Do not name core session abstractions as clients when they apply equally to
  servers.
- Do not introduce runtime type erasure for parser/builder protocol selection
  unless a future requirement needs it.
- Do not introduce C++ concepts or `requires`.
- Do not replace `QPackEncoderTx` with a CRTP/static writer in this plan; that
  is a follow-on refactor after session extraction.
- Do not introduce HTTP/1 pipelining, H3 server push, WebTransport, DATAGRAM,
  0-RTT, QUIC v2, multipath, or active ECN behavior as part of this work.
- Do not move Alt-Svc retry/fallback policy into the core `zhttp` library.

## Options and Open Questions
- Option: keep session templates in `ZhttpH1.hh` and `ZhttpH3.hh`.
  - Use this only if the added templates stay small and do not pull transport
    dependencies into codec/message code.
- Option: add `ZhttpH1Session.hh` and `ZhttpH3Session.hh`.
  - Preferred if session code grows beyond thin helpers or needs `Ztcp`,
    `Ztls`, or `Zquic` includes.
- Decision: retain `QPackEncoderTx` as an internal virtual bridge.
  - This is the selected near-term path because current parser/builder APIs
    depend on it.
  - The H3 session owns the concrete writer and supplies it to H3
    parser/builder internals.
  - Applications and application-like tests should not subclass
    `QPackEncoderTx` or store `qpackEncoderTx_` / `qpackDecoderTx_` fields.
  - The CRTP/static writer replacement is intentionally moved to
    `followon.md`.
- Decision: add `zhttp/example/zhttpserver.cc`.
  - `Zhttp3InteropTest` remains important coverage, but the example is now a
    required public demonstration of the server-side API.

No blocking open questions remain. The complex parts are feasible with the
current technology stack because the required QUIC stream operations, H3 frame
parsers, QPACK tables, and client/server test patterns already exist locally.
The main implementation risk is threading ownership around QPACK Tx mutation;
the plan resolves that by keeping QPACK Tx state connection-affine and mutating
it only on the owning send path.
