# Zquic: QUIC transport

`Zquic` is the QUIC transport module. It owns QUIC connection state, packet
encoding/protection, stream scheduling, UDP path handling, loss recovery, flow
control, timers, qlog instrumentation, and transport-facing diagnostics.
It is a transport library, not an HTTP/3 stack; production HTTP/3 is owned by
`Zhttp` and uses `Zquic` underneath.

Zquic uses the raw TLS 1.3 handshake driver in `libZtls`. The Rx owner feeds
handshake bytes from CRYPTO streams and supplies the output buffer. Zquic
retains the returned buffer and epoch ranges for CRYPTO transmission and
recovery. The handshake reports traffic secrets to Rx. Zquic owns packet
protection, key phases, and packet-space retirement.

This file is a map for navigating the implementation.  Required review rules
for editing `zquic` live in `zquic/GUIDELINES.md`.

## Public Surface

The primary include is `Zquic.hh`, which pulls in the transport vocabulary,
packet/frame codecs, crypto, recovery, stream/link runtime, socket adapter,
diagnostics, and qlog support.

Initialize the QUIC hub before starting its multiplexer. Hub initialization uses
`Zquic::pktRxAllocator(mx)` and `Zquic::pktTxAllocator(mx)`, which create and
register packet allocators through `ZiMultiplex::allocator` on that multiplexer.
Concrete application links use their own first-base `ZmNHeap<Link>` so cache-line
alignment and allocation size match the actual link type; the examples demonstrate
both policies.

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
`HubParams`, which configures the multiplexer, Rx/Tx threads, TLS paths,
ALPN, qlog, transport limits, idle timeout, maximum UDP payload, congestion control, ECN, and
address validation policy.  It also configures single-active-path migration:
`migrationMode()` defaults to `MigrationMode::Passive`, `activeMigration(true)`
advertises active migration support, `migrationCIDReserve()` reserves spare
connection IDs for migration, and `migrationCloseOnFailure()` selects whether
local-rebind failures close active migration attempts.

`HubParams::congestionControl()` defaults to enabled. Set it to `false` during
stopped-state initialization for a controlled-network policy that bypasses
NewReno. The setting is fixed while the hub runs. Flow credit, anti-amplification,
PMTU limits, retained packet accounting, ACK retirement, RTT, loss detection,
and PTO remain active. Disabled-mode cwnd and ssthresh diagnostics are zero;
bytes in flight still come from the packet-number spaces. Local packets use
Not-ECT with congestion control disabled, regardless of `ecn()`. Received
ECT/CE counters continue to produce ACK_ECN feedback in every configuration.

Named hash settings are applied after local defaults. Load them through
`ZmHashMgr` before constructing hubs and links. The route and server tables use
`Zquic.Endpoint.CxnRouter` and `Zquic.Server.Links`; stream registries use
`Zquic.Stream.ObjectHash`, `Zquic.Stream.TxHash`, and `Zquic.Stream.ClosedHash`.
Recovery indexes use `Zquic.Pkt.OutstandingFlow`, `Zquic.Pkt.OutstandingExact`,
and `Zquic.Pkt.AckdFrame`, including indexes created lazily after startup.
`Zquic.StreamScheduler` also honors named settings when given explicit local
defaults. These owner-local tables retain their existing lock policy.

Path migration is exposed through the link API.  `CliLink::migrate()`,
`migrateLocal()`, and `migrateRemote()` request client active migration; the
transport rejects active requests when the peer sent
`disable_active_migration`.  `CliLink` and `SrvLink` expose `pathInfo()` and
`migrationState()` snapshots, and applications may implement
`pathUpdate(const PathInfo &)`, `migrationStarted(const MigrationResult &)`,
`migrationPromoted(const MigrationResult &)`, and
`migrationFailed(const MigrationResult &)`.  This is QUIC v1 path migration,
not multipath: normal STREAM/control traffic uses one active path while at most
one candidate path is being validated.

