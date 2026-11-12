# Zhttp HTTP/1.1, HTTP/2, and HTTP/3

`Zhttp` owns production HTTP behavior. HTTP/1.1 uses `Ztcp` or `Ztls`,
HTTP/2 uses `Ztls`, and HTTP/3 uses `Zquic`. HPACK, H2, QPACK, H3, ALPN
selection, physical-session management, and logical request streams remain
inside `zhttp/src`.

Initialize HTTP hubs and client pools before starting their `ZiMultiplex`.
Default hubs own and register their typed receive/transmit allocators during
initialization. Registration requires the multiplexer to be stopped.

Applications that discover additional TCP/TLS destinations while running can
register matching typed allocator pairs before startup and retain them in
`TCPConfig::rxAllocator/txAllocator` or `TLSConfig::rxAllocator/txAllocator`
(`H2Config::tls()` exposes the TLS configuration). TLS also requires a prepared
socket `RxTCP` allocator in `TLSConfig::socketRxAllocator` and a packet `TxTCP`
allocator in `TLSConfig::txPacketAllocator`. Obtain all four TLS roles from the
same stopped multiplexer before initializing later hubs. The configuration
stores borrowed allocator pointers; the multiplexer owns the instances. The policy
buffer type and geometry must match the selected transport's
`NativeTraits<Profile>::Transport::RxAllocator/TxAllocator` types. Endpoints
retain the original receive buffers.

The HTTP/3 surface includes ordinary messages and ordered Extended CONNECT
logical streams:

- ALPN `h3` is configured only when HTTP/3 is enabled;
- each connection sends one control stream with SETTINGS first;
- QPACK encoder and decoder streams are opened by default, including the
  zero-capacity profile;
- request and response HEADERS are QPACK field sections using RFC 9204
  prefix integers, static indexed fields, static-name references, and
  literal-name field lines;
- DATA frames carry streaming bodies without hidden full-body buffering;
- receive-side trailers are decoded as regular-field HEADERS; streaming response
  producers emit final fields through `BodyEmitter::trailers`;
- cancellation maps to QUIC reset and stop-sending behavior;
- GOAWAY is emitted and parsed as HTTP/3 graceful-shutdown state distinct from
  QUIC transport close.

QPACK defaults are conservative: dynamic table capacity and blocked streams are
zero unless configured. Applications may allowlist fields for indexing with
`qpackIndex()` and mark sensitive fields with `qpackNeverIndex()`. Sensitive
headers such as `authorization`, `cookie`, and `set-cookie` are never-index by
default. Dynamic table state and instructions are bounded by the advertised
maximum. Dynamic HEADERS encoding emits QPACK encoder-stream insert
instructions and may use relative dynamic indexed field lines once the encoder
knows the peer has received the referenced entries. Dynamic field-section
references are decoded through the connection's QPACK decoder state when they
are immediately satisfiable. Non-zero blocked-stream buffering is not part of
this profile. QPACK encoder and decoder stream bytes are consumed as ordered
instruction sequences by the connection.

For `http:` URLs, the example client always uses HTTP/1.1 over `Ztcp`; if a
redirect moves the request to `https:`, HTTPS policy then applies. For `https:`
URLs, the default `-3 prefer` / `--http3=prefer` mode tries DNS-advertised
HTTP/3 first, then makes one TLS connection offering HTTP/2 and HTTP/1.1.
TLS ALPN selects the HTTP version without an application-managed second
connection attempt. Alt-Svc can promote later requests to HTTP/3.
`-3 force` / `--http3=force` skips DNS and Alt-Svc probing and runs HTTP/3
over QUIC directly for `https:` requests. `-3 disable` / `--http3=disable`
disables HTTP/3 probing. `-2 force|prefer|disable` /
`--http2=force|prefer|disable` controls TLS ALPN policy; the default is
`prefer`. `-v`/`--verbose` shows DNS and Alt-Svc probing. IPv6, DoH, DoT, ECH,
WebTransport, DATAGRAM, and QUIC v2 discovery remain out of scope for this
resolver path.

URL syntax is centralized in `ZhttpURL.hh`. `URLView` is a borrowed view over a
mutable input span and normalizes DNS host names in place; `URL` is the explicit
owning form, with `assign()` for copying immutable input and `adopt()`
for transferring an existing `URLString`. `Target` represents the HTTP
request-target forms and exposes mutable borrowed `raw`, `path`, and `protocol`
spans; `path` contains the complete path plus optional query.
Alt-Svc parsing, formatting, cache policy, and HTTPS discovery are grouped in
`ZhttpDiscovery.hh`.

