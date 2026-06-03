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
instructions and then uses relative dynamic indexed field lines through the
connection's QPACK encoder state. Dynamic field-section references are decoded
through the connection's QPACK decoder state. QPACK encoder and decoder stream
bytes are consumed as ordered instruction sequences by the connection.

HTTP/3 errors and QPACK errors have distinct enum values and stable wire-code
mappings. Fallback-capable clients should try HTTP/3 when configured and fall
back to HTTP/1.1 over TLS when HTTP/3 is unavailable or rejected by policy.
`Zhttp::H3::FallbackPolicy` records that decision explicitly: it selects
HTTP/3 when enabled, available, ALPN-accepted, and connected; otherwise it can
select HTTP/1.1 with a reason such as HTTP/3 disabled, unavailable, ALPN
rejected, or connect failed. If HTTP/1.1 fallback is disabled, the decision
fails closed instead of silently selecting an insecure or unconfigured path.

HTTP/3 diagnostics count H3 frames, header bytes, body bytes, control frames,
GOAWAY, QPACK instructions, and the last surfaced H3/QPACK error. Stable
formatter helpers expose H3 frame, stream, setting, message-part, field-section,
and summary names. Log subsystem names are `Zhttp`, `Zhttp.H3`, and
`Zhttp.QPack`.

First-release exclusions match the transport scope: no server push, no
WebTransport, no DATAGRAM, no 0-RTT, no QUIC v2, no multipath, and no active
ECN behavior.
