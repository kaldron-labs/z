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

All arenas, ring mappings and packet bookkeeping are allocated during setup.
Packet service performs no heap allocation. Allocation and return use the
agreed non-sharded ZmHeap payload-overlay slist. Packet admission, receive and
completion resolve backing directly; they never search packets or buffers.

The receive entry is `rx()`: process incoming packets through the configured
callback, spin for a configurable count when idle, then use the most efficient
suitable kernel blocking mechanism. An epoll implementation waits on packet
readiness and an internal `eventfd`, following ZiEventLoop/ZiMultiplex. Packet
readiness resumes processing; the call returns when explicitly woken. Expose
`wake()` for Zutcp to interrupt the spinning or blocked receive loop and cause
`rx()` to return. With epoll, `wake()` posts to that `eventfd`; no per-wakeup
object is allocated. Wake observation must remain prompt during continuous traffic
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
