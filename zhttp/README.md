# Zhttp HTTP/1.1, HTTP/2, and HTTP/3

`Zhttp` owns production HTTP behavior. HTTP/1.1 uses `Ztcp` or `Ztls`,
HTTP/2 uses `Ztls`, and HTTP/3 uses `Zquic`. HPACK, H2, QPACK, H3, ALPN
selection, physical-session management, and logical request streams remain
inside `zhttp/src`.

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
- receive-side trailers are decoded as regular-field HEADERS, with send-side
  trailer emission available for the same profile;
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

URL syntax is centralized in `ZhttpURL.hh`. `URL` is a borrowed view over a
mutable input span and normalizes DNS host names in place; `URLStorage` is the
explicit owning form, with `assign()` for copying immutable input and `adopt()`
for transferring an existing `URLString`. `RequestTarget` represents the four
HTTP request-target forms and preserves path/query structure without reparsing.
Alt-Svc parsing, formatting, cache policy, and HTTPS discovery are grouped in
`ZhttpDiscovery.hh`.

`libZhttp` is the HTTP integration layer over `libZtcp`, `libZtls`, and
`libZquic`.  Applications configure TCP, TLS, and QUIC with public `Zhttp`
configuration types; HTTP ALPN and mandatory H3 transport defaults are
library-owned.  Native transport traits remain private implementation detail.
For complete HTTP applications, `Zhttp::Client` owns client routing,
discovery, pools, retries, redirects, fallback, and hub lifecycle, while
`Zhttp::Server` owns server listeners, admission, sessions, message
selection, and hub lifecycle. Applications provide protocol configuration
and message-typed application contracts:

```c++
struct Request_ : ZmObject {
  using Headers = RequestHeaders;
  Zhttp::BodyPolicy::T bodyPolicy() const {
    return put ? Zhttp::BodyPolicy::OptionalFixed : Zhttp::BodyPolicy::None;
  }
  Zhttp::URLStorage url;
  uint64_t key() const;
  uint64_t length() const { return 1; }

  void reset();
  template <typename L> void operation(L &&);
  template <typename L> void host(L &&);
  template <typename Key, typename L> void header(L &&);
  template <typename L> void header(L &&);
  template <typename Emit> void body(Emit &&);
  template <typename L> void bodyHdrs(L &&);

  bool replayable() const;
  bool reproducible() const;
  void connected(const Zhttp::ConnectedInfo &);
  void disconnected(bool peer);
  void connectFailed(bool transient);
  void selected(const Zhttp::Endpoint &);
  void redirected(const Zhttp::URL &);
  void observed(const Zhttp::ClientEvent &);
  void completed(const Zhttp::Result &);
};

struct ResParser {
  using Headers = ResponseHeaders;
  static constexpr uint64_t BodyMax = ResponseBodyMax;
  void init(const Request_ &);
  void status(unsigned);
  template <typename Key> void header(ZuBSpan);
  template <typename Rx> void body(Rx &);
  void complete(bool);
};

struct App;
using RequestQ = ZmPQueue<Request_,
  ZmPQueueOverlap<false, ZmPQueueNode<Request_>>>;
using Request = RequestQ::Node;
using TxQ = ZmPQTx<App, RequestQ, ZmPQTxOrdered<false>>;
struct App : Zhttp::Client<TxQ, ResParser> {
  RequestQ *txQueue() { return &requests; }
  void archive_(Request *) { }
  ZmRef<Request> retrieve_(uint64_t, uint64_t) { return {}; }
  RequestQ requests;
};
App client;
client.init(
  Zhttp::HubConfig{&mx, "rx", "tx"},
  Zhttp::ClientConfig{}
    .protocol(Zhttp::ProtocolPolicy::PreferH3)
    .h2Policy(Zhttp::H2Policy::Prefer)
    .retainedBodyMax(uint32_t(-1))
    .retainedMessageMax(uint32_t(-1)),
  Zhttp::TCPConfig{},
  Zhttp::H2Config{}.caPath(ca),
  Zhttp::QUICConfig{}.caPath(ca));
client.start();
ZmRef<Request> request = new Request;
client.enqueue(request);
client.seal();
// Request_::body() runs synchronously on Tx;
// ResParser::body() runs synchronously on Rx.
client.stop([](bool) { /* shutdown continuation */ });
// The main thread waits for that continuation before final().
client.final();
```