`libZhttp` is the HTTP integration layer over `libZtcp`, `libZtls`, and
`libZquic`.  Applications configure TCP, TLS, and QUIC with public `Zhttp`
configuration types; HTTP ALPN and mandatory H3 transport defaults are
library-owned.  Native transport traits remain private implementation detail.
For complete HTTP applications, `Zhttp::Client` coordinates an
application-sized array of destination-specific pools. Each `Zhttp::Pool`
owns its transmit queue, routing/discovery state, request policy, active
requests, and stable link slots. A link owns at most one current connection,
but a connection can carry multiple overlapping operations up to its
configured per-link concurrency. Pool concurrency, link count, and per-link
concurrency are independent. `Zhttp::Server` owns server listeners, admission,
sessions, message selection, and hub lifecycle. Applications provide protocol
configuration and message-typed application contracts:

`ZhttpHeaders(...)` expands to alternating key and value-list types. A header
without a fixed value has an empty `ZuTypeList<>`; a Parser may declare several
fixed values for static dispatch, while a Builder fixed-value list is a
singleton. For each Builder fixed pair, `header<Key, Value>(emit)` controls
whether it is present by calling `emit()` zero or one times.

`Builder::neverIndex(name)` defaults to `false` and is evaluated once for each
emitted field. Override it for runtime metadata that must use HPACK/QPACK
never-index representations, including matches to static or public seeded
values. Existing sensitive-name defaults still apply. Put sensitive values in
runtime declared or dynamic fields; fixed catalog values are public compression
seeds. H1 evaluates the same policy while emitting ordinary text fields.

```c++
struct ReqBuilder_ : ZmObject, Zhttp::ReqBuilder {
  using HdrCatalog = RequestHeaders;
  void init(struct App &);
  Zhttp::BodyPolicy::T bodyPolicy() const {
    return put ? Zhttp::BodyPolicy::OptionalFixed : Zhttp::BodyPolicy::None;
  }
  Zhttp::URLString target{"/"};
  uint64_t key() const;
  uint64_t length() const { return 1; }
  bool idempotent(Zhttp::Method::T method) const;

  template <typename L>
  void operation(L &&l) const {
    l(Zhttp::Method::GET, [](auto &&emit) {
      emit([](auto &tx) { tx << "/search?q=test&page=2"; });
    });
  }
  template <typename Key, typename L> void header(L &&);
  template <typename L> void header(L &&);
  template <typename Emit> void body(Emit &&);
  template <typename L> void bodyHdrs(L &&);

  void connected(const Zhttp::ConnectedInfo &);
  void disconnected(bool peer);
  void connectFailed(bool transient);
  void selected(
    const Zhttp::Endpoint *, uint64_t request, uint64_t attempt,
    unsigned pool, unsigned link,
    Zhttp::Transport::T, Zhttp::Version::T);
  void attemptFailed(
    uint64_t request, uint64_t attempt, unsigned pool, unsigned link,
    unsigned status, Zhttp::Transport::T, Zhttp::Version::T,
    bool transient, bool responseStarted);
  void redirected(
    const Zhttp::URLView &, uint64_t request, uint64_t attempt,
    uint64_t previousAttempt, unsigned status, uint16_t redirects);
  void retried(
    uint64_t request, uint64_t attempt,
    uint64_t previousAttempt, uint16_t retries);
  void fallback(
    uint64_t request, uint64_t attempt, uint64_t previousAttempt,
    Zhttp::Transport::T fromTransport, Zhttp::Version::T fromVersion);
  void cancelled(const Zhttp::Result &);
  void completed(const Zhttp::Result &);
};

struct ResParser : Zhttp::Parser {
  using Headers = ResponseHeaders;
  void init(const ReqBuilder_ &);
  void status(unsigned);
  bool bodyInfo(Zhttp::BodyType::T, uint64_t);
  template <typename Key>
  void header(Zhttp::FieldSection::T, ZuBSpan);
  void header(Zhttp::FieldSection::T, ZuBSpan key, ZuBSpan value);
  template <typename Rx> bool body(Rx &);
  template <typename Link> void complete(Link *, bool);
  void reset();
};

struct App;
struct Pool;
using ReqBuilderQ = ZmPQueue<ReqBuilder_,
  ZmPQueueOverlap<false, ZmPQueueNode<ReqBuilder_>>>;
using ReqBuilder = ReqBuilderQ::Node;
using TxQ = ZmPQTx<Pool, ReqBuilderQ, ZmPQTxOrdered<false>>;
struct Pool : ZmNHeap<Pool>,
  Zhttp::Pool<App, TxQ, ResParser, Zhttp::AllProfiles> {
  friend inline ZuStringT<"App.Pool"> ZmNHeapID(Pool *);
  using Base = Zhttp::Pool<App, TxQ, ResParser, Zhttp::AllProfiles>;
  Pool(App *app) : Base{app} { }
  ReqBuilderQ *txQueue() { return &requests; }
  void archive_(ReqBuilder *) { }
  ZmRef<ReqBuilder> retrieve_(uint64_t, uint64_t) { return {}; }
  ReqBuilderQ requests;
};
struct App : Zhttp::Client<App, Pool> {
  void idle() { }
};
App client;
client.init(
  Zhttp::HubConfig{&mx, "rx", "tx"},
  1,
  Zhttp::Config{}
    .links(10)
    .concurrency(100)
    .linkMax(10)
    .protocol(Zhttp::ProtoPolicy::PreferH3)
    .h2Policy(Zhttp::H2Policy::Prefer)
    .retainedBodyMax(uint32_t(-1))
    .retainedMessageMax(uint32_t(-1)),
  Zhttp::TCPConfig{},
  Zhttp::H2Config{}.tls([&ca](auto &tls) {
    tls.caPath(ca);
  }),
  Zhttp::QUICConfig{}.caPath(ca));
client.pool(
  0, Zhttp::Destination{"www.example.com", 443});
client.start();
ZmRef<ReqBuilder> request = new ReqBuilder;
request->target = "/resource?version=1";
client.send(0, request);
client.seal(0);
client.limited(0, 3, true);  // stop future assignments to link slot 3
client.limited(0, 3, false); // make it eligible again when not saturated
// ReqBuilder_::body() runs synchronously on Tx;
// ResParser::body() runs synchronously on Rx.
client.stop([](bool) { /* shutdown continuation */ });
// The main thread waits for that continuation before final().
client.final();
```

