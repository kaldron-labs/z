# Zutcp: user-mode TCP over AF_XDP

* **Author:** hrogers-kaldron
* **Status:** Draft — OQ-1 through OQ-7 resolved; implementation slices adopted; detailed file-level implementation pending
* **Date:** 2026-11-09
* **Predecessor:** [Concepts brief](./concepts_brief.md)

## Overview

Introduce `zutcp`, a user-mode TCP implementation derived from `zquic`, for short FIX order, execution, and market-data messages. Zutcp implements the existing `ZiMxPlugin` interface and is registered with `ZiMultiplex` while stopped, before start and I/O use. Applications continue to use `ztcp` and `ztls` through ZiMultiplex; those modules are not modified for this work. An installed Zutcp plugin supplies user-mode transport, while an mx without that plugin retains its regular kernel transport. This supersedes the earlier Hub::utcp proposal; backend selection remains fixed for the active mx lifetime.

The starting point is a verbatim copy of `zquic` into `zutcp`, followed by global renaming of identifiers and names with case preserved. Remove encryption, the TLS handshake, connection migration, and multiple-stream facilities from the renamed copy; exclude QIR integration from Zutcp. Retain one bidirectional stream per connection and the existing link/stream application model. TLS remains available through `ztls` above the selected TCP backend.