```c++
using Server = Zhttp::Server<Workload>;

Workload workload;
Server server;
server.init(
  Zhttp::HubConfig{&mx, "rx", "tx"},
  Zhttp::ServerConfig{}
    .port(port)
    .tcp(Zhttp::TCPConfig{})
    .tls(Zhttp::H2Config{}
      .certPath(cert).keyPath(key)
      .policy(Zhttp::H2Policy::Prefer))
    .quic(Zhttp::QUICConfig{}.certPath(cert).keyPath(key)),
  &workload);
server.start();
// Workload::Request is owned by one logical request. Its body(Rx &) consumes
// request input; respond(meta, request, emit) synchronously emits one Response
// on Tx. Server calls Response::reset() exactly once before status, headers,
// or body construction. committed() reports local transport admission and
// completed() reports
// the profile's terminal Tx outcome on Rx.  H1/H2 success means the final
// native socket/TLS buffer completed locally; H3 success means the peer
// acknowledged the stream FIN and all response bytes.  None of these events
// is an application-level acknowledgement.
// requestError(meta, request, error) chooses Continue or Disconnect for a
// recoverable request-scope error; protocol-mandated stream/connection errors
// are not offered to the application.
// disconnected(transport) observes admission release on the owning Rx shard.
server.stop([](bool) { /* shutdown continuation */ });
// The main thread waits for that continuation before final().
server.final();
```

Responses are structural Builders, not subclasses of a runtime Zhttp type.
One workload can select unrelated concrete response types per endpoint; each
type supplies `reset()`, `Headers`, `bodyPolicy()`, status/header operations, and
the operations required by its body policy:

```c++
template <typename Emit>
void Workload::respond(
    const Zhttp::RequestMeta &meta, Request &request, Emit &&emit) {
  if (meta.path() == "/health") {
    emit(EmptyResponse{204});
    return;
  }
  if (meta.path() == "/record") {
    emit(JSONResponse{request.record});
    return;
  }
  emit(FileResponse{lookup(meta.path())});
}
```

`bodyPolicy()` may be `constexpr` when a Builder always uses one policy, or a
run-time value when one type-erased Builder dispatches to different message
implementations. Its value remains fixed for the duration of one message.

`EmptyResponse`, `JSONResponse`, and `FileResponse` can have different header
typelists and body policies. `JSONResponse` can use synchronous
`body(emit)`/`bodyHdrs()` construction, while a streaming `FileResponse` can
provide asynchronous `next(max, done)` production.

`BodyPolicy::None` is allocation-free. `BodyPolicy::Fixed` and
`BodyPolicy::OptionalFixed`
retain the complete message while `bodyHdrs()` patches body-dependent header
values. `ClientConfig` and `ServerConfig` bound retained entity and complete
message sizes with `retainedBodyMax()` and `retainedMessageMax()`; the fixed
entity limit is additionally capped at `UINT_MAX`. `BodyPolicy::Stream` and
`BodyPolicy::OptionalStream` release buffers as they fill; H1 maps them to
chunked transfer encoding and H2/H3 map them to DATA and their native final
boundary.
Client stream writers remain synchronous.  A server streaming response exposes
`next(max, done)`: each turn produces at most one pooled `ZiIOBuf`, and the
server asks for the next turn only after Tx capacity is released.  Each retry
creates a fresh Builder. Body-bearing requests are non-replayable by default;
applications opt in only when the Builder can reproduce the source from byte
zero.

```c++
template <typename Emit>
void body(Emit &&emit) {
  emit([this](auto &body) {
    ZfJSON::save(body, record); // concrete layered ZiTxStream
    contentLength = body.produced();
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

template <typename Rx>
void body(Rx &rx) {
  Zhttp::bodyEach(rx, [](ZuBSpan bytes) {
    consume(bytes);                    // span is callback-scoped
  });
}
```