The seven request-event callbacks shown above are optional. `Zhttp::Pool`
detects each exact signature on the final `ReqBuilder` type and prepares its
arguments only when that callback exists. `Zhttp::ReqBuilder` does not provide
no-op implementations for them.
Client and pool completion/failure counts include the delivered result before
`cancelled()` or `completed()` runs.

```c++
struct ResBuilder_ : ZmObject, Zhttp::ResBuilder {
  using HdrCatalog = ResponseHeaders;
  Zhttp::Method::T method() const;
  // bodyPolicy(), status(), headers, and body production
};
using ResBuilderQ = Zhttp::ResponseQ<ResBuilder_, ZuStringT<"App.ResBuilder">>;
using ResBuilder = ResBuilderQ::Node;

struct App;
struct Parser : Zhttp::Parser {
  using HdrCatalog = RequestHeaders;
  void init(App &);
  bool operation(Zhttp::Method::T, Zhttp::Target &);
  template <typename Key>
  void header(Zhttp::FieldSection::T, ZuBSpan);
  template <typename Rx> bool body(Rx &);
  template <typename Link>
  void complete(Link *link, bool ok) {
    if (!ok) return;
    ZmRef<ResBuilder> response = new ResBuilder{};
    // Populate response from application-owned request state.
    link->send(ZuMv(response));
  }
  void reset();
};

struct App : Zhttp::Server<App, ::Parser, ::ResBuilderQ, Zhttp::AllProfiles> {
  using HTTP = Zhttp::Server<App, ::Parser, ::ResBuilderQ, Zhttp::AllProfiles>;
  using HTTP::connected;
  using HTTP::disconnected;
  // listening(), listenFailed(), connected(), disconnected()
};

App app;
app.init(
  Zhttp::HubConfig{&mx, "rx", "tx"},
  Zhttp::ServerConfig{}
    .port(port)
    .tcp(Zhttp::TCPConfig{})
    .tls(Zhttp::H2Config{}.tls([&cert, &key](auto &tls) {
      tls.certPath(cert).keyPath(key);
    })
      .policy(Zhttp::H2Policy::Prefer))
    .quic(Zhttp::QUICConfig{}.certPath(cert).keyPath(key)));
app.start();
// Parser callbacks run synchronously on Rx and receive callback-scoped spans.
// Channel::send() accepts an owning response from any thread. Server queues it on
// that link, then calls its Builder operations on Tx. Applications must submit
// asynchronous H1 responses through Route, which preserves request order.
// disconnected(transport) observes admission release on the owning Rx shard.
server.stop([](bool) { /* shutdown continuation */ });
// The main thread waits for that continuation before final().
server.final();
```

