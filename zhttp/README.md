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
```

Useful client checks:

```sh
zhttp -o body http://127.0.0.1:8080/
zhttp -c cert.pem -o body https://localhost:8443/
zhttp -3 force -c cert.pem -o body https://localhost:8443/
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
ECN behavior.
