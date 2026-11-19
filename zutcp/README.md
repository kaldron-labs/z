# Zutcp: User-mode TCP/IP stack

Zutcp is a Linux user-mode TCP and UDP transport integrated with `ZiMultiplex`, using
[Zxdp](../zxdp/README.md) for raw AF_XDP packet I/O. It targets short FIX order,
execution and market-data messages while keeping the existing Zi connection,
allocator and application framing interfaces.

The native transport uses standard TCP headers/options and raw application payload.
Selected IPv4/IPv6 native and real Linux-kernel peer fixtures validate transfer,
recovery, options and lifecycle behavior. See [native tests](itest/README.md) and
[independent-peer tests](interop/README.md) for runnable commands and coverage.
Physical-Ethernet peer operation has not been validated in this environment.

The current native path carries SYN/SYNACK sequence origins, connection byte
sequence, cumulative ACK, FIN and negotiated receive windows in the TCP header.
Standard options carry MSS, SACK/DSACK, window scaling and timestamps/PAWS.
Initial sequence numbers use HMAC-SHA256 through OpenSSL's crypto library.
Internal byte positions remain wide offsets relative to the chosen origins.
Endpoint routing uses the TCP four-tuple. Intermediate packet/frame codecs,
bootstrap IDs and packet-number acknowledgment formats have been removed.

`InstanceParams::sackPermitted`, `windowScale` and `timestamps` default to enabled.
Configure them before starting the multiplexer. SACK permission is directional;
scaling and timestamps require both peers' offers. The receive scale derives
from `maxData`. Without a peer MSS option, the send cap is 536 bytes for IPv4 or
1220 for IPv6; actual TCP option bytes reduce the application budget.
Establishment publishes that budget before callbacks.
SACK retains original transmit backing until cumulative acknowledgment, including
when a receiver reneges on its earlier report.

The native pair uses RACK send chronology and one accepted TLP probe
per episode when SACK is available. Timestamp-free recovery remains supported;
without SACK it uses cumulative ACK recovery and RTO. Classic TCP ECN is
configured through the existing default-off `ecn` setting. New DATA carries ECT
only after negotiation; retransmissions and control packets remain Not-ECT.
With congestion control enabled, accepted DATA sends create pacing debt;
pure ACK/window traffic can proceed.

`InstanceParams::congestionControl = false` disables congestion-window limits,
congestion accounting and pacing for controlled low-latency networks. Set it before starting
the multiplexer. The default is enabled; the setting is cached at initialization.
Disabled mode suppresses ECN negotiation while retaining receiver flow control,
bounded local buffer credits, ACK/SACK retirement, loss recovery and RTO backoff.
Nagle remains disabled in both modes.

`InstanceParams::ackHoldTimeout` configures the cumulative ACK hold. It defaults to 25
milliseconds, accept zero to disable holding, and require a value below 500
milliseconds. Eligible in-order DATA may hold one pending ACK; each receive
updates it to the latest cumulative position. Outgoing DATA can carry that ACK
before the deadline. A second full-size segment, FIN, receive gap, duplicate,
ECN exception or receive-window reopening requests an immediate reply.

After successful native submission, the Tx owner publishes the packet's cumulative
ACK atomically, preserving its coordinate conversion and monotonic progress.
Refused sends leave that ACK unchanged. Recovery bookkeeping finishes on the same
Tx owner before queued peer ACK work runs. Incoming peer ACKs pass directly to
ACK processing without validation against a published transmitted frontier.

Without timestamps, an exact full sequence-cycle alias can be indistinguishable
from current data. PAWS adds timestamp checks under its ordinary clock and
segment-lifetime assumptions; it does not create an unlimited wire epoch.

## Application integration

Configure the multiplexer’s Rx, Tx, and XDP sender slots before constructing it.
Open the XDP instance synchronously on that stopped multiplexer. A successful
open exposes the interface index, MTU, and Rx queue count. Move XDP into
`Zutcp::Instance::init` on the same multiplexer before XDP allocation or start,
then move the initialized transport into `mx.plugin()`. The second transfer
must precede the first allocator request, allocation, or start attempt.