Responses are application-owned structural Builders in `ResponseQ`; the core
queues their embedded intrusive links. The final `ResBuilder` is `ResBuilderQ::Node` and publicly derives
the application `ResBuilder_`; each node represents one response. The
application Parser copies only the request data it needs while the callback
spans are valid, and may dispatch synchronously or asynchronously. The server
performs neither copy nor dispatch:

```c++
void Parser::complete(Zhttp::Channel *channel, bool ok) {
  if (!ok) return;
  Zhttp::Route<ResBuilder> route{channel, request.method};
  app->dispatch([route = ZuMv(route), request = ZuMv(request)]() mutable {
    ZmRef<ResBuilder> response = new ResBuilder{};
    response->plan(request);
    route.send(ZuMv(response));
  });
}
```

`bodyPolicy()` may be `constexpr` when a Builder always uses one policy, or a
run-time value when one type-erased Builder dispatches to different message
implementations. Its value remains fixed for the duration of one message.

A single response type can select its body policy at run time. Fixed and
streaming responses both use synchronous `body(emit)` production turns.

`BodyPolicy::None` is allocation-free. `BodyPolicy::Fixed` and
`BodyPolicy::OptionalFixed`
retain the complete message while `bodyHdrs()` patches body-dependent header
values. `Config` and `ServerConfig` bound retained entity and complete
message sizes with `retainedBodyMax()` and `retainedMessageMax()`; the fixed
entity limit is additionally capped at `UINT_MAX`. `BodyPolicy::Stream` and
`BodyPolicy::OptionalStream` release buffers as they fill; H1 maps them to
chunked transfer encoding and H2/H3 map them to DATA and their native final
boundary.

Receive parsers take independent limits at run time from the effective client,
pool, or server configuration. `bodyMax()` bounds cumulative decoded bytes and
Content-Length for one message (default `UINT_MAX`); `retainedBodyMax()` bounds
currently queued decoded bytes. Consumed or discarded bytes release retained
capacity, and a DATA payload larger than that capacity is delivered in bounded
fragments. H3 delivers available DATA prefixes and returns consumed-byte credit
before the rest of the frame arrives, including frames larger than the QUIC
receive window. H1 likewise delivers available fixed-length, chunked and
EOF-delimited body prefixes; chunk terminators and EOF still govern framing
acceptance and completion. A consumer that fills the queue without consuming
causes a bounded receive failure. `retainedMessageMax()` retains its output-only
role, and `bodyMax()` does not change fixed-output limits. The
response/request parser types do not define a compile-time `BodyMax`. After
the final header section,
`bodyInfo(type, length)` reports `None`, `Streamed`, or `Fixed` framing
(`Fixed, 0` is a valid empty body). It returns `true` to accept that body type
and length, or `false` to reject the message with the same consequences as
`body()` returning `false`. Every selected or run-time `header` callback
receives its `Zhttp::FieldSection::T`, including informational and trailer
fields. Field callbacks precede validation of the complete section, so a field
alone does not establish HTTP acceptance. `bodyInfo` follows accepted final
initial fields and can receive the borrowed channel to acquire a server `Route`
before request DATA completes. Trailing fields follow body prompts, and normal
`complete(channel, true)` establishes acceptance and receive end, including
HEADERS followed by delayed H3 FIN. Copy selected metadata during its borrowed
field callback and publish terminal status only at normal completion. Failure
completes once with `false`; completion is followed immediately by parser reset.

H3 also retires partial HEADERS and QPACK instruction bytes while assembling
their bounded payloads in pooled storage. SETTINGS pairs and unknown extension
payloads drain incrementally. Field-section and instruction limits remain
independent of the native QUIC receive window.

Every writer is invoked synchronously with a queue-backed Tx stream and returns
`WriteOutcome::End`, `Stream`, `Abort`, or `Failed`. The library never retains
the writer. A server producer returning `Stream` may retain the `emit` callable
and invoke it again when ready; the application must dispatch retained calls to
the response Tx shard itself. Immediate sequential turns and zero-byte turns
are valid. Queue refusal or an invalid Tx stream fails the response.

`End` emits the native final boundary. `Abort` intentionally terminates the
stream: H1 closes the connection without draining, while H2 and H3 reset only
the logical stream. A streaming Builder must not emit `content-length`.
`ResBuilder::close()` invalidates application producer state and requires the
Builder to destroy its retained emitter. A later call through another queued
copy returns `false` without invoking its writer. `disconnect() const` is the
separate query for closing a connection after a successful response.

