# Repair plan: intrusive container ownership

Repair all eight findings from the container-ownership audit of `zquic`,
`zum`, `ztc`, `zdbus`, `zjrpc`, `zmcp`, `zws`, and `zgtk`. Five findings are
demonstrated object/node allocation pairs; three are QUIC registry or work-queue
memberships whose links can be embedded in their existing application objects.
This document specifies pending work, not completed repairs.

Apply `GUIDELINES.md` and `zquic/GUIDELINES.md`. Preserve unrelated local work,
protocol behavior, limits, callbacks, cancellation, shutdown, and shard ownership.
Propagate changed node types through dependent code rather than adding
compatibility aliases, forwarding APIs, or fallback wrapper allocations.

## Ownership and allocation contract

- Allocate the application object with its container links, metadata, and
  reference count in one allocation. Use the container's allocated `Node` as the
  application type where appropriate.
- Stack intrusions for separate memberships. Disable inner-node allocation with
  an empty heap ID; retain the final concrete object's existing tunable heap.
- Use shadow inner memberships when an owning membership guarantees their
  lifetime. Establish that guarantee through publication, removal, cancellation,
  and teardown before dropping any reference. QUIC's independently retired Rx
  and Tx memberships must retain their existing ownership guarantees; document
  any independently owning intrusion required by that contract. Do not force
  cross-shard lifetime coupling merely to make an inner membership shadow.
- Keep Rx links and membership changes on Rx, and Tx links and membership changes
  on Tx. Preserve reference-carrying cross-shard handoffs. Add no locks or
  non-owner container access.
- For I/O buffers, inject the intrusive node base through `Zi::IOBufAlloc<Base,
  ...>`. Preserve role-specific heap IDs, alignment, growth, maximum size, and
  payload storage rules. Pooling alone does not justify a separate node.
- Do not move live intrusive objects. Move references or unlink/relink the same
  object, and ensure all dependent memberships are drained before final release.
- Do not replace these allocations with temporary payload copies, new allocator
  layers, or unconditional links in unrelated buffer roles.

## 1. QUIC outstanding flows

Owning code: `zquic/src/ZquicRecovery.hh`, `OutstandingFrames::Flow`, `Flows`,
`flow_()`, `findFlow_()`, and `delRange_()`.

The hash node currently owns a separately allocated `ZmRef<Flow>`. The flow has
no independent owner and is removed when its range tree becomes empty.

1. Define flow data containing `OutstandingFlowKey` and the existing range tree.
2. Make that data the intrusive hash-node base and expose the allocated node as
   the flow type. Allocate it directly through the existing outstanding-flow
   hash heap; remove the separate `new Flow` and stored reference.
3. Update lookup, insertion, coverage, clipping, last-range removal, and clear
   paths to use the node directly. Avoid adding reference counting if ordinary
   hash ownership suffices.
4. Preserve range splitting, merging, overlap counts, FIN handling, and exact
   frame bookkeeping.

Verify overlapping packets, partial ACK/loss, final-range removal, reuse of a
flow key after removal, and clear with outstanding ranges in
`ZquicRecoveryTest` and the existing priority-queue tests.

## 2. QUIC CRYPTO receive fragments

Owning code: `zquic/src/ZquicPQueue.hh`, `ZquicBuf.hh`, `ZquicCrypto.hh`, and
`ZquicCrypto.cc`.

`CryptoStream::receive()` allocates a private CRYPTO buffer and then a
`CryptoRxPQueue::Node` containing `RxData::buf`.

1. Define a CRYPTO receive buffer base with its stream offset and existing
   clipping/key/length behavior, deriving from `ZiIOBuf`.
2. Intrude the priority-queue node into that base, disable node allocation, and
   allocate the complete buffer through the CRYPTO receive pool. Keep unrelated
   packet and STREAM buffer layouts independent of these links.
3. Adapt novel-span construction to allocate, fill, and enqueue the final node
   directly. Change the shared helper only as needed; do not preserve the
   wrapper through another relay object or extra payload copy.
