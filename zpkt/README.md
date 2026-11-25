# Zpkt: Linux packet I/O

Zpkt supplies raw Ethernet packet service over AF_XDP and optional DPDK.
The [interface contract](INTERFACE.md) defines its boundary. The caller supplies
transport state, application adaptation and scheduling.
Include `<zlib/Zpkt.hh>` for device operations and `<zlib/ZpktEnv.hh>` for
application-owned construction. Link common `libZpkt`, `libZi` and their framework
dependencies. Load `libZpkt_xdp.so` or `libZpkt_dpdk.so` through Env configuration;
production callers have no direct native-backend linkage.
For Zi provider and transport integration see [Zutcp](../zutcp/README.md) and its
[kernel/native quickstart](../zutcp/util/README.md), including sudo, interface
ownership, routes and static neighbors.
Both modules are Linux-only and are excluded from Windows builds.

The package and API naming change is breaking. Rebuild callers against the
`Zpkt` headers and namespace, select `libZpkt_xdp.so` or `libZpkt_dpdk.so` in
module configuration, and use `ZPKT_*` fixture/runtime settings. Backend module
loaders resolve the `ZpktPlugin` initialization entry point.

## Modules and named interfaces

Common code in `src` builds versioned `libZpkt`. The module-specific source
directories `xdp` and optional `dpdk` build unversioned libtool modules
`libZpkt_xdp.so` and `libZpkt_dpdk.so` in the existing library installation directory.
The installed headers are `ZpktLib.hh`, `Zpkt.hh`, `ZpktPlugin.hh` and `ZpktEnv.hh`.
Backend headers and the core query header are private.