Client body emitters, writers, and late-header patchers are synchronous and
callback-scoped. Server response selection is likewise synchronous, while an
incremental response producer retains only its own continuation state and one
pooled buffer per turn. The same bounded `ZiRxStream` contract is used by the
server application `Request`. The application may leave a trailing incomplete
application frame in that queue; later body appends prompt it again without
copying or coalescing the retained spans. An application which needs
contiguous storage owns that gather buffer. Headers precede body input,
validated body completion precedes the exact-once terminal result, and no
callback follows that result.
`Result` reports request bytes produced, committed, reset, and discarded plus
response bytes received, consumed, reset, and discarded. `RequestMeta`
reports server-side request bytes received, consumed, reset, and discarded.
These owner-shard counters are diagnostics; they do not gate production,
parsing, or transport flow control and are not stable cross-shard snapshots.

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
Zhttp::Stream stream{link};
if (stream.peerCap()) {
  stream.txStream([](auto &tx) {
    tx << bytes;
    tx.flush();                  // application message/latency boundary
  });
  stream.end();                  // ordered after the flushed bytes
}

int process(auto stream, auto &rx) {
  while (rx) {
    int64_t n = rx.consume(frame, consume);
    if (n < 0) return -1;
    if (!n) break;               // incomplete application frame stays queued
  }
  return 1;
}

void peerEnd(auto stream) { peerEnded(); }
void error(auto stream) { stream.reset(); }
```

The Rx queue reference and native pooled Tx layer are valid only for their
respective callback durations; unread Rx nodes remain owned by the logical
stream between prompts. A negative `process()` result resets only the affected
logical stream. The logical-stream contract contains no WebSocket fields,
framing, masking, close codes, or subprotocol policy; those belong in a
dependent protocol library.

The lower-level typed client/server application flow is likewise
protocol-independent:

```c++
template <typename Profile>
struct Client : Zhttp::ClientHub<Client<Profile>, Profile> {
  struct Request :
    Zhttp::MessageTraits<Profile>::template Request<
      Request, ZuTypeList<>, ZuTypeList<>, false, false> {
    template <typename L>
    void operation(L &&l) { l(Zhttp::Method::GET, "/"); }
    template <typename L>
    void host(L &&l) { l("127.0.0.1"); }
  };

  struct Link :
    Zhttp::ClientLink<Client, Link, Profile> {
    using Base = Zhttp::ClientLink<Client, Link, Profile>;
    using Base::Base;
  };

  void connected(Link &link, Zhttp::ConnectedInfo) {
    Request request;
    auto tx = link.transmit(request);
    request.begin(tx);
    request.finish(tx);
    link.finish();
  }
  template <typename Rx>
  int process(Link &link, Rx &rx) {
    // link.receive(parser, rx), then process the response
  }
};
```

Servers use the corresponding `Zhttp::Server`, `Zhttp::ServerLink`, and
`Zhttp::ServerSession` templates. Profiles such as `H1TCP`, `H1TLS`, `H2TLS`,
and `H3QUIC` instantiate the same connection/message contract; only
initialization configuration differs. The high-level `Client` and `Server`
normally own these hubs:

```c++
hubs.init(tcp, hub, Zhttp::TCPConfig{});
hubs.init(tls, hub, Zhttp::H2Config{}
  .certPath(cert).keyPath(key)
  .policy(Zhttp::H2Policy::Prefer));
hubs.init(h3, hub, Zhttp::QUICConfig{}
  .certPath(cert).keyPath(key));
hubs.start();
// process connections and messages
hubs.stop([](bool) { /* shutdown continuation */ });
// The main thread waits for the continuation before finalization.
hubs.final();
```

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
tools are reported as TAP skips and are never downloaded:

```sh
cd zhttp/interop
./zhttph2interoptest.sh
```

Set `ZHTTP_H2_ARTIFACTS` to an existing artifact directory,
`ZHTTP_H2_KEEP_ARTIFACTS=1` to retain successful runs, and
`ZHTTP_H2_PORT`/`ZHTTP_H2_NGHTTPD_PORT` when the default loopback ports are
occupied.  Every retained run includes tool versions plus separate client,
server, and conformance output.

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
