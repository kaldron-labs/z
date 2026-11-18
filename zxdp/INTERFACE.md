# Backend boundary

This is the implementation contract supplied by the caller. It supersedes the
earlier expanded interface in this module.

Select the cold backend with `Zxdp::instance(Zxdp::Type::Xdp)`. `Instance` is
a polymorphic interface; its virtual `init(const Params &)` dispatches a tagged
union of backend configurations. Construct its payload in place:

```cpp
Zxdp::Params config(
  Zxdp::Params::New<Zxdp::Params::Index<Zxdp::Xdp::Params>{}>{});
auto &params = config.p<Zxdp::Xdp::Params>();
```

`Xdp::Params` inherits common `Params_` fields
and adds `ifindex`, `netmask`, `txSid` and `txThread`. Assign fields directly.
Initialization takes:

* Network interface index (`Xdp::Params::ifindex`).
* Optional network mask for packet selection. Without a mask, XDP takes over
  all traffic on the interface.
* An owning `Zi::IOBufPoolList` containing at least one nonempty Tx pool. Each pool supplies
  count, packet size and alignment. Provision the Tx arena from that list and
  initialize one recycling free list per pool.
* One unlinked `ZmRef<Zi::IOBufPool>` Rx pool, with its own dedicated Rx arena.
* Raw packet receive callback.

Zxdp depends on Zi. `instance->bufPool(count, size, headRoom, tailRoom, align)`
returns `ZmRef<Zi::IOBufPool>` implementing allocation/return over registered
arena slots. The concrete implementation is internal; generic Zi pools cannot
provide the required arena bookkeeping. Headroom and tailroom default to zero,
alignment to 64 bytes. Use the same pool directly with Zi's typed metadata
allocator. Add nodes to `Params_::txPools` with `pushNode()`, or move in an existing
list. A node belongs to one owning list at a time. Populate `Xdp::Params` in the
generic `Params` union and pass that union by const reference to init. The backend
retains one reference per pool through final; intrusive membership remains only
in the caller configuration list.
Freeze pool geometry through active service. Retain a pool reference for packets
that outlive the instance.

The lifecycle follows `ZmEngine`: call `init(config)` before `start()`, use
`start()`/`stop()` for I/O control, then call `final()` after stop. Init and final
are idempotent while stopped. Calling init while starting, running or stopping
returns a structured error. Init stores configuration and pool references; it
does not activate the interface or allocate a registered arena.

`start()` opens the arena, rings and guarded XDP attachment. It requires the
assigned scheduler to be running. Both synchronous and callback start/stop forms
and `state()` come from `ZmEngine`, including StartPending/StopPending handling.
Use callback operations from a service thread; synchronous calls belong to an
external control caller. `error()` preserves a failed start/stop's structured
cause after completion. Failed start completes only after rollback is safe.

Quiesce receive, descriptor production, completion service and external wake
callers before stop. Stop drains the dedicated sender and proves kernel
retirement before reporting Stopped. Pending retirement runs in bounded
continuations on the assigned sender slot; it never authorizes early backing
release. Keep the scheduler running until stop completes.

Stop retains configuration and final ring telemetry. A subsequent start reuses
the same handle and configuration, requiring unused pools; retained caller
backing must be returned before its pool can bind another arena. Final releases
stopped storage, callbacks and configuration, and may run from the destructor.
Final does not implicitly stop a running instance. Retain pool references for
raw backing that outlives final or destruction, and serialize init/final against
start/stop requests.

`init(config)` returns `ZuUnion<void, ZeException>`; start/stop return bool or
deliver their result through the standard engine callbacks. There is no public
close/retry loop.

There is no transmit-completion callback. Allocation holds one caller lease;
accepted submission holds a separate kernel lease. Explicit pool return releases
the caller lease, and kernel CQ or safe stop releases the kernel lease. Recycle
only when both are gone. Return may precede CQ; a caller can also retain backing
past CQ for retransmission and return it later. Rejected send adds no kernel lease.
One descriptor may be outstanding for an allocated slot at a time.

