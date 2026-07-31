# zhttp Separation of Concerns

The goal is to make `zhttp` a reusable HTTP messaging and transport framework
for both client and server applications. `zhttpclient` is the immediate proving
ground, but the API should not assume that every application is a client, that
every inbound message is a response, or that every outbound message is a
request.

Applications should focus on transport-agnostic message concerns:

- building requests;
- parsing requests;
- building responses;
- parsing responses;
- handling headers, bodies, completion, and application policy.

`zhttp` should own protocol and transport mechanics:

- HTTP/1 parsing/framing/chunking;
- HTTP/3 frames and connection streams;
- QPACK encoder/decoder stream wiring;
- dynamic table ownership;
- request/response stream routing;
- reusable TCP/TLS/QUIC link/session glue.

The current codebase already has a useful low-level split:

- `ZhttpHPack.hh/.cc`: HPACK Huffman support.
- `ZhttpQPack.hh/.cc`: QPACK types, static table, dynamic tables, field
  encoding/decoding, and QPACK instruction streams.
- `ZhttpUtil.hh`: HTTP parser utilities shared by higher layers.
- `Zhttp.hh`: public aggregate header containing HTTP message APIs plus both
  HTTP/1 and HTTP/3 protocol machinery.
- `zhttp/example/zhttpclient.cc`: a working client example, but it currently
  mixes application policy, message callbacks, connection/link plumbing,
  HTTP/3 control streams, QPACK stream wiring, fallback, DNS probing, redirects,
  logging, and file I/O.

The plan is to preserve the low-level codec split and introduce a clean middle
layer so application code does not reimplement HTTP transport mechanics.

## Target Layers

### 1. Codec and Protocol Primitives

Owned by existing low-level headers:

- `ZhttpHPack.hh`
- `ZhttpQPack.hh`
- `ZhttpUtil.hh`

These should remain independent protocol primitives. They should not know about
TCP, TLS, QUIC, DNS, redirects, files, CLI options, or application policy.

### 2. Transport-Agnostic Message Layer

Owned by `Zhttp.hh` as the public application-facing aggregate:

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

`Zhttp` should isolate protocol-specific handling so applications can focus on
individual message sending and parsing. The application-level surface should
cover both client and server directions:

- clients build requests and parse responses;
- servers parse requests and build responses;
- proxies and gateways may parse and build both.

Parser callbacks should represent an inbound message, not the transport stream:

```c++
void reset();
void operation(Method::T method, ZuBSpan path);
void status(unsigned);
template <typename Key> void header(ZuBSpan value);
template <typename Key, typename Value> void header();
void contentLength(uint64_t);
void xferCompression(XferCompression::T);
void chunked();
void body(ZuBSpan);
void complete(ParserState::T);
```

Builder callbacks should represent an outbound message, independent of whether
that message is a request or response:

```c++
void reset();
void operation(Method::T method, ZuBSpan path);
void status(unsigned);
template <typename Key> void header(ZuBSpan value);
template <typename Key, typename Value> void header();
void contentLength(uint64_t);
void body(ZuBSpan);
void complete(ParserState::T);
```

For applications, `complete()` must mean "end of message", not "end of stream".
HTTP/3 stream lifecycle and QPACK section acknowledgements are transport-layer
details.

The concrete low-level CRTP builder may continue using the current callback
shape where it is more efficient, such as `operation(L &&)`, `host(L &&)`, and
`header<Key>(L &&)`. The higher-level application contract should still be
message-shaped and symmetric across client/server roles.

### 3. Protocol-Specific Message Implementations

Split the current namespace bodies out of `Zhttp.hh`:

- `ZhttpH1.hh`
  - `Zhttp::H1::Parser`
  - `Zhttp::H1::Builder`
  - HTTP/1 chunking and content-length handling.
- `ZhttpH3.hh`
  - `Zhttp::H3::Parser`
  - `Zhttp::H3::Builder`
  - H3 frame encoding/decoding helpers.
  - H3 connection-stream parser.
  - QPACK table integration hooks.

This keeps the public API stable while making ownership clear. `Zhttp.hh`
should become an aggregate over these headers, not the implementation home for
both protocols.

### 4. Transport Session Layer

Add reusable transport/session adapters after the message split is complete.
These adapters should be role-neutral where possible and specialized by CRTP
policy where client and server behavior differs.

HTTP/1 session mechanics:

- TCP/TLS connection callbacks.
- Request or response stream creation/use.
- Message emission through `H1::Builder`.
- Message parsing through `H1::Parser`.
- HTTP/1 connection close, content-length, and chunked completion behavior.
- Error and disconnect propagation.

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