4. Update processing and clipping to read the buffer directly. Preserve the
   in-order borrowed-span fast path, existing permitted fragment copy,
   synchronous delivery, asynchronous delivery, and reset behavior.
5. Remove `RxData` and its helpers if no production use remains; update dependent
   tests to exercise the replacement rather than retaining a legacy type.

Verify out-of-order delivery, overlap/duplicate trimming, head/tail clipping,
multi-fragment delivery, growth, reset, and asynchronous callbacks using
`ZquicCryptoTest`, `ZquicPQueueTest`, `ZquicBufferTest`, and handshake coverage.

## 3. JSON-RPC SSE event buffers

Owning code: `zjrpc/src/ZjrpcHTTP.hh`; dependent SSE discard paths in
`zmcp/src/ZmcpServer.hh`.

Each SSE event currently allocates a private `SSEBuf` and a separate
`SSERecord` queue node. The buffer is consumed with the record.

1. Put the terminal flag and list intrusion in an SSE buffer base and allocate
   the complete object through the existing SSE buffer pool.
2. Serialize directly into that object, then enqueue it with `pushNode()`.
   Update dequeue and write paths to use the buffer node directly.
3. Preserve queue concatenation and bounded garbage draining in Zmcp. Moving a
   discarded SSE queue must transfer the existing nodes without allocation or
   copying. Keep owner callbacks and close completion ordering unchanged.
4. Remove the wrapper and its inaccurate shared-buffer exception comment.

Verify multiple events, terminal records, queue-byte accounting, overflow,
producer invalidation, close with queued events, and bounded deferred cleanup in
the existing JSON-RPC HTTP integration tests and `ZmcpPeerTest`/HTTP tests.

## 4. JSON-RPC stdio receive chunks

Owning code: `zjrpc/src/ZjrpcStdio.hh`, especially `rxWork_()`, `rxEnqueue_()`,
`drainRx_()`, and failed/closing receive drains.

The receive worker creates a private `StdioBuf`, then allocates a `StdioRecord`
node solely to hold it until framing.

1. Introduce a receive-chunk buffer with its list intrusion and allocate it
   directly through the stdio buffer pool, preserving existing size policies.
2. Hand the same node from the read worker into the receive queue. Keep the
   removed node referenced while framing outside the existing queue lock.
3. Preserve locking, wakeup coalescing, work budgets, EOF/failure ordering,
   queue-byte accounting, and shutdown drains.
4. Remove the receive wrapper and correct the shared-buffer comment. Keep
   message-framing buffers separate: a chunk can contain multiple messages or
   part of one message.
5. Do not assume the Tx overload's arbitrary caller-supplied buffers have the
   same lifetime. Retain and accurately document that shared-buffer case rather
   than widening this repair into a transmit API redesign.

Verify split lines, multiple lines per read, retained tails, EOF with pending
chunks, queue exhaustion, read failure, and stop with queued work in existing
JSON-RPC and MCP stdio unit/integration coverage.

## 5. Ztc browser sessions

Owning code: `ztc/src/ztchub_daemon.cc`, `BrowserSession`, `BrowserSessions`,
`createBrowserSession()`, `browserPrincipal()`, and `clearBrowsers()`.

The session already owns its cookie, but the hash separately stores a cookie
and `ZmRef<BrowserSession>`.

1. Define session data containing the existing principal, cookie, timer, and
   reference-counted base. Make the hash node derive from that data, keyed by
   the session's cookie, and allocate the session node directly.
2. Replace wrapper-value access with direct session-node access and remove the
   redundant object allocator and duplicate stored key.
3. Keep timer captures as references to the same intrusive session node. Preserve
   consumption before expiry, expiry removal, timer cancellation during clear,
   and safe lifetime through the callback that removes the owning hash entry.
4. Preserve one-use cookies, authorization/admission checks, expiry checks, and
   movement of the principal to its consumer.

Verify consume-once behavior, expiration, consumption before timer firing,
failed admission, and shutdown with active sessions using `ZtcHubTest` and the
hub integration fixtures; add focused lifecycle coverage where absent.

