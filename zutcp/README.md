# Zutcp: User-mode TCP/IP stack

Zutcp is a Linux user-mode TCP/IP stack integrated with `ZiMultiplex`, using
[Zxdp](../zxdp/README.md) for raw AF_XDP packet I/O. It targets short FIX order,
execution and market-data messages while keeping the existing Zi connection,
allocator and application framing interfaces.

Implementation is in progress. This guide describes the final facility in
[the execution plan](../zutcp_plan.md); it does not claim that the current
intermediate transport is interoperable TCP. The plan records implementation
checkpoints and remaining acceptance work.

The current native path carries connection byte sequence, cumulative ACK, FIN
and the unscaled receive window in the TCP header. Plain packet ACK ranges still
serve the intermediate bootstrap/control protocol. Relative byte positions and
bootstrap IDs remain pending TCP establishment and lifecycle conversion; window
scaling and final wire interoperability are later work.

## Application integration

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

## TCP facilities

The planned TCP implementation includes cumulative ACKs, SACK/DSACK, window
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

`Zutcp::Allocator` composes Zi buffer providers over caller-owned Zxdp pools.
Providers publish fixed headroom, payload and tailroom geometry before start.
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
Refused output retains caller ownership. Shutdown drains work on its owners and
retries raw close until kernel access retires. Providers and pools outlive every
application-held or recovery-held packet.

## Source and verification

Include `<zlib/ZutcpMx.hh>` for the stack and `<zlib/ZutcpAlloc.hh>` for provider
composition. Native connection facilities are under `Zutcp.hh`; IP framing and
route helpers live in `ZutcpIP.hh`. Link `libZutcp`, `libZxdp` and their framework
dependencies. Both modules are Linux-only and are excluded from Windows builds.

The module follows the standard `src`, `test`, `itest` and `example` layout.
Build through the top-level `make -j8`; isolated tests run through
`make -C zutcp/test test`. Privileged packet tests and their scoped network
fixtures are described in [itest/README.md](itest/README.md).

Final acceptance includes Zutcp pairs over veth and physical Ethernet, plus
independent Linux TCP peers in both client and server roles, with loss,
reordering, pressure and lifecycle coverage. Performance benefits remain to be
measured against the kernel path; current intermediate-protocol results do not
establish TCP interoperability or a latency improvement.
