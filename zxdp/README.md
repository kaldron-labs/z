# Zxdp: Linux packet I/O

Zxdp supplies raw Ethernet packet service over AF_XDP and optional DPDK.
The [interface contract](INTERFACE.md) defines its boundary. The caller supplies
transport state, application adaptation and scheduling.
Include `<zlib/Zxdp.hh>` and link `libZxdp`, `libZi` and their framework dependencies.
For Zi provider and transport integration see [Zutcp](../zutcp/README.md) and its
[kernel/native quickstart](../zutcp/util/README.md), including sudo, interface
ownership, routes and static neighbors.
Both modules are Linux-only and are excluded from Windows builds.

## DPDK

On Linux, `z.config` enables DPDK when configuring the tree. It requires
`libdpdk >= 25.11` in pkg-config. Pass `-K` for an AF_XDP-only build. For
direct `configure` invocations, enable the feature with `--with-dpdk=yes`.

Include `<zlib/ZxdpDpdk.hh>` and initialize EAL explicitly before selecting the
backend. The aggregate EAL configuration owns its strings and token lists.
This example selects a software ring device and a single EAL lcore on an allowed
CPU while preserving the initializing thread's affinity.

```cpp
auto eal = Zxdp::Dpdk::EAL::instance();
Zxdp::Dpdk::EALParams ealParams;
ealParams.noPCI = ealParams.noHuge = ealParams.noShconf = true;
ealParams.noTelemetry = true;
ealParams.vdev.push("net_ring0");
auto initialized = eal->init(ealParams);
if (initialized.is<ZeException>()) {
  ZiLogEvent(ZuMv(initialized).p<ZeException>());
  return 1;
}

auto device = Zxdp::instance(Zxdp::Type::Dpdk);
Zxdp::Params config{Zxdp::Dpdk::Params{.name = "net_ring0"}};
auto opened = device->init(&mx, config);
if (opened.is<ZeException>()) {
  ZiLogEvent(ZuMv(opened).p<ZeException>());
  return 1;
}
device->status();
```

The DPDK factory throws `ZeException` if the backend is omitted or EAL is
uninitialized. Open the named Ethernet device on a stopped multiplexer. For
Zutcp, provide a Linux control-plane `ifindex` with matching MTU and MAC. The
standalone software packet fixtures can omit the link.

DPDK uses the multiplexer’s existing Rx and Tx owners. It submits native mbufs
directly with one driver reference per accepted send. Caller backing stays valid
while its allocation lease is held. The same pools survive stop and restart.
`rxInterval` caps idle parks in microseconds and defaults to 50. Set it to zero
to poll continuously. Ring forwards mbuf pointers and pcap copies bytes.
With the AF_PACKET PMD, the native ingress data room also accommodates the
packet capacity of the configured `framesz`. When omitted, the PMD's default
slot size is 2,048 bytes. The device MTU still bounds the caller's frame size.
See the [DPDK lifecycle](INTERFACE.md#dpdk-lifecycle) and
[software acceptance commands](itest/README.md#dpdk-software-fixtures).

## AF_XDP interface

Configure the sender slot in the multiplexer before constructing it. Open the
selected XDP link synchronously while the multiplexer is stopped.

```cpp
ZiMultiplex mx{ZiMxParams{}.scheduler([](auto &scheduler) {
  scheduler.thread(3, [](auto &sender) { sender.isolated(true); });
})};
auto xdp = Zxdp::instance(Zxdp::Type::Xdp);
Zxdp::Params config(
  Zxdp::Params::New<Zxdp::Params::Index<Zxdp::Xdp::Params>{}>{});
auto &params = config.p<Zxdp::Xdp::Params>();
params.ifindex = ifindex;
params.txSid = 3;
auto opened = xdp->init(&mx, config);
if (opened.is<ZeException>()) {
  ZiLogEvent(ZuMv(opened).p<ZeException>());
  return 1;
}
auto mtu = xdp->mtu();
auto queues = xdp->rxQueues();
```

`ifindex()` and `mac()` expose the interface identity after initialization.
The optional `netmask` field selects a destination IPv4 or IPv6 CIDR. An empty
mask selects all traffic, including non-IP frames. The classifier handles Ethernet
and up to two VLAN tags. `spin` configures idle probes before Rx blocks or the
sender returns to its scheduler.

An application using Zutcp moves this initialized, unallocated, never-started XDP
instance into TCP with `tcp->init(&mx, ZuMv(xdp), tcpParams)`. After successful
initialization, move TCP into `mx.plugin(ZuMv(tcp))`, request allocator handles,
and allocate through `mx.alloc()` or implicitly through `mx.start()`. TCP drives
XDP allocation, start, stop, and finalization under the multiplexer lifecycle.
See the full [Zutcp integration](../zutcp/README.md#application-integration).

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
if (!xdp->start([&rx](uint8_t *packet, unsigned length) {
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
mx.stop();
```

The same allocated arena and packet addresses survive activation failure, stop,
and restart. Return dependent buffers before finalization. Standalone callers
own that ordering, including when they allocate and never start. `final()` is
idempotent while stopped and the destructor also calls it.

Allocate with `txPool.alloc()`, construct the complete Ethernet packet directly
in that backing and call `instance->send(packet, length)`. A false return leaves
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

## Arenas and service

`Instance::allQueues()` enumerates the actual RX, FILL, TX and completion ring
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
zero by Zxdp's admission invariant: queue zero's CQ capacity is at least the sum
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
rings for `Xdp::Params::spin` rounds (default 1000), then blocks in epoll on packet
readiness and an internal eventfd. A round probes the configured Rx queues;
zero spin goes directly to the wait after checking available work. Packet
arrival resumes processing inside the same call, rather than returning it.

`wake()` may run on any thread, including the receive callback. It records a
counted pulse and posts to the internal eventfd only when Rx is entering or
inside epoll, interrupting spinning, blocking or active processing so
`rx()` returns. Each posted wake permits one return, including wakes before
entry; concurrent posts are preserved. Kernel wait descriptors remain private.
`pollTx()` services bounded completion turns on the caller's Tx owner.

Allocation and return can run on other threads. Each interface Rx queue has a
reserved share of the common ingress pool. Returning a frame off the Rx owner
coalesces a wakeup so the active receiver can replenish FILL. Wake and drain
the receiver and quiesce Tx and wake callers before stop.

Stop withdraws ingress destinations, drains callbacks and accepted submissions,
and closes the per-run sockets and their RX/TX mappings. Persistent queue owners
keep their FILL/CQ rings and their share of posted frames for restart. They share
a single UMEM registration whose owning FD closes during finalization.
Guarded attachment preserves unrelated XDP programs. A failed activation uses
the same cleanup path and returns its originating error through `error()`.

## Verification

Build the configured tree with `make -j8`; run `make -C zxdp/test test` for
unprivileged tests. See [integration tests](itest/README.md) for scoped privileged
kernel tests and [utilities](util/README.md) for independent packet processes.
The implementation targets Linux AF_XDP with single-frame packets fitting one
kernel chunk. Physical NIC zero-copy requires driver support and separate hardware
validation; veth verifies software-copy operation only.
