# Packet device lifecycle and ownership

## AF_XDP

Configure the multiplexer and its sender slot before construction. Select XDP
with `Zxdp::instance(Zxdp::Type::Xdp)` and initialize it synchronously with
`init(&mx, config)`. Construct the `Xdp::Params` payload in `Zxdp::Params`.
Set `ifindex`, `txSid`, and idle `spin`. AF_XDP redirects all interface traffic,
including non-IP frames. The sender slot belongs to the multiplexer and is distinct from its
Rx and Tx owners.

```cpp
auto opened = xdp->init(&mx, config);
if (opened.is<ZeException>()) {
  ZiLogEvent(ZuMv(opened).p<ZeException>());
  return 1;
}
auto interface = xdp->ifindex();
auto mtu = xdp->mtu();
auto receiveQueues = xdp->rxQueues();
```

Initialization retains the borrowed multiplexer pointer and link resources.
A query or open failure returns a structured error and releases provisional
resources. A repeat with the same effective stopped configuration succeeds
without opening again. A different stopped configuration returns an error and
preserves the original initialization.

## Shared allocation

In applications using Zutcp, move the initialized, unallocated, never-started
XDP instance into TCP. After success, TCP owns XDP lifecycle calls. Move TCP
into the same multiplexer and request its allocator handles. `mx.alloc()`
allocates before start when the application needs native Tx buffers early.
Otherwise `mx.start()` performs that allocation.

Standalone XDP tests may call `xdp->alloc(rxPool, txPools)` directly. Create
the pools with
`bufPool(type, count, size, headRoom, tailRoom, align)` over `Rx`, `TxTCP`,
and `TxUDP`. One common ingress pool supplies receive frames. The intrusive
Tx list may contain multiple pools per role, each with independent geometry.
A transport can also supply internal Tx reserves with `reserveTxPools()`
before backing allocation. XDP retains pool references without changing the
caller list's membership.

XDP maps one contiguous arena and registers it with one AF_XDP socket.
Linux requires a bound UMEM owner and separate FILL/CQ pairs for distinct
device/queue pairs. Queue zero owns the registration. Other queues retain
their own bound owners sharing that registration. Per-run service sockets
share with the owner for their queue. XDP publishes pool backing only after
registration and ring setup succeed. Failure releases provisional resources.
After success, allocation with the same pool set returns success without a
new mapping or registration. A different set returns an error.

## Packet service

Activate with `start(receive)` or `start(receive, txStopped, completed)`
after the multiplexer scheduler starts. The second form reports activation
through a callback. Supply each run's receive callback and optional sender
drain continuation at this point. An activation failure releases any per-run
sockets and mappings and retains the allocation for retry. Inspect `error()`
after start or stop completes.

Call `rx()` from the receive owner. It processes frames until `wake()`
interrupts the loop. The receiver spins up to the configured idle budget and
then blocks on packet readiness and eventfd. A return of an ingress frame
on another thread wakes the parked owner to replenish FILL.

Allocate on a Tx pool and write the complete Ethernet frame directly into
that backing. `send(packet, length)` accepts one outstanding kernel
submission per slot. The sender uses nonblocking `sendto()` on the assigned
multiplexer slot. The producer increments a descriptor count before publish
and schedules the sender on the zero-to-one transition. Descriptor consumption
tracks pending work. `pollTx()` separately retires kernel leases on the Tx
owner. The caller returns its own allocation lease with the original pool.
Recycling waits for both leases.

`bool Instance::tcpChksumOffload()` reports whether the started device is
actually configured for TCP checksum offload. Query it after each start, including
restart. Device capability discovery/enablement belongs to Zxdp; software TCP
checksum calculation belongs to its caller. AF_XDP queries the netdev generic-
netlink XSK checksum capability, provisions a private Tx metadata prefix and
registers its length with UMEM. It reports true only with successful metadata
registration, native zero-copy service and current driver capability. Copy mode,
unsupported registration and pools requiring a full-page frame report false.
DPDK reports its enabled setting after configuration fallback. All frames use
plain `send()`.