Clients process the server's `preferred_address` after HANDSHAKE_DONE. A
same-family address is probed with the advertised connection ID on a staged UDP
socket, even when the server disables active migration. The original socket
continues carrying ordinary traffic until a matching PATH_RESPONSE validates
the candidate. Failed probes retain the original path. `MigrationMode::Disabled`
disables this automatic migration.

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
- `ZquicCrypto.hh` / `ZquicCrypto.cc`: Ztls raw handshake integration,
  Initial/Handshake/0-RTT/1-RTT key state, packet protection, and early data.
- `ZquicRecovery.hh`: ACK ranges, sent-packet tracking,
  PTO/loss recovery, retransmit queues, RTT, congestion, and ECN validation.
- `ZquicStream.hh`: stream objects, Rx reassembly, Tx
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
- `ZquicPath.hh`: path validation, anti-amplification,
  ECN path state, and PMTUD/DPLPMTUD state.
- `ZquicSched.hh`: stream scheduling and pacing helpers.
- `ZquicBuf.hh` and `ZquicPQueue.hh`: packet/stream/CRYPTO buffer allocation
  aliases and priority queues.  Buffer rules are in `zquic/GUIDELINES.md`.
- `ZquicDiag.hh` / `ZquicDiag.cc`: diagnostic counters and stable formatter
  helpers.
- `ZquicLog.hh` / `ZquicLog.cc`: qlog JSON-SEQ writer, event model, ring,
  file aging, and diagnostics. The writer uses `ZmEngine` for asynchronous
  start/stop and restart-after-drain; synchronous start/stop are for main-thread
  callers. Trace initialization precedes start, and finalization follows drain.
  qlog instrumentation rules are in
  `zquic/GUIDELINES.md`.

## Feature Landmarks

- PTO recovery selects the oldest eligible retained packets, so newly emitted
  retransmissions cannot continually displace older control or STREAM work.
  It services the selected packet-number space and can probe another space
  with data in flight within the two-datagram budget. Application Data does
  not independently arm PTO before handshake confirmation. Candidate path
  validation keeps priority over ordinary recovery.
- Handshake keys retire at server TLS completion and client receipt of
  `HANDSHAKE_DONE`, on their respective Rx and Tx owners. Retirement updates
  both loss and PTO timers; stale crypto or ACK posts cannot restore the
  retired space. The regression suites cover confirmation held in flight,
  protected transfer after retirement, and recovery with newer packets added
  during consecutive PTOs.
- QUIC version support is currently v1 only. `VersionNeg` and server
  long-header routing live in `Zquic.hh`; packet codec support is in
  `ZquicPacket.*`.
- Address validation spans `AddressToken`, `ClientBootstrap`,
  `ServerBootstrap`, client `NEW_TOKEN` retention, and server Retry/NEW_TOKEN
  logic.
- 0-RTT is application-owned through `Hub` early-data hooks, with crypto
  state in `ZquicCrypto.*` and packet/stream enforcement in `ZquicLink.hh` and
  `ZquicCliLink.hh`.
- PMTUD/DPLPMTUD is split between `ZquicPath.*` state and runtime send/probe
  paths in `ZquicLink.hh`.
- ECN handling crosses socket marking, ACK_ECN parsing, recovery validation,
  path fallback, diagnostics, and qlog events.
- QUIC path migration covers passive peer tuple changes, NAT rebinding, client
  active migration, local UDP endpoint rebinding, CID selection, diagnostics,
  and qlog migration event sourcing.  HTTP/3 policy uses this API from
  `Zhttp`; HTTP/3 itself is not part of `Zquic`.
- HTTP/3, QPACK, WebTransport, DATAGRAM, multipath, and QUIC v2 are not
  exposed by this module.

Queue telemetry follows existing ownership: the hub visits links and pools,
links visit their packet spaces and Rx/Tx stream tables, and endpoints visit
their current and candidate connections. Queue identities remain local to those
objects; there is no global queue registry. Enumeration borrows queue views for
the callback only. The existing link-keyed Rx/Tx summaries remain available to
`Ztc`; detailed role IDs are available through direct `allQueues` enumeration.