Client sessions should support outbound requests and inbound responses.
Server sessions should support inbound requests and outbound responses. The
transport layer should not bake in request/response direction assumptions that
prevent either role.

These adapters may live in `ZhttpH1.hh` and `ZhttpH3.hh` if they are template
only. If they introduce non-template implementation, add corresponding `.cc`
files and update `zhttp/src/Makefile.am`.

The transport adapters may depend on `Zi`, `Ztcp`, `Ztls`, and `Zquic` as
appropriate. Keep those dependencies out of the codec layer.

### 5. Application Layer

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
- `ZiMultiplex` configuration (threads, etc.);
- `Ztls` configuration.

`zhttpclient` should keep only example-specific policy and payload behavior:

- CLI options and usage text.
- URL parsing and redirect policy.
- Alt-Svc interpretation and retry/fallback policy.
- CA/output file configuration.
- Response body sink and logging.
- Request header values such as `user-agent` and `accept`.
- Overall run orchestration.

`zhttpclient` should not own generic transport machinery such as local H3
control/QPACK stream setup, peer QPACK stream parsing, response stream routing,
or reusable link glue.

## State Ownership

`State` in `zhttpclient` should be split by ownership as an example of the
general boundary.

`zhttpclient`-owned state:

```c++
URL		url;
Options		options;
ZtString<>	location;
unsigned	status = 0;
ZiFile		bodyFile;
int64_t		contentLength = -1;
uint64_t	bodyBytes = 0;
unsigned	bodyChunks = 0;
bool		bodyFileOpen = false;
bool		chunked = false;
bool		redirect = false;
bool		framingLogged = false;
bool		done = false;
bool		failed = false;
```

Application policy may also retain Alt-Svc metadata because upgrade policy is
not a protocol invariant:

```c++
ZtString<>	altSvcHost;
uint16_t	altSvcPort = 0;
bool		altSvcH3 = false;
```

Transport/session-owned state:

```c++
Protocol::T			protocol;
Zhttp::H3::CxnState::T	h3State;
int64_t				responseStreamID;
bool				peerControl;
bool				peerEncoder;
bool				peerDecoder;
Zhttp::H3::QPackRxTable	qpackRxTable;
Zhttp::H3::QPackTxTable	qpackTxTable;
```

Stream refs, parser pointers, and QPACK encoder/decoder stream writers are also
transport/session state and should move out of the example.

For a server, analogous transport-owned state includes listener/session state,
accepted connection state, active request stream IDs, response stream routing,
and per-connection QPACK tables. Application state remains request/response
payload state and service policy.

## Callback Boundaries

Application callbacks should be message-shaped and role-neutral.

Inbound message parser callbacks:

```c++
void operation(Method::T method, ZuBSpan path);	// request
void status(unsigned);				// response
void contentLength(uint64_t);
void xferCompression(XferCompression::T);
void chunked();
template <typename Key> void header(ZuBSpan);
template <typename Key, typename Value> void header();
void body(ZuBSpan);
void complete(ParserState::T);
```

Outbound message builder callbacks:

```c++
void operation(Method::T method, ZuBSpan path);	// request
void status(unsigned);				// response
void contentLength(uint64_t);
template <typename Key> void header(ZuBSpan);
template <typename Key, typename Value> void header();
void body(ZuBSpan);
void complete(ParserState::T);
```

The callback set is intentionally symmetric. A client and server differ in
which side of the message pair they use first; the application-facing concepts
are the same.

Transport callbacks should not leak into application code unless the app is
customizing transport behavior:

```c++
Zhttp::H3::QPackRxTable *qpackRx();
Zhttp::H3::QPackTxTable *qpackTx();
Zhttp::H3::QPackEncoderTx *qpackEncoderTx();
Zhttp::H3::QPackEncoderTx *qpackDecoderTx();
Zhttp::H3::CxnState::T h3State() const;
void h3State(Zhttp::H3::CxnState::T);
uint64_t streamID() const;
```

The transport/session layer should implement these for normal clients and
servers.

## Migration Plan

### Phase 1: Split Message Headers Without Behavior Change

1. Add `ZhttpH1.hh` and move the current `Zhttp::H1` parser/builder code from
   `Zhttp.hh` into it.
2. Add `ZhttpH3.hh` and move the current `Zhttp::H3` parser/builder/frame
   helpers from `Zhttp.hh` into it.
3. Keep `Zhttp.hh` as the aggregate application header:
   `ZhttpUtil.hh`, `ZhttpHPack.hh`, `ZhttpQPack.hh`, `ZhttpH1.hh`,
   `ZhttpH3.hh`.