## 6. QUIC Tx stream registry

Owning code: `zquic/src/ZquicStream.hh` and `ZquicLink.hh`.

`TxStreams` allocates a `(stream ID, StreamRef)` node even though the stream
already carries its Rx hash node and owns its immutable ID.

1. Factor the necessary stream data/key accessor to stack separate Rx and Tx
   hash intrusions into the allocated stream. Keep allocation at the final
   stream node and preserve its tunable heap.
2. Publish the stream node directly into each registry. Update lookup,
   iteration, control cleanup, retirement, and all dependent allocated-node
   aliases and casts.
3. Preserve each shard's independent membership and reference-release timing.
   Specifically cover Rx-first and Tx-first publication, retirement while a
   cross-shard post retains the stream, and disconnect during publication.
4. Do not substitute raw cross-shard pointers or a single shared set of hash
   links for the two independent registries.

Verify local and peer stream creation, lookup on both shards, FIN/reset
retirement, early data, and link shutdown in `ZquicStreamTest`,
`ZquicStreamStateTest`, `ZquicFlowTest`, and runtime/loop tests.

## 7. QUIC writable stream queue

Implement together with section 6. `StreamQueue` currently stores `StreamRef`
and creates queue storage around each writable enqueue; `txQueued()` already
enforces at most one queued membership per stream.

1. Add a dedicated intrusive Tx writable-list membership to the complete stream
   node. Replace the reference-value queue with a list supporting the existing
   FIFO operations.
2. Preserve enqueue deduplication, FIFO order, dequeue flag clearing, requeue,
   and retention through packetization. Use a shadow list only after proving
   that Tx registry ownership spans every queued membership and queue drain.
3. Audit all early-data rejection, reset, disconnect, and teardown paths for
   correct flag and link cleanup before stream ownership is released.
4. Remove the old queue storage/heap without introducing a per-enqueue adapter.

Verify repeated writable notifications, dequeue/requeue, multiple-stream
fairness, flow-control blocking/resumption, cancellation, and shutdown. Confirm
that writable enqueue no longer allocates a separate queue entry.

## 8. QUIC endpoint link registry

Owning code: `zquic/src/Zquic_.hh`, `Zquic.hh`, and `ZquicLink.hh`.

`ServerLinks_` allocates a reference-value hash node for each accepted link.
Other references and registries do not prevent a dedicated endpoint intrusion.

1. Factor an endpoint hash-node base that can be embedded in the common link
   hierarchy without circular declarations. Preserve pointer-identity lookup
   and duplicate detection; do not introduce copied identifiers.
2. Keep the inner node allocator disabled and preserve allocation at the final
   concrete link type. Propagate the changed hierarchy through application link
   factories, examples, tests, and dependent transports.
3. Register accepted links with `addNode()` and remove the endpoint membership
   directly. Preserve endpoint ownership and outstanding callback references.
4. Preserve normal disconnect removal, stopping-time retention, stop counts,
   route retirement, and final registry cleanup. Remove the blanket exception
   comment or replace it with the actual ownership contract.

Verify duplicate registration, rejected acceptance, peer disconnect, multiple
links, stop during handshake, and stop with outstanding callbacks using existing
QUIC API, CID, handshake, runtime, socket, and loop tests.

## Implementation order and acceptance

1. Repair the isolated outstanding-flow and browser-session hashes.
2. Repair CRYPTO receive buffers and JSON-RPC SSE/stdio receive buffers; update
   their direct dependents, including Zmcp SSE cleanup.
3. Repair QUIC Rx/Tx stream-node composition and writable scheduling together.
4. Repair endpoint link-node composition and propagate all dependent factories.
5. Review every changed line against the ownership contract, remove dead types,
   stale exception comments, unused heaps, and obsolete wrapper casts.

Retain the current build configuration in the existing generated Makefiles and
`config.status`; do not reconfigure or clean as part of this repair.
Other compilers, release/debug build
flags, platforms, and sanitizer configurations are out of scope. Rebuild
infrequently: batch source changes into large batches and do not rebuild or
retest until major implementation milestones are reached. The numbered work
items above do not each require a build/test cycle.