An ordinary early response preserves its complete output before disposing an
unfinished request. H2 follows terminal response output with RST_STREAM(NO_ERROR).
H3 stops request reception with H3_NO_ERROR after the response FIN is acknowledged;
it does not reset response output. Discarded input returns receive credit and
receives no further body prompts. H1 clients close an unfinished request's
connection before assigning another request to it.

After a streaming response turn returns `Stream`, H2 and H3 producers can call
`emit.trailers(fields)` on Tx, outside a writer. The field provider follows
`Builder`'s catalog, header overloads and `neverIndex` policy; only its fields
are used. Values are rendered synchronously into bounded pooled storage and
need live only through the call. Mutable-header offsets and patch hooks are
not used for trailers. Pseudo-fields and Content-Length are rejected.

A `true` trailer result admits the terminal section behind all accepted DATA,
including data waiting for flow-control credit. It seals producer admission;
`close()` runs once outside an active body callback, while the existing response
queue retains the response until its completion notification. Compression runs
when the DATA prefix is eligible: H2 sends HEADERS with END_STREAM; H3 sends
HEADERS followed by FIN. H1, fixed-body and invalidated emitters return `false`
without calling the provider. A rendering or queue failure terminates the
producer through its existing result path. The result means admission, not
transport delivery. A trailers-only response uses the ordinary no-body path.

Client streaming request producers can retain `BodyEmitter` across turns.
Return `Stream` to keep output open and `End` to half-close the request while
response reception continues. Override `replayable()` to false for multi-turn
production; a first `Stream` result with replay enabled fails the request.
`ReqBuilder::close()` runs once on Tx after emitter invalidation, outside the
initial body callback and active writer, and drops retained application emitters.
Every client Builder must reproduce the same message when a retry or redirect
traverses it again. Fixed-body requests retain their single-turn writer contract.
`zhttp/itest/zhttpclientstreamtest` exercises native H2/H3 retained production,
H1 request half-close, replay rejection, early replies, failure, stop and bounded
producer allocation over many turns. Channel cancellation is ordered through Tx
producer invalidation before native teardown; a held-Tx regression verifies
queued writes on H1/H2/H3. It also probes retained emitter copies
after request retirement and client finalization. Peer-abort cases hold client
Tx while an independent peer resets H2/H3: accepted writers drain before native
handles are released. Build the module's `all` target before running
`make -C zhttp/itest test`.
`ReqBuilder::replayable()` defaults to `true`. Override it to `false` before
submission to prohibit every subsequent HTTP attempt, including unprocessed
attempts, redirects, retries and transport fallback. The policy is sampled once
at submission and survives wire resets; refusal reports `ReplayUnsafe` with the
original status and attempt identity. Initial transport discovery remains allowed.
`ReqBuilder::idempotent(method)` defaults to the standard HTTP method
semantics.  Override it when application semantics make a normally
non-idempotent method, such as `POST`, idempotent. It cannot override
`replayable() == false`. For replayable requests, Zhttp may repeat any
attempt known not to have been applied; after possible application it repeats
only an idempotent request.  Redirects which repeat the method and body use the
same rule.

`Result::errorDomain` and `errorCode` retain the first native stream diagnostic
for the completed attempt. The neutral defaults are `ErrorDomain::None` and
zero. H2 reset and GOAWAY codes remain distinct; H3 distinguishes received
RESET_STREAM, STOP_SENDING, local stream errors and GOAWAY. H3 GOAWAY has no
error code, so its code is zero. These diagnostics survive subsequent local
cancellation and Tx drain, and reset when a new wire attempt starts. A reset
after successful response completion cannot replace that response's result.
HTTP/1 has no equivalent stream-reset code and keeps the neutral defaults.