When offload is false, supply a completed TCP checksum. When true, TCP frames
must contain the pseudo-header seed; DPDK derives native header lengths/flags
from the Ethernet/IP/TCP headers and leaves caller bytes untouched. It excludes
software Ethernet padding from the mbuf extent; the NIC supplies wire padding.
The native TCP caller emits contiguous IPv4/IPv6 TCP frames. UDP and other raw
protocols retain their existing checksum contract. AF_XDP writes checksum start/
offset metadata before accepted TCP frames and excludes software padding from
the descriptor extent. The private prefix preserves caller capacity, alignment
and configured head/tail room. Every accepted descriptor selects fresh metadata
options; refusal leaves caller bytes and the prefix unchanged. DPDK clears
stale mbuf flags on refusal/reuse. No software fallback exists inside Zxdp.

Before rewriting headers in retained backing, call `txReady(packet)` on the Tx
owner. It checks the existing slot's outstanding-submission state without taking
a lease or reserving ring capacity. A true result permits encoding followed by
synchronous `send()` on that same owner, with no intervening submission of the
slot. Other threads may return their caller leases but cannot submit the slot.
`send()` still owns admission and may refuse for ring/device pressure. Bytes
remain immutable after acceptance until `pollTx()` retires the submission.

## Stop and finalization

The caller quiesces its Rx and Tx service before stop and keeps the multiplexer
scheduler running until completion. Stop blocks new submissions and delivery,
drains queued sender work and Tx completion, and retires the run's callback
references and service sockets. XDP preserves its registered arena, FILL/CQ
mappings, posted frame accounting, and any application-held slots. Restart
opens fresh service sockets and uses the same pools and packet addresses.
If device failure leaves a transmit completion unresolved, its backing stays
accounted for and later activation first checks the retained CQ. If ingress
quiescence cannot establish a kernel grace period, finalization is required
before the backing can be replaced.

Return dependent buffers before finalization. While stopped, `final()`
releases the persistent ring mappings, registration socket, arena and
initialized link resources. This applies after any number of runs and before
the first start. It clears the borrowed multiplexer pointer. Standalone tests
call it while their multiplexer still lives. In an application, multiplexer
finalization drives TCP finalization, and TCP drives XDP finalization.

## DPDK lifecycle

Obtain the platform EAL singleton with `Dpdk::EAL::instance()` and initialize it
once with an owned `Dpdk::EALParams` aggregate. Named fields and raw argv tokens
can be combined when they configure different options. Repeated list values
preserve their order.
Keep the singleton alive until every device instance and dependent pool has
retired. EAL closes its remaining registered ports and cleans up at process
shutdown.

The DPDK device opens a named Ethernet port exclusively on a stopped
multiplexer. A successful open exposes MTU, MAC, the selected Rx queue count
of one, and the configured Linux routing-link identity before any pools exist.
An identical stopped init is idempotent, including the idle interval. A
conflicting init preserves the original configuration.

Provision the common Rx pool and each distinct Tx pool as one unit.
If native allocation fails, retry with the same pool collection. Direct
standalone allocation is idempotent for that collection. Backing has native
mbuf headroom and exact logical capacity for the requested role. Tx pools
may be smaller than an MTU. The PMD can use every Rx mbuf.

Start configures one contiguous-frame Rx queue and one Tx queue. Run `rx()`
and `pollTx()` on their single owners. Call `send()` on the Tx owner with a
pointer within its native backing. Rejected sends preserve caller bytes and
ownership. Accepted sends hold one driver reference until retirement. Keep
bytes immutable until then. The same `txReady(packet)` Tx-owner contract applies
before re-encoding retained backing. `pollTx()` counts each retired submission once,
including when the application retains a separate recovery allocation lease.

The Rx owner checks counted wakes between bursts. It spins through the
configured number of empty probes, then uses a receive interrupt fd when
available and short timed eventfd waits capped by `Dpdk::Params::rxInterval`.
A zero interval keeps polling. Receive callbacks own the received mbuf through
the common Rx pool. Return off the owner coalesces a wake while Rx is parked.

Stop gates new work, drains the Rx callback and services accepted Tx on the
Tx owner while the scheduler is running. A stopped or failed run retains the
opened port and native backing for restart. Return dependent application
buffers before finalization. Finalization releases backing and lets another
instance open the same port.

`allQueues()` enumerates the configured Rx and Tx queues. Consume each view
inside its callback. DPDK counters are available only as supplied by the PMD.
`status()` reports the backend, opened device, link geometry, copy mode and
the current queue setup. Ring uses direct mbuf forwarding. Pcap uses copies
and supports finite capture input in this lifecycle.
