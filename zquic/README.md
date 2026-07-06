# Zquic

`Zquic` is the QUIC transport module. It owns QUIC connection state, packet and
stream buffers, UDP path handling, loss recovery, flow control, and timers.
It is a transport library, not an HTTP/3 stack.

The public API is transport-oriented:

- client applications derive from `Zquic::Client<App>` and create
  `Zquic::CliLink<App, Link, Stream>` objects;
- server applications derive from `Zquic::Server<App, Link>`, call `start()`,
  and accept logical connections by returning `SrvLink` instances from
  `accepted(const Zquic::InitialInfo &)`;
- each link owns its streams and creates local streams with `stream()`;
- client and server streams derive from `CliStream` and `SrvStream`;
- each stream exposes `txStream()`, `fin()`, `reset()`, and `stop()`;
- peer-created streams are reported to the link implementation with
  `streamed(ZmRef<Stream>)`.

Runtime entry points are also transport-oriented. `Zquic::Server::start()`
opens one unconnected UDP listener and routes datagrams to active `SrvLink`s by
connection ID. `Zquic::CliLink::connect()` opens a
connected UDP socket. `ClientParams` and `ServerParams` are aliases of
`EngineParams`; they configure the multiplexer, Rx/Tx threads, TLS paths, ALPN,
qlog, flow/stream limits, idle timeout, maximum UDP payload, ECN, and address
validation policy.

Runtime diagnostics expose endpoint readiness/failure counts, datagram byte
counts, parsed packet/frame counts, handshake progress, stream frames, ACKs,
ECN, congestion/recovery state, PMTUD state, path validation, packet protection,
address validation, and qlog counters. Endpoint Tx queues are bounded and report
back-pressure through endpoint diagnostics when enqueue fails.

`Zquic` does not expose HTTP/3 request, response, QPACK, or WebTransport types.
Production HTTP/3 is owned by `Zhttp` and uses `Zquic` as its transport.

`zquic/example` contains small noinst transport examples:
`ZquicServer` listens on one UDP socket and accepts routed `SrvLink`s, while
`ZquicClient` opens a `CliLink`, sends one stream payload, and prints the
server's stream response.

Current protocol scope:

- QUIC v1 only;
- no QUIC v2;
- no compatible version negotiation;
- no multipath;
- no WebTransport;
- no DATAGRAM support;
- 0-RTT support is present behind application callbacks for session-ticket
  storage, remembered transport-parameter validation, maximum early data, early
  stream admission, and accepted/rejected notification;
- ECN marking, ACK_ECN parsing, validation, fallback, and diagnostics are
  present when enabled by `EngineParams::ecn(true)`;
- no full connection migration beyond path and CID state needed for future
  extension.

Version negotiation is v1-only. Unsupported long-header versions elicit a
Version Negotiation packet advertising only `Version1`. Retry packets use the
QUIC v1 Retry Integrity Tag calculation and client bootstrap validates the raw
Retry packet before accepting the Retry token and server-selected connection ID.
Server address validation can require Retry tokens and can issue `NEW_TOKEN`
tokens for later connection attempts. Tokens are authenticated with an
application-supplied or generated secret, have a configurable lifetime, and can
optionally bind to the peer port.

The implementation advertises and enforces a local active connection ID limit of
8; CID storage is dynamically backed but remains bounded by that negotiated
policy. It also maintains stateless reset tokens for routed short-header CIDs.

## TLS And 0-RTT

`Zquic` uses `ztls`/picotls for QUIC TLS. Servers require `certPath()` and
`keyPath()`. Clients may configure `caPath()` and may configure client
certificate/key paths together. `keyLogPath()` enables TLS key logging.

0-RTT is application-owned. The base `Engine` defaults reject early data: it
returns no ticket, advertises zero maximum early data, disallows early streams,
and treats application parameters as invalid unless empty. Applications that
want 0-RTT override the early-data hooks on their `Client`/`Server` CRTP type:
`earlyData()`, `validateEarlyDataParams()`, `saveEarlyData()`,
`maxEarlyData()`, `allowEarlyStream()`, `earlyDataAccepted()`, and
`earlyDataRejected()`.

