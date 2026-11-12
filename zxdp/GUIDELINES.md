# Zxdp implementation rules

Follow the repository guidance in ../AGENTS.md and the caller-supplied
[interface contract](INTERFACE.md). The contract supersedes earlier plans.

* Creation takes named parameters: interface, optional destination network mask,
  intrusive Tx pool list with count and size, one dedicated Rx pool, receive
  callback. No transmit-completion callback. Without a mask all traffic is selected.
* Keep allocation and kernel leases distinct in preallocated slot bookkeeping.
  Explicit pool return may run before or after CQ. Recycle only after both leases
  end; safe close releases kernel leases while caller-held backing stays valid.
  One descriptor may be outstanding for a slot. Rejected send acquires no lease.
* Application and transport adaptation belongs to the caller. This library owns
  arena provisioning, raw packet allocation/return and kernel ring service.
* Preallocate arenas and all packet bookkeeping at setup. Recycle each pool with
  the non-sharded ZmHeap payload-overlay slist. Exhaustion returns null.
* Packet lookup and completion use direct slot identity, with constant-time
  updates. No packet/buffer searches or hot-path heap allocations.
* Query kernel ring mapping offsets and preserve SWSR ownership. The caller
  provides one Rx service owner and one Tx service owner; final pool return may
  run on any thread. Keep thread-exclusive mutable state on separate cache lines.
* Construct payload directly in registered backing. No payload staging or copy.
* Quiesce service before close. Retire kernel access before returning submitted
  backing. A caller-owned pool retains its arena through the last packet return.
* Queue configuration and mask compilation are cold-path operations.
* Own the required `rx()`/`wake()` pair: spin for a configurable count before
  blocking with the most efficient suitable kernel mechanism; receive packets
  until explicitly woken, then return. With epoll, post wakeups through an
  internal `eventfd`, following ZiEventLoop/ZiMultiplex, with no per-wakeup
  allocation. Own these descriptors and their lifecycle. Wake
  must interrupt spinning and blocking promptly, including under continuous
  traffic and at the transition into the wait. Internal batches do not cause
  `rx()` to return on their own. The caller owns scheduling and lifecycle retries.
