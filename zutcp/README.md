# Zutcp: User-mode TCP/IP stack

Zutcp is a Linux user-mode TCP/IP stack integrated with `ZiMultiplex`, using
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

See the [kernel/native quickstart](util/README.md) for the stopped-multiplexer
setup, actual command-line tools, sudo fixture commands and deployment prerequisites.

`Zutcp::Instance` implements `ZiMxPlugin`. Register it on a stopped multiplexer
before creating application allocators or starting I/O. One Instance owns both
outgoing and accepted connections and all listeners for its multiplexer. The app
constructs and configures it statically; no module loader or factory is involved.
Instance implements the inherited `Ztc::Mx` telemetry interface, not `Ztc::Hub`.
Select the interface and
optional destination network mask at setup; backend selection stays fixed while
the multiplexer runs. An mx without this plugin uses its normal kernel backend.

Applications continue to use [Ztcp](../ztcp/README.md) and, for encryption above
TCP, [Ztls](../ztls/README.md). Zutcp itself supplies TCP transport and has no
Ztls dependency. Ordinary `ZiIP` addresses and `ZiResolver` remain available.
Zutcp resolves routes and static neighbors synchronously on mx Rx during
connection setup, constrained to the configured interface. This bounded blocking
netlink setup work finishes before the first SYN or SYNACK; there is no resolver
worker. With a destination mask, unmatched traffic stays with the
host stack; without a mask Zxdp selects the whole interface.

Call `Instance::init()` before starting the multiplexer. Init is idempotent
while stopped and returns an error during starting, running or stopping. The
plugin uses ZmEngine's control FSM, including queued opposite operations, while
preserving ZiMxPlugin's existing completion callbacks. Stop retains configuration
and history, and restart reuses the same Zxdp instance. After stop, `final()`
releases stopped resources, secrets and history; it is idempotent and the
destructor also calls it. Release dependent application buffers before final.

In the application bootstrap, initialize Instance before installing the plugin.
Handle its structured initialization result before registering allocators or
starting mx; this example returns false from a boolean bootstrap function:

```c++
// mx already has distinct Rx/Tx owners and an assigned AF_XDP sender slot.
ZmRef<Zutcp::Instance> plugin = new Zutcp::Instance{&mx};
auto initialized = plugin->init({.interface = ifindex, .txSid = senderSid,
  .ackHoldTimeout = ZuTime{0, 25000000}});
if (initialized.is<ZeException>()) {
  ZiLogEvent(ZuMv(initialized.p<ZeException>()));
  return false;
}
mx.plugin(plugin);
```

Connection and transfer code uses Ztcp/Ztls or the existing generic Zi callbacks.
The former Zutcp Hub, Client and Server application transport hierarchy is removed.
Private protocol headers are available only in the source tree for backend tests.

Each connection owns one ordered bidirectional byte flow: reassembly, delivery,
receive-credit retirement, retained transmit backing, recovery and close state.
Application parsers must handle arbitrary receive boundaries. Protocol work runs
on the multiplexer Rx and Tx owners configured through `ZiMxParams`, shared by
multiple connections.

### Returning to kernel TCP

Use fresh application owners and a fresh default-kernel multiplexer. A graceful
process replacement provides that separation:

1. Stop accepting or producing new application work. Close existing sessions
   through their normal owner callbacks while the native multiplexer remains
   running, and wait for every initiated connection's terminal notification.
2. Stop and finalize the application. Stop the native multiplexer and await its
   plugin completion: accepted Tx submissions must retire before sender shutdown
   and the XDP engine stop completes. Socket closure alone does not establish kernel retirement.
3. Destroy remaining application owners before the native multiplexer. Allocator
   registrations remain valid through this teardown, and providers/pools outlive
   every packet lease. Check the normal process exit and teardown errors, not
   merely a successful final application message.
4. Verify that the instance's XDP attachment has been removed before starting
   kernel endpoints. Preserve interfaces, addresses, routes and neighbors owned
   by deployment configuration; the transport does not own them. Do not remove
   another owner's XDP program to force a transition.
5. Start fresh kernel processes, or construct a fresh multiplexer without the
   plugin and obtain fresh builtin allocators while it is stopped. Reinitialize
   application owners, start the multiplexer and establish new TCP sessions.

Do not clear the plugin on a stopped native multiplexer and reuse its existing
allocator registrations. An established native TCP session cannot be transferred
to a kernel socket. The process-replacement procedure does not establish an
in-process backend-switching facility.

## TCP facilities

The TCP implementation includes cumulative ACKs, SACK/DSACK, window
scaling, timestamps with PAWS, RACK-TLP loss detection, RTO and zero-window
persist handling, ECN and NewReno congestion response. Peer extensions are
negotiated optionally, with fallback when absent. Connection establishment,
half-close, reset and bounded closed-tuple history follow TCP sequence semantics.

TCP Fast Open is disabled in ordinary plugin application use. Private backend
fixtures exercise explicit early-data authorization, cookie fallback and data
arriving before establishment; these are protocol tests, not replacement
application hooks. Initial sequence numbers
and Fast Open cookies use secure keyed primitives independently of TLS.

## Buffers and ownership

`Zutcp::Allocator` implements `Zi::IOBufPool`, forwarding storage operations
and receive-return notification to an injected common
pool obtained from the selected `Zxdp::Instance` backend's `bufPool()` factory.
Concrete registered pools remain internal to Zxdp. Instance selects that backend
before creating pools, then constructs the typed configuration in place:

```c++
Zxdp::Params config(
  Zxdp::Params::New<Zxdp::Params::Index<Zxdp::Xdp::Params>{}>{});
auto &params = config.p<Zxdp::Xdp::Params>();
// Populate params, then call backend->init(config) before backend->start().
```

Configuration owns the Tx pool list and Rx reference; the initialized backend retains its own
pool references independently of configuration-list membership. Instance retains
raw pools through its transport providers between starts. Tx buffer allocation
failure drops the connection rather than waiting for a pool grant. Pools publish
fixed count, size, alignment,
headroom and tailroom geometry before start.
Zutcp writes complete Ethernet/IP/TCP packets directly into registered backing;
there is no intermediate payload staging buffer. Hardware zero-copy depends on
the kernel and NIC driver and requires physical-NIC validation.

One physical ingress reserve serves the instance. Tunable instance and
per-connection retention limits preserve receive-service headroom; separate
control backing and admission credits keep ACK, window, probe and close work
moving under payload pressure. Connection and pending-handshake limits bound
admission independently of allocator counts.

Transport ACK/recovery ownership retains the raw allocation lease independently
of the kernel submission lease. CQ retires kernel access, not peer delivery.
Refused output retains caller ownership. Shutdown keeps sender and CQ service
active after admission stops, until accepted submissions complete on a healthy
backend. This includes the final FIN ACK. The sender barrier and socket retirement
follow that completion gate; a failed backend retains cancellation semantics.
Shutdown drains work on its owners and
retries raw close until kernel access retires. Providers and pools outlive every
application-held or recovery-held packet.

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

Include `<zlib/Zutcp.hh>` for the stack and `<zlib/ZutcpAlloc.hh>` for provider
composition. `Zutcp.hh` is a small backend umbrella; connection/endpoint
implementations remain private source-tree headers. IP framing and route helpers
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
