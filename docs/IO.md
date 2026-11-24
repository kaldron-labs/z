# I/O ownership and lifecycle

Part of the repository guidance mapped in [AGENTS.md](AGENTS.md).

## I/O sharding
Every data member involved in I/O rx/tx should have one owning shard:

- Rx-owned state is accessed only on the Rx thread.
- Tx-owned state is accessed only on the Tx thread.
- A function must not access Rx-owned and Tx-owned data in the same thread
  context.
- Cross-shard work is transferred by posting a function to the owning shard,
  not by locking around the non-owning shard's data.

State that is inescapably shared by both rx and tx should be rx-owned,
and tx activity should enqueue mutations; locking/atomics may be permitted,
exceptionally, if the tx side just needs read access without the overhead
of posting work onto the rx thread just to read then posting back again
the remainder

`Zi::IOBufRing` slot allocation and return are explicitly shared, contended
operations using the `ZmHeapCache` MPMC recycling protocol. Final release may
return a slot on any thread. The next pointer overlaps the aligned buffer head
only while the slot is free; there is no per-slot prefix allocation.
SQ/CQ access and provided-buffer publication remain I/O-owner operations.
Transfer the original buffer reference and range across shards. Mutable protocol
state remains sharded. One FIX message is constructed in its own output buffer
on application Tx. Retained shared ranges carry operation-local transmit
progress in `ZiTxContext`; they do not arbitrate mutable descriptor cursors.

`ZiRxContext` exposes available input through its held `buf->length`; it has no
separate latest-receive result. Read the available size before moving or clearing
that backing. Initialization and `connected()` may have no buffer. Native message
`flags` remain separate from the packed disconnect `state`; receive errors belong
to connection error/teardown handling.

`ZiTxContext::length` retains the latest signed send result, including callbacks
that select subsequent output without a buffer installed. `offset` tracks partial
send progress independently of the buffer descriptor. On Tx, `io.disconnect()`
marks the current buffer final and preserves its callback and unsent suffix
through short writes and backpressure. `io.close()` abandons the suffix and
disarms the context. Both requests are terminal for that connection. A pending
`sync()` returns on local acceptance of the final buffer, explicit close or a
terminal send error. The transport holds any backing or provider lease needed
after local acceptance until its own retirement.

`ZmPQRx` is Rx-owned in its entirety. `ZmPQTx` is Tx-owned in its entirety.
A class with both facilities keeps each base subobject on its owner.
`Zquic::Stream` uses its Rx base on Rx and its unsent plaintext FIFO on Tx.

### Allocator registration

`ZiMultiplex::allocator<...>()` creates a typed allocator and returns a handle
while the multiplexer is stopped. Attach any plugin before the first allocator
request, allocation, or start attempt. The application must serialize these
operations with each other and with teardown.

Acquire every allocator, including App, before the first explicit or implicit
`alloc()` call. After that call begins, all allocator requests return
`nullptr`. Registration binds the allocator directly without I/O dispatch.
A network allocator remains bound to one multiplexer for its lifetime. Protocols
retain their Rx handle and pass it to each endpoint setup, including reconnect.
`findAllocator<Concrete>()` only looks up an existing registration; it remains
available after registration closes and creates no allocator or storage.
Serialize lookup with setup and teardown as well.
See the [Zi buffer contract](../zi/README.md#buffers-and-streams).

### I/O Function Organization
Functions should be organized by owning shard:

- Rx-only functions assert or are otherwise guaranteed to run on `rxThread`.
  They may read/write Rx-owned members only.
- Tx-only functions assert or are otherwise guaranteed to run on `txThread`.
  They may read/write Tx-owned members only.
- Public entry points that may be called from either shard should be thin
  dispatchers that copy/capture only the data needed by the destination shard
  and immediately `rxRun`/`rxInvoke` or `txRun`/`txInvoke` to trailing
  underscored variants that can assume correct thread ownership and
  assert accordingly in debug mode
  - the thread-specific variants can be publicly accessible for callers to use
    when the owning thread can be relied on, to elide the overhead of `*Invoke`

Avoid functions that are "mostly Rx" but opportunistically touch Tx queues, or
"mostly Tx" but inspect Rx stream state. Split those into two functions with an
explicit handoff.

I/O functions should align with this convention:
- `[function]` uses `rxInvoke` or `txInvoke` to call `[function]_`
- `[function]_` uses `ZiAssert` to assert that it is on-shard and does the work
  - in a release build, this elides `invoke`
- callers use `[function]_` if they are already assured to be on-shard, `[function]` otherwise
- `[function]` only exists if off-shard calling is supported (i.e. public app-facing functions)

### Cross-Shard Handoffs
Small fixed-size information may be snapshotted and captured by value when
posting to another shard. Examples include scalar counters, packet numbers, or
bounded ACK ranges.

Large or variable-size information should not be captured directly by lambda.
Write it into a buffer or queue owned by the destination shard, then capture
only the handle and fixed metadata needed to consume it. For Tx work, this
usually means writing into a Tx buffer on the Tx side.

Do not use `ZmLock` to make non-owner access acceptable. A lock can protect
memory, but it does not preserve the sharding model and tends to hide work
running on the wrong I/O thread.

### Lifetime
I/O buffers and dependent structures should use raw back-pointers to owning
link/session/connection/... objects where that is the local pattern.
The owner is responsible for draining or cancelling dependent structures during
shutdown before destruction. Do not keep links alive by adding buffer-level reference
churn unless the ownership model explicitly requires it.

### Teardown
Sharded I/O teardown requires a 3-phase asynchronous process:
1. prevent further ingress, timer and I/O work (e.g. `m_up = false`), and post a teardown continuation on the rx thread to drain rx activity
2. (in the rx thread continuation) post a teardown continuation on the tx thread to drain tx activity
3. (in the tx thread continuation) complete the teardown and release object ownership
- do NOT block except in the main thread, this is async continuation code
- I/O buffers, queued lambdas etc. are populous short-lived objects owned by fewer longer-lived objects such as connections/links/sessions/streams
  - populous short-lived objects should not hold reference counts back to their owners, this anti-pattern:
    - causes reference-count churn in the owner
    - risks ownership cycles
  - such back-pointers should be raw pointers (example: `ZiIOBuf::owner`)
  - owners are responsible for draining all activity using orderly teardown as described above, ensuring that objects with stale backpointers cannot outlive their owners

### Slow Receivers
If a slow receiver causes backpressure on a sender that sustains faster sending, the sender's tx buffer capacity will eventually be exceeded. With the kernel TCP stack, the limit is the usual socket buffer size. With `zutcp`, the limit is the tx buffer pool size. In either case, exceeding tx buffer capacity should close the connection. Do NOT implement blocking/waiting/propagating backpressure onwards - a slow receiver should NOT disrupt tx to other receivers.
