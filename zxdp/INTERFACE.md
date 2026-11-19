# XDP lifecycle and ownership

Configure the multiplexer and its sender slot before construction. Select XDP
with `Zxdp::instance(Zxdp::Type::Xdp)` and initialize it synchronously with
`init(&mx, config)`. Construct the `Xdp::Params` payload in `Zxdp::Params`.
Set `ifindex`, `txSid`, and the optional destination `netmask` and idle
`spin`. The sender slot belongs to the multiplexer and is distinct from its
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