## Tests

Tests are standalone binaries under `zquic/test`.  Useful entry points:

- `ZquicQueueTest`: owner enumeration, queue identity, stream retirement, and
  router, scheduler, and crypto queue counters.
- `ZquicCodecTest`: packet/frame/transport-parameter codec coverage.
- `ZquicPacketProtectionTest`: RFC 9001 packet protection vectors and traffic
  secret protection.
- `ZquicHandshakeTest` and `ZquicRuntimeTest`: TLS handshake and loopback
  runtime behavior.
- `ZquicRttLossTest`: falling/rising/constant RTT thresholds, initial fallback,
  granularity floor, and packet-loss deadline edges at full time precision.
- `ZquicAckPublishTest`: concurrent Rx/Tx ACK metadata and completion,
  ordinary publication without owner posts, sender parking, ring pressure,
  ordered bounded peer-ACK retirement, reset with deferred ACKs, and recovery
  callbacks already handed off when an ACK arrives. Serialized ACK_ECN feedback
  and protected output marking cover all four congestion-control/ECN policies.
- `ZquicTimerTest`: actual scheduler handoffs, reservation reuse, full ACK
  suppression followed by a new flight, packet-space retirement, idle activity
  publication, and explicit zero deadlines.
- `ZquicStreamTest`: stream delivery, retransmission, control queues, PMTUD,
  ECN, 0-RTT, qlog, and key-update state. Reset cases cover unsent and
  flow-stalled output: final size, control retransmission and ACK matching use
  the transmitted extent, and reset releases queued data and recovery ranges.
- `ZquicRecoveryTest`, `ZquicFlowTest`, `ZquicCIDTest`, `ZquicSockTest`,
  `ZquicLogTest` and `ZquicPQueueTest`: focused subsystem
  coverage. The logger regression holds its old writer during shutdown and
  requires a concurrent restart to wait for ring drain before reopening it.
  Earlier validation results do not establish results for the transferred
  recovery repairs; current validation status is recorded in `zquic_status.md`
  at the repository root.
- Migration behavior is covered across `ZquicAPITest`, `ZquicSockTest`,
  `ZquicLogTest`, and runtime loopback tests: public migration params and
  callbacks, endpoint rebinding, CID policy, passive/active validation,
  diagnostics, and qlog JSON-SEQ event streams.

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

## Completed UDP output

Transmit packets are intrinsic `Zquic_::TxPkt` nodes selected through the mx
TxUDP allocator factory, with destination and ECN stored alongside their queue
links. Each physical endpoint uses `ZiTx`; no separately allocated packet wrapper
or private completion pump is used. Retained stream and CRYPTO plaintext stays
under QUIC ACK/loss recovery ownership.

Packet-provider exhaustion closes the affected link through the local
resource-failure path. Allocation uses the selected provider's original
headroom, tailroom and MPS; it does not wait for returned buffers.

The endpoint capacity is 4096 datagrams including the active head. Full admission
returns false to direct callers and records backpressure/drop; posted calls can
return true before a later local drop. Priority packets are inserted immediately
after the active head, with the newest priority ahead of previous pending work.
Every head installs its own destination and ECN. Socket completion updates local
output diagnostics only. Queued packets and late completions belong to their
original physical connection through rebind and retirement. Posted sends carry
raw physical identity and generation; they do not pin an owner per packet.

Protocol ACK handling services pending retained retransmissions before ordinary
output consumes reopened congestion credit. This preserves recovery progress under
bulk transfer and rebinding while using the existing bounded batch and records.

ZiTx services queued successors within the common I/O turn. QUIC pacing remains
above that queue. See `itest/README.md` for sustained-output and servicing checks,
and `../review_context/zquic-zitx-contracts.md` for frozen dependency contracts,
including the positive-short UDP backend limitation.