QIR means the open-source [QUIC Interop Runner](https://github.com/quic-interop/quic-interop-runner). The Zquic integration lives in [zhttp/interop](./zhttp/interop/README.md), with integration coverage in zhttp/itest, because its HTTP/3 (H3) tests require Zhttp. QIR is external test infrastructure, not a QUIC transport mechanism to delete from the copied runtime. Existing Zhttp QIR facilities remain; Zutcp will use TCP-specific testing instead.

Create and independently test `zxdp` to encapsulate AF_XDP packet I/O. It supplies Rx and Tx ring abstractions and integrates reception with `ZmScheduler`. Its configuration follows `ZmRing_::Params`, including low-latency spinning. The packet-I/O boundary must accommodate future backends without making their implementation part of this work.

Preserve Zquic's dedicated protocol Rx/Tx roles and its association of functionality with thread-exclusive state. In phase 1, Zutcp retains independently configured protocol Rx/Tx threads, which typically differ from mx's I/O threads but may coincide. In phase 2, bind those protocol roles directly to mx's I/O Rx/Tx pair and remove the Zutcp-specific thread-selection parameters in favor of ZiMxParams. Threads are shared by the connections they serve; this does not create two threads per connection. This sharding is a central latency opportunity alongside avoiding kernel data-path syscalls: receive and transmit work retain dedicated execution and state ownership throughout the conversion to TCP.

The first Zutcp deliverable is a working client and server exchanging application bytes through `veth` and `zxdp` on one host, after copying, renaming, and removing excluded features. There are no TCP-specific protocol modifications in this milestone: it deliberately implements an intermediate protocol that is neither QUIC nor TCP. Incrementally convert that working client/server to TCP, verifying communication throughout the conversion.

## The Problem

The target workload sends streams of short FIX messages, each individually small enough to fit in one Ethernet packet. The current kernel TCP path incurs syscall and network-stack work. The motivating hypothesis is that a user-mode implementation can reduce that work and improve both message latency and throughput for this workload. The benefit must be demonstrated against the kernel path under comparable traffic and resource conditions; the design does not assume a measured improvement in advance.

Applications need to retain ordinary address resolution, address types, and host networking while selecting a different TCP data path. `ZiResolver`, `ZiIP`, and the kernel network stack continue to serve their existing roles. Packet steering and host-stack coexistence still need explicit design; ordinary address resolution alone does not settle them.

A conversion directly from copied QUIC code to interoperable TCP would change packet I/O and transport behavior at once. The intermediate client/server milestone establishes a working packet path and retained transport behavior before TCP-specific changes, so each subsequent change can be checked against an executable baseline.

## Goals

* Request zero-copy packet I/O from the kernel and preserve an implementation that does not obstruct it: register application Zi::IOBuf backing for NIC use and avoid userspace payload staging or configuration that prevents supported zero-copy operation. Actual kernel-side copying and capability selection are the responsibility of the kernel/NIC driver and outside this work's scope.
* Independently demonstrate `zxdp` packet exchange, ring ownership, scheduler wakeups, and clean shutdown before Zutcp integration.
* Deliver the first Zutcp client/server over single-host `veth`/`zxdp` using the copied protocol after excluded-feature removal, without requiring TCP interoperability or introducing TCP-specific mechanisms.
* Preserve a working client/server as the implementation is converted incrementally to TCP.
* Implement SACK, timestamps with PAWS, window scaling, DSACK, RACK-TLP, ECN, and TCP Fast Open. Decide separately which extensions peers must negotiate.
* Establish eventual interoperability using packetdrill and the kernel TCP stack over single-host veth loopback, including reliable ordered byte delivery, connection lifecycle, flow control, and recovery from loss and reordering.
* Support both single-host `veth` testing and two hosts connected over Ethernet. Use physical-host measurements to evaluate the hardware packet path.
* Implement Zutcp as a ZiMxPlugin registered on mx before start, preserving the existing Ztcp/Ztls-facing contracts without modifying those modules.
* Preserve the Zquic application model: application-created accepted links, one automatically created bidirectional stream per connection, `streamed(...)` notification, and stream-level `process(RxStream &)`. Accepted Fast Open data may be delivered before `connected()`; that callback signals completed establishment.
* Follow repository shard ownership, retained-buffer lifetime, bounded-memory, and asynchronous teardown rules. Avoid input rejection solely for conformance policing while retaining checks needed for message integrity and safe protocol operation.
* Preserve the copied transport's dedicated Rx/Tx thread model, functionality placement, and thread-exclusive data ownership through every intermediate protocol. Port TCP mechanisms into that model using explicit owner-directed handoffs.
* Evaluate latency and throughput for the target short-message workload against the kernel backend using Clang release builds and author/agent optimization guided by -pg profiles. Record measurements without imposing hard CPU budgets, throughput targets, or latency-percentile acceptance thresholds at this stage.

## Non-Goals

* MPTCP, TCP-MD5, and TCP-AO.
* Encryption or a TLS handshake inside Zutcp; this does not exclude TLS through `ztls`.
* Connection migration, multiple streams per connection, or QIR in Zutcp.
* Switching backends during the active mx lifetime.
* Modifying ztcp or ztls, adding Hub::utcp, or introducing a second backend interface alongside ZiMxPlugin.
* Implementing DPDK or vendor-specific packet-I/O backends in the initial delivery.
* TCP or QUIC wire interoperability for the first intermediate-protocol milestone.
* Preserving message boundaries through TCP: applications must continue to frame FIX messages across arbitrary receive-buffer boundaries.

## Open Questions

Question IDs are permanent. Add new questions with the next unused ID; do not renumber or reuse IDs. Keep resolved questions under their existing IDs with their resolution or a link to the decision.

* **OQ-1 — Intermediate protocol (resolved: OQ-1.A):** Exchange the surviving encoded transport parameters directly in plaintext Initial payloads, retaining Initial/bootstrap, connection-ID routing, and ACK/recovery. Use the Initial → ACK-bearing Initial response → ACK setup described below.
* **OQ-2 — Kernel coexistence (architectural choices resolved):** Follow Yasukata's same-NIC, kernel-configured addressing and selective XDP redirection model with framework three-phase teardown. Selections are OQ-2.1.B (reserved ports), OQ-2.2.B (static kernel neighbors), OQ-2.3.A (endpoint classification), and OQ-2.4.A (endpoint exclusion during teardown). Concrete APIs and retirement events will be specified in implementation planning.
* **OQ-3 — Packet I/O (resolved):** Implement AF_XDP directly, with separate Rx/Tx reserves, zero-copy Zi::IOBuf payload flow, and direct BPF syscalls. Reuse compile-time IOBuf geometry, per-allocator buffer counts supplied through mx, ZiTxStream submission, ZiEventLoop integration, and rxQuantum. Request kernel zero-copy and do not obstruct supported operation; kernel/driver-side capability selection is out of scope. Derive capacities and write concrete bindings/defaults in Detailed Implementation; no further OQ-3 architecture gate remains.
* **OQ-4 — Extension negotiation (resolved: B):** Negotiate all agreed peer extensions optionally. Accept connections without SACK or timestamps and implement the corresponding recovery/RTT behavior. Use window scale zero when scaling is absent; ECN and Fast Open are optional. RACK-TLP remains a local mechanism used with SACK. See the selected profile below.
* **OQ-5 — Backend integration (resolved: ZiMxPlugin):** Zutcp implements the new interface in zi/src/ZiMultiplex.hh/.cc; ztcp and ztls are unchanged. Use plugin registration, allocator/lifecycle hooks, ZiConnection contexts/callbacks, rx/wake, and mx.rxQuantum directly. The earlier Hub::utcp and Ztcp/Ztls adapter alternatives are superseded.
* **OQ-6 — Conversion stages (resolved: slices adopted):** Each Zutcp phase extends the previous working client/server, ports or adapts existing QUIC mechanisms toward TCP, and ends with an executable intermediate protocol. The final slice establishes TCP interoperability. Phase 1 retains independently configured protocol threads; phase 2 moves protocol work onto mx I/O threads and removes Zutcp thread selectors. Independently verified Zxdp and the verbatim copy/rename are prerequisites; complete file-level changes remain to be enumerated.
* **OQ-7 — Performance methodology and configuration (resolved for this stage):** Use -pg profiling of Clang release builds to guide author/agent optimization against representative short-message workloads. Provide configuration equivalent to Zquic::HubParams, with maxCxns connection-count equivalents replacing maxStreams limits. Measure results without hard CPU budgets, throughput targets, or latency-percentile acceptance thresholds.

## Technical Plan (Architecture)

### Preserve the Zquic Rx/Tx ownership model

Dedicated protocol Rx and Tx ownership is an architectural requirement. Preserve the association between each function and its exclusively owned data while changing thread binding between phases 1 and 2. Phase 1 retains the copied HubParams protocol rxThread/txThread configuration, independently of the multiplexer I/O threads; these thread pairs typically differ, but may coincide. Phase 2 executes protocol work directly on mx's I/O Rx/Tx slots, configured through ZiMxParams, and deletes Zutcp's separate thread selectors. This is a change in execution placement, not a relaxation of thread-exclusive ownership. Control/startup and logging facilities do not become additional protocol-data owners. Keep the existing separation and cache-line layout of Rx-owned and Tx-owned state where applicable.

The starting ownership map comes from the copied implementation, including the explicit Rx/Tx sections and assertions in [ZquicStream.hh](./zquic/src/ZquicStream.hh) and [ZquicLink.hh](./zquic/src/ZquicLink.hh). Adapt protocol semantics within those ownership boundaries. Do not consolidate the connection into a shared mutable TCP control block accessed from either thread.

The table describes the integrated ownership from phase 2 onward. In phase 1, packet-I/O ring owners and independently selected protocol owners can be different threads; preserve explicit handoffs between them, transferring original buffer references without payload copies. When configured to coincide, the same ownership contracts apply without an unnecessary cross-thread dispatch.

| Owner | Functionality and exclusively owned state to preserve or adapt |
| --- | --- |
| Rx thread | Packet reception/parsing, receive sequence/reassembly state, receive credit and application delivery, incoming connection acceptance, and receive-side lifecycle. Own AF_XDP RX consumption and FILL publication. |
| Tx thread | Application transmit processing, packet construction/submission, retained sent data, sender acknowledgment/SACK processing, retransmission/loss timers, RTT estimation, congestion control/pacing, peer-advertised send credit, and transmit-side lifecycle. Own AF_XDP TX publication and COMPLETION consumption. |

Receiving an ACK on Rx does not move sender recovery to Rx. Parse and capture its bounded evidence, then post processing to Tx, which updates its own sent-data, recovery, RTT, and congestion state. Similarly, Rx owns receive-window/reassembly state and hands the information needed to advertise a window or send an ACK to Tx; Tx does not inspect Rx-owned structures while constructing a packet. Hand off peer window updates to the Tx owner of send credit. Tx completion/close events affecting Rx are posted back to Rx rather than mutating receive state directly.

Keep cross-thread entry points as thin dispatchers, with owner-only implementations and thread assertions, following [IO.md](./docs/IO.md). Transfer original buffer references and fixed metadata; use destination-owned storage/handles for larger handoffs. A retained packet can cross threads without copying payload, but a reference does not grant concurrent mutation of its metadata, headers, or protocol state. Maintain explicit ownership and in-flight retirement rules. The selected contended allocator free slists are a deliberate cross-thread reclamation mechanism; they do not authorize cross-thread ring or connection-state access.

Keep protocol timer callbacks and telemetry observation consistent with the same ownership map: execute mutable work on its owner and observe through the established snapshots/enumeration contracts. Preserve the Rx-drain then Tx-drain shutdown sequence. Do not introduce connection locks, shared mutable queues, or arbitrary atomics as substitutes for the copied shard boundaries.

For every conversion slice, record the source QUIC function/state owner, its TCP counterpart, and required handoffs. Verify owner assertions while the actual client/server runs under bidirectional traffic, ACK/loss processing, timer expiry, backpressure, observation, and teardown. This makes preservation of the thread model part of each phase's exit evidence, rather than a later optimization. Evaluate the resulting latency under OQ-7; thread isolation is a design commitment, not evidence of a performance result by itself.

### OQ-1: Establishment after deleting encryption and TLS

Deleting TLS does not delete the transport's need to identify a connection, learn its peer's receive limits, and determine whether the peer received its response. The natural remainder is an Initial/response/ACK exchange using the copied transport mechanisms. It does not require a second, independently designed hello protocol or TCP SYN semantics. This is a proposed derivation for the intermediate milestone, not a claim that standard QUIC supports an unencrypted mode.

#### What is already independent of TLS

* [ClientBootstrap](./zquic/src/Zquic.hh) creates initial destination and source connection IDs.
* The server routes an unknown Initial to `App::accepted(...)`, obtains the application's link, and retains it; see the server's `routeLong_()` and `accept_()` in [Zquic.hh](./zquic/src/Zquic.hh).
* [ServerBootstrap](./zquic/src/Zquic.cc) records the client's source ID and original destination ID, chooses a server source ID, and supplies bootstrap metadata. Its current reset-token generation and other excluded-feature dependencies must be removed along with those features; transport identity does not require them.
* Packet numbers, ACK ranges, retained control output, loss recovery, and PTO provide transport delivery evidence and recovery independently of TLS message contents.
* The lifecycle already separates Starting, Handshaking, and Established in [ZquicLink.hh](./zquic/src/ZquicLink.hh). A pending setup state can remain after the TLS machinery is deleted.

The current coupling is explicit: `readyToEstablish_()` requires `oneRTTReady()` and installed transmit/receive AppData secrets. Peer parameters also live behind `m_crypto.peerParams()`, and client startup emits TLS output to cause the first Initial. Removing the cryptographic code without replacing those dependencies leaves a silent client or a connection that never becomes established.

Transport parameter serialization itself is reusable: `Crypto::encodeParams()` and `decodeParams()` merely delegate to `TransportParams::encode()` and `decode()`. Preserve the required transport values and codec outside the deleted Crypto object. Standard QUIC carries those values in TLS; their new placement is an intermediate-protocol decision. [QUIC transport handshake](https://www.rfc-editor.org/rfc/rfc9000.html#section-7), [TLS integration](https://www.rfc-editor.org/rfc/rfc9001.html#section-8.2).

#### Natural packet exchange

```text
Client                                                   Server
  |                                                         |
  | Initial: original DCID, client SCID,                     |
  |          ack-eliciting setup content ------------------->| accept link
  |                                                         | choose server SCID
  |<---------------- Initial response: DCID = client SCID,   |
  |                  SCID = server SCID, ACK of client setup,|
  |                  ack-eliciting setup content             |
  |                                                         |
  | ACK of server setup response -------------------------->| setup confirmed
  |       (may be carried with first stream data)            |
  |                                                         |
  |<============ retained single-stream protocol ===========>|
```

The server retains the mapping from the client's original destination ID so retransmitted client Initial packets reach the same pending link. The client adopts the server's source ID for subsequent destination addressing. Bind the connection to its peer tuple; keeping bootstrap IDs at this milestone does not retain connection migration.

Proposed completion semantics are asymmetric, as in many transport exchanges: the client has peer-response evidence when it receives the server's setup response; the server has response-receipt evidence when its setup output is acknowledged. These become establishment events in place of TLS completion. Prepare the sole stream and preserve the native link/stream callbacks while mapping the plugin path onto the existing ZiConnection callback contract described in OQ-5. Exact internal publication sequencing belongs in implementation planning, not a new Ztcp/Ztls API. Initial-stage application early data is not required by this proposal; the previously accepted TCP Fast Open behavior applies when Fast Open is implemented later.

Do not require an acknowledgment of the final pure ACK. If that ACK is lost, the server's outstanding ack-eliciting response drives recovery, and the client acknowledges the repeated setup output. Retransmissions use the copied packet-number/recovery conventions, with new packet numbers for retransmitted content. A duplicate Initial, response, or confirmation must not recreate the link, reset parameters, recreate the stream, or repeat `connected()`. Apply setup deadlines and drain pending output on failure.

Packet receipt is transport delivery evidence, not cryptographic peer authentication. With protection removed, define the plaintext packet layout without header-protection sampling or AEAD tag assumptions and retain accidental-corruption detection covering setup metadata and application bytes. An AF_XDP transport constructs and checks its packet integrity explicitly; packet I/O alone does not replace the deleted authentication tag.

#### Selected policy: OQ-1.A, plaintext transport parameters

The author selected OQ-1.A. Both endpoints exchange the surviving encoded TransportParams directly in plaintext Initial payloads, install peer limits before allowing dependent transmission, and use the existing ACK/recovery mechanisms to complete setup. The fixed-profile option below is retained only as a considered alternative.

| Option | Mechanism | Trade-off and failure condition |
| --- | --- | --- |
| **OQ-1.A — Retain parameter exchange** | Put the surviving encoded TransportParams directly in a bounded, explicitly identified plaintext Initial payload. Initial packet context distinguishes this from normal frame payload; no TLS records or CRYPTO byte stream remain. Both parameter-bearing setup packets elicit ACKs. | Preserves independent endpoint settings and tests receive-credit initialization. Requires a small temporary payload-format change and recovery retention for those values. Fails if the server assumes receipt of a header means the parameter payload was processed, or if bounds/credits are installed after application transmission begins. |
| **OQ-1.B — Fixed initial profile** | Configure an explicitly matching initial profile for the paired test applications. Send existing PING frames in the Initial and its ACK-bearing response to make setup ack-eliciting. No parameter payload is needed. | Uses existing frame types and minimizes the intermediate wire change. Cannot negotiate independently configured receive limits; it fails when peer profiles differ or when bootstrap defaults leave zero transmit credit. Document and test the shared profile rather than assuming equality. |

OQ-1.A preserves the mandatory copy/rename/delete sequence and contains no TCP-specific modifications. Establishment follows the existing Initial and ACK machinery; the transport-parameter exchange replaces its TLS carrier.

Acceptance tests must cover normal setup, loss of each flight, duplicate setup packets, a lost final ACK, setup timeout, consistent connection-ID routing, exactly-once callbacks, differing receive limits, and data exchange on the sole stream. No kernel TCP or standard QUIC interoperability is expected at this stage.

### OQ-2: Kernel coexistence following Yasukata's work

The author selected alignment with Yasukata's implementation. Follow the same-NIC coexistence mechanism in [bench-iip](https://github.com/yasukata/bench-iip/blob/master/README.md#optional-sharing-a-network-interface-with-the-kernel-space-network-stack), together with [iip-af_xdp](https://github.com/yasukata/iip-af_xdp/blob/master/main.c), rather than requiring a separately addressed or dedicated NIC.

#### Source mechanisms to preserve

* Keep the interface configured through the kernel. Yasukata's backend obtains the interface MAC and IPv4 address using a control socket and `SIOCGIFHWADDR`/`SIOCGIFADDR`. Continue to use the framework's existing address and resolution facilities; this observation does not require duplicating those facilities or ioctl wrappers where Z already supplies them.
* Load a custom XDP classifier. The documented sharing example matches a TCP destination port, redirects matching traffic through the XSK map, and returns `XDP_PASS` for other traffic. This keeps other host services and kernel network functions available on the same interface.
* Prevent the AF_XDP library from replacing that classifier: the example uses `XSK_LIBBPF_FLAGS__INHIBIT_PROG_LOAD` and explicitly registers the created sockets in the selected XSK map. The exact library dependency is still part of OQ-3; preserve these semantics whichever implementation is selected.
* Keep the mapping from receive queue to AF_XDP socket explicit. Yasukata's benchmark configures queue count to match its packet worker count; adapt queue ownership to the Z scheduler shards rather than copying its private thread implementation.

For the first Zutcp milestone, classify the intermediate protocol's actual traffic, including its UDP-shaped envelope if retained, rather than matching TCP before TCP exists. At the eventual TCP stage, classify server listener endpoints and client return traffic. Continue ordinary `ZiResolver`/`ZiIP` use on the control path. No userspace replacement for DNS is needed.

#### Details to finish within this selected model

The referenced coexistence example is a server demonstration with a fixed destination port and fixed IPv4 offsets. It establishes the architectural direction; it does not settle active-client endpoint allocation, all packet forms, or shutdown races. Complete those mechanisms explicitly:

* Extend packet selection to configured local addresses/listener ports and active-client endpoints, with bounded parsing of actual header lengths and supported address families. Do not redirect all traffic on a queue merely because an XSK exists.
* Define exclusive ownership of selected endpoints against kernel sockets. The reference's port filter is not itself a kernel port reservation. Select the concrete enforcement mechanism during interface planning.
* Inspect and specify the neighbor/next-hop path for active opens and any reply path not covered by receive-side addressing. The backend's local interface-address queries do not constitute route lookup or neighbor resolution; the sharing example passes nonselected discovery traffic to the kernel. Do not claim that a new kernel-neighbor cache integration already exists in the reference.
* Install socket/map and selection state before exposing an endpoint or sending its first packet. Define how late packets are handled during close and after socket failure, avoiding unintended kernel TCP responses for connections still owned by Zutcp. Yasukata's sample passes on an unavailable XSK entry; evaluate that fallback against live-connection ownership rather than adopting it silently.
* Attach/detach only the program and map state owned by this backend, preserving unrelated interface users.

#### Selected shutdown contract: Z framework I/O teardown

Follow [IO.md: Teardown](./docs/IO.md#teardown) and [FRAMEWORK.md: Timer Teardown](./docs/FRAMEWORK.md#timer-teardown). The author explicitly selected this contract; it is not an architectural alternative.

1. Prevent new application submissions, connection admission, ingress dispatch, and timer/I/O work for the stopping owner. Cancel its timers with `ZmScheduler::del`, guard late callbacks, and wake any polling or blocked worker. Quiesce selection of further packets into the retiring runtime without prematurely returning still-owned connections to kernel TCP. Post a continuation to the Rx owner to drain previously admitted work.
2. In the Rx continuation, finish or cancel pending receive processing and arrange retirement of retained receive buffers through their owning return paths. Drain late timer callbacks on their execution shards. Post the teardown continuation to the Tx owner; do not inspect or mutate Tx-owned state from Rx.
3. In the Tx continuation, drain or cancel pending sends and complete the backend's kernel/DMA retirement protocol before releasing packet backing. Complete teardown and release object ownership only when callbacks, queued work, and dependent resources can no longer reference the retiring objects. Use further asynchronous continuations where hardware retirement or buffer returns are still pending; do not block a shard waiting for them.

Keep the scheduler and required wake mechanisms operational through these continuations. Unregister wake functions only after their consumers are quiescent. Release XSKs, maps, ring mappings, and UMEM in an order that prevents kernel or queued userspace access to released storage. A connection teardown must not destroy packet resources still shared by other connections; final backend teardown owns that release. Do not keep owners alive by adding reference counts to populous buffers or timer callbacks where the framework requires drained raw back-pointers. Only main-thread callers may block waiting for overall completion.

The three phases settle shutdown organization. OQ-2.4.A selects endpoint-level exclusion around that sequence. Normal TCP close/TIME-WAIT behavior must remain distinct from destruction of the packet-I/O backend.

#### Remaining choices and suggested answers

The table preserves the considered alternatives within Yasukata's selected shared-interface model. The selected answers are OQ-2.1.B, OQ-2.2.B, OQ-2.3.A, and OQ-2.4.A; the other alternatives are not part of the plan.

| Stable ID | Option A | Option B |
| --- | --- | --- |
| **OQ-2.1 — Local endpoint ownership** | Hold a kernel socket bound to each claimed local address/port, without performing kernel TCP connect/listen for user-mode traffic. Use the appropriate protocol socket for the intermediate UDP envelope and eventual TCP. Keep it through endpoint retirement. This uses kernel allocation to detect conflicts but costs descriptors and needs explicit bind/reuse/wildcard tests; the bound socket does not itself suppress kernel replies to leaked packets. | Allocate user-mode client ports from an operator-reserved range and explicitly assign server ports. Exclude the range from kernel automatic assignment and enforce exclusive use operationally. This reduces control sockets but adds deployment policy. `ip_local_reserved_ports` does not prevent explicit binds, so it alone cannot guarantee exclusivity. |
| **OQ-2.2 — Active-open neighbor resolution** | Use kernel route/neighbor control APIs to find the outgoing interface, next hop, and MAC, request resolution when missing, and receive asynchronous updates. Keep ARP/ND packets on the kernel path. This preserves ordinary networking but requires a control-plane adapter and bounded waiting when discovery fails. | Supply static neighbor entries through normal kernel configuration for the directly connected host/veth topology, then read them for initial transmission. This simplifies the first deliverable but requires operator/test setup and fails when neighbor addresses change without configuration updates. It is not a complete dynamic-neighbor solution for eventual general deployment. |
| **OQ-2.3 — Classifier granularity** | Redirect selected local address/protocol/port endpoints and demultiplex connections in Zutcp. This closely follows Yasukata's port-based sample and keeps BPF state proportional to endpoints. It cannot divide ownership among kernel and user-mode peers sharing the same endpoint. | Redirect server listener matches plus exact active-client/established flow matches, with explicit precedence. This narrows capture but adds per-flow map lifecycle and races to test. Initial server traffic must still match a listener before a flow exists. |
| **OQ-2.4 — Late packets and unavailable XSKs** | Keep endpoint-level selection active with a quiesced discard action while its userspace owner drains; retire the exclusion only when endpoint/transport lifetime permits reuse. This is simple for exclusively owned ports but can temporarily prevent reuse of the whole endpoint. | Replace retiring flow entries with bounded per-flow discard tombstones while retaining listener and endpoint ownership separately. This allows finer reuse but adds retirement state and must handle late Initial/SYN packets as well as established traffic. |

**OQ-2.1 resolved: Option B.** Configure explicit user-mode server ports and a user-mode client-port range. Operators exclude those ports from kernel automatic allocation using `ip_local_reserved_ports`, preserving existing reservations when applying configuration. Zutcp allocates client ports within its configured range and detects exhaustion without unbounded searching or allocation. Deployment policy prohibits explicit kernel binds and overlapping assignments by independently managed user-mode instances to these ports. The sysctl alone does not enforce those prohibitions. Do not create placeholder bound sockets as the reservation mechanism. Document the reservation scope and verify it in deployment/test setup; the configuration interface and any coordination between multiple Zutcp instances remain to be detailed in implementation planning.

**OQ-2.2 resolved: Option B.** Configure static kernel neighbor entries for the veth or directly connected Ethernet peers and read them to obtain the next-hop MAC before active transmission. Retain ordinary kernel-configured interfaces, routes, ZiResolver, and ZiIP. The test fixture installs its own entries; physical-host deployment supplies the corresponding entries. Missing or unusable entries produce an actionable connection/setup failure rather than an implicit dynamic-neighbor fallback. The deployment must update entries when link-layer addresses change. Dynamic neighbor discovery by Zutcp is outside this selected configuration.

**OQ-2.3 resolved: Option A.** Match configured local address, IP protocol, and reserved server/client ports in the XDP classifier. Redirect matching traffic to the appropriate queue's XSK and demultiplex connections inside Zutcp. BPF selection state is endpoint-based, not per connection. Unselected traffic passes to the kernel. Match the intermediate protocol's actual envelope in the first milestone and TCP only after wire conversion.

**OQ-2.4 resolved: Option A.** Retain endpoint-level exclusion during owner shutdown. Quiesce owned endpoints to discard further packets before draining their runtime, retaining the exclusion until endpoint/transport retirement permits reuse. XSK unavailability for an owned endpoint must not cause fallback to kernel TCP. Apply this fence to the stopping endpoint owner, not to unrelated live connections using the same backend. Individual connection closes still use the transport's close and late-packet rules while their endpoint remains active. Do not add per-connection BPF tombstones as the shutdown mechanism.

Unrelated traffic continues through the kernel. Retirement follows explicit transport/owner events and any required protocol deadlines, not periodic background garbage-collection scans. Specify the exact retirement event in implementation planning rather than inventing an arbitrary timeout.

Yasukata's [iip implementation](https://github.com/yasukata/iip/blob/master/main.c) supplies another useful distinction for OQ-2.2: passive-open code extracts the received Ethernet source address, and active-open APIs accept link-layer addresses. That explains how server replies can work without a generic route/neighbor adapter; it does not solve the first outgoing packet for our active client. Preserve that distinction when adapting the design. The kernel's [neighbor `use`/`managed` controls](https://man7.org/linux/man-pages/man8/ip-neighbour.8.html) provide concrete mechanisms to investigate for Option A. [IP socket binding](https://man7.org/linux/man-pages/man7/ip.7.html) and [reserved-port semantics](https://docs.kernel.org/networking/ip-sysctl.html#ip-variables) support the OQ-2.1 alternatives.

Attach/detach only backend-owned programs and map entries, preserving unrelated users of the interface; this is a requirement common to the options. The library/program-loading mechanism remains part of OQ-3, not a second competing OQ-2 decision.

Verify owned user-mode traffic alongside an unrelated kernel service on the same interface, with both incoming server connections and outgoing client connections. Include configured/missing static neighbors, reserved-port allocation and exhaustion, endpoint classification, map/socket unavailability, duplicate or late packets, and stop/restart. Verify three-phase teardown both while idle and with retained buffers, pending Tx, timers, and queued continuations outstanding.

### OQ-3: Proposed packet-I/O interface and execution model

This design follows Yasukata's separation of packet I/O from the transport, while retaining Z framework buffer, shard, and scheduler contracts. OQ-3's architectural decisions are resolved. The API details below are aligned with the ZiIOBuf and ZiMultiplex source inspected on 2026-11-09; remaining concrete declarations belong in Detailed Implementation.

**OQ-3.3 resolved: Option B.** Use direct kernel BPF syscalls to create and populate maps and load the endpoint classifier, rather than a libbpf loader dependency or external program provisioning as the runtime setup mechanism. Implement attachment and detachment through the kernel interfaces and track backend-owned resources explicitly. Preserve existing interface users and the selected OQ-2 endpoint/quiescing policy. Include verifier diagnostics and cleanup of partially completed initialization. The concrete classifier instruction packaging, map declarations, attachment interface, supported-kernel requirements, and ownership checks must be specified in Detailed Implementation; this selection does not authorize overwriting another program. The remaining-options list below keeps OQ-3.3 as a resolved ID.

#### OQ-3.1 — AF_XDP implementation dependency

| Option | Mechanism | Trade-off and failure condition |
| --- | --- | --- |
| **OQ-3.1.A — Encapsulate libxdp** | Use libxdp for UMEM/XSK creation and ring helpers, and the chosen BPF loading facilities for our endpoint classifier. Keep all XSK types and handles inside Zxdp. Disable default program loading and explicitly populate the owned XSK map, following Yasukata's coexistence example. | Reduces locally maintained kernel setup and barrier code, but introduces a build/runtime dependency and a supported-version requirement. Fails if library setup silently installs a broad default classifier or if its cleanup removes another interface user's program. |
| **OQ-3.1.B — Direct AF_XDP ABI wrapper** | Implement socket setup, memory mapping, descriptor access, publication ordering, and wakeup checks inside Zxdp. Use BPF loading support separately for the same endpoint classifier. | Gives direct control over the wrapper but makes this repository maintain the ABI and memory-ordering implementation. Fails if cursor caching, barriers, or feature detection differ from the kernel contract. It must pass the same ring and lifecycle tests as Option A. |

**OQ-3.1 resolved: Option B.** Implement AF_XDP socket creation and configuration, UMEM registration, ring-offset discovery and memory mapping, device/queue binding, descriptor access, cursor caching, acquire/release publication, and driver-wakeup handling directly in Zxdp. Do not depend on libxdp's `xsk_*` lifecycle or ring-access helpers. Keep the kernel ring ABI and all AF_XDP handles private to Zxdp. Validate memory ordering and cursor wraparound against kernel definitions and independent tests on supported x64/ARM targets; neither ZmRing's layout nor unqualified volatile accesses replace that ordering contract.

Yasukata's AF_XDP backend uses `xsk_*` helpers and supports selecting their header source. Retain its ownership, servicing, and coexistence mechanisms as primary precedents while replacing those helper calls with the selected direct wrapper. Option A remains documented as an unselected alternative. The separate method/dependency for compiling, loading, and attaching the XDP classifier is not decided by OQ-3.1.B; direct AF_XDP access does not itself require reimplementing BPF loading.

#### Proposed normalized API

Expose a device/queue binding and packet operations, without TCP connection state. The following operation names describe responsibilities; final declarations belong in Detailed Implementation.

* **Configuration:** require the application's `mx` and use its scheduler and configured Rx/Tx slots. Reuse existing compile-time IOBuf payload geometry and cumulative layer framing requirements; obtain each pool's requested buffer count from the `count` argument to mx's allocator factory, forwarded to ZiMxPlugin::allocator. Device/queue binding, descriptor-ring capacity mapping, waiting controls, and required copy/zero-copy capabilities complete backend configuration. Do not introduce a parallel payload-geometry or reserve-count configuration scheme. Resolve and validate all immutable resources before starting packet service.
* **Receive:** acquire a bounded batch of packet references and metadata, then release descriptor slots independently of retained packet backing. Provide lengths and packet metadata needed by the copied transport without exposing libxdp descriptors.
* **Transmit:** allocate application output from registered Tx backing with cumulative layer headroom/tailroom, construct the application payload once in that backing, add protocol framing in its reserved space, and submit the same backing in a bounded batch on Tx. Do not gather application bytes into a second packet buffer. Report accepted versus backpressured work without hidden unbounded queuing. The transport retains responsibility for retransmission bytes and peer acknowledgments.
* **Completion and recycling:** retire kernel-held transmit frames on Tx; return received-frame ownership to Rx only after final reference release. Provide counters for pool depletion, full rings, drops, invalid descriptors, driver wakeups, and outstanding kernel/application ownership.
* **Lifecycle:** asynchronous init/start/stop/final responsibilities with the selected three-phase teardown and explicit completion. Endpoint registration and quiescing implement OQ-2's selection policy; Zxdp does not parse TCP connection state to decide when a connection is established.

Reuse `Zi::IOBuf` and the existing `Zi::IOBufPluginAllocator` adapter directly for retained packets and ranges. Its capacity, data pointer, offset, length, allocator, and owner fields already supply the required per-buffer metadata; do not introduce a duplicate packet-metadata wrapper or treat metadata placement as an open architectural question. Implement the backing provider through `Zi::IOBufPlugin`. It derives frame identity from the backing address and registered layout and owns reserve/free-frame accounting. Do not keep a naked span after retiring its frame or copy received payload into a staging array just to cross shards. Final releases return frames through the existing allocator callbacks; they do not keep the connection alive by adding references contrary to framework lifetime rules. See [ZiIOBuf.hh](./zi/src/ZiIOBuf.hh).

#### Current IOBuf allocation and release contract

Use the interfaces already supplied by Zi; do not add a parallel allocator hierarchy:

| Interface | Current contract | Zutcp/Zxdp use |
| --- | --- | --- |
| `Zi::IOBufAllocator__` | Carries mx/type and provides const `alloc(unsigned size, bool initial = false)` and `free(uint8_t *ptr, unsigned size, bool initial = false)` backing callbacks. `IOBufAllocator_` is its intrusive list-node type. | Reuse the supplied allocator adapter; keep mx/type and the allocator pointer already present in IOBuf. |
| `Zi::IOBufAllocator` | Adds `allocBuf(void *owner = 0, uint32_t length = 0)`, returning `ZmRef<IOBuf>`. | Upper layers allocate buffer objects through this interface, preserving owner/length. |
| `Zi::IOBufPlugin` | Supplies `alloc(uint32_t size, bool initial)` and `free(uint8_t *ptr, uint32_t size, bool initial)`. | Implement registered-frame acquisition and intrusive recycling here. |
| `Zi::IOBufPluginAllocator<Type, Size, Base>` | Its `allocBuf()` obtains backing with `plugin->alloc(Size, true)` and constructs its supplied buffer type with initial backing. All subsequent backing allocation/free operations also delegate to the plugin. | Use the existing object adapter with the required buffer base; no new inline payload array or duplicate object wrapper. |
| `Zi::IOBufBuiltinAllocator<Type, Base, Size, MaxSize, Align, HeapID>` | Allocates builtin buffer objects and delegates backing allocations to its VHeap. | This is mx's no-plugin path, not Zutcp's UMEM backing path. |

IOBuf now tracks `Builtin`, `Initial`, and `Failed` separately. For a newly allocated UMEM-backed buffer, `initial()` is true and `builtin()` is false. Use `data_()` to recover its unoffset backing pointer, `data()` for the readable pointer after skip, and the existing accessors to mask packed flags. Initial backing release reaches `allocator->free(data_(), size, initial())`, and the plugin allocator forwards that pointer, size, and initial flag to its backing provider. Identify the registered reserve from the canonical backing address, not from the current readable offset or length.

The generic plugin adapter supports growth: a growth allocation is forwarded with `initial=false`, and replacing initial backing clears the Initial flag while freeing the old backing with its original identity. That generic capability does not change the selected fixed-UMEM contract. Zutcp/Zxdp's provider must make a non-initial growth allocation a fatal internal error before replacement backing or a payload copy can occur. Do not change Zi's generic plugin-growth behavior or assume that a global ZiIOBuf guard already implements this backend-specific invariant. Initial `allocBuf()` length must fit its fixed capacity; normal ZiTxStream rollover prevents jumbo output from reaching a growth request.

The backing provider and registered reserves must outlive every buffer and allocator callback that references them. Mx retains factory-created allocator objects in its allocator list through stop and destroys that list at mx destruction; stop is not allocator deregistration. Preserve the selected asynchronous retirement ordering without releasing a provider or UMEM still reachable by an outstanding initial buffer.

**Selected recycling mechanism.** Use the non-sharded `ZmHeapCache::alloc_()`/`free_()` algorithm from [ZmHeap.hh](./zm/src/ZmHeap.hh). Maintain a separate intrusive free slist for each Rx and Tx reserve. Overlay each free frame's next pointer on its unused payload backing; do not add a per-frame list-node prefix or a separate return-node allocation. Reuse the existing contention protocol: allocation claims the atomic head using its low-bit marker before reading the next pointer and publishing the new head; free writes the previous head into the returned frame and publishes it by compare-and-exchange, retrying against concurrent operations. Preserve the source algorithm's atomic ordering and alignment requirements. Ignore its sharded fast path, as directed by the author, and do not copy the outer heap-allocation fallback into the fixed UMEM reserve.

The allocator slist is the explicit ownership handoff: final release may call `free_()` from another shard, and `alloc_()` obtains exclusive ownership from the same reserve. No additional cross-shard return queue is needed. This shared allocator operation does not grant non-owner access to RX, FILL, TX, or COMPLETION rings or to mutable protocol state. Only the Rx owner publishes acquired receive frames to FILL; only Tx owns its transmit/completion rings. Overlay the free link only after all application/transport references and any kernel/NIC use have retired. Recover the frame's canonical backing address before recycling; never place the link at an advanced readable offset. Initialize the lists before publication/start and retain their allocator/UMEM lifetime through all outstanding frame releases.

**OQ-3.4 resolved: fixed UMEM backing with no builtin storage.** UMEM-backed Zi::IOBuf instances have no builtin payload storage; the initial backing and capacity come from their registered frame. Reuse the existing compile-time payload-geometry specification without instantiating an unused builtin payload array. Tx uses ZiTxStream's fixed-capacity and cumulative framing limits so jumbo output rolls into additional buffers before any individual buffer would exceed that capacity. Attempting to grow a Tx buffer beyond its initial UMEM-backed allocated capacity is a fatal internal error. It must not trigger heap reallocation, frame replacement, a payload copy, or a recoverable allocation-failure fallback. Enforce this invariant in release builds as well as debug builds, including indirect grow paths through generic buffer helpers.

Rx receives Ethernet packets as-is into backing sized from the MTU and required link-layer geometry; a received UMEM buffer never grows. Reassembly retains references/ranges across received packets rather than expanding the packet backing. Concrete UMEM allocator declarations and final-release recycling operations belong in Detailed Implementation; reuse the existing IOBuf metadata rather than designing additional per-buffer bookkeeping. The adapter's architecture and growth policy are settled. Reserve exhaustion is separately handled as bounded backpressure and is not the same condition as a fatal attempt to grow an already allocated frame.

Keep the boundary backend-neutral by using packet references, capacities, queue bindings, and ownership operations rather than exposing UMEM offsets or XSK objects to Zutcp. Supporting another backend later does not require implementing a DPDK abstraction or backend registry now. The choice of static or indirect dispatch at that future boundary is not settled by this proposal.

#### Zero-copy buffer flow and existing layering

**Selected geometry and scheduler dependency.** Use the existing IOBuf compile-time sizing, capacity/alignment specification, and layer headroom/tailroom requirements as the source of payload geometry. UMEM allocation/registration adapts to that geometry; it does not define another application-visible packet geometry. Kernel-required chunk alignment, registration limits, and descriptor offsets remain backend details that must preserve the specified usable capacity. Define the compatible layout and diagnose unsupported configurations rather than silently changing the payload contract.

**Updated pool-count contract.** Applications request each pool's desired number of buffers through `mx->allocator<Type, Base, Size, MaxSize, Align, HeapID>(count)`. Mx forwards count with the requested geometry to `ZiMxPlugin::allocator(type, size, maxSize, align, count)`. The current factory default is `ZiIOBuf_DefltCount` (1024 unless overridden at compile time). Preserve each allocator request's count alongside its geometry, so differently sized application pools can be provisioned independently. Zutcp's separate control allocator also has an explicit pool count. This supersedes the earlier design using ZiMxParams::rxBufSize/txBufSize as user-mode pool counts; those fields retain their kernel socket-buffer meaning. Do not derive a per-allocator count from them or add a parallel application pool-count interface.

**Framing budgets are derived, not configured.** Zxdp contributes the Ethernet framing geometry. Zutcp adds the IP and TCP framing geometry above it. The existing layer machinery deterministically sums the required headroom/tailroom from the actual protocol stack; these budgets are not another operator setting or an architectural choice. Account for supported protocol variants/options in that calculation, reserving the required capacity before application payload construction and using the actual encoded lengths for each packet. For the initial QUIC-derived intermediate milestone, derive its surviving transport framing in the same way; use TCP geometry once the transport is converted. Pass the accumulated geometry to the existing compile-time IOBuf allocation contract without introducing an independent UMEM payload specification.

Both `zxdp` and `zutcp` require an `mx`, even when no kernel UDP/TCP data-path socket is used. `ZiMultiplex` inherits ZmScheduler; use that existing scheduler and lifecycle context rather than creating a second independent scheduler. Kernel resolver/control-plane services can remain available through normal framework facilities. A user-mode Hub still supplies its mx during initialization, and mx must outlive the dependent packet servicing and asynchronous teardown.

The author requires zero-copy transfer of Zi::IOBuf-backed payload storage between the application and packet I/O. For AF_XDP, allocate the packet storage in userspace and register it as UMEM; UMEM registration is not an allocation of application payload storage by the kernel. Allocate application output directly from the registered Tx reserve, with the complete framing budget supplied through the existing ZiRx/ZiRxStream and ZiTx/ZiTxStream layers. Add intermediate QUIC-derived or eventual TCP, IP, and Ethernet headers and any software-required trailers in reserved space. Change buffer ranges/offsets on receive rather than moving payload bytes.

The existing code provides part of this mechanism. H3's `DataStream` in [ZhttpH3.hh](./zhttp/src/ZhttpH3.hh) derives from ZiTxLayer, adds nine bytes of headroom for its own DATA frame header, and prepends that header with `rewind()`. ZiTxLayer accumulates lower-layer headroom/tailroom, and ZiRxLayer similarly accumulates receive geometry. This supports payload-preserving layering into the QUIC stream.

Current Zquic deliberately retains plaintext stream buffers and encrypts gathered input ranges directly into a final packet backing in `sendProtShortPkt_`. H3 buffers are inputs to individual QUIC streams, while QUIC packetization can combine data from several streams into one packet. This separate output backing serves both encryption and packet consolidation; it is appropriate for QUIC and does not imply an avoidable payload-copy stage. Encryption writes the final packet directly rather than first copying plaintext into a staging buffer. Source-to-destination encryption need not impose a meaningful penalty relative to in-place encryption; the relative performance depends on the actual packetization and crypto implementation and is not established by this design discussion. [Zquic's guidelines](./zquic/GUIDELINES.md) explicitly require that architecture while retaining plaintext for recovery. Its receive path already retains slices of the decrypted packet.

The author adopts a different packetization target for Zutcp because encryption and inter-stream consolidation are removed. Preserve the existing layer geometry and retained-buffer machinery, accumulate transport/IP/Ethernet framing space beneath the application, and submit the original application payload backing directly. This is an adaptation to the single-stream unencrypted transport, not a repair of QUIC's encrypted packet construction. Segmentation and retransmission still need explicit range and header-lifetime treatment to maintain this property.

Existing generic helpers also have copy paths: `ZiTxStream::append(buffer)` copies when adoption lacks capacity or framing space, and `ZiRxStream::extract()` can gather/split into new storage. For the admitted zero-copy Zutcp path, provide compatible registered backing and geometry and use retained ranges so these paths do not silently move payloads. Explicit application writes from external memory remain distinct from copying an already constructed buffer inside the transport.

Keep submitted backing stable while the kernel/NIC owns it, including headers: retransmission cannot rewrite a header still in flight. Retain the same application payload backing until transport retirement as well as DMA retirement permit release. Independent descriptor/control metadata may be copied; application payload bytes must not be repacketized through a staging buffer. Specify partial acknowledgments, retransmission ranges, segmentation, variable header sizes, and insufficient-room behavior without assuming all received FIX messages preserve packet boundaries.

**Selected retransmission granularity: one buffer per packet.** Zutcp's wire buffer-to-packet relationship is 1:1. Retain and retransmit at buffer/packet granularity rather than designing arbitrary interior-subrange retransmission. A wire resend begins at `skip=0` and includes the packet headers, which may need updating before resubmission. Do not advance skip into the payload for a retransmission; that would omit the headers. The existing length field may truncate a resend when required, with header fields and checksums describing the actual transmitted bytes. Retain the same registered backing and payload bytes, with no separate range API or finer-than-buffer retransmission machinery. Capture each submitted address/length and keep headers/backing stable while the kernel/NIC uses them; update them only after that use retires. Packet sizing must fit the selected path and peer limits before application construction. Detail packet-level ACK/loss bookkeeping and legal truncation in OQ-6, preserving this buffer/packet model. This author clarification supersedes the earlier suggestion to use skip for partial retransmission.

**Selected byte-range recovery policy: resend bracketing packet buffers.** TCP ACK/SACK processing remains byte-sequence based. When recovery identifies missing bytes whose boundaries lie inside previously sent packets, select the retained packet buffers covering the missing interval and resend those packets, including their headers. Duplicate bytes at the interval's edges are acceptable; do not split or copy payload merely to match the interval exactly. Acknowledge and retire data using byte-range bookkeeping while keeping retransmission at packet-buffer granularity. Preserve a buffer's backing while any of its bytes still require retention or any NIC operation references it; a partial acknowledgment is not permission to free the whole buffer. SACK bookkeeping must preserve the selected TCP recovery semantics, including retention needed if selectively acknowledged data is reneged.

This differs deliberately from current Zquic, which maps lost packet numbers to retained stream-frame ranges and can split/repacketize those ranges into newly encrypted packets. Do not carry that range-splitting architecture into Zutcp as a requirement. The chosen policy applies even if intermediary processing changes the receiver-visible segment boundaries; it does not depend on an intermediary retaining or retransmitting bytes on our behalf. Define the covering-buffer lookup, suppression of redundant recovery work, and byte-retirement accounting in OQ-6 without adding arbitrary sub-buffer transmission machinery.

Verify backing identity and unchanged payload addresses across layer preparation, submission, receive slicing, and retained retransmission. Test the geometry boundaries that could otherwise trigger generic helper copies. Verify that setup requests zero-copy and that our buffer layout, registration, submission, and configuration do not prevent it when supported. Kernel/driver-side copying is outside the implementation's acceptance policy; veth remains the required functional harness.

#### OQ-3.2 — Buffer reserves

| Option | Mechanism | Trade-off and failure condition |
| --- | --- | --- |
| **OQ-3.2.A — Separate Rx/Tx reserves** | Partition registered packet storage into independently sized Rx and Tx frame reserves. Rx owns RX/FILL; Tx owns TX/COMPLETION. Final releases from other shards enqueue returns to the appropriate owner. Begin with one XSK per configured device/queue binding, without sharing one FILL/COMPLETION pair among multiple packet workers. | Makes exhaustion and ownership easier to reason about but can strand capacity in one reserve while the other is exhausted. Test reserve sizing with retained input, full Tx rings, and delayed completions. It does not require separate UMEM registrations: the selected implementation must use a valid socket/UMEM arrangement. |
| **OQ-3.2.B — Shared reserve with guarantees** | Keep the same ring owners but allocate packet frames from shared storage with explicit per-direction minimum reserves and bounded returns. Permit frame movement between reserve owners only through an ownership handoff. | Allows capacity to follow traffic demand but adds coordination and quota bookkeeping. Fails if retained input or output exhausts the receive guarantee, or if a frame is offered to two rings at once. Shared storage does not permit concurrent access to a single SWSR ring. |

**OQ-3.2 resolved: Option A.** Partition registered packet storage into separately sized Rx and Tx frame reserves, using each allocator request's count and existing compile-time IOBuf geometry. Rx manages its receive supply and RX/FILL rings; Tx manages its transmit supply and TX/COMPLETION rings. Final releases from another shard return frames directly to their original reserve's contended intrusive free slist, using the selected ZmHeap non-sharded alloc_/free_ protocol. Do not implement a combined Rx/Tx reserve or dynamic capacity borrowing between them. Separate reserves can be logical partitions of the same registered UMEM; they do not require separate registrations. Concrete allocator declarations and the AF_XDP-compatible physical layout will be specified in Detailed Implementation.

**ZO-1 resolution — Separate control-packet storage.** Within the Tx allocation model, Zutcp owns a separate pool of small registered buffers for ACKs, window updates, establishment/close output, and other protocol control messages. Its reserve and intrusive free slist are independent of application-payload pools; any software ring used for that pool has the same explicit owner discipline. Application allocations cannot consume control-packet backing, and control generation does not depend on an application buffer becoming available or on piggybacking. This allocation separation does not require a separate hardware TX/COMPLETION ring: those rings retain their existing per-queue SWSR ownership.

Applications can request multiple allocators with different compile-time buffer geometries tuned to their messages, such as per-table replication buffers in Zdb. Honor the geometry supplied by each allocator factory request; do not impose one application payload size or combine every application allocator and transport control allocation into a single exhaustible free list. Each pool has fixed-capacity backing and its own recycling/accounting, using the existing Zi::IOBuf metadata and selected ZmHeap free-slist protocol. The control pool's small-buffer geometry includes the deterministically required framing and control content. Map these pool geometries to an AF_XDP-compatible registered layout in Detailed Implementation; logical buffer size does not by itself specify the physical UMEM chunk layout.

Tx owns control construction, scheduling, submission, and completion retirement. Reserve isolation protects control backing, but shared descriptor pressure must also permit pending control output to progress as completions arrive; application backlog must not monopolize every subsequent submission opportunity. Keep this bounded and owner-managed. All control backing follows the same in-flight stability and final-release rules as payload backing. Specify the control allocator's count/default, count-to-layout/ring-capacity mapping for each allocator request, descriptor arbitration, and reporting in Detailed Implementation, preserving the Rx/Tx reserve separation and avoiding payload copies or heap fallback.

Size and verify the reserves independently: retained receive slices can exhaust Rx even when Tx has free capacity, and delayed completions can exhaust Tx while Rx remains available. Report each condition separately and apply bounded backpressure. Preserve the descriptor-versus-payload and DMA-versus-transport lifetime rules below.

Verify application-pool exhaustion with held/unsent and unacknowledged payload buffers, including after DMA completion, while the working pair still exchanges ACKs, window updates and close traffic from the control pool. Cover differently sized application allocator requests, independent recycling/accounting, full descriptor rings and subsequent control progress, and teardown with both pool classes in flight. Apply this verification from the first intermediate protocol onward, using that slice's equivalent control messages.

Extra application sending threads dispatch to Tx. Additional NIC queues create additional explicitly owned bindings. Configuration must verify that traffic can actually reach the selected queue; a software XSK map is not a general substitute for NIC queue steering.

#### Proposed scheduler and waiting implementation

**OQ-3.6 resolved: follow mx's plugin integration with configurable `rxQuantum`.** ZiMxParams already supplies `rxQuantum(unsigned)` and its getter, and ZiMultiplex exposes `rxQuantum()`; use them directly. The current default is `ZiMxParams::RxQuantum`, which is 8, and mx startup rejects zero or values greater than INT_MAX. The former epollQuantum naming is historical: the current kernel epoll loop also uses rxQuantum as its maximum event count. For the plugin, it bounds packets read before returning to the scheduler's main loop. Count packets across all descriptor batches in that servicing turn; do not reset it for each batch. Returning when fewer packets are ready is allowed. Continuous readiness must not bypass the configured limit, including in low-latency mode. Ensure pending reception is serviced again through the existing mx scheduler pattern after returning. Do not add another rxQuantum parameter, default, or validation policy.

Bind packet servicing to mx's configured isolated Rx/Tx slots. ZiEventLoop remains the integration reference, but mx already owns the scheduler wake registration: startup installs `wakeFn(rxThread(), ...)` calling `wakeRx()` and queues the first `rx()` invocation. Mx's `rx()` delegates to `plugin->rx()`. Implement that hook rather than installing a second wake owner on mx's Rx slot. Its return permits other scheduler work; budget exhaustion must leave a subsequent servicing turn queued so pending packets are not stranded.

Implement the plugin's wake notification inside its own receive wait/spin mechanism. Mx's `wakeRx()` queues a new mx `rx()` callback while running and calls `wake()`; mx's `wake()` delegates to `plugin->wake()`. That non-Rx-thread hook must interrupt the current plugin `rx()` and make it return, including while blocked, spinning with `ll(true)`, or processing continuous traffic. Preserve notifications across the check-to-wait boundary and repeated wakes so queued callbacks remain serviceable. No new scheduler-wake composition interface is needed for this plugin integration.

In ordinary mode, spin for the configured count, recheck readiness and pending wake state, and wait on AF_XDP readiness together with the scheduler wake source. Use a zero timeout to mean no periodic wait timeout, consistently with ZmRing; scheduler/timer wakeups must still interrupt it. Handle AF_XDP need-wakeup separately from scheduler wakeups, invoking the required driver progress operations rather than assuming a ring spin drives NAPI.

Use each allocator request's IOBuf geometry and count as the pool-sizing inputs; the count is supplied through mx's existing allocator factory rather than through a new backend configuration interface. Specify how those inputs determine valid power-of-two descriptor-ring capacities, including the factory default and handling of zero counts, before implementation. Report effective capacities and diagnose invalid or overflowing requests without adding a conflicting payload-size convention. In the inspected ZmRing implementation, `cpuset` controls memory binding through hwloc; preserve that meaning for packet storage rather than silently treating it as a thread-affinity setting. Configure worker affinity through mx scheduler thread parameters separately.

**OQ-3.7 resolved: request zero-copy and do not obstruct it.** Zxdp and Zutcp must request zero-copy through the kernel interface and preserve backing, registration, descriptors, and configuration that permit it. If the NIC driver supports zero-copy for that setup, our implementation must not prevent it. Whether kernel-side zero-copy actually occurs is the kernel/NIC driver's responsibility and outside this work's scope. Do not impose a separate capability-enforcement, verification gate, or initialization-failure policy merely because hardware zero-copy is unavailable, and do not force copy mode for the veth harness. The earlier requirement to force XDP_ZEROCOPY and fail if unavailable is withdrawn; exact API setup must follow this request-without-obstruction contract rather than treating the flag spelling as an independently accepted policy. Keep the required veth milestone and physical-host measurements. Ring-capacity formulas, zero/default handling, and device/queue configuration are implementation details derived from the accepted contracts, not unresolved architectural alternatives.

The detailed ring comparison and independent test requirements below apply to the selected direct-ABI and separate-reserve architecture. Acceptance requires packet progress, bounded ownership, scheduler/timer progress, and shutdown in both low-latency and ordinary waiting modes before Zutcp integration.

#### Remaining OQ-3 items

These IDs are permanent, like the top-level question IDs. They identify unresolved details without reopening the selected direct AF_XDP wrapper, separate Rx/Tx reserves, zero-copy payload objective, Z framework layering, or scheduler integration model.

| Stable ID | What remains to specify | Existing constraint |
| --- | --- | --- |
| **OQ-3.3 — BPF program lifecycle (resolved: B)** | Direct BPF syscalls for classifier/map setup; implementation planning supplies instruction packaging, attachment, ownership checks, and partial-failure cleanup. | No libbpf loader or required external provisioning. Preserve OQ-2's classifier/exclusion behavior and unrelated interface users. |
| **OQ-3.4 — Registered backing and buffer adapter (resolved)** | Reuse Zi::IOBuf and IOBufPluginAllocator over fixed UMEM backing supplied by IOBufPlugin; no builtin payload or duplicate wrapper. Initial allocation/release uses initial=true; growth uses false and is fatal in the UMEM provider. Rx never grows. | Inherit compile-time geometry, deterministic budgets, and per-allocator counts. The allocator owns reserve accounting. Use data_()/initial() and preserve backing lifetime; no heap growth, replacement, or payload-copy fallback. |
| **OQ-3.5 — Submission contract (resolved by existing ZiTxStream use)** | Reuse ZiTxStream's allocBuf_, sendBuf_, flush, retained-buffer adoption, fixed-capacity layering, and failure contracts. Map those existing operations to the Zxdp backend; concrete declarations and completion retirement are implementation work. | No independent application submission API or alternate framing-space policy. Keep submitted backing stable through NIC ownership. TCP segmentation/retransmission mechanics belong under OQ-6, not an additional Zxdp API decision. |
| **OQ-3.6 — Scheduler integration (resolved)** | Use mx's existing rx/wake dispatch and wakeRx re-queueing. Existing mx.rxQuantum() bounds packets per turn; inherit its default 8 and startup validation. | Preserve ZmRing wait controls and teardown. Apply the quantum across batches, including low-latency mode. Do not replace mx's scheduler wake registration. Driver wakeups remain separate. |
| **OQ-3.7 — Capacity and capability configuration (resolved)** | Request kernel zero-copy and preserve an unobstructed path when the driver supports it. Derive descriptor capacities/default handling and concrete bindings in Detailed Implementation. | Kernel/driver-side copying and capability selection are out of scope; no additional hardware-capability enforcement/failure policy. Reuse IOBuf geometry and per-allocator counts, and retain the veth milestone. |

OQ-3.5 follows from the already selected dependent use of ZiTxStream; it is not another architecture gate. Its send result must retain the existing local admission/failure meaning rather than being redefined as a remote TCP acknowledgment. AF_XDP completion retirement remains internal backend ownership work. OQ-3.6 follows ZiEventLoop integration plus rxQuantum. OQ-3.7 requires requesting zero-copy without obstructing it and leaves kernel-side behavior to the kernel/driver. Enumerate and verify concrete backend hooks, capacity mappings, wakeups, and re-entry in Detailed Implementation. OQ-3 is architecturally resolved.

### Zxdp ring buffers and ZmRing comparison

This subsection records the ring-buffer requirements requested by the author. OQ-3.1.B selects direct AF_XDP access and OQ-3.2.A selects separate Rx/Tx reserves; the complete transport architecture is still being drafted. Give [Yasukata's AF_XDP backend](https://github.com/yasukata/iip-af_xdp/blob/master/main.c) primary weight as an implementation precedent for ring servicing, references to packet buffers, completion handling, and driver wakeups. Adapt that precedent to the repository's separate Rx/Tx ownership and scheduler rules.

#### Concurrency and entry representation

Every AF_XDP ring is single-writer/single-reader (SWSR), also called single-producer/single-consumer (SPSC). This applies independently to RX, TX, FILL, and COMPLETION. Sharing packet storage does not change their concurrency contract. [AF_XDP ring specification](https://docs.kernel.org/networking/af_xdp.html#rings).

| Property | ZmRing | AF_XDP rings |
| --- | --- | --- |
| Purpose | Application communication inside a process | Transfer packet-buffer ownership between userspace and the kernel |
| Concurrency | SWSR, MWSR, SWMR, or MWMR | SWSR for every individual ring |
| Entry storage | Fixed- or variable-sized application messages stored in the ring | Fixed-size descriptors referring to separate packet storage in UMEM |
| Capacity unit | Bytes of ring storage | Descriptor entries; ring entry counts must be powers of two |
| Multiple readers | Broadcast to attached readers | One consumer, with no broadcast facility |
| Publication | Reserve/write with `push()`, publish with `push2()` | Reserve slots, populate descriptors, submit |
| Consumption | `shift()`, process, `shift2()` | Peek descriptors, process, release |
| Waiting and termination | Ring spin/block controls, wakeups, and EOF | External waiting and driver wakeups; application-defined stop/drain lifecycle |

ZmRing's multiple-reader behavior is broadcast, not competing workers dividing messages. Its source explicitly recommends sharding writes to multiple MWSR rings for unicast. See [ZmRing.hh](./zm/src/ZmRing.hh). AF_XDP has no native MWSR or multiple-reader mode. A normalized Zxdp API must not imply otherwise.

The ring layout is part of the kernel ABI. Preserve its required publication ordering and barriers, whether using [libxdp's ring helpers](https://github.com/xdp-project/xdp-tools/blob/main/headers/xdp/xsk.h) or a separately selected direct implementation. Do not substitute ZmRing's control block, message headers, or cursor algorithms for the mapped AF_XDP layout.

#### Four rings and their owners

| Ring | Producer | Consumer | Entry contents and meaning | Proposed Zxdp userspace owner |
| --- | --- | --- | --- | --- |
| FILL | Userspace | Kernel | UMEM frame address offered for future reception | Rx shard |
| RX | Kernel | Userspace | Received-packet descriptor with frame address, length, and options | Rx shard |
| TX | Userspace | Kernel | Descriptor identifying packet bytes to transmit | Tx shard |
| COMPLETION | Kernel | Userspace | Address of a frame returned after kernel processing | Tx shard |

Separate Rx and Tx threads can operate these distinct rings without making any ring multi-writer or multi-reader. Multiple application producers dispatch work to the owning Tx shard through framework queues; they do not directly publish to the same AF_XDP TX ring. Returned receive frames similarly reach the owning Rx shard through an explicit recycling path, rather than arbitrary threads writing FILL.

This mapping follows the existing shard rules and selected separate-reserve model. The contended intrusive allocator slist is selected for recycling; only concrete declarations remain to be detailed. Its atomic free operations can run across shards, while each AF_XDP ring remains owner-only. The combined-reserve alternative in [design options](./design_options.md#3-af_xdp-rings-ownership-waiting-and-scheduler-integration) is not selected.

#### Descriptor lifetime versus packet lifetime

Advancing a consumer index releases descriptor slots. It does not necessarily release the packet bytes those descriptors reference.

For reception, copy the small descriptor metadata into the retained buffer representation as necessary, then release the RX slot. Keep its UMEM frame unavailable to FILL while reassembly, the stream parser, or any retained application slice still references those bytes. Final payload release makes the frame eligible for recycling. Refilling FILL early permits incoming packets to overwrite retained data.

For transmission, keep submitted packet backing stable until the kernel returns the frame through COMPLETION. A completion returns ownership; it does not independently prove successful transmission and is not a peer acknowledgment. Transport-retained byte ranges remain live until acknowledgment, cancellation, or connection teardown releases them. If the transport and DMA use the same backing, satisfy both lifetimes before reuse. If they use different backing, retiring the packet frame must not release the separate retransmission source.

Never offer the same frame concurrently for reception and transmission, or to multiple FILL rings. Track the frame's ownership explicitly and keep all pools and pending-return paths bounded. An empty FILL ring, full TX ring, stalled completion path, or application retaining input must have defined backpressure and diagnostics rather than unbounded allocation. [AF_XDP ownership and corruption guidance](https://docs.kernel.org/networking/af_xdp.html).

#### Shared UMEM and sharding

Each AF_XDP socket retains its own RX and TX rings. Sockets sharing UMEM on the same device/queue tuple share a FILL/COMPLETION pair. Sharing UMEM across different device/queue tuples requires a pair per unique tuple. Every shared pair still needs one userspace owner for each ring. Merely creating another socket or process does not authorize concurrent access to that pair. [Shared UMEM specification](https://docs.kernel.org/networking/af_xdp.html#xdp-shared-umem-bind-flag).

Shared UMEM also does not decide which socket receives a packet. That follows the XDP program and device/queue binding. Receive scaling therefore needs an explicit mapping among NIC queues, XSK map entries, sockets, and owning shards. Frame allocation must coordinate storage ownership even where the descriptor rings are separate.

#### Familiar configuration with backend-specific mechanics

Expose the configuration model in [ZmRing_::Params](./zm/src/ZmRing.hh): `size`, `ll`, `cpuset`, `spin`, and `timeout`. Its current defaults are size 0, `ll(false)`, an empty CPU set, 1000 spins, and a 1 millisecond timeout. These are reference defaults, not a selection of AF_XDP capacities or placement policy.

* `size`: distinguish ZmRing byte capacity from AF_XDP descriptor counts and UMEM byte capacity. Define the normalized unit and its mapping before fixing the public API; never silently treat bytes as descriptor entries. RX, TX, FILL, COMPLETION, and packet storage have distinct capacities even if one convenience setting supplies their defaults.
* `ll`: spin on ring readiness in low-latency mode while still servicing scheduler wake requests and shutdown. This configures userspace waiting; it does not guarantee kernel/driver receive progress.
* `spin`: in ordinary mode, bound readiness checks before entering the backend's wait path, following the ZmRing control model.
* `timeout`: preserve millisecond units and document zero/infinite-wait behavior. A wait must remain interruptible by scheduler work and shutdown; it cannot delay transport timers beyond their deadlines.
* `cpuset`: retain the familiar placement control, with its relationship to scheduler thread affinity, UMEM placement, and NIC queue ownership specified explicitly. It is not a native AF_XDP ring flag.

Use [ZiMultiplex.cc](./zi/src/ZiMultiplex.cc)'s plugin rx/wake dispatch and existing scheduler wake registration directly, with [ZiEventLoop.cc](./zi/src/ZiEventLoop.cc) as the integration precedent. Use [ZiLog.cc](./zi/src/ZiLog.cc) for consume/process/retire and draining; its worker is a dedicated ZmThread. AF_XDP has no ZmRing EOF marker, so implement stopping as explicit owner state, removal of further ingress, wakeup of any waiter, and asynchronous draining consistent with [IO.md](./docs/IO.md).

Process bounded packet batches and yield through the scheduler's continuation mechanism to allow queued work and due timer callbacks to execute. Check wake requests under continuous traffic and while spinning on an empty ring. The blocking path needs a race-safe readiness recheck and a wake source integrated with its wait, so enqueued work cannot be stranded between checking readiness and sleeping.

Distinguish userspace spinning from NAPI busy polling and `XDP_USE_NEED_WAKEUP`. With need-wakeup enabled, check the relevant producer-ring flags and invoke the required kernel wakeup operation when needed. Spinning alone can otherwise leave reception or transmission stalled. A syscall-free steady state is a performance aim, not a guarantee of AF_XDP operation. [Driver wakeup contract](https://docs.kernel.org/networking/af_xdp.html#xdp-use-need-wakeup-bind-flag).

#### Independent Zxdp verification

Before integrating Zutcp, exercise these ownership and progress requirements in standalone Zxdp integration tests:

1. Bidirectional packet exchange across veth, including ring wraparound and batches crossing the wrap boundary.
2. Hold received payloads after releasing their RX descriptors; send more traffic and verify held bytes are unchanged until their owners release them.
3. Return held buffers from other shards through the intrusive slist while allocating concurrently; verify unique frame ownership, correct original-reserve return, no corruption or loss, and that only Rx republishes receive frames to FILL. Exercise empty-list, head contention, and repeated reuse cases without adding a heap fallback.
4. Exhaust receive supply, fill transmit capacity, and delay completion consumption; verify bounded memory, defined backpressure, and recovery after resources return.
5. Exercise ordinary spin/wait mode and low-latency mode, including driver wakeup requests. Verify scheduler work and timers progress under idle polling and sustained traffic. Configure a small rxQuantum and verify that the total packets processed across batches never exceeds it before scheduler re-entry, while pending reception continues afterwards.
6. Stop while idle, while waiting, and with packets or retained slices outstanding. Verify ordered draining and no access after UMEM release.
7. If shared UMEM is selected, exercise its FILL/COMPLETION ownership and frame accounting across the supported socket/queue combinations.

8. Verify that UMEM-backed IOBufs have no builtin payload storage, normal jumbo Tx output rolls over within fixed capacity, received packet backing never grows, and attempted Tx growth beyond initial capacity terminates as a fatal internal error in both release and debug configurations. Keep fatal-path checks isolated from the integration-test driver.

Use [Yasukata's backend](https://github.com/yasukata/iip-af_xdp/blob/master/main.c) as the primary implementation reference and the [Linux veth selftests](https://github.com/torvalds/linux/blob/v6.12/tools/testing/selftests/bpf/test_xsk.sh) as supporting test precedent. Record copy versus zero-copy mode explicitly; veth functionality is not evidence of hardware zero-copy performance. Exact test filenames and commands will be assigned in Detailed Implementation.

### OQ-4: Peer extension profile

Implementation scope remains SACK, timestamps/PAWS, window scaling, DSACK, RACK-TLP, ECN, and TCP Fast Open. The author selected **OQ-4.B**: none of these peer extensions is mandatory for connection establishment. The table retains OQ-4.A as a considered, unselected alternative.

| Mechanism | OQ-4.A: require SACK and timestamps | OQ-4.B: optional peer extensions |
| --- | --- | --- |
| SACK | Advertise permission and require it from the peer; refuse unsupported connections with a specific local diagnostic. | Advertise permission; use SACK when available and cumulative-ACK recovery otherwise. |
| Timestamps/PAWS | Offer timestamps and require negotiation; use PAWS and timestamp RTT handling. | Offer timestamps; if absent, use timestamp-free RTT/retransmission rules and do not assume PAWS protection. |
| Window scaling | Offer and use when negotiated. An absent option means scale zero, not connection refusal. | Same. |
| DSACK | Generate/process duplicate reports under SACK rules; do not require the peer to generate them. | Same when SACK is enabled. |
| RACK-TLP | Local recovery strategy, enabled on every accepted connection because SACK is required. Keep timeout/persist behavior needed for progress. | Use with SACK; implement separate loss recovery for connections without SACK. |
| ECN | Offer/negotiate support and use only when negotiated. Absence does not reject the connection. | Same. |
| TCP Fast Open | Support cookie/data exchange and use when available and requested by the application. Retain ordinary establishment if absent or declined. | Same. |

OQ-4.A eliminates the no-SACK recovery path and makes timestamp protection available on every accepted connection. It narrows interoperability to peers permitting those features; a configured modern peer with either feature disabled cannot connect. This is a supported profile decision, not input rejection solely for syntactic conformance.

OQ-4.B admits peers without those extensions, with additional recovery, RTT ambiguity, and sequence-lifetime paths to implement and verify. It fails operationally if those fallback paths are left untested or if the implementation silently assumes SACK or PAWS despite their absence. Timestamp-free high-rate sequence reuse must receive an explicit treatment if this profile is selected.

RACK-TLP is not separately negotiated with the receiver. It requires SACK and local per-transmission timing, but does not require the wire TCP Timestamp option. DSACK is not a separately negotiated capability bit either. [RFC 8985](https://www.rfc-editor.org/rfc/rfc8985.html#section-4). Window scaling and timestamps have their own handshake rules. [RFC 7323](https://www.rfc-editor.org/rfc/rfc7323.html). TCP permits basic interoperability without these common options, so requiring them must be documented as this implementation's selected peer profile. [RFC 9293](https://www.rfc-editor.org/rfc/rfc9293.html#section-3.2.1).

In both profiles, parse options with bounds sufficient for integrity and safe operation, accept supported encodings without conformance-only policing, and skip unsupported optional options where their wire format permits it. Do not confuse offering an option with successful negotiation. Preserve packet-buffer retransmission and bracketing recovery; selecting a peer profile does not reopen those decisions. Peer extension negotiation starts at the TCP conversion stages, not the first intermediate-protocol milestone.

**OQ-4 resolved: Option B.** Accept absent SACK/timestamps without an extension-based connection refusal. Use SACK and RACK-TLP when available; supply cumulative-ACK loss recovery when SACK is absent. Use timestamp RTT handling and PAWS only when negotiated, with timestamp-free RTT/sequence-lifetime handling otherwise. Window scaling defaults to scale zero; DSACK processing/generation follows SACK availability, and ECN/Fast Open absence uses ordinary operation. Detailed Implementation must specify and verify both negotiated and extension-absent paths, including timeout/persist behavior and Fast Open fallback, without silently assuming an optional feature was enabled.

### OQ-5: Integrate through ZiMxPlugin

**Selected integration boundary.** Zutcp implements `ZiMxPlugin` as declared in [ZiMultiplex.hh](./zi/src/ZiMultiplex.hh) and dispatched by [ZiMultiplex.cc](./zi/src/ZiMultiplex.cc). Do not modify ztcp or ztls or introduce the earlier Hub::utcp boolean and dual-adapter design. Register the plugin on mx while stopped, then create dependent allocators through mx's factory, all before start and I/O use. Registering after allocator creation would leave those existing allocators on the no-plugin builtin path. Keep the plugin fixed while its factory-created allocators or I/O depend on it. Preserve the upper-layer client/server, TLS, stream, allocation, and connection callback contracts.

The inspected interface supplies the responsibilities that previously appeared open:

* `ZiMxPlugin::allocator(Zi::IOBufType::T type, unsigned size, unsigned maxSize, unsigned align, unsigned count)` returns `Zi::IOBufPlugin *`, a backing provider rather than an IOBufAllocator. Handle the network roles RxUDP, TxUDP, RxTCP, TxTCP, RxTLS, and TxTLS, using each request's geometry and desired buffer count, separate reserves, and intrusive recycling. There is no headRoom argument in this hook; keep framing headroom/tailroom in the existing layer geometry. Use the plugin's `mx()` accessor for its existing scheduler/configuration context.
* `mx->allocator<Type, Base, Size, MaxSize, Align, HeapID>(count)` is the current stopped-state allocator factory; count defaults to ZiIOBuf_DefltCount. With an installed plugin it creates `Zi::IOBufPluginAllocator<Type, Size, Base>` from `plugin->allocator(Type, Size, MaxSize, Align, count)`; without one it creates `Zi::IOBufBuiltinAllocator<Type, Base, Size, MaxSize, Align, HeapID>`. Mx retains the returned allocator in its intrusive list. Reuse this factory rather than adding the former allocator(a) registration/binding API or constructing a separate adapter. The size/maxSize/align request specifies geometry and count specifies the desired number of pool buffers. The no-plugin builtin path retains its current behavior and does not use count to create a UMEM pool. None of these arguments authorizes growth of fixed UMEM frames.
* `start()` and `stop()` are idempotent plugin hooks with completion through `mx()->pluginStarted(StartResult)` and `mx()->pluginStopped(StopResult)`, where each result is `ZuUnion<void, ZeException>`. Mx starts its scheduler before invoking plugin start and waits for completion on the scheduler control thread. During shutdown mx performs its connection/listener stop sequence, invokes plugin stop, and waits for its completion before stopping the scheduler and final cleanup. Respect the interface's Rx-owned runtime work through the existing dispatchers and perform three-phase teardown asynchronously; do not block an I/O shard or signal pluginStopped before backend work/backing use retires.
* `listen()` and `stopListening()` use existing local addresses, ports, callbacks, receive allocator, and options. `connect()` and `udp()` additionally receive `ZiIPType::T ipType`, local/remote addresses and ports. Callback types remain ZiListenFn, ZiFailFn, and ZiConnectFn; the latter returns the application's ZiConnection from ZiCxnInfo. Keep the passed receive allocator and options rather than inventing an upper-layer transport API.
* `send(ZiConnection *)` consumes the connection's Tx context on Tx. `ZiConnection::send()` dispatches to this plugin hook when installed. Use `ZiConnection::txContext()` only on Tx and `rxContext()` only on Rx; preserve callback, local-send-result, and buffer-retirement semantics. A NIC completion must not become a peer acknowledgment.
* `rx()` runs on mx's Rx thread until woken or `mx()->rxQuantum()` packets have been processed; `wake()` is callable from a non-Rx thread and causes rx to return. Mx already installs its scheduler wakeFn, dispatches rx/wake to the plugin, and re-queues servicing through wakeRx. Use these hooks and the existing default/validation rather than adding another scheduler, wake owner, or quantum setting.
* Implement the inherited `Ztc::Mx` connection/queue enumeration, watch/unwatch, and telemetry obligations. With a plugin, mx's allCxns/watch/unwatch delegate to it; allQueues adds the plugin queues to the scheduler queues. Do not report the same connection or scheduler queue twice.

Keep provider and buffer lifetimes consistent with the factory: the adapter stores a raw IOBufPlugin pointer, mx holds the plugin reference, and allocator objects survive stop until mx destroys its allocator list. Complete all buffer callbacks before destroying backing providers or unmapping UMEM. Registration, allocator factory calls, and startup are serialized by the application while stopped. The existing plugin setter and allocator factory assert that stopped-state precondition; neither retrofits already created allocators.

#### Required Ztc::Mx telemetry and observability implementation

ZiMxPlugin inherits [Ztc::Mx](./zi/src/ZtcMx.hh). Implement all six inherited virtual functions in addition to the main transport functions; satisfying listen/connect/send/rx alone is insufficient:

| Required member | Implementation obligation |
| --- | --- |
| `TelKey telKey() const` | Supply the telemetry key for the plugin's mx view, consistent with its owning mx and the ownership keys used for its connections/queues. |
| `void telemetry(Ztc::MxTelemetry &data) const` | Populate the existing mux telemetry model from mx's scheduler, configuration, and lifecycle context. Do not invent another schema or omit this method because packet I/O bypasses kernel sockets. |
| `unsigned allCxns(AllCxnsFn fn) const` | Enumerate the plugin's live `Ztc::Connection` objects and return the enumeration count. Mx delegates this view to the plugin when installed. |
| `unsigned allQueues(Ztc::QueueMgr::AllFn fn) const` | Enumerate the backend's observable `Ztc::Queue` objects with consistent ownership and capacity/occupancy units. Mx adds its scheduler queues separately, so the plugin must not enumerate those again. |
| `void watch(AddCxnFn add, DelCxnFn del)` | Retain and deliver connection-add/remove notifications at the corresponding registry publication/removal points. Mx delegates subscription to the plugin. |
| `void unwatch()` | Remove the subscription and honor its lifetime during teardown; callbacks must not reach a destroyed subscriber. |

Each enumerated connection also supplies the `Ztc::Connection` key and `telemetry(CxnTelemetry &)`, and each enumerated queue supplies the `Ztc::Queue` key and `telemetry(QueueTelemetry &)`. Reuse the vocabulary in [ZtcMx.hh](./zi/src/ZtcMx.hh) and [ZtcQueue.hh](./zm/src/ZtcQueue.hh). Connection snapshots must describe the user-mode transport's endpoints, traffic, and buffering; kernel socket queries are not a substitute for backend state. Keep keys and objects valid during enumeration/watch callbacks using the framework's established lifetime and observation conventions, without moving mutable protocol work onto an observing thread.

Add integration verification for inherited-interface completeness, mux/connection/queue snapshots, enumeration counts and uniqueness, watch add/remove delivery, unwatch, and shutdown with an observer attached. Verify allocation plumbing through mx's actual factory as well: registration precedes factory creation, explicit and default counts reach the correct backing providers unchanged, independently sized/count-configured pools preserve their requested capacities, initial plugin allocation/free flags are preserved, the UMEM provider rejects growth fatally, and final callbacks precede provider destruction. Existing generic plugin-backing tests in [ZiIOBufTest.cc](./zi/test/ZiIOBufTest.cc) exercise both initial and growth callbacks; add backend-specific coverage rather than changing their generic growth expectations.

The native Zutcp link/sole-stream model remains the copied transport's implementation structure; the plugin maps it beneath ZiConnection. Preserve the agreed early-data ordering for the native interface and existing callback expectations of upper layers. This integration change does not put encryption back into Zutcp: unchanged Ztls supplies TLS above the selected mx transport.

The first deliverable remains independently tested Zxdp followed by the copied/renamed/stripped Zutcp client/server over veth, with no TCP-specific modifications. Add the ZiMxPlugin implementation in the staged conversion/integration plan without making kernel TCP interoperability a prerequisite for that first milestone. Validate the eventual plugin through existing Ztcp/Ztls applications and external integration fixtures without changing those modules.

### OQ-6: Vertically sliced protocol conversion

**Selected phasing principle.** Each Zutcp phase builds on the previous phase and ends with a working client/server exchanging application data over Zxdp using that phase's protocol. The intermediate protocol evolves toward TCP; the final slice establishes TCP interoperability. A completed codec, recovery engine, or plugin in isolation does not complete a phase. Each phase includes the necessary endpoint integration, ownership, diagnostics, and end-to-end verification.

**Invariant in every slice:** Preserve the protocol Rx/Tx roles and the copied association of functionality and thread-exclusive data described above, with independent protocol-thread configuration in phase 1 and direct mx I/O-thread execution from phase 2 onward. A slice is incomplete if its client/server works by accessing another shard's state directly or moving sender recovery onto Rx. Extend the ownership/handoff map alongside each port, and exercise it in that slice's end-to-end tests.

Most transport work is a port or adaptation of mechanisms already present in Zquic. Identify the retained implementation first, then change its semantics and signaling as needed for TCP. ACK ranges, stream offsets/reassembly, flow control, RTT/loss timers, congestion control/pacing, ECN, and early-data machinery are starting points, not missing facilities to recreate. Their QUIC wire formats and rules are not automatically TCP-compatible. TCP-specific work includes, for example, PAWS, the window-scale option, DSACK semantics, and Fast Open cookies. Early-data behavior retained after TLS deletion must be assessed separately from the original TLS-dependent machinery.

The previous OQ-6.A/B ordering alternatives are superseded by this vertical-slicing direction; they are retained here only as retired decision IDs. The author adopted the eight slices below, including the phase-1/phase-2 thread-binding transition. They are not implementation permission gates. Keep each completed revision reproducible, without maintaining previous intermediate protocols as parallel production modes. Expand the adopted phase boundaries into the exhaustive Detailed Implementation file list before implementation.

#### Prerequisites — Baseline, independent Zxdp, then mechanical copy

Record the source revision, inventory Zquic's files/build inputs/dependencies, run the existing endpoint baseline, and identify reusable tests and callbacks. Identify any copied QIR references/build dependencies to exclude; preserve the existing Zhttp QIR implementation and tests.

Build and independently test Zxdp first: direct AF_XDP and BPF interfaces, UMEM-backed Zi::IOBuf allocation, separate reserves, intrusive recycling, owner-only rings, scheduler integration, low-latency waiting, rxQuantum, selected endpoint steering/coexistence, and asynchronous teardown. Raw packet exchange over veth establishes this prerequisite without a TCP dependency. Verify ring wrap/exhaustion, wakeup races, cross-shard releases, outstanding NIC ownership, fatal capacity-growth rejection, and partial-initialization cleanup. Request zero-copy without obstructing driver-supported operation; actual driver capability remains out of scope.

Then copy Zquic verbatim into Zutcp and globally rename names/identifiers/filenames with case preserved, including zquic → zutcp, Zquic → Zutcp, and ZQUIC → ZUTCP. Check the copy against the recorded source before feature deletion. Register the module in the build and verify the renamed baseline. These preparatory checkpoints are prerequisites to the first vertical slice, not substitutes for a working Zutcp deliverable.

#### Phase 1 — Copied-and-stripped protocol over Zxdp

**Reuse and change:** Retain link/stream dispatch, connection-ID routing, packet numbers, ACK ranges, recovery, congestion control, and applicable integrity checks from the renamed copy. Exclude copied QIR references; delete encryption/protection, TLS/CRYPTO handshake machinery, migration, and multiple-stream behavior. Preserve the separate QIR implementation/tests in Zhttp. Implement OQ-1.A's plaintext transport parameters and Initial/response/ACK establishment. Carry the intermediate protocol through Zxdp and the selected endpoint classifier. Connect the sole-stream application path to fixed UMEM backing using the agreed headroom/tailroom layering, without redundant plaintext-to-wire staging after encryption is removed.

Provide Zutcp's separate small control-buffer pool from this slice, alongside geometry-specific application payload pools. Exercise control progress while application allocations are exhausted, using the intermediate protocol's ACK/setup/close equivalents. Preserve this invariant through every subsequent port.

**Working result:** Native client and server exchange bidirectional application bytes over veth/Zxdp, with one stream per connection. Retain the copied HubParams rxThread/txThread settings: protocol Rx/Tx threads are independently configured and typically differ from mx I/O Rx/Tx threads, although they may be the same. Preserve the existing owner-directed packet/protocol handoffs. This is the first Zutcp deliverable and contains no TCP-specific protocol changes.

**Exit evidence:** Exercise lost/duplicate setup packets, once-only accept/stream/connected callbacks, data loss/reordering, retained-buffer lifetime and NIC retirement, and graceful shutdown. Verify both distinct and coincident protocol/mx I/O thread configurations, including correct dispatch, timer ownership, and retained-buffer handoffs. Confirm no TLS/CRYPTO exchange or packet encryption remains. Establish the milestone on veth, then repeat the basic pair on two hosts without making hardware availability a prerequisite to the veth deliverable.

#### Phase 2 — The same working protocol through ZiMxPlugin

**Reuse and change:** Map the native link and sole stream beneath ZiConnection through the current ZiMxPlugin interface. Reuse its allocator factory, lifecycle, listen/connect/udp, send, rx/wake, scheduler dispatch, and configuration contracts. Implement all six Ztc::Mx virtuals, connection/queue telemetry, enumeration, and watch/unwatch as specified in OQ-5. Keep the phase-1 protocol and recovery behavior while replacing the application integration path. Move protocol Rx/Tx work directly onto mx I/O Rx/Tx threads and remove the separate Zutcp HubParams rxThread/txThread parameters, storage, accessors, and initialization logic. Configure execution through ZiMxParams rxThread/txThread instead, including for the native API. Elide obsolete packet-I/O-to-protocol handoffs once the corresponding owners coincide; retain Rx-to-Tx and Tx-to-Rx handoffs where required by exclusive state ownership.

Verify multiple mx allocator-factory requests with different application geometries and counts, including explicit count forwarding and the current default, plus the separate transport control pool. Preserve their independent capacities and recycling through plugin lifecycle, telemetry, and shutdown.

**Working result:** Client/server fixtures using existing Ztcp and Ztls APIs communicate through the intermediate Zutcp protocol, with neither module modified. Unchanged Ztls can supply TLS above this transport even though the intermediate transport itself has no encryption. Keep the native pair working too; both use the same transport implementation.

**Exit evidence:** Run both application paths over veth. Verify protocol processing, callbacks, and timers execute on the configured mx I/O owners; ensure no stale protocol-thread settings or dispatch targets remain. Verify registration before allocator creation/start, Tx/Rx affinity, local-send-result semantics, buffer/provider lifetimes, asynchronous start/stop, telemetry counts/keys, watch/unwatch, and shutdown with an observer attached. The equivalent fixture without a plugin continues to use kernel transport. Carry these integration checks forward through every later protocol slice.

#### Phase 3 — Port acknowledgment and retransmission to byte sequence space

**Reuse and change:** Adapt existing stream offsets/reassembly, ACK-range tracking, retained sends, RTT estimation, and loss recovery to TCP-style byte sequence ownership and acknowledgments. Carry the changed fields in the intermediate envelope while keeping the working bootstrap. Replace packet-number-based delivery evidence where it no longer applies; TCP retransmissions retain byte sequence identity rather than acquiring a new identity for the same bytes. Preserve the useful recovery/diagnostic structure instead of introducing a second transport engine.

Keep buffer:packet at 1:1 and acknowledgment bookkeeping separate from backing granularity. Retransmit retained covering packets for unaligned missing ranges, with skip=0 and mutable headers included. Retain partially acknowledged backing, tolerate duplicate edge bytes, and update headers only after preceding NIC use retires. Port cumulative-ACK loss recovery and RTO behavior so operation does not depend on SACK or timestamps. Selective range behavior can remain in the intermediate protocol pending the next slice's TCP adaptation.

**Working result:** The same client/server pair delivers ordered bytes under byte-based acknowledgments and packet-granularity retransmission over the intermediate wire format.

**Exit evidence:** Inject loss, duplicates, reordering, partial ACKs and ACK loss. Verify exact application delivery, retained-buffer bounds, sequence accounting, RTT behavior, and independent NIC/peer retirement. The unchanged application/telemetry fixtures from phase 2 continue to work.

#### Phase 4 — Port flow control and connection lifecycle

**Reuse and change:** Adapt existing receive limits/backpressure to TCP's advertised byte window, including reopening and zero-window persist. Port stream completion/reset and connection lifetime handling to TCP FIN/half-close/reset semantics, including their sequence-space effects. Adapt establishment toward SYN/SYN-ACK/ACK semantics within the intermediate envelope and remove dependence on connection IDs where endpoint-based demultiplexing replaces them. Keep each transition wired through both endpoints and the existing application callbacks.

**Working result:** The pair connects, transfers bidirectionally, pauses/resumes for receiver pressure, half-closes, resets, and reconnects using TCP-oriented connection semantics carried by the intermediate protocol.

**Exit evidence:** Test zero windows/reopening, lost setup and close messages, simultaneous activity, reset, repeated endpoint reuse, outstanding frames during close, and safe sequence lifetime handling. Preserve once-only notifications and bounded, asynchronous teardown. Retain working recovery from phase 3.

#### Phase 5 — Port selective acknowledgment and RTT handling; implement TCP option semantics

**Reuse and change:** Port existing ACK-range and receive-reassembly logic to TCP SACK block semantics and sender bookkeeping; implement DSACK duplicate reporting and SACK-reneging retention. Adapt existing receive-window accounting to negotiated window scaling. Reuse RTT sampling/timing infrastructure while implementing TCP timestamp exchange and PAWS. Represent the necessary negotiation/fields in the evolving intermediate envelope until final wire conversion. Existing internal send timestamps are not the TCP timestamp option or PAWS.

**Working result:** The same pair transfers and recovers with SACK, DSACK, scaled windows, and timestamps/PAWS semantics, each enabled or absent according to OQ-4.B.

**Exit evidence:** Run enabled/absent combinations through the actual endpoints, including SACK holes/reneging, duplicate reports, scaled/unscaled window limits, timestamp wrap/old segments, and cumulative-ACK/timestamp-free fallback. Verify these paths with application traffic rather than only option-codec tests.

#### Phase 6 — Port loss probing and congestion signaling

**Reuse and change:** Adapt the existing time-based loss detection, send history, RTT timers, probing, congestion control, and pacing to RACK-TLP. QUIC PTO is a starting mechanism to adapt, not already TCP TLP. Keep RTO and persist. Port existing ECN marking, accounting, and congestion response to TCP negotiation and feedback semantics; preserve useful path machinery when deleting migration-related facilities. Do not recreate ECN as if Zquic lacked it, or retain QUIC ACK_ECN counters as TCP wire feedback.

**Working result:** The pair recovers tail loss and reordered traffic using TCP-oriented recovery and responds to congestion marking through the intermediate protocol. It also works without ECN and, where SACK is absent, through the selected cumulative-ACK recovery path.

**Exit evidence:** Exercise tail/multiple loss, reordering, spurious retransmission, timer cancellation during close/reuse, CE marking, feedback, and congestion response. RACK uses local send-time information and does not depend on negotiating the TCP timestamp option. All previous application, ownership, and lifecycle checks still pass.

#### Phase 7 — Port early-data delivery to Fast Open semantics

**Reuse and change:** Reuse the retained application early-data ordering, buffering, and duplicate-suppression facilities where applicable. Implement TCP Fast Open cookies, establishment-data sequence accounting, acceptance/rejection, and ordinary-handshake fallback. Do not describe TLS-dependent QUIC 0-RTT authentication as a reusable TCP cookie implementation. Carry Fast Open semantics through the current intermediate setup messages before the final TCP encoding.

**Working result:** The pair delivers early application data when accepted and completes ordinary establishment when early data is unavailable or rejected, preserving one accept/stream creation and data delivery.

**Exit evidence:** Test first contact, accepted subsequent early data, stale/rejected cookie, lost/duplicate setup messages, failed early-data attempts, and disabled Fast Open. Confirm accepted data may precede connected(), and neither fallback nor retransmission delivers bytes twice.

#### Phase 8 — Complete TCP wire conversion and interoperability

**Reuse and change:** Encode the preceding slices' connection, acknowledgment, window, recovery, option, ECN, and Fast Open behavior in TCP headers/options over IP/Ethernet. Replace remaining intermediate bootstrap/frame/parameter representations and classifier assumptions with their TCP equivalents. Complete checksum, segment-size/option-space, sequence-wrap, demultiplexing, and endpoint-lifetime details that depend on the final wire representation. Delete obsolete intermediate codecs/state and their replaced tests; maintain one production protocol implementation.

Wire-dependent work can begin in earlier slices where it simplifies their implementation, provided every slice still ends with a working pair. This phase is the complete TCP interoperability acceptance point, not a requirement to defer all TCP header work until the end.

**Working result:** The accumulated client/server implementation speaks interoperable TCP. Existing native and Ztcp/Ztls application fixtures continue to work, and either endpoint can be replaced by a kernel TCP peer.

**Exit evidence:** Run the selected packetdrill scenarios against Zutcp. Verify Zutcp client → kernel server and kernel client → Zutcp server over single-host veth loopback, plus Zutcp → Zutcp on veth and physical Ethernet. Exercise enabled/absent extension paths, data integrity under loss/reordering, advertised-window pressure, Fast Open fallback, FIN/reset, connection reuse, telemetry and clean shutdown. Check actual integrity and operation rather than input rejection solely for conformance. Capture packets to confirm standard TCP signaling and absence of intermediate framing.

#### Measurements carried through the slices

Collect functional throughput/latency and buffer-pressure observations from the first working pair onward so regressions can be located. Once the final protocol is interoperable, compare short FIX traffic against kernel TCP using equivalent framing, affinity, scheduler/wait settings, reserve geometry, connection counts, and NIC/offload settings. Measure latency distributions, throughput, CPU use, drops, and reserve pressure. Veth results establish functionality; physical-NIC runs evaluate the intended deployment path. Trace userspace backing identity separately from kernel/driver capability.

Use OQ-7's Clang release-build profiling and optimization methodology for tuning and performance evaluation. There are no hard CPU budgets, throughput targets, or latency-percentile acceptance thresholds at this stage. Performance tuning is not a substitute for a vertical slice's working protocol, and OQ-6's final result is the interoperable transport rather than an isolated benchmark deliverable.

### OQ-7: Clang release profiling, optimization, and equivalent configuration

**Selected performance posture.** Here, profile-guided optimization means the author and agent use profiles collected with -pg from Clang release builds to decide and evaluate source-code and configuration changes. It does not prescribe compiler feedback-directed optimization or a profile-use rebuild pipeline. Do not set hard CPU budgets, throughput targets, or latency-percentile acceptance thresholds at this stage. Report CPU use, throughput, and latency distributions as observations that guide changes, rather than pass/fail limits. The motivating improvement over kernel TCP remains a hypothesis to measure.

#### Configuration derived from Zquic::HubParams

Use [Zquic::HubParams](./zquic/src/Zquic.hh) as the configuration model for Zutcp, carrying the applicable parameter/getter/default/initialization pattern forward from the mechanical copy. Preserve the mx association, relevant resource/flow-control limits, idle/heartbeat behavior, ECN control, and error reporting where applicable. Retain independent protocol rxThread/txThread selection only in phase 1; from phase 2 onward those Zutcp settings disappear and ZiMxParams exclusively configures the I/O Rx/Tx threads on which the protocol runs. Adapt their protocol semantics explicitly; equivalent configurability does not mean retaining QUIC-only settings.

Replace maxStreams* limits with maxCxns connection-count equivalents. TCP connections each have one bidirectional stream; configuration must limit connections rather than expose obsolete multiplexed-stream counts. These are local resource/admission controls, not a new TCP wire negotiation. Specify final connection-limit names, defaults, scope, and accounting in the file-level implementation plan, including active establishment, accepted connections, failure/close retirement, and exhaustion behavior. Do not mechanically preserve a simplex/duplex distinction that has no corresponding TCP connection meaning.

Remove settings for deleted TLS/ALPN, migration, and other excluded facilities. Adapt packet-size controls to the selected Ethernet/IP/TCP geometry instead of retaining a misleading maxUDP setting. Keep mx's existing allocator geometry/count factory and rxQuantum as their configuration sources, and the selected ZmRing-style waiting configuration at the packet-I/O layer. Avoid duplicate controls for the same setting. From phase 2 onward, the params object configures the native transport and its ZiMxPlugin integration without separate protocol-thread selectors; it does not revive the superseded Hub::utcp switch or require Ztcp/Ztls changes.

Exercise applicable configuration in the working client/server slices, including connection-limit admission and capacity recovery after failure/close. Record effective HubParams, mx, and backend settings with profiles and benchmark results so tuning remains reproducible. Backend selection remains fixed for the mx lifetime as already decided.

#### Profile, inspect, optimize, and remeasure

Configure Clang release builds through [z.config](./z.config), using -L for Clang and -p for -pg profiling; release is the default. Run representative client/server workloads and inspect the resulting profiles together to identify expensive functions, call paths, and avoidable work. Use that evidence to change code or configuration, rebuild, rerun the workload, and compare results. Preserve the dedicated Rx/Tx ownership model and zero-copy contracts while optimizing.

Exercise short FIX order/execution and market-data messages across the Rx/Tx pair, relevant connection counts, request/response, and sustained/bursty streams. Include recovery and control activity. Record compiler, source revision, target architecture, flags, workload, runtime settings, profile artifacts, and the rationale and measured effect of each optimization. Compare against the kernel backend with equivalent application framing and documented build/execution settings; separate veth functional observations from physical-NIC measurements.

Use profiles to locate work and comparable release measurements to assess the resulting changes. Record whether each measurement includes -pg instrumentation so its overhead is not confused with a transport difference. Follow [BUILD.md](./docs/BUILD.md): configure through z.config and perform clean top-level rebuilds when switching build modes. Keep sanitizer correctness runs separate from release performance measurements. No llvm-profdata merge, compiler profile ingestion, or new compiler-PGO build mode is required by this decision.

**Acceptance at this stage:** Deliver reproducible -pg profiles, author/agent analysis, source/configuration optimizations supported by those profiles, and before/after release measurements, with the functional client/server checks still passing. There are no hard CPU budgets, throughput targets, or latency-percentile acceptance gates.

### Selected TCP interoperability testing

**Author decision:** Adopt [Google packetdrill](https://github.com/google/packetdrill) for protocol/interoperability testing and use [Alipay's TLDK packetdrill integration](https://github.com/alipay/tldk) as an implementation reference. The other interoperability peer is the kernel TCP stack over single-host veth loopback, tested with Zutcp as both client and server. These tests become applicable as the slices acquire TCP wire behavior; the first intermediate-protocol milestone continues to use its own working-pair tests. Phase 8 completes the selected interoperability coverage.

The following comparison records the considered frameworks. Packetdrill is selected; packetimpact and IxANVL are not selected for this work.

| Framework | What it supplies | Zutcp integration and limitations |
| --- | --- | --- |
| [Google packetdrill](https://github.com/google/packetdrill) — open source | Timestamped scripts combining socket operations with injected/expected TCP packets. Supports local TUN testing and remote wire testing; useful for precise ACK, option and timer scenarios. | Ordinary scripts invoke operating-system sockets, so running them unchanged would test kernel TCP. A test-only adapter must drive ZiMxPlugin/Zutcp operations and expose their asynchronous results to the harness. Remote wire mode is a candidate for Ethernet/AF_XDP coverage; its timing variability must be accommodated. Establish packet visibility at the peer rather than assuming host capture sees every XDP-redirected packet. |
| [gVisor packetimpact](https://github.com/google/gvisor/blob/master/test/packetimpact/README.md) — open source | Go tests with control flow, raw packet injection/observation, and a separate control network. Its existing C++ posix_server executes socket commands over gRPC. | A test-only DUT server could map the needed commands onto Zutcp callbacks while preserving its shard ownership. This matches namespace/veth fixtures but adds Go/gRPC and harness integration. The upstream documentation identifies packetdrill as more precise for timing-sensitive tests; assess scenario coverage before porting its suite. |
| [Keysight IxANVL](https://www.keysight.com/us/en/assets/3120-1119/data-sheets/IxANVL-Automated-Network-Validation-Library.pdf) — commercial | TCP conformance suites and a portable C TCP Stub, remotely controlled to stimulate the stack from its application side while packets exercise the wire side. | Assess a Zutcp-backed stub integration and the licensed suite's exact coverage of the selected extensions. Costs include licensing, target-side porting and external infrastructure. A conformance test report is not by itself a certification award. |

Alipay's public TLDK fork includes packetdrill source/tests and documents loading libtldk.so through --so_filename. Inspect its application-call adaptation, stack initialization/dispatch, and packet-I/O boundary as reference code. This is separate from Alibaba Cloud's LUNA stack; no public LUNA source is assumed. Port the useful test-integration structure to Zutcp while retaining its two-shard ownership and actual Zxdp data path, rather than importing TLDK's production socket layer or DPDK implementation.

The adapter requirements above remain engineering inferences from the documented boundaries, not claims of existing ZiMxPlugin support. Keep adapters under Zutcp test infrastructure; do not add POSIX emulation or alter Ztcp/Ztls as a production requirement. Any local/remote packet injection path must satisfy the selected AF_XDP queue, endpoint-steering, reserved-port and static-neighbor setup.

The kernel-peer fixture uses ordinary kernel sockets at one endpoint and a plugin-backed Zutcp endpoint at the other, connected through a veth pair, optionally in network namespaces. Verify both connection directions and select the backend explicitly so a successful kernel-to-kernel exchange cannot be mistaken for Zutcp coverage. Preserve reserved ports, static neighbors, endpoint steering, and unrelated kernel traffic. Packetdrill supplies controlled injected/expected packet scenarios; the kernel peer supplies real independent TCP behavior. Physical-host Zutcp-to-Zutcp tests remain part of the deployment topology checks, separately from this selected kernel-over-veth interoperability fixture.

Follow [BUILD.md](./docs/BUILD.md)'s distinction: put packetdrill integration, scenarios, test drivers and assertions in zutcp/interop, along with tests specifically exercising the external kernel TCP peer. Put native Zutcp/Zxdp networking and complete endpoint-pair integration tests in itest, and isolated unit/self-tests in test. Shared utility programs and scripts belong in util, which builds independently of its consumers; itest may depend on util, but util must not depend on itest. Repository-owned test drivers use ZuTest/ZuTestUtil and emit TAP; external framework scripts in interop may retain their native format. No commercial suite, third-party certification service, or additional TCP-stack peer is required by this decision.

For any suite, record the supported-feature profile, applicable cases, exclusions and actual failures. Use scenarios to verify data integrity, interoperability, recovery and progress; preserve the agreed posture against input rejection solely for conformance policing. External TCP tests do not replace UMEM ownership, control-pool exhaustion, scheduler and teardown tests specific to this implementation.

### File mapping and verification discipline

The copied `ZutcpPacket`, `ZutcpFrame`, `ZutcpCrypto`, `ZutcpPath`, `ZutcpLink`, `ZutcpCliLink`, `ZutcpSrvLink`, `ZutcpSock`, `ZutcpStream`, `ZutcpRecovery`, `ZutcpSched`, and buffer helpers are the initial change map, not a requirement to retain obsolete QUIC file boundaries. Remove crypto files after their callers are stripped; retain reusable scheduling/recovery pieces where their behavior still applies. Specify the new Zxdp and plugin declarations, every source/header and test, and all build/documentation changes in the subsequent exhaustive file list. Inspect `configure.ac`, top-level/module `Makefile.am` inputs, and `z.config` for actual registration needs; do not assume every candidate must change. Ztcp and Ztls sources remain outside the modification set.

Follow the module roles in [BUILD.md](./docs/BUILD.md): test for isolated unit/self-tests; itest for networking/setup and native complete endpoint-pair integration tests; interop for tests specifically exercising third-party implementations or conformance frameworks, including packetdrill and the kernel TCP peer; util for reusable tools/scripts; and bench for measurements. Preserve the stated traversal of src, test, util, itest, then other directories in dependency order. Keep external-framework dependencies out of native itest drivers by putting the corresponding tests in interop. Repository-owned unit/integration drivers emit TAP through ZuTest/ZuTestUtil and run under their directory's make test target; external harness scripts may remain native. Retain the veth smoke test throughout the conversion, updating it to the current protocol rather than requiring compatibility with previous intermediate revisions. Use controlled loss/reordering fixtures and deterministic mechanism tests where practical. Check actual integrity and operation rather than adding input rejection solely to satisfy conformance tests.

Build changed libraries before their dependents and build `all` before running the applicable `make test` targets. Perform release verification through z.config and a clean top-level build; follow the repository's sanitizer/leak acceptance guidance. Document privileged network setup/cleanup and preserve existing port reservations, neighbors, and unrelated XDP users. Each phase records its evidence and outstanding failures; adoption of the slices does not claim these tests have already run.

*Drafted with EGM*
