# Zquic

`Zquic` is the QUIC transport module. It owns QUIC connection state, packet
encoding/protection, stream scheduling, UDP path handling, loss recovery, flow
control, timers, qlog instrumentation, and transport-facing diagnostics.
It is a transport library, not an HTTP/3 stack; production HTTP/3 is owned by
`Zhttp` and uses `Zquic` underneath.

This file is a map for navigating the implementation.  Required review rules
for editing `zquic` live in `zquic/GUIDELINES.md`.

## Public Surface

The primary include is `Zquic.hh`, which pulls in the transport vocabulary,
packet/frame codecs, crypto, recovery, stream/link runtime, socket adapter,
diagnostics, and qlog support.

Applications use the CRTP API:

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

`Zquic::Server::start()` opens the UDP listener. `Zquic::CliLink::connect()`
opens the client UDP socket. `ClientParams` and `ServerParams` are aliases of
`EngineParams`, which configures the multiplexer, Rx/Tx threads, TLS paths,
ALPN, qlog, transport limits, idle timeout, maximum UDP payload, ECN, and
address validation policy.

`zquic/example` contains small noinst transport examples:
`ZquicServer` listens on one UDP socket and accepts routed `SrvLink`s, while
`ZquicClient` opens a `CliLink`, sends one stream payload, and prints the
server's stream response.

## Source Map

- `ZquicTypes.hh`: protocol constants and enum vocabulary.
- `ZquicPacket.hh` / `ZquicPacket.cc`: QUIC packet header parsing, packet
  number handling, Retry, stateless reset, and Version Negotiation encoding.
- `ZquicFrame.hh` / `ZquicFrame.cc`: frame parsing/formatting and transport
  parameter encoding.
- `ZquicCrypto.hh` / `ZquicCrypto.cc`: picotls integration, Initial/Handshake/
  0-RTT/1-RTT key state, packet protection, TLS output, and early-data state.
- `ZquicRecovery.hh` / `ZquicRecovery.cc`: ACK ranges, sent-packet tracking,
  PTO/loss recovery, retransmit queues, RTT, congestion, and ECN validation.
- `ZquicStream.hh` / `ZquicStream.cc`: stream objects, Rx reassembly, Tx
  retention, stream-state transitions, RESET/STOP, and stream GC.
- `ZquicLink.hh`: shared client/server connection runtime: stream tables,
  flow control, ACK generation, frame dispatch, control queues, path state,
  PMTUD, CID management, key updates, close/drain handling, and diagnostics.
- `ZquicCliLink.hh`: client socket lifecycle, bootstrap, Retry handling,
  TLS client setup, 0-RTT send path, NEW_TOKEN retention, and client packet I/O.
- `ZquicSrvLink.hh`: server connection bootstrap, TLS server setup, frame-send
  gating, server packet I/O, and server close behavior.
- `Zquic_.hh` and `ZquicSock.hh`: private UDP endpoint adapter, routing tables,
  socket mode planning, Tx queueing, and datagram ownership.
- `ZquicPath.hh` / `ZquicPath.cc`: path validation, anti-amplification,
  ECN path state, and PMTUD/DPLPMTUD state.
- `ZquicSched.hh` / `ZquicSched.cc`: stream scheduling and pacing helpers.
- `ZquicBuf.hh` and `ZquicPQueue.hh`: packet/stream/CRYPTO buffer allocation
  aliases and priority queues.  Buffer rules are in `zquic/GUIDELINES.md`.
- `ZquicDiag.hh` / `ZquicDiag.cc`: diagnostic counters and stable formatter
  helpers.
- `ZquicLog.hh` / `ZquicLog.cc`: qlog JSON-SEQ writer, event model, ring,
  file aging, and diagnostics.  qlog instrumentation rules are in
  `zquic/GUIDELINES.md`.

## Feature Landmarks

- QUIC version support is currently v1 only. `VersionNeg` and server
  long-header routing live in `Zquic.hh`; packet codec support is in
  `ZquicPacket.*`.
- Address validation spans `AddressToken`, `ClientBootstrap`,
  `ServerBootstrap`, client `NEW_TOKEN` retention, and server Retry/NEW_TOKEN
  logic.
- 0-RTT is application-owned through `Engine` early-data hooks, with crypto
  state in `ZquicCrypto.*` and packet/stream enforcement in `ZquicLink.hh` and
  `ZquicCliLink.hh`.
- PMTUD/DPLPMTUD is split between `ZquicPath.*` state and runtime send/probe
  paths in `ZquicLink.hh`.
- ECN handling crosses socket marking, ACK_ECN parsing, recovery validation,
  path fallback, diagnostics, and qlog events.
- HTTP/3, QPACK, WebTransport, DATAGRAM, multipath, QUIC v2, and full active
  migration are not exposed by this module.

## Tests

Tests are standalone binaries under `zquic/test`.  Useful entry points:

- `ZquicCodecTest`: packet/frame/transport-parameter codec coverage.
- `ZquicPacketProtectionTest`: RFC 9001 packet protection vectors and traffic
  secret protection.
- `ZquicHandshakeTest` and `ZquicRuntimeTest`: TLS handshake and loopback
  runtime behavior.
- `ZquicStreamTest`: stream delivery, retransmission, control queues, PMTUD,
  ECN, 0-RTT, qlog, and key-update state.
- `ZquicRecoveryTest`, `ZquicFlowTest`, `ZquicCIDTest`, `ZquicSockTest`,
  `ZquicTimerTest`, `ZquicLogTest`, and `ZquicPQueueTest`: focused subsystem
  coverage.

Run the module tests with:

```sh
make -C zquic/test -j8
make -C zquic/test test
```

Log subsystem names used by the implementation:

- `Zquic`
- `Zquic.Endpoint`
- `Zquic.Crypto`
- `Zquic.Recovery`
- `Zquic.Stream`
- `Zquic.PMTUD`

`Zquic.Endpoint` is the internal UDP-adapter diagnostic category; the public
runtime API is `Client` / `Server` with `CliLink` / `SrvLink`.