Initialize an intrusive Env from a ZfCf tree. Initialization loads owned typed
configuration; it creates no named device. The Cf tree can be released after init.
Both plugins use the Linux interface name as the map key. For AF_XDP, select
a sender slot (`txSid`) distinct from the multiplexer's Rx and Tx slots. For
DPDK, map that same key to a native port as shown in the [DPDK example](#dpdk).

```text
module libZpkt_xdp.so
preload false
interfaces {
  veth0 { txSid 3 }
}
```

`module` is required. `preload` defaults to false and adds `ZiModule::Pre` only
when true. A module exports the C-linkage `ZpktPlugin(cf)` entry point, which
returns a fully initialized `Zpkt::Plugin`. Loader and startup configuration
errors throw `ZeException`. Missing modules/symbols, throwing factories and null
results never publish a partially initialized Env. After entry-point invocation,
module code remains resident for framework callbacks and object destruction.

Configure the sender slot before constructing the multiplexer. Open a named Nif
while that multiplexer is stopped. Handle startup exceptions around the entire
application scope; inspect the result union for named creation:

```cpp
ZmRef<Zpkt::Env> env = new Zpkt::Env;
env->init(cf);
ZiMultiplex mx{ZiMxParams{}.scheduler([](auto &scheduler) {
  scheduler.nThreads(3).thread(3, [](auto &sender) { sender.isolated(true); });
}).rxThread(1).txThread(2)};
auto opened = env->nif(&mx, "veth0");
if (opened.is<ZeException>()) {
  ZiLogEvent(ZuMv(opened).p<ZeException>());
  return 1;
}
ZmRef<Zpkt::Nif> xdp = opened.p<Zpkt::Nif *>();
auto mtu = xdp->mtu();
auto queues = xdp->rxQueues();
```

The factory returns an initialized, stopped, unallocated Nif. `ifindex()`, `mtu()`,
`mac()` and `rxQueues()` expose its geometry. Env queries the Linux interface on
one cache miss and passes trusted `NifInfo` to its backend. A same-name/multiplexer
lookup returns the same pointer without querying or reopening, including while
running; a different multiplexer returns a structured error. The caller acquires
its own reference immediately. AF_XDP redirects all interface traffic, including
non-IP frames. Its `spin` controls idle probes before Rx blocks or the sender
returns to its scheduler; `txSid` is required and nonzero.

Env setup and teardown are caller-serialized. It owns a name-keyed tree of Nif
references; children have raw Env backpointers. Env has no engine or start/stop.
Successful child finalization retires its cache entry and permits recreation.
Callers stop/drain children and return buffers before `Env::final()`, which
asserts stopped children and invokes their idempotent final operations before
finalizing its plugin. A child-finalization error retains that child and plugin.
Release external Nif references before the final Env reference. Env must precede
and outlive every Nif.

An application using Zutcp passes the borrowed Env alongside the Nif reference:
`tcp->init(&mx, env.ptr(), ZuMv(xdp), tcpParams)`. Success consumes the Nif;
failure preserves the caller's reference, which the caller finalizes. Move TCP
into `mx.plugin(ZuMv(tcp))` before requesting allocators. TCP drives Nif allocation,
start, stop and finalization through the multiplexer lifecycle. TCP never finalizes
Env. See [Zutcp integration](../zutcp/README.md#application-integration).

For deterministic dependent tests, `env->nifQueryFn(fn)` substitutes a nonnull
plain function pointer of type `ZuUnion<NifInfo, ZeException> (*)(ZuCSpan)` during
serialized setup. It defaults to the real core query and survives init. A hit
calls neither query nor plugin. A query error propagates without backend creation.

## DPDK

On Linux, `z.config` enables DPDK when configuring the tree. It requires
`libdpdk >= 25.11` in pkg-config. Pass `-K` for an AF_XDP-only build. For direct
`configure` invocations, enable it with `--with-dpdk=yes`.

Select the DPDK module through the same Env loader:

```text
module libZpkt_dpdk.so
eal {
  noPCI true
  noHuge true
  noShconf true
  noTelemetry true
  vdev [ "net_af_packet0,iface=veth0" ]
}
interfaces {
  route0 { port net_af_packet0 spin 1000 rxInterval 50 }
}
```

The plugin owns EAL directly and initializes it during construction. The `eal`
child uses existing EALParams field metadata; absent fields keep existing defaults.
An empty `interfaces {}` map is valid for EAL/pool-only work. Native EAL still
permits one process-wide initialization attempt. `Dpdk::EAL::init` throws
`ZeException` on failure. Final Env reference release
destroys its plugin and EAL after all children have retired.

The Linux key `route0` must resolve. Its queried geometry and routing identity
are independent of the nonempty native `port` name. DPDK resolves that native
Ethernet port exclusively, trusts the configured association, and performs no
second Linux query or Linux/native MTU/MAC equality check. Ring and pcap fixtures
create real dummy Linux Nifs even when packet service uses a simulated wire.
A build without DPDK reports the normal loader error for a missing DPDK module.

DPDK uses the multiplexer’s existing Rx and Tx owners. It submits native mbufs
directly with one driver reference per accepted send. Caller backing stays valid
while its allocation lease is held. The same pools survive stop and restart.
`rxInterval` caps idle parks in microseconds and defaults to 50; zero polls
continuously. Ring forwards mbuf pointers and pcap copies bytes. With AF_PACKET,
the native ingress data room also accommodates the PMD's `framesz` packet capacity,
defaulting to 2,048 bytes. The supplied Linux MTU bounds the caller's frame size.
See [DPDK lifecycle](INTERFACE.md#dpdk-lifecycle) and
[software acceptance commands](itest/README.md#dpdk-software-drivers).

## AF_XDP allocation

Direct allocation is the standalone XDP test path. It uses a single unlinked
Rx pool and an intrusive list of Tx pools. Each pool carries its role, count,
packet size, headroom, tailroom, and alignment.

```cpp
auto rx = xdp->bufPool(Zi::IOBufType::Rx, 8 * queues, mtu + 14);
auto tx = xdp->bufPool(Zi::IOBufType::TxUDP, 16, mtu + 14);
Zi::IOBufPoolList txPools;
txPools.pushNode(tx.ptr());
auto allocated = xdp->alloc(rx, txPools);
if (allocated.is<ZeException>()) {
  ZiLogEvent(ZuMv(allocated).p<ZeException>());
  return 1;
}
if (!mx.start()) return 1;
if (!xdp->start([&rx](uint8_t *packet, unsigned length, ZiRxBurst *) {
  // Consume the raw Ethernet frame, then return its slot.
  rx->free(packet);
})) {
  ZiLogEvent(xdp->error());
  mx.stop();
  return 1;
}
// Drive receive and transmit service. Quiesce owners before stopping XDP.
xdp->stop();
xdp->final();
xdp = nullptr;
mx.stop();
auto finalized = env->final();
if (finalized.is<ZeException>()) ZiLogEvent(ZuMv(finalized).p<ZeException>());
```

The same allocated arena and packet addresses survive activation failure, stop,
and restart. Return dependent buffers before finalization. Standalone callers
own that ordering, including when they allocate and never start. `final()` is
idempotent while stopped and the destructor also calls it.

Allocate with `txPool.alloc()`, construct the complete Ethernet packet directly
in that backing and call `xdp->send(packet, length)`. A false return leaves
ownership with the caller. A true return publishes a descriptor and holds a kernel
lease; keep bytes immutable until kernel retirement. Explicitly return the caller's
allocation lease with `pool.free(packet)`. Recycling waits for both leases to end,
so return may precede CQ. Kernel completion does not prove successful wire delivery.
Exhaustion returns null and never allocates a replacement.
A shared pending-descriptor counter coalesces sender work. The producer increments
it before descriptor commit and schedules the assigned slot with `run()` after
commit on a zero-to-one transition. That slot calls nonblocking `sendto()`, tracks
kernel consumer progress, and spins for `spin` idle rounds before returning to
the scheduler. Atomic subtraction of consumed descriptors preserves a concurrent
publication. No Tx epoll, eventfd or wait timeout is used; Rx retains its separate
epoll/eventfd service. CQ retirement and backing release remain on the Tx owner.

Rx callbacks likewise own their packet until they return it to the Rx pool.
An advanced Rx wire pointer can be passed directly to `rxPool->free()`.

One descriptor may be outstanding for a slot. A caller can retain its allocation
lease past CQ and resubmit the same backing, then return it when transport recovery
permits. `pollTx()` returns the number of kernel submissions retired and releases
their kernel leases; there is no completion callback or application packet object.

## AF_XDP arenas and service

`Nif::allQueues()` enumerates the actual RX, FILL, TX and completion ring
objects as `Ztc::Queue`. The installed Zutcp plugin forwards these through the
multiplexer's existing queue manager. Keys use the scheduler ID and
`xdp.<ifindex>.<hardware-queue>.rx`, `.fill`, `.tx` or `.cq`. Queue type describes
the userspace direction: RX/completion are `Rx`, TX/FILL are `Tx`.

Capacity and occupancy use descriptor slots. Input/output counts expose the
kernel's wrapping 32-bit producer/consumer positions; byte counts are unavailable.
Live occupancy is advisory and bounded to capacity when cursor reads race.
`full` counts failed capacity reservations for TX/FILL and reports the kernel's
`rx_ring_full` statistic for RX, queried only during telemetry collection. The
normalized 32-bit RX counter saturates at its maximum. Completion `full` remains
zero by Zpkt's admission invariant: queue zero's CQ capacity is at least the sum
of all physical Tx pool slots, including control pools, and each slot permits
only one descriptor until its CQ entry is consumed. Other hardware queues have
no Tx submissions. Linux exposes no separate CQ reservation-refusal counter;
the zero is a structural bound, not a measured kernel refusal total. A zero RX
value also applies when the statistics query is unsupported or fails.

Enumeration and mapping retirement share a cold lifetime lock. Read telemetry
synchronously inside the callback. Active instances expose RX and FILL per queue,
plus TX and CQ on queue zero. Stopped instances expose zero active queues.

Setup reserves one contiguous registered UMEM mapping containing disjoint,
page-aligned Rx and Tx arena regions. Rx has one pool; Tx pools partition its
region according to each count and size, rounded for alignment. The recycling
slist overlays free payload exactly as in non-sharded ZmHeap arenas. Pools retain
the mapping through their last packet return. Packet bookkeeping, direct page
ownership lookup and active slot arrays are allocated during setup.

`rx()` runs on the caller's Rx owner. It processes packets and invokes the
receive callback until explicitly woken. On idle input it probes the mapped
rings for configured `spin` rounds (default 4096), then blocks in epoll on packet
readiness and an internal eventfd. A round probes the configured Rx queues;
zero spin goes directly to the wait after checking available work. Receiving a
packet or reposting a returned frame restarts the idle budget. Packet
arrival resumes processing inside the same call, rather than returning it.

`wake()` may run on any thread, including the receive callback. It records a
counted pulse and posts to the internal eventfd only when Rx is entering or
inside epoll, interrupting spinning, blocking or active processing so
`rx()` returns. Each posted wake permits one return, including wakes before
entry; concurrent posts are preserved. Kernel wait descriptors remain private.
`pollTx()` services bounded completion turns on the caller's Tx owner.

Allocation and return can run on other threads. Each interface Rx queue has a
reserved share of the common ingress pool. Before blocking, Rx arms a one-shot
return notification if the pool is empty and a queue has at most half its share
of frames still posted. It rechecks the pool after arming. The first returned
frame restarts FILL service. After any queue runs dry, refill distributes one
frame per empty queue in round-robin order before restocking busy queues.
Healthy queues replenish on ordinary receive turns. Wake and drain
the receiver and quiesce Tx and wake callers before stop.

Stop withdraws ingress destinations, drains callbacks and accepted submissions,
and closes the per-run sockets and their RX/TX mappings. Persistent queue owners
keep their FILL/CQ rings and their share of posted frames for restart. They share
a single UMEM registration whose owning FD closes during finalization.
Guarded attachment preserves unrelated XDP programs. A failed activation uses
the same cleanup path and returns its originating error through `error()`.

## Verification

Build the configured dependency source layers before zpkt's `src`, `xdp`,
configured `dpdk`, and dependent targets; run `make -C zpkt/test test` for
unprivileged tests. For this refactor follow the restricted [rebuild protocol](../zpkt_plugin_plan.md#rebuild-protocol-and-scope)
with one eight-job jobserver; do not traverse unrelated modules. See [integration tests](itest/README.md) for scoped privileged
kernel tests and [utilities](util/README.md) for independent packet processes.
The implementation targets Linux AF_XDP with single-frame packets fitting one
kernel chunk. Physical NIC zero-copy requires driver support and separate hardware
validation; veth verifies software-copy operation only.

### Breaking rename verification

The directory, module, public headers, namespace, export macros, utilities,
fixtures and dependent build inputs use the new names. The shared libraries are
`libZpkt-10.0.0.so`, `libZpkt_xdp.so` and `libZpkt_dpdk.so`; both backend modules
export `ZpktPlugin`. Callers must rebuild against this API.

Verification retained the configured Clang release build and enabled DPDK,
using the scoped eight-job rebuild protocol:

- Packet unit tests: 8 programs, 20 TAP tests; all 13 native packet test commands
  passed, including AF_XDP and DPDK fixtures.
- Zutcp: 23 unit programs / 330 tests, 7 native programs / 97 tests,
  36 kernel interoperability cases, four IPv4/IPv6 UDP and mixed profiles,
  and 52 DPDK assertions passed.
- FIX: default and kernel suites passed; AF_XDP TCP passed 125 tests and TLS
  passed 133 tests. The canonical interoperability matrix passed all 76 cases.
- Six functional benchmark checks covered kernel, AF_XDP and DPDK over IPv4
  and IPv6. All 12 endpoints delivered exactly 110 messages; these checks
  do not establish performance comparisons.
- A staged installation supplied exactly four public headers. An external
  consumer compiled and ran against the staged API, and each staged backend
  loaded independently with the other backend absent. ELF SONAMEs, exports
  and dependent linkage passed the naming audit.
- Memcheck on `ZpktEnvTest` reported zero errors and no definitely, indirectly
  or possibly lost memory.