Rebuild incrementally, reusing current build artifacts; do not clean the build.
Use **`-j4`**, including aggregate builds, and run only one build at a time.
Defer dependent rebuilding until implementation is complete, while propagating
source changes through dependent code during implementation. At the final build
milestone, run top-level `make -j4` to refresh libraries and dependent binaries
in dependency order.

Run the relevant existing TAP tests and integration fixtures at major validation
milestones, after their binaries are built. The per-finding verification lists
specify required coverage, not instructions to rebuild or retest after each
edit. Add focused tests for demonstrated ownership invariants
where existing coverage is insufficient, particularly last-owner removal,
timer cancellation, cross-shard retirement, and allocation-free requeue. Do not
add production instrumentation or tests that merely mirror inheritance syntax.

Record before/after allocation identities or heap counts on matched paths:
one flow allocation instead of object plus hash node; one allocation per retained
CRYPTO fragment, SSE event, stdio receive chunk, browser session, stream, and
endpoint link; no separate writable-queue allocation. Account separately for
hash buckets, range-tree nodes, buffer growth, and framing allocations. Record
any increase in embedded object size, especially for client links and streams
that do not use every membership. Do not infer latency gains from layout alone.

Use Valgrind sparingly: to debug memory corruption as needed during
implementation, and as the final memory leak and corruption gate after the
implementation and dependent rebuild are complete. Use `libtool exec` for
source-tree memory-debugging tools. Run Valgrind outside the sandbox with
networking available. Do not introduce other build configurations
for memory checking. The earlier Valgrind attempt failed before execution
because glibc loader debug symbols were missing; verify availability before
relying on that check and record any unavailable tool explicitly rather than
claiming a pass.

Acceptance requires every finding above to be repaired, dependent code to build,
relevant tests to pass, and no new leaks, extra payload copies, or non-owner
container access. Record exact commands, configuration, results, and any gaps.
Do not expand the repair into genuinely shared QUIC packet slices, JSON-RPC
batch-call membership records, Ztc alert buffers, or generic caller-supplied
transport buffers. The audit found no additional demonstrated same-lifetime
wrapper in `zum`, `zdbus`, `zmcp`, `zws`, or `zgtk`; build and test them as
dependents where the changed APIs reach them.

## Follow-up: handshake fixture stack storage

User-directed scope addition: repair the excessive stack utilization found by
Valgrind in `ZquicHandshakeTest::exchangeTLS_()`. Replace the two fixed arrays
of 32 × 64 KiB messages with intrusive queues of pooled I/O buffers carrying
epoch metadata. Write frames into their final queued buffers and transfer node
references on dequeue, eliminating full-slot copies, queue shifts, and 64 KiB
stack temporaries. Allocate the buffered peer state through an identified
`ZmHeap` with single-owner RAII lifetime. Measure the compiled frame and rerun
the handshake test under Valgrind using its default stack-frame threshold.
Raising `--max-stackframe` is diagnostic only and is not acceptance.

## Follow-up: deterministic runtime checks

Repair the endpoint close/drain fixture's barriers to target app-owned shards
and observe expiry/disconnect completion before stop. Root-cause diagnostics
failures rather than accepting timing-dependent outcomes. The old-source trace
identified missing Handshake PING padding for the header-protection sample; pad
before long-header length encoding and cover tiny Handshake/0-RTT packets for
all packet-number widths with deterministic encryption/decryption tests. Keep
strict failure-count and qlog assertions. Rebuild affected dependents once the
source batch is complete, using the same configuration and `-j4`.

## Implementation record

All eight findings and the stack/runtime assertion follow-ups are implemented.
See `repair5-results.md` for allocation/layout evidence, configured incremental
`-j4` builds, normal test results, deterministic padding regression, and final
Valgrind results. The runtime assertions now pass; its independently reproduced
32-byte core TLS allocator leak remains recorded separately and is not claimed
as a clean memory pass.