4. Update `zhttp/src/Makefile.am` header installation.
5. Preserve all existing CRTP contracts and test behavior.

Acceptance:

- `make -C zhttp` passes.
- `ZhttpParserTest`, `ZhttpHPackTest`, `ZhttpQPackTest`, and
  `ZhttpQPackDynamicTest` pass.
- `zhttpclient` compiles without behavior changes.

### Phase 2: Define Role-Neutral Message Facades

Add or clarify application-facing CRTP/facade types that represent individual
messages, independent of transport and role:

- request parser facade;
- response parser facade;
- request builder facade;
- response builder facade;
- common completion semantics where `complete()` means end of message.

These facades can wrap the existing efficient low-level H1/H3 parser and
builder templates. They should hide QPACK tables, stream IDs, H3 connection
state, and peer control stream handling from ordinary applications.

Acceptance:

- Application code can express "parse request", "parse response", "build
  request", and "build response" without naming H1/H3 internals.
- Existing low-level H1/H3 parser and builder tests continue to pass.

### Phase 3: Extract HTTP/3 Session Mechanics

Move from `zhttpclient` into reusable H3 transport/session templates:

- `openH3LocalStreams`;
- `QPackStreamTx`;
- peer control/QPACK stream tracking;
- `QUICClient::Stream`-style `CxnParser` integration;
- QPACK Rx/Tx table ownership;
- request/response stream routing;
- H3 message send path using `H3::Builder`;
- H3 message parse path using `H3::Parser`.

Design this for both roles:

- client: outbound request, inbound response;
- server: inbound request, outbound response.

Acceptance:

- `zhttpclient` no longer defines `openH3LocalStreams`, QPACK stream writers,
  peer QPACK stream checks, or H3 connection-stream parser glue.
- The H3 session API does not encode client-only naming or assumptions.
- H3 dynamic QPACK tests still pass.
- H3 interop tests still compile and pass where available.

### Phase 4: Extract HTTP/1 Session Mechanics

Move from `zhttpclient` into reusable H1 transport/session templates:

- TCP/TLS connection callbacks;
- request/response stream handling;
- H1 message send path using `H1::Builder`;
- H1 message parse path using `H1::Parser`;
- message completion and disconnect propagation.

Design this for both roles:

- client: connect then send request and parse response;
- server: accept then parse request and send response.

Acceptance:

- `zhttpclient` no longer defines generic `CliLink` logic for H1.
- The H1 session API does not encode client-only naming or assumptions.
- Existing H1 parser tests and the example build pass.

### Phase 5: Rebuild `zhttpclient` Around Policy

After the reusable adapters exist, reduce `zhttpclient` to:

- CLI parsing;
- URL and redirect handling;
- Alt-Svc parsing and upgrade policy;
- body file sink;
- request/response callback types;
- high-level run selection.

Do not move redirect policy, output-file policy, CLI, or example logging into
the library.

`runH3DNSFirst` and `runH1AltSvcFirst` are client policy orchestration. They
may remain in `zhttpclient`, or become example-level helper templates if
multiple client examples need them. They should not be part of the core
protocol or role-neutral session layer.

### Phase 6: Add a Server Proving Example

Add a small server example or test once the session layer exists. It should
exercise the same application-facing message surface from the opposite role:

- parse inbound requests;
- build outbound responses;
- handle bodies and completion as message events;
- avoid direct QPACK, H3 connection-stream, or HTTP/1 framing code.

This prevents the API from drifting back into a client-only shape.

## Non-Goals

- Do not merge HPACK, QPACK, H1, and H3 into one header again.
- Do not add compatibility aliases for old internal names.
- Do not make dynamic QPACK mandatory.
- Do not make `zhttp` own application redirect, URL, CLI, filesystem,
  authentication, authorization, routing, or service policy.
- Do not hide protocol errors from the application; surface them as structured
  message/session completion or failure callbacks.
- Do not name core session abstractions as clients when they apply equally to
  servers.

## Final Acceptance Criteria

- `Zhttp.hh` is a compact public aggregate over protocol-specific headers.
- H1 and H3 message code live in separate protocol headers.
- Application-facing parser and builder surfaces cover request and response
  messages for both client and server roles.
- Transport/session adapters own reusable link, stream, and QPACK wiring.
- Applications implement message callbacks and policy, not protocol mechanics.
- `zhttpclient` is materially smaller and mostly reads as an example of client
  request/response policy.
- At least one server example or test proves the same abstractions work for
  inbound requests and outbound responses.
- Existing zhttp unit tests and example builds pass.