The packet operations are allocation from a Tx pool, return to an Rx or Tx pool,
and submission of a Tx packet. Application and transport object ownership is
outside this module. The caller owns adapter composition.

`bool send(uint8_t *packet, unsigned length)` returns true for accepted
submission. False acquires no kernel lease, publishes no descriptor and leaves
backing with the caller. After true the packet must remain immutable until
kernel retirement; the independent caller lease may be returned before or after
that retirement.

`send()` submits and coalesces sender service on the application-assigned
`Xdp::Params::txSid`. Before scheduler startup the application configures that slot
with `Xdp::Params::txThread`, a `ZmSchedTParams` defaulting to `isolated(true)`.
The dedicated sender calls nonblocking `sendto()` to drive kernel Tx processing.
The caller remains the only descriptor producer and CQ owner. Descriptor
consumption is separate from CQ retirement and never authorizes backing reuse.

Submission increments a shared pending-descriptor counter before committing the
initialized descriptor. A zero-to-one transition schedules sender service with
`scheduler->run()` after commit. The active sender observes the kernel consumer
cursor and tracks its unsigned delta from a persistent cursor. While no additional
pending descriptors are visible beyond that delta, it spins for `Params_::spin`
rounds and checks shutdown. It then subtracts the observed delta atomically and
uses the returned count to decide whether to continue. A racing publication is
therefore retained in the count or starts a new scheduled invocation. There is
no intentional accumulation delay and no Tx wait timeout.

Tx service uses no custom scheduler wake function, epoll set or eventfd. The
scheduler owns idle waiting after the sender returns. Rx retains its independent
epoll/eventfd implementation described below.
Start the scheduler before submitting packets and drain the instance before
stopping the scheduler. After Tx production is quiescent, the internal sender barrier publishes
exit and queues a FIFO sender-slot barrier. The active loop checks exit, including
during idle spin; the barrier clears pending state and marks sender exit before
calling `txStopped`. Socket retirement follows that barrier. The scheduler owns
thread startup and shutdown. Arrange CQ service independently of arrivals.

All arenas, ring mappings and packet bookkeeping are allocated during setup.
Packet storage and metadata are reused during service. Allocation and return use
the agreed non-sharded ZmHeap payload-overlay slist. Normal sender callbacks are
placed directly in the scheduler ring; scheduler overflow can allocate fallback
entries. Packet admission, receive and completion resolve backing directly;
they never search packets or buffers.

The receive entry is `rx()`: process incoming packets through the configured
callback, spin for a configurable count when idle, then use the most efficient
suitable kernel blocking mechanism. An epoll implementation waits on packet
readiness and an internal `eventfd`, following ZiEventLoop/ZiMultiplex. Packet
readiness resumes processing; the call returns when explicitly woken. Expose
`wake()` for Zutcp to interrupt the spinning or blocked receive loop and cause
`rx()` to return. With epoll, `wake()` posts to that `eventfd` only while Rx has
published a wait attempt; active and preposted wake pulses stay in userspace.
No per-wakeup object is allocated. Wake observation must remain prompt during continuous traffic
and must not be lost at the transition into epoll. The wait mechanism and its
internal descriptors belong to this library; caller scheduling remains outside.

Zutcp delegates its plugin `rx()`/`wake()` hooks to this pair. Internal driver
NEED_WAKEUP handling and safe kernel retirement also remain in this library.
These facilities must not grow into a second transport or application runtime.
The public receive/wake pair owns the eventfd/epoll mechanism; raw readiness
descriptors and a nonblocking receive-poll entry are not exposed.

Current receive/wake validation covers preposted and concurrent pulses, wake
races at epoll entry, continued receive processing after packet readiness, and
wake during active callbacks. Full transport integration and hardware DMA
validation remain separate work.
