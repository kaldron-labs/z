# Zpkt implementation rules

Follow the repository guidance in ../AGENTS.md and the caller-supplied
[interface contract](INTERFACE.md). The contract supersedes earlier plans.

* Applications retain an intrusive `Env` before creating Nifs. `Env::init(cf)`
  loads a configured plugin and its owned typed interface map. DPDK Plugin owns
  EAL directly. Opening happens only in `Env::nif(mx, name)`, which returns an
  initialized, stopped, unallocated Nif. Claim a DPDK port through finalization;
  allocate the whole immutable pool set transactionally and retain it across runs.
* Serialize Env setup and finalization. Env owns a unique name-keyed tree of
  Nif references; each child has a raw Env pointer. A same-name/multiplexer hit
  returns the existing Nif without querying or reopening, including while running.
  Env has no engine or start/stop API. Stop/drain children and return buffers
  before Env finalization; release external child references before Env destruction.
* Core Nif query and shared rtnetlink transport belong in `src`. AF_XDP-only
  sockets, UMEM, BPF attachment and pools belong in `xdp`; DPDK native resources,
  EAL and pools belong in `dpdk`. These module-specific source directories build
  `libZpkt_xdp` and optional `libZpkt_dpdk` after common `libZpkt`.
* Production callers link common Zpkt and load their chosen backend by module
  path. Backend-private tests may link one backend explicitly. Module code remains
  resident after the initialization entry point has been invoked.
* DPDK uses the existing Rx and Tx owners. Keep caller and driver leases
  distinct. Account for completion only when the driver relinquishes its
  mbuf reference. Stop with the scheduler running so its owners can quiesce.
* The stock ring PMD leaves its rings with the fixture. The fixture copies
  across its simulated wire into an mbuf from the destination's Rx pool.
  Quiesce the software producer before draining that queue.

## TCP checksum offload

* Discover device TCP checksum offload through DPDK / AF_XDP and enable it when
  supported. `tcpChksumOffload()` reports active offload after start, including
  configuration fallback; it does not merely report advertised capability.
* Zpkt implements no software TCP checksum. The caller finalizes checksums when
  offload is unavailable and supplies the pseudo-header seed when it is active.
  All output uses plain `send()`. DPDK installs native header lengths/flags and
  excludes Ethernet padding from the offloaded extent; clear stale metadata on
  every submission/refusal. AF_XDP queries netdev XSK checksum capability and
  enables UMEM Tx metadata when supported. Report true only for registered
  metadata on native zero-copy service with current driver support. Reserve a
  private aligned prefix without consuming configured packet capacity or
  head/tail room; disable metadata if it would exceed the kernel page limit.
  Registration fallback preserves software output in the caller. Never request
  `XDP_UMEM_TX_SW_CSUM` in the library. Reset descriptor options on every
  accepted submission; refuse before touching metadata or caller bytes.

## AF_XDP

* Configure the sender slot before constructing the multiplexer and include its
  nonzero `txSid` in the named interface Cf. `env->nif(mx, name)` validates that
  slot and opens the link synchronously with core-supplied NifInfo. Retain link
  resources until finalization. There is no Nif initialization method.
* Provision one registered arena from the common Rx pool and the typed Tx
  pools. Preserve intrusive membership in the caller's list and retain an
  independent reference per pool. Keep backing and registration across
  activation failures and stop/restart. Finalization requires stopped service
  and returned application buffers.
* Follow ZmEngine's control FSM, including queued opposite requests. Keep the
  multiplexer scheduler running through stop. Quiesce caller service, withdraw
  ingress destinations, drain the sender and completion work, and release the
  run's mappings and callbacks. Persistent owners retain each tuple's FILL/CQ
  and kernel-owned ingress frames until finalization.
* Keep allocation and kernel leases distinct in preallocated slot bookkeeping.
  Explicit pool return may run before or after CQ. Recycle only after both leases
  end; safe stop releases kernel leases while caller-held backing stays valid.
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
* The application assigns a sender `txSid` in its multiplexer configuration and
  configures the slot through its scheduler parameters.
  The scheduler owns thread startup, waiting and teardown. Zpkt owns no sender
  thread or semaphore. Publish initialized descriptors immediately: increment
  the pending-descriptor counter before commit and use scheduler `run()` after
  commit only on a zero-to-one transition. The dedicated sender observes the
  kernel consumer delta, spins for the configured `spin` when pending work is
  exhausted, then atomically subtracts that delta. A zero result relinquishes
  ownership. While active, recheck the published descriptors and the kernel
  NEED_WAKEUP flag before each nonblocking `sendto()`.
  Keep a persistent consumer cursor; never count the same consumption twice.
  Tx service has no custom wake function, epoll/eventfd or wait timeout. Rx keeps
  its epoll/eventfd wake protocol. The caller remains the sole descriptor
  producer and CQ owner; consumption accounting does not release kernel leases.
  Check exit during service and idle spin, then drain sender callbacks with a
  FIFO continuation before engine stop retires sockets.
* Construct payload directly in registered backing. No payload staging or copy.
* `txReady(packet)` reads existing slot submission state on the Tx owner before
  retained header re-encoding. It reserves nothing and adds no lease or slot state.
  Encoding and the subsequent synchronous send must stay on that owner without an
  intervening submission of the slot. Raw send retains its own admission checks.
* Recycle submitted backing after its CQ retires the kernel lease. Reuse the
  same allocated arena across runs and return caller-held leases before final.
* Queue configuration and redirect program loading are cold-path operations.
* Own the required `rx()`/`wake()` pair: spin for a configurable count before
  blocking with the most efficient suitable kernel mechanism; receive packets
  until explicitly woken, then return. With epoll, post wakeups through an
  internal `eventfd` when Rx has published a wait attempt, following
  ZiEventLoop/ZiMultiplex, with no per-wakeup allocation. Active and preposted
  wake pulses remain in userspace. Own these descriptors and their lifecycle. Wake
  must interrupt spinning and blocking promptly, including under continuous
  traffic and at the transition into the wait. Internal batches do not cause
  `rx()` to return on their own. The caller owns scheduling and lifecycle retries.