Early STREAM data is policy-gated. Only frame types legal in 0-RTT are accepted;
late 0-RTT, missing keys, rejected TLS early data, invalid remembered transport
parameters, and disallowed streams are rejected and surfaced through diagnostics
and qlog.

## Buffers

`Zquic` keeps packet, stream, and CRYPTO buffers separate. The buffer contract
is documented in `zquic/GUIDELINES.md` and is part of the implementation
contract:

- `PktRxBufAlloc` uses `"Zquic.Pkt.Rx"` for received UDP datagrams and
  in-place packet protection removal;
- `PktTxBufAlloc` uses `"Zquic.Pkt.Tx"` for transmit packetization and
  packet protection output;
- `StreamTxBufAlloc` uses `"Zquic.Stream.TxBuf"` for application stream bytes
  retained for ACK/loss/retransmission/reset/cancellation/teardown;
- `CryptoRxBufAlloc` uses `"Zquic.Crypto.RxBuf"` and `CryptoTxBufAlloc` uses
  `"Zquic.Crypto.TxBuf"` for CRYPTO/TLS buffers.

The built-in packet buffer size is `1472`. The active UDP payload limit is
separate from that buffer capacity and starts at the QUIC minimum before
PMTUD/DPLPMTUD raises it.

Tx packet protection is source-to-destination: retained plaintext ranges are
gathered as read-only inputs and encrypted directly into the final packet
buffer. Rx packet protection decrypts in place in the received packet buffer.
STREAM receive delivery retains slices of the decrypted packet buffer; there is
no frame-only STREAM copy fallback. Hidden fallback copies below the public API
boundary are forbidden unless added to `zquic/GUIDELINES.md` and covered by a
test that explains why they are required.

## UDP And PMTUD

Client UDP sockets may be connected when the platform exposes useful PMTU
queries for connected sockets. Server UDP sockets remain unconnected so one
socket can serve many peers and paths.

The runtime surface is `Client` / `Server` with `CliLink` / `SrvLink`. Internal
UDP adaptation and endpoint diagnostics are implemented through the private
`Endpoint_` layer and exposed through `endpointDiag()`.

`Zquic` owns packetization and PMTUD/DPLPMTUD. It disables IP fragmentation
where the platform allows it, starts with a safe active payload size, and raises
the active PMTU only after QUIC probe success. Kernel PMTU, ICMP Packet Too Big,
`EMSGSIZE`, and Winsock send-size failures are advisory hints that can lower or
cap the active PMTU; they do not raise it without a successful QUIC probe.
Timer-driven probe expiry retries the same candidate a bounded number of times
before lowering the search ceiling. Repeated expiry of an active-size probe
falls back to the QUIC minimum payload size as a blackhole response.

Unsupported socket controls are diagnostics, not fatal errors, unless the
platform would force fragmentation for datagrams above the safe minimum.

## Diagnostics

Transport diagnostics count packets, bytes, header/body bytes, stream bytes,
loss, PTO, retransmission, congestion window, bytes in flight, handshake state,
stream counts, PMTUD probe outcomes, ECN state, address-validation outcomes,
packet protection failures, key updates, and buffer-contract violations. Stable
formatter utilities expose packet-space, frame, stream, recovery, and summary
names for tests and logs.
Retransmit-drop reporting is zero by construction because the current
retransmit queue has no drop policy.

## Qlog

Debug builds can emit qlog JSON-SEQ through `ZquicLogger`. Applications enable
it through `EngineParams::qlog(true)` and optionally set `qlogPath()`,
`qlogRingSize()`, and `qlogAge()`. The default output path is `zquic.sqlog`,
and existing files are aged through the configured archive depth.

Qlog is process-wide and asynchronous. `ZquicLogger::Trace` is owned by the
engine, started with the engine, closed during stop, and finalized by
`final()`. Runtime diagnostics expose records enqueued, written, dropped,
ring back-pressure, writer failures, and bytes written. Release builds erase
qlog-only call-site work behind `ZquicLOG`.

Log subsystem names are stable constants:

- `Zquic`
- `Zquic.Endpoint`
- `Zquic.Crypto`
- `Zquic.Recovery`
- `Zquic.Stream`
- `Zquic.PMTUD`

`Zquic.Endpoint` is the internal UDP-adapter diagnostic category; the public
runtime API is `Client` / `Server` with `CliLink` / `SrvLink`.
