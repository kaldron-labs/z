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

`HubParams::sackPermitted`, `windowScale` and `timestamps` default to enabled.
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

`HubParams::congestionControl(false)` disables congestion-window limits,
congestion accounting and pacing for controlled low-latency networks. For the
ZiMultiplex plugin, set `StackConfig::congestionControl = false` before starting
the multiplexer. The default is enabled; the setting is cached at initialization.
Disabled mode suppresses ECN negotiation while retaining receiver flow control,
bounded local buffer credits, ACK/SACK retirement, loss recovery and RTO backoff.
Nagle remains disabled in both modes.

`HubParams::ackHoldTimeout` configures the cumulative ACK hold; the
`ZiMultiplex` plugin uses `StackConfig::ackHoldTimeout`. Both default to 25
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

`Zutcp::Stack` implements `ZiMxPlugin`. Register it on a stopped multiplexer
before creating application allocators or starting I/O. Select the interface and
optional destination network mask at setup; backend selection stays fixed while
the multiplexer runs. An mx without this plugin uses its normal kernel backend.

Applications continue to use [Ztcp](../ztcp/README.md) and, for encryption above
TCP, [Ztls](../ztls/README.md). Zutcp itself supplies TCP transport and has no
Ztls dependency. Ordinary `ZiIP` addresses and `ZiResolver` remain available.
Zutcp uses cold kernel route and static-neighbor queries constrained to the
configured interface. With a destination mask, unmatched traffic stays with the
host stack; without a mask Zxdp selects the whole interface.

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
   and backend close. Socket closure alone does not establish kernel retirement.
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

TCP Fast Open is disabled by default and requires application authorization.
Cookie rejection falls back to ordinary establishment while preserving queued
data. Accepted early data can arrive before the established callback; application
replay policy remains an application responsibility. Initial sequence numbers
and Fast Open cookies use secure keyed primitives independently of TLS.

## Buffers and ownership

`Zutcp::Allocator` implements `Zi::IOBufPool`, forwarding storage operations
and receive-return notification to an injected common
pool obtained from the selected `Zxdp::Instance` backend's `bufPool()` factory.
Concrete registered pools remain internal to Zxdp. Stack selects that backend
before creating pools, then constructs the typed configuration in place:

```c++
Zxdp::Params config(
  Zxdp::Params::New<Zxdp::Params::Index<Zxdp::Xdp::Params>{}>{});
auto &params = config.p<Zxdp::Xdp::Params>();
// Populate params, then call backend->init(config).
```

Configuration owns the Tx pool list and Rx reference; the initialized backend retains its own
pool references independently of configuration-list membership. Stack retains
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

Before constructing the Stack or server, applications can configure the existing
`ZmHashMgr` IDs `Zutcp.Endpoints`, `Zutcp.Endpoint.TupleRouter`, and
`Zutcp.ServerLinks`. These constructors retain their default 32 buckets and load
factor 1 when no named override is installed. The tables are owner-local and use
`ZmNoLock`, so they have no lock stripes regardless of configured `cBits`. Named
settings are applied after the defaults, including server membership recreation
on restart. For 128 connections, a 256-bucket setting (`bits(8)`) leaves headroom
without resizing these indexes. This configures initial geometry, not a capacity
limit. Server membership still uses the concrete connection's heap; naming its
hash does not enable allocation of the inner intrusive node.

## Queue telemetry lifetime

`ZiMultiplex::allQueues` includes the plugin's actual transport queues through
`ZiMxPlugin::allQueues`. Callbacks borrow those queue objects; native owners stay
alive until enumeration returns, even if a callback requests connection closure
and observes disconnection. Complete a synchronous full multiplexer shutdown
after returning from enumeration: shutdown waits for those same native owners to
retire. Queue pointers must not escape the callback. Fields are advisory cold
reads, and unsupported optional normalized metrics remain zero.

## Source and verification

Include `<zlib/ZutcpMx.hh>` for the stack and `<zlib/ZutcpAlloc.hh>` for provider
composition. Native connection facilities are under `Zutcp.hh`; IP framing and
route helpers live in `ZutcpIP.hh`. Link `libZutcp`, `libZxdp` and their framework
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