```cpp
ZiMultiplex mx{ZiMxParams{}.rxThread(1).txThread(2).scheduler([](auto &s) {
  s.nThreads(3).thread(3, [](auto &t) { t.isolated(true); });
})};
auto xdp = Zxdp::instance(Zxdp::Type::Xdp);
Zxdp::Params xdpParams{Zxdp::Xdp::Params{.ifindex = ifindex, .txSid = 3}};
auto opened = xdp->init(&mx, xdpParams);
if (opened.is<ZeException>()) {
  ZiLogEvent(ZuMv(opened).p<ZeException>());
  return false;
}

ZmRef<Zutcp::Instance> plugin = new Zutcp::Instance;
auto initialized = plugin->init(&mx, ZuMv(xdp), {.packetCount = 2048});
if (initialized.is<ZeException>()) {
  ZiLogEvent(ZuMv(initialized).p<ZeException>());
  return false;
}
mx.plugin(ZuMv(plugin));

// Protocol initialization retains each typed receive handle for endpoint setup.
auto tcpRx = mx.allocator<Zi::IOBufType::Rx, TcpRxBuf, 2048>();
auto udpRx = mx.allocator<Zi::IOBufType::Rx, UdpRxBuf, 2048>();
auto tcpTx = mx.allocator<Zi::IOBufType::TxTCP>(2048);
auto udpTx = mx.allocator<Zi::IOBufType::TxUDP>(512);
if (!tcpRx || !udpRx || !tcpTx || !udpTx) return false;

if (prepareTxBeforeStart) {
  auto allocated = mx.alloc();
  if (allocated.is<ZeException>()) {
    ZiLogEvent(ZuMv(allocated).p<ZeException>());
    return false;
  }
  // tcpTx and udpTx can allocate native buffers here.
}
if (!mx.start()) return false;
mx.listen(tcpListening, tcpFailed, tcpAccepted, localIP, tcpPort, 32, tcpRx);
mx.udp(udpReady, udpFailed, localIP, udpPort, {}, 0, udpRx, ZiCxnOptions{}.udp(true));
```

Each `listen`, `connect`, or `udp` call selects its network receive allocator.
Accepted TCP connections inherit the listener’s handle. An application may
use TCP, UDP, or both on the same transport. Applications using
[Ztcp](../ztcp/README.md) or [Ztls](../ztls/README.md) initialize their hub before
starting the multiplexer so the hub can obtain its allocators.

XDP owns the shared registration after the first successful allocation.
`mx.alloc()` is optional. When omitted, `mx.start()` allocates before packet
activation and reports allocation or activation failure through its result.
After allocation begins, every new allocator request returns `nullptr`. An
identical stopped initialization succeeds without reopening the device or
changing transport state. A conflicting initialization returns an error.

Close endpoints and let their callbacks drain, stop the multiplexer, and
return any retained buffers before destroying it. The multiplexer finalizes the
TCP/UDP plugin while its allocators are still live. The plugin in turn finalizes
XDP, which releases the registration, arena, and device resources. This also
happens when the multiplexer never starts. Stop and restart retain allocation
and TCP history. The plugin’s binding to the multiplexer is permanent.

Zutcp uses the injected XDP interface for route and static-neighbor resolution.
This bounded netlink work runs on mx Rx during connection setup. A destination
mask configured on XDP leaves unmatched traffic to the host stack.

### Using kernel TCP

Construct a new multiplexer with the regular kernel transport. Initialize
application owners and request their allocators from that multiplexer before
starting it. When replacing a native deployment, close its sessions, drain and
stop its multiplexer, and return dependent buffers before destroying it. Start
new TCP sessions on the kernel multiplexer.

See the [client example](example/zutcp.cc), [server example](example/zutcpd.cc),
and [kernel/native quickstart](util/README.md).

## TCP facilities

The TCP implementation includes cumulative ACKs, SACK/DSACK, window
scaling, timestamps with PAWS, RACK-TLP loss detection, RTO and zero-window
persist handling, ECN and NewReno congestion response. Peer extensions are
negotiated optionally, with fallback when absent. Connection establishment,
half-close, reset and bounded closed-tuple history follow TCP sequence semantics.

TCP Fast Open is disabled in ordinary plugin application use. Private protocol
fixtures exercise explicit early-data authorization, cookie fallback and data
arriving before establishment; these are protocol tests, not replacement
application hooks. Initial sequence numbers
and Fast Open cookies use secure keyed primitives independently of TLS.

## Buffers and ownership

Zutcp creates typed pools through the injected XDP instance. The multiplexer
collects the first Rx pool and all application Tx pools. Zutcp includes its
internal TCP data and control reserves in the same XDP allocation. Pool
geometry is available before allocation. Native Tx slot allocation returns
immediately and can fail when a pool is exhausted.

