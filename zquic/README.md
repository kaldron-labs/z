# Zquic

`Zquic` is the QUIC transport module. It owns QUIC connection state, packet and
stream buffers, UDP path handling, loss recovery, flow control, and timers.

The public API is transport-oriented:

- client applications derive from `Zquic::Client<App>` and create
  `Zquic::CliLink<App, Link, Stream>` objects;
- server applications derive from `Zquic::Server<App>`, call `listen()`, and
  accept logical connections by returning `SrvLink` instances from
  `accepted(const Zquic::InitialInfo &)`;
- each link owns its streams and creates local streams with `stream()`;
- client and server streams derive from `CliStream` and `SrvStream`;
- each stream exposes `txStream()`, `fin()`, `reset()`, and `stop()`;
- peer-created streams are reported to the link implementation with
  `streamed(ZmRef<Stream>)`.

Runtime entry points are also transport-oriented. `Zquic::Server::listen()`
opens one unconnected UDP listener on `ZiMultiplex::udp()` and routes datagrams
to active `SrvLink`s by connection ID. `Zquic::CliLink::connect()` opens a
connected UDP socket through `CliCxn`. Runtime diagnostics expose endpoint
readiness/failure counts, datagram byte counts, parsed packet/frame counts,
handshake progress, stream frames, ACKs, and packet protection counters.
The endpoint Tx backlog is explicitly bounded and reports back-pressure through
endpoint diagnostics when enqueue fails.

`Zquic` does not expose HTTP/3 request, response, QPACK, or WebTransport types.
Production HTTP/3 is owned by `Zhttp` and uses `Zquic` as its transport.

`zquic/example` contains small noinst transport examples:
`ZquicServer` listens on one UDP socket and accepts routed `SrvLink`s, while
`ZquicClient` opens a `CliLink`, sends one stream payload, and prints the
server's stream response.

First release scope:

- QUIC v1 only;
- no QUIC v2;
- no compatible version negotiation;
- no multipath;
- no WebTransport;
- no DATAGRAM support;
- no 0-RTT send or accept;
- no active ECN marking or validation;
- no full connection migration beyond path and CID state needed for future
  extension.

Version negotiation is v1-only. Retry packets use the QUIC v1 Retry Integrity
Tag calculation and client bootstrap exposes a raw-packet validation path before
accepting the Retry token and server-selected connection ID.
The implementation advertises and enforces a local active connection ID limit
of 8; CID storage is dynamically backed but remains bounded by that negotiated
policy.

## Buffers

`Zquic` keeps packet and stream buffers separate. Packet buffers use the
`"Zquic.Packet"` heap and stream buffers use the `"Zquic.StreamBuf"` heap; both
have built-in size `1472`. The active UDP payload limit is separate from that
buffer capacity and starts at the QUIC minimum before PMTUD/DPLPMTUD raises it.

Tx encryption writes directly into the final packet buffer from retained
plaintext ranges. Rx decryption happens in place in the received packet buffer.
After STREAM frame parsing, the decrypted STREAM payload is copied exactly once
into a stream buffer before delivery. Hidden fallback copies below the public
API boundary are forbidden unless added to `zquic/buffers.md`, counted, and
tested.

## UDP And PMTUD

Client UDP sockets may be connected when the platform exposes useful PMTU
queries for connected sockets. Server UDP sockets remain unconnected so one
socket can serve many peers and paths.

`Zquic::Endpoint` is retained only as a private/test UDP utility. Public
datagram ownership and endpoint diagnostics live in `ZquicDatagram.hh`; the
aligned runtime surface is `Cxn`, `CliCxn`, `SrvCxn`, `CliLink`, and `SrvLink`.

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
stream counts, PMTUD probe outcomes, required Rx packet-to-stream copies, and
buffer-contract violations. Stable formatter utilities expose packet-space,
frame, stream, recovery, and summary names for tests and logs.
Retransmit-drop reporting is zero by construction because the current
retransmit queue has no drop policy.

Log subsystem names are stable constants:

- `Zquic`
- `Zquic.Endpoint`
- `Zquic.Crypto`
- `Zquic.Recovery`
- `Zquic.Stream`
- `Zquic.PMTUD`

`Zquic.Endpoint` is the internal UDP-adapter diagnostic category; the public
runtime API is `Client` / `Server` with `CliLink` / `SrvLink` and `Cxn`.
