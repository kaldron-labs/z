# Zquic

`Zquic` is the QUIC transport module. It owns QUIC connection state, packet and
stream buffers, UDP path handling, loss recovery, flow control, and timers.

The public API is transport-oriented:

- applications create `Zquic::Link` objects;
- a link creates local streams with `stream()`;
- each `Zquic::Stream` exposes `txStream()`, `fin()`, `reset()`, and `stop()`;
- peer-created streams are reported to the link implementation with the
  `streamed(ZmRef<Stream>)` callback in later protocol phases.

Runtime entry points are also transport-oriented. `Zquic::Server::listen()`
opens an unconnected UDP endpoint on `ZiMultiplex::udp()`, while
`Zquic::Client::connect()` opens a connected UDP endpoint when a remote address
is supplied. Runtime diagnostics expose endpoint readiness/failure counts,
datagram byte counts, parsed packet/frame counts, and probe Tx counts. The
current `sendInitialProbe()` path is a packet-pump diagnostic: it emits a
padded QUIC v1 Initial datagram carrying PING so endpoint, packet-codec, and
diagnostic wiring can be proved over loopback before the full TLS handshake and
stream scheduler are joined to the runtime path.

`Zquic` does not expose HTTP/3 request, response, QPACK, or WebTransport types.
Production HTTP/3 is owned by `Zhttp` and will use `Zquic` as its transport.
Any HTTP/3 code inside the `zquic` module is limited to test-only interop
harnesses under `zquic/test`.

The `zquic/test` H3Lite harness is intentionally narrow: it uses ALPN `h3`,
SETTINGS, HEADERS, DATA, GOAWAY, and zero-capacity QPACK field sections with
static indexed, static-name-reference, and literal-name field lines. It rejects
dynamic references and has no dynamic table. It exists to drive transport
interop tests and is not installed or exposed as a supported HTTP API.

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
buffer-contract violations. Stable formatter helpers expose packet-space,
frame, stream, recovery, and summary names for tests and logs.

Log subsystem names are stable constants:

- `Zquic`
- `Zquic.Endpoint`
- `Zquic.Crypto`
- `Zquic.Recovery`
- `Zquic.Stream`
- `Zquic.PMTUD`