Protocols prepare their receive objects with the selected typed Rx allocator.
Zutcp attaches bytes from the shared ingress pool to each prepared object,
and the originating pool receives the slot back when the buffer is released.
Use App allocators for parser copies and other storage owned solely by the
application. Backing held across stop retains its address and contents.

Zutcp constructs TCP wire headers over registered backing. UDP copies each
outgoing datagram into a native wire buffer so Zi completion callbacks can
still use the original application buffer. Hardware zero-copy depends on the
kernel and NIC driver.

One physical ingress reserve serves the instance. Tunable instance and
per-connection retention limits preserve receive-service headroom; separate
control backing and admission credits keep ACK, window, probe and close work
moving under payload pressure. Connection and pending-handshake limits bound
admission independently of allocator counts.

Transport ACK/recovery ownership retains the raw allocation lease independently
of the kernel submission lease. CQ retires kernel access, not peer delivery.
Refused output retains caller ownership. Shutdown keeps sender and CQ service
active after admission stops until accepted submissions complete. This includes
the final FIN ACK. Shutdown drains work on its owners and closes the per-run
sockets after the sender barrier. Allocation and the registering socket remain
live through restart.

## Connection hash sizing

Before constructing the Instance, bootstrap code can configure the existing
`ZmHashMgr` IDs `Zutcp.Endpoints`, `Zutcp.Endpoint.TupleRouter`, and
`Zutcp.ServerLinks`. These constructors retain their default 32 buckets and load
factor 1 when no named override is installed. The tables are owner-local and use
`ZmNoLock`, so they have no lock stripes regardless of configured `cBits`. Named
settings are applied after the defaults, including server membership recreation
on restart. For 128 connections, a 256-bucket setting (`bits(8)`) leaves headroom
without resizing these indexes. This configures initial geometry, not a capacity
limit. Server membership still uses the concrete connection's heap; naming its
hash does not enable allocation of the inner intrusive node.

## Connection ownership and queue telemetry

`Cxn` is the common connection identity and lifecycle interface. `CliCxn` and
`SrvCxn` share TCP behavior through the internal `Cxn_` implementation.
`Instance` owns one intrusive list containing pending, published and retiring
connections. `allCxns()` filters pending and retiring entries, takes temporary
application connection references under `m_cxnsLock`, then invokes callbacks
without holding the lock. No separate observation list or manager is maintained.

Instance also owns admission counters and checks limits before connection
construction. Cxn carries admitted, pending and closing flags directly. Failed
setup cancels its reservation; successful setup attaches it to Cxn before first
packet delivery. Capacity remains charged through owner drain, independently of
ownership-list membership. No separate admission token or ledger is maintained.

Clients and listeners embed their local binding nodes. Instance holds the
non-owning endpoint hash directly, with no separate binding-record allocations.
Packet dispatch finds the exact local address first, then the family wildcard;
accepted connections share their listener's binding and use its tuple routing.
Unbinding removes the endpoint's own node before owner teardown. Embedded hash
telemetry is available through the framework's `HashMgr::capture()` operation.

`Instance::allQueues()` delegates to `Zxdp::Instance::allQueues()`, exposing its
Rx, fill, Tx and completion rings. It does not traverse connections or listeners.
Callbacks borrow the rings; finish enumeration before shutting down their owning
XDP instance. Queue pointers must not escape the callback. Fields are advisory
cold reads, and unsupported optional normalized metrics remain zero.

## Source and verification

Include `<zlib/Zutcp.hh>` for the transport. Connection and endpoint
implementations remain in private source-tree headers. IP framing and route helpers
live in `ZutcpIP.hh`. Link `libZutcp`, `libZxdp` and their framework
dependencies. Both modules are Linux-only and are excluded from Windows builds.

The module uses `src`, `test`, `util`, `itest`, `example`, `interop` and `bench`.
Build `zutcp/src` first with `-j8`, then affected module targets. Isolated tests
run through `make -C zutcp/test test`. Ztcp and Ztls are optional application
consumers, not dependencies. Privileged packet tests and their scoped fixtures
are described in [itest/README.md](itest/README.md).

Native veth and independent Linux TCP fixtures cover both endpoint roles and IP
families, including recovery, options, flow control and lifecycle. Physical
Ethernet acceptance remains unverified. Performance tuning is separate; current
veth measurements establish no latency or throughput advantage over kernel TCP.
