# Backend boundary

This is the implementation contract supplied by the caller. It supersedes the
earlier expanded interface in this module.

Instance creation takes named parameters:

* Network interface.
* Optional network mask for packet selection. Without a mask, XDP takes over
  all traffic on the interface.
* Head of an intrusive list of Tx pools. Each pool supplies count and packet
  size. Provision the Tx arena from that list and initialize one recycling
  free list per pool.
* One Rx pool, with its own dedicated Rx arena.
* Raw packet receive callback.

There is no transmit-completion callback. Allocation holds one caller lease;
accepted submission holds a separate kernel lease. Explicit pool return releases
the caller lease, and kernel CQ or safe close releases the kernel lease. Recycle
only when both are gone. Return may precede CQ; a caller can also retain backing
past CQ for retransmission and return it later. Rejected send adds no kernel lease.
One descriptor may be outstanding for an allocated slot at a time.

`BufPool::writable(packet)` is a Tx-owner query with the caller allocation
lease held. It reads the existing direct-slot lease state and becomes true
only after kernel use retires. Use it before changing retained packet headers
for another submission. It acquires no lease and supplies no callback, object
association or additional bookkeeping. The caller must serialize the query,
header writes and send on its Tx owner.

The packet operations are allocation from a Tx pool, return to an Rx or Tx pool,
and submission of a Tx packet. Application and transport object ownership is
outside this module. The caller owns adapter composition.

`send(packet, length, commit)` optionally accepts a borrowed synchronous
`CommitFn` (`ZmFn<bool()>`). After every ordinary admission check and a successful
Tx-ring reservation, the sole producer invokes it before changing slot ownership,
adding the kernel lease, or publishing the descriptor. False cancels the reservation
and returns false with no lease or descriptor. True commits the initialized
descriptor with no further refusal. This is a publication hook, not a completion
callback. It must not throw, retain its borrowed context, reenter Tx service, or
perform asynchronous work. Bind a raw context pointer and `ZmFnPtr` to avoid
allocation; a context on the caller's stack remains live through the synchronous
call. Release-publish any caller state before returning true so a peer response
cannot precede that state after kernel consumption. Empty hooks preserve raw send.

`send()` submits and coalesces sender service on the application-assigned
`Params::txSid`. Before scheduler startup the application configures that slot
with `Params::txThread`, a `ZmSchedTParams` defaulting to `isolated(true)`.
The dedicated sender calls nonblocking `sendto()` to drive kernel Tx processing.
The caller remains the only descriptor producer and CQ owner. Descriptor
consumption is separate from CQ retirement and never authorizes backing reuse.

Submission increments a shared pending-descriptor counter before committing the
initialized descriptor. A zero-to-one transition schedules sender service with
`scheduler->run()` after commit. The active sender observes the kernel consumer
cursor and tracks its unsigned delta from a persistent cursor. While no additional
pending descriptors are visible beyond that delta, it spins for `Params::spin`
rounds and checks shutdown. It then subtracts the observed delta atomically and
uses the returned count to decide whether to continue. A racing publication is
therefore retained in the count or starts a new scheduled invocation. There is
no intentional accumulation delay and no Tx wait timeout.

Tx service uses no custom scheduler wake function, epoll set or eventfd. The
scheduler owns idle waiting after the sender returns. Rx retains its independent
epoll/eventfd implementation described below.
Start the scheduler before submitting packets and drain the instance before
stopping the scheduler. After Tx production is quiescent, `stopSender()` publishes
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
