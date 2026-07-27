# Zhttp HTTP/3

`Zhttp` owns production HTTP behavior. HTTP/1.1 continues to use the existing
`Ztls` path, while HTTP/3 uses `Zquic` as the QUIC transport and keeps H3 and
QPACK code in `zhttp/src`.

The HTTP/3 surface is REST-oriented:

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
HTTP/3 first, then uses HTTP/1.1 over TLS and follows Alt-Svc HTTP/3 upgrades,
then remains on HTTP/1.1 when HTTP/3 is unavailable or rejected by policy.
`-3 force` / `--http3=force` skips DNS and Alt-Svc probing and runs HTTP/3
over QUIC directly for `https:` requests. `-3 disable` / `--http3=disable`
uses HTTP/1.1 exclusively without HTTP/3 probing. `-v`/`--verbose` shows DNS
and Alt-Svc probing. IPv6, DoH, DoT, ECH,
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
    ClientApp, Request, RequestHeaders, ResponseHeaders, ResponseBodyMax> {
  // Supply request intent and consume response callbacks here.
};

ClientApp client;
client.init(
  Zhttp::EngineConfig{&mx, "rx", "tx"},
  Zhttp::AgentConfig{}.protocol(Zhttp::ProtocolPolicy::PreferH3),
  Zhttp::TCPConfig{},
  Zhttp::TLSConfig{}.caPath(ca),
  Zhttp::QUICConfig{}.caPath(ca));
client.start();
client.submit(requests, requestCount);
// ClientApp receives the same request/response callbacks for H1 and H3.
client.stop();
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
    .tls(Zhttp::TLSConfig{}.certPath(cert).keyPath(key))
    .quic(Zhttp::QUICConfig{}.certPath(cert).keyPath(key)),
  &workload);
service.start();
// Workload receives the same request/response callbacks for H1 and H3.
// disconnected(transport) observes admission release on the owning Rx shard.
service.stop();
service.final();
```

When TLS and QUIC are enabled together, `Service` emits the HTTP/3 Alt-Svc
header on TLS responses. `altSvcMaxAge()` controls its lifetime; zero disables
advertising. No HTTP/3 response branch belongs in the application.

The lower-level typed client/server application flow is likewise
protocol-independent:

```c++
template <typename Protocol>
struct Client : Zhttp::Client<Client<Protocol>, Protocol> {
  struct Link :
    Zhttp::ClientLink<Client, Link, Protocol> {
    using Base = Zhttp::ClientLink<Client, Link, Protocol>;
    using Base::Base;
  };

  void connected(Link &link, Zhttp::ConnectedInfo) {
    auto tx = link.txStream();
    // build and send the request, then link.finish()
  }
  template <typename Rx>
  int process(Link &link, Rx &rx) {
    // link.receive(parser, rx), then process the response
  }
};
```

Servers use the corresponding `Zhttp::Server`,
`Zhttp::ServerLink`, and `Zhttp::ServerSession` templates.  TCP, TLS, and QUIC
instantiate the same connection/message templates; only initialization
configuration differs:

```c++
engines.init(tcp, engine, Zhttp::TCPConfig{});
engines.init(tls, engine, Zhttp::TLSConfig{}
  .certPath(cert).keyPath(key));
engines.init(h3, engine, Zhttp::QUICConfig{}
  .certPath(cert).keyPath(key));
engines.start();
// process connections and messages
engines.stop();
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
zhttp -3 force -c cert.pem -o body https://localhost:8443/
zhttp -3 force -c cert.pem --quic-migration=active \
  --quic-migration-cid-reserve=2 --quic-migrate-after-headers \
  -o body https://localhost:8443/
```

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
