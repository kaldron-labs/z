# Zxdp

Zxdp supplies raw AF_XDP packet arenas, intrusive pools and kernel ring service.
The [interface contract](INTERFACE.md) defines its boundary. The caller supplies
transport state, application adaptation and scheduling.

## Interface

Include `Zxdp.hh`. Create an `Instance` with named `Params` members:

* `interface`: network-interface index in the current network namespace.
* `netmask`: optional destination IPv4/IPv6 CIDR. An empty mask selects all
  interface traffic, including non-IP frames. Unmatched traffic passes to the
  kernel. The classifier handles Ethernet and up to two VLAN tags.
* `txPools`: head of the caller-owned intrusive `BufPool::next` list. Each pool
  supplies `count` and maximum packet `size`.
* `rxPool`: one nonempty caller-owned pool with a dedicated Rx region.
* `rx`: callback receiving the raw packet address and length.
* The author has removed `CompleteFn`/`txCompleted` from the target API; the
  existing completion-callback implementation is pending migration.
* `spin`: idle probe rounds before the receive loop blocks (default 1000).

Allocate with `txPool.alloc()`, construct the complete Ethernet packet directly
in that backing and call `instance->send(packet, length)`. A false return leaves
ownership with the caller. A true return publishes a descriptor; keep its backing
alive and immutable until completion. Completion returns kernel ownership, not
proof of successful wire transmission. Explicitly return backing with
`pool.free(packet)`. Exhaustion returns null and never allocates a replacement.
Rx callbacks likewise own their packet until they return it to the Rx pool.
An advanced Rx wire pointer can be passed directly to `rxPool.free()`.

Repeated immutable Tx submissions may share backing. Count accepted submissions
and completions before recycling it. Normal completion reports each retired
submission; close may coalesce remaining submissions of one backing. The library
creates no application packet objects.

## Arenas and service

Setup reserves one contiguous registered UMEM mapping containing disjoint,
page-aligned Rx and Tx arena regions. Rx has one pool; Tx pools partition its
region according to each count and size, rounded for alignment. The recycling
slist overlays free payload exactly as in non-sharded ZmHeap arenas. Pools retain
the mapping through their last packet return. Packet bookkeeping, direct page
ownership lookup and active slot arrays are allocated during setup.

`rx()` runs on the caller's Rx owner. It processes packets and invokes the
receive callback until explicitly woken. On idle input it probes the mapped
rings for `Params::spin` rounds (default 1000), then blocks in epoll on packet
readiness and an internal eventfd. A round probes the configured Rx queues;
zero spin goes directly to the wait after checking available work. Packet
arrival resumes processing inside the same call, rather than returning it.

`wake()` may run on any thread, including the receive callback. It posts to the
internal eventfd and interrupts spinning, blocking or active processing so
`rx()` returns. Each posted wake permits one return, including wakes before
entry; concurrent posts are preserved. Kernel wait descriptors remain private.
`pollTx()` services bounded completion turns on the caller's Tx owner.

Allocation and return can run on other threads. Every interface Rx queue is bound
to the shared registration and has a reserved share of the Rx pool. Tx uses queue
zero. After an off-owner return to a depleted pool, wake the receive owner and
arrange another `rx()` entry to replenish input. All packet service and wake
operations use preallocated state; they perform no heap allocation or packet/
buffer searches. Wake/join the receiver and quiesce Tx and wake callers before
close.

Creation and close are control-path operations. Quiesce both service owners before
calling `close(error)`. False with an empty error means retry is pending; an error
retains the handle for explicit recovery/retry. Submitted backing is returned only
after kernel queue retirement has been observed. Continue close until successful,
then release the instance. Caller-owned pools outlive the instance and all
outstanding raw packets. Freeze their configuration while bound.

Creation normally returns null on failure. If kernel rollback is still pending,
it returns a non-ready handle with an error; retain that handle and finish close.
The interface must have no unrelated XDP attachment. Guarded attach/detach protects
other owners, including changes concurrent with control operations.

## Verification

Build the configured tree with `make -j8`; run `make -C zxdp/test test` for
unprivileged tests. See [integration tests](itest/README.md) for scoped privileged
kernel tests and [utilities](util/README.md) for independent packet processes.
The implementation targets Linux AF_XDP with single-frame packets fitting one
kernel chunk. Physical NIC zero-copy requires driver support and separate hardware
validation; veth verifies software-copy operation only.