```c++
template <typename Emit>
void body(Emit &&emit) {
  emit([this](auto &body) {
    ZfJSON::save(body, record); // concrete layered ZiTxStream
    contentLength = body.produced();
    return body ? Zhttp::WriteOutcome::End :
      Zhttp::WriteOutcome::Failed;
  });
}

template <typename L>
void bodyHdrs(L &&l) {
  l.template operator()<ZuStringT<"content-length">>(
    [n = contentLength](ZuSpan<uint8_t> span) {
      ZuStream<uint8_t> s = span;
      s << ZuBoxed(n).fmt<ZuFmt::Right<10>>();
    });
}

// Streaming Builder alternative:
template <typename Emit>
void body(Emit &&emit) {
  retainedEmit = emit;
  emit([this](auto &body) {
    body << initialBytes;
    return Zhttp::WriteOutcome::Stream;
  });
}

// Dispatched by the application to the response Tx shard.
void nextTurn() {
  retainedEmit([this](auto &body) {
    body << laterBytes;
    return finalTurn ? Zhttp::WriteOutcome::End :
      Zhttp::WriteOutcome::Stream;
  });
}

void abortTurn() {
  retainedEmit([](auto &) { return Zhttp::WriteOutcome::Abort; });
}

void close() {
  unsubscribeProducer();
  retainedEmit = {};
}

template <typename Rx>
void body(Rx &rx) {
  Zhttp::bodyEach(rx, [](ZuBSpan bytes) {
    consume(bytes);                    // span is callback-scoped
  });
}
```

Client body emitters, writers, and late-header patchers are synchronous and
callback-scoped. Server Parser callbacks are likewise synchronous on Rx. The
application may leave a trailing incomplete application frame in the bounded
Rx queue; later body appends prompt it again without copying or coalescing the
retained spans. An application which needs contiguous or asynchronous request
state owns that copy. `Result` retains the client-side request/response byte
diagnostics.

When TLS and QUIC are enabled together, `Server` emits the HTTP/3 Alt-Svc
header on TLS responses. `altSvcMaxAge()` controls its lifetime; zero disables
advertising. No HTTP/3 response branch belongs in the application.

Extended CONNECT is opt-in with `H2Config::extendedConnect(true)` for H2 and
`QUICConfig::extendedConnect(true)` for H3. `libZhttp` advertises and parses
`SETTINGS_ENABLE_CONNECT_PROTOCOL`, tracks local and peer capability
independently, and refuses to emit or accept `:protocol` without the relevant
capability. A successful 2xx handshake transitions the logical H2 or H3
stream to `ParserState::Stream`. H1 Upgrade and H2/H3 Extended CONNECT then
expose the same queue-backed, shard-affine logical-stream contract. Rx is
passed separately to `process()`, explicit callbacks report peer end/reset,
and Tx is callback-scoped:

```c++
// channel is borrowed; retain ChannelRef explicitly for asynchronous use.
if (channel->streamPeerCap()) {
  channel->streamTx([](Zhttp::Writer &tx) {
    tx << bytes;
    return tx.flush();
  });
  channel->streamTxEnd();
}

// The framing state owns these synchronous callbacks through shutdown.
Zhttp::FrameSink frames{
  &state,
  [](void *ctx, Zhttp::Channel *channel, Zhttp::Input &rx) {
    return static_cast<FrameState *>(ctx)->process(*channel, rx);
  },
  [](void *ctx, Zhttp::Channel *channel) {
    static_cast<FrameState *>(ctx)->peerEnd(*channel);
  },
  [](void *ctx, Zhttp::Channel *channel) {
    static_cast<FrameState *>(ctx)->error(*channel);
  }};
channel->streamAccept(frames);
```

The Rx queue reference and pooled writer are valid only for their
respective callback durations; unread Rx nodes remain owned by the logical
stream between prompts. A negative `process()` result resets only the affected
logical stream. The logical-stream contract contains no WebSocket fields,
framing, masking, close codes, or subprotocol policy; those belong in a
dependent protocol library.

Public endpoints require an explicit `Profiles<...>` selection. The pool owns
its client's selection; `Client<App, Pool>` adds no second selection. Use
`AllProfiles` for tools that deliberately support every profile. Repeated tags
are deduplicated. H1TLS and H2TLS share one TLS backend when selected together.
Only selected transport accessors are available. A server still activates each
family explicitly through `ServerConfig::tcp`, `tls`, or `quic`. Client defaults
are derived from the selection after configuration overlays.

Parsers and builders use the typed callback interfaces above. Wire parsers,
compression tables, native hubs and links are private library implementation.
A borrowed `Channel *` identifies the actual connection or logical stream;
retain a `ChannelRef` explicitly when asynchronous work needs it. `Route<Builder>`
retains that channel together with its request sequence for ordered responses.
Frame protocols accept a `FrameSink` through `channel->streamAccept(sink)` and
write directly through the pooled `Writer` supplied by `streamTx` or `streamTx_`.
`streamTx_` is Tx-owned; stream end and reset preserve that shard's ordering.

