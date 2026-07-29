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

`libZhttp` is the HTTP integration layer over `libZtcp`, `libZtls`, and
`libZquic`.  Applications configure TCP, TLS, and QUIC with public `Zhttp`
configuration types; HTTP ALPN and mandatory H3 transport defaults are
library-owned.  Native transport traits remain private implementation detail.
For complete HTTP applications, `Zhttp::Agent` owns client routing,
discovery, pools, retries, redirects, fallback, and engine lifecycle, while
`Zhttp::Service` owns server listeners, admission, sessions, message
selection, and engine lifecycle. Applications provide protocol configuration
and one protocol-neutral callback contract:

```c++
struct ClientApp :
  Zhttp::Agent<
    ClientApp, Request, RequestHeaders, ResponseHeaders, ResponseBodyMax,
    Zhttp::Body::Fixed<BodyCursor>> {
  uint64_t requestContentLength(const Request &) const;
  BodyCursor requestBodyCursor(Request &);
  template <typename Tx>
  int requestBody(Request &, BodyCursor &, Tx &, unsigned batch);
  template <typename Rx>
  void responseBody(Request &, Rx &);
};

ClientApp client;
client.init(
  Zhttp::EngineConfig{&mx, "rx", "tx"},
  Zhttp::AgentConfig{}
    .protocol(Zhttp::ProtocolPolicy::PreferH3)
    .h2Policy(Zhttp::H2Policy::Prefer),
  Zhttp::TCPConfig{},
  Zhttp::H2Config{}.caPath(ca),
  Zhttp::QUICConfig{}.caPath(ca));
client.start();
client.submit(requests, requestCount);
// requestBody() runs on Tx; responseBody() runs synchronously on Rx.
client.stop([](bool) { /* shutdown continuation */ });
// The main thread waits for that continuation before final().
client.final();
```

```c++
using Service = Zhttp::Service<
  Workload, RequestHeaders, ResponseHeaders, RequestBodyMax>;

Workload workload;
Service service;
service.init(
  Zhttp::EngineConfig{&mx, "rx", "tx"},
  Zhttp::ServiceConfig{}
    .port(port)
    .tcp(Zhttp::TCPConfig{})
    .tls(Zhttp::H2Config{}
      .certPath(cert).keyPath(key)
      .policy(Zhttp::H2Policy::Prefer))
    .quic(Zhttp::QUICConfig{}.certPath(cert).keyPath(key)),
  &workload);
service.start();
// Workload::RequestState is owned by one logical request.  requestBody()
// receives (RequestInfo, RequestState, Rx) before request() constructs the
// response from the completed state.
// disconnected(transport) observes admission release on the owning Rx shard.
service.stop([](bool) { /* shutdown continuation */ });
// The main thread waits for that continuation before final().
service.final();
```

`Body::None` is allocation-free. `Body::Fixed<Cursor>` derives
`content-length`; `Body::Stream<Cursor>` selects H1 chunked transfer and
H2/H3 DATA without a content length. Each retry creates a fresh cursor.
Body-bearing requests are non-replayable by default; applications must opt in
only when they can reproduce the source from byte zero. A fixed producer
writes entity bytes directly to its concrete `ZiTxStream`:

```c++
template <typename Tx>
int requestBody(
  Request &request, Cursor &cursor, Tx &tx, unsigned batch) {
  return produce(request, cursor, tx, batch); // no HTTP framing here
}

template <typename Rx>
void responseBody(Request &, Rx &rx) {
  Zhttp::bodyEach(rx, [](ZuBSpan bytes) {
    consume(bytes);                    // span is callback-scoped
  });
}
```

The same producer shape implements a non-replayable streaming POST by selecting
`Body::Stream<Cursor>`, returning `BodyProduce::More` while input remains, and
leaving the default `requestReplayable()` and `requestReproducible()` false.
The `batch` argument bounds one scheduler turn only. It is not transport credit:
H1 has no flow control, H2 flow control remains in Zhttp's H2 layer, and QUIC
flow control remains in Zquic.

The same bounded `ZiRxStream` contract is used by
`Workload::requestBody()`. The callback must consume or copy all offered
input before returning. Headers precede body input, validated body completion
precedes the exact-once terminal result, and no callback follows that result.
`Result` reports request bytes produced, committed, reset, and discarded plus
response bytes received, consumed, reset, and discarded. `RequestInfo`
reports server-side request bytes received, consumed, reset, and discarded.
These owner-shard counters are diagnostics; they do not gate production,
parsing, or transport flow control and are not stable cross-shard snapshots.

When TLS and QUIC are enabled together, `Service` emits the HTTP/3 Alt-Svc
header on TLS responses. `altSvcMaxAge()` controls its lifetime; zero disables
advertising. No HTTP/3 response branch belongs in the application.

Extended CONNECT is opt-in with `H2Config::extendedConnect(true)` for H2 and
`QUICConfig::extendedConnect(true)` for H3. `libZhttp` advertises and parses
`SETTINGS_ENABLE_CONNECT_PROTOCOL`, tracks local and peer capability
independently, and refuses to emit or accept `:protocol` without the relevant
capability. A successful 2xx handshake transitions the logical H2 or H3
stream to `ParserState::Stream`. H1 Upgrade and H2/H3 Extended CONNECT then
expose the same borrowed, shard-affine logical-stream contract. Rx is passed
separately to `process()` and Tx is callback-scoped:

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
  while (rx.input()) {
    auto events = rx.events();   // Start/Input/Final/Error, read and clear
    if (events & Zi::RxEvent::Error()) return -1;
    if (rx.consume(frame, consume) <= 0) return 0;
  }
  auto events = rx.events();
  if (events & Zi::RxEvent::Final()) peerEnded();
  return 1;
}
```

The Rx layer and native pooled Tx layer are valid only for their respective
callback durations. A negative `process()` result resets only the affected
logical stream. The logical-stream contract contains no WebSocket fields,
framing, masking, close codes, or subprotocol policy; those belong in a
dependent protocol library.

The lower-level typed client/server application flow is likewise
protocol-independent:

```c++
template <typename Profile>
struct Client : Zhttp::Client<Client<Profile>, Profile> {
  struct RequestBuilder :
    Zhttp::MessageTraits<Profile>::template Builder<
      RequestBuilder, ZuTypeList<>, ZuTypeList<>, false, false> {
    template <typename L>
    void operation(L &&l) { l(Zhttp::Method::GET, "/", ""); }
    template <typename L>
    void host(L &&l) { l("127.0.0.1"); }
  };

  struct Link :
    Zhttp::ClientLink<Client, Link, Profile> {
    using Base = Zhttp::ClientLink<Client, Link, Profile>;
    using Base::Base;
  };

  void connected(Link &link, Zhttp::ConnectedInfo) {
    RequestBuilder builder;
    auto tx = link.transmit(builder);
    if (builder.request(tx)) link.finish();
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
initialization configuration differs. The high-level `Agent` and `Service`
normally own these engines:

```c++
engines.init(tcp, engine, Zhttp::TCPConfig{});
engines.init(tls, engine, Zhttp::H2Config{}
  .certPath(cert).keyPath(key)
  .policy(Zhttp::H2Policy::Prefer));
engines.init(h3, engine, Zhttp::QUICConfig{}
  .certPath(cert).keyPath(key));
engines.start();
// process connections and messages
engines.stop([](bool) { /* shutdown continuation */ });
// The main thread waits for the continuation before finalization.
engines.final();
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
cd zhttp/test
./ZhttpH2InteropTest.sh
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