`Hubs`, client pools, `Client`, and `Server` require a completed stop before `final()`
(initialization failures that never started are also safe to finalize).
Finalization does not initiate another shutdown or wait for Rx/Tx. A service
owner can post finalization to its owning shard after the asynchronous stop
continuation; synchronous `stop()` remains a main-thread convenience. Clients
that own the process-wide resolver include its shutdown in stop completion;
its stopped resources can then be finalized without joining a control thread.
Worker-owned clients may use an externally managed resolver.

Client and pool engines cover the complete component lifecycle. A client's
startup includes its owned resolver and every configured pool; a failed pool
startup completes dependency rollback before reporting failure. A pool starts
its transports, publishes Rx state, and opens Tx queue admission before start
completion. Stop closes admission, cancels timers and discovery, waits for all
outstanding discovery posts, stops native transports, drains Tx queues and Rx
completion callbacks, and only then reports stopped. Repeated operations and
pending opposite operations use `ZmEngine` semantics. Restart after a completed
stop retains configuration and resets sealing and idle/admission state.

Native `Hubs` aggregates remain child controllers for their distinct transport
or pool collections. `Hubs::add` registers an already initialized, externally
owned component without reinitializing it, preserving pool configuration
failure isolation. The parent engine's completion is the application's complete
lifecycle boundary. Query/protocol failures do not make successful shutdown fail.

Configure H2's TLS base through `h2.tls()` or inline with
`H2Config{}.tls([](auto &tls) { tls.caPath("ca.pem"); }).policy(policy)`.
The lambda overload preserves the outer H2 type. TLS getters and setters are
available through `tls()`; H2Config does not expose an implicit TLSConfig
conversion or narrowing TLS setters. Use TLSConfig directly for an H1-only
TLS interface. TLSConfig and QUICConfig retain their own setters.

QUIC configuration defaults include H3 ALPN, control streams, QPACK, flow
control, and passive migration.  Applications override only the policy they
need.  The test client and server expose these migration policy switches:

- `--quic-migration=disable|disabled|passive|active` selects the local QUIC migration
  policy; the default is `passive`.
- `--quic-migration-cid-reserve=N` reserves spare peer CIDs for active
  migration; the default is `1`.
- `--quic-migration-close-on-failure` closes active migration attempts when a
  local UDP rebind failure makes fallback impossible.
- `--quic-migration-local=ADDR[:PORT]` sets the client local address used for
  active HTTP/3 migration tests; the default is the current local IP and an
  ephemeral port.
- `--quic-migrate-local` is a test-client switch that requests one HTTP/3
  client local UDP port migration after H3 control streams open.
- `--quic-migrate-after-headers` requests client migration after response
  headers are parsed.
- `--quic-migrate-after-bytes=N` requests client migration after N response
  body bytes for large-response tests.

H3 connection hooks receive transport-neutral `pathUpdate()`,
`migrationStarted()`, `migrationPromoted()`, and `migrationFailed()` callbacks.
Those callbacks must not reset QPACK, control streams, request streams, or
GOAWAY state; path migration is a QUIC transport path change for an existing
HTTP/3 connection.

The example server is a static file server:

```sh
mkdir -p /tmp/www
printf 'zhttp-ok\n' >/tmp/www/index.html
zhttpd /tmp/www --http --addr 127.0.0.1 --port 8080
```

For local TLS and HTTP/3 tests, generate a temporary localhost certificate:

```sh
openssl req -x509 -newkey rsa:2048 -nodes -days 1 \
  -subj /CN=localhost \
  -addext basicConstraints=critical,CA:TRUE \
  -addext keyUsage=critical,digitalSignature,keyEncipherment,keyCertSign \
  -addext subjectAltName=DNS:localhost,IP:127.0.0.1 \
  -keyout key.pem -out cert.pem
```

Then start the TLS or HTTP/3 transports:

```sh
zhttpd /tmp/www --https --addr 127.0.0.1 --port 8443 \
  --cert cert.pem --key key.pem
zhttpd /tmp/www --https --http2=force --addr 127.0.0.1 --port 8443 \
  --cert cert.pem --key key.pem
zhttpd /tmp/www --http3 --addr 127.0.0.1 --port 8443 \
  --cert cert.pem --key key.pem
zhttpd /tmp/www --http3 --addr 127.0.0.1 --port 8443 \
  --cert cert.pem --key key.pem --quic-migration=active \
  --quic-migration-cid-reserve=2
```

To serve HTTPS and HTTP/3 together and advertise HTTP/3 automatically, enable
both transports on the same port:

```sh
zhttpd /tmp/www --https --http3 --addr 127.0.0.1 --port 8443 \
  --cert cert.pem --key key.pem
```

Useful client checks:

```sh
zhttp -o body http://127.0.0.1:8080/
zhttp -c cert.pem -o body https://localhost:8443/
zhttp -3 disable -2 force -c cert.pem -o body https://localhost:8443/
zhttp -3 force -c cert.pem -o body https://localhost:8443/
zhttp -3 force -c cert.pem --quic-migration=active \
  --quic-migration-cid-reserve=2 --quic-migrate-after-headers \
  -o body https://localhost:8443/
```

The repeatable H2 interoperability gate runs the in-tree client/server pair,
curl against `zhttpd`, and `zhttp` against Caddy.  When `h2spec`, `nghttp`, or
`nghttpd` are already installed, it also runs those roles; absent optional
tools are reported as TAP skips and are never downloaded. Use a TLS 1.3-enabled
`h2spec` build. The upstream v2.6.0 Linux binary uses Go 1.12, where TLS 1.3
requires `GODEBUG=tls13=1`; rebuilding with current Go also enables negotiation.
See the [Go 1.12 TLS notes](https://go.dev/doc/go1.12#tls_1_3).

Run the gate with:

```sh
ZHTTP_MATRIX=./zhttp/itest/zhttpmatrix \
ZHTTP_CLIENT=./zhttp/util/zhttp \
ZHTTP_SERVER=./zhttp/util/zhttpd \
sh ./zhttp/interop/zhttph2interoptest.sh
```

Set `ZHTTP_H2_ARTIFACTS` to the failure-residue directory and
`ZHTTP_H2_PORT`/`ZHTTP_H2_NGHTTPD_PORT` when the default loopback ports are
occupied. Successful runs clean up their temporary files; the residue helper
retains recent failed runs with tool versions and separate client, server and
conformance output.

Static-server options include directory indexes and listings, MIME overrides,
single-file mode, hidden-dotfile rejection, Basic auth, host redirects,
HTTP-to-HTTPS redirects, keep-alive timeout, maximum accepted connections,
`ZiLog` file/syslog sinks, daemon/PID handling, Unix chroot, and Unix
uid/gid dropping. Chroot and uid/gid options are rejected on unsupported
platforms. Basic auth protects only credentials at the HTTP layer; use TLS for
confidentiality. Symlink escape prevention is not enforced by the first static
server pass.

First-release exclusions match the transport scope: no server push, no
WebTransport, no DATAGRAM, no 0-RTT, no QUIC v2, no multipath, and no active
ECN behavior.  QUIC path migration is single-active-path migration, not
multipath.

Qualification on 2026-10-18 used Linux 7.0.0-1006-aws, x86_64, Clang 21.1.8,
GNU C++2b and debug builds. The full tree passes `make -j4`. Normal and
ASan/LSan builds pass the HTTP unit suite (26 binaries, 253 groups), HTTP endpoint
suite (8 binaries, 87 groups), HTTP interoperability tools and affected core,
WebSocket, RPC, REST, JSON-RPC and MCP regressions. The sanitizer build uses
`-O1`, address instrumentation and leak detection; no memory suppressions were
added. GCC, ARM, Windows/MinGW and release builds remain untested.

Independent Go 1.26.8 H2/H3 peers and grpc-dotnet 10.0.12 H3 peers pass in both
roles, including applications compiled against staged installed public headers
on H2 and H3. The .NET SDK is 10.0.112 and MsQuic is 2.6.3. All seven H2 tool
roles pass; h2spec 2.6.0 reports 146 passed, zero skipped and zero failed.
Focused Valgrind 3.26.0 runs cover the 27 HTTP client-stream groups, 31 RPC client
groups and four server-only deadline groups, with zero errors and zero
definitely, indirectly or possibly lost bytes.

One initial interoperability-matrix run failed without case diagnostics. Its
82-case direct and harness reruns, the complete normal endpoint-suite rerun and
the final sanitizer endpoint suite passed; the initial cause is unconfirmed.

Native I/O pool sizing: `zhttp` and `zhttpd` expose `--rx-ring-size=N` and
`--tx-ring-size=N` (default 128, range 2..32768), shared across each multiplexer’s
receive sockets and inherited by producer-owned transmit pools. The integration
matrix sizes both directions from concurrent exchanges and complete response
fragments, with request/response/retirement headroom; the HTTP benchmark uses
four slots per concurrent small-body exchange, subject to the default floor and
kernel maximum. One-slot lifetime fixtures and retained-pool exhaustion tests
remain deliberately small.
