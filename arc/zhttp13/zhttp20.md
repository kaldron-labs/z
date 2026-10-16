# `Zhttp` Hot-Path Linear-Search Audit

## Scope

Audit the current `zhttp` request, response, connection, stream, and
compression paths for linear searches whose cost scales with configured
concurrency, queued work, connection churn, or message size.  Distinguish
these from unavoidable byte-oriented parsing, protocol-bounded iteration,
amortized table maintenance, diagnostics, and shutdown traversal.

`GUIDELINES.md` is authoritative.  In particular, latency matters more than
throughput, long-lived containers must not grow monotonically, short-lived
objects must be removed deterministically, and hot-path storage and algorithms
must be workload-aware.

## Findings

### 1. Critical: H2 frame scheduling scans and shifts the global backlog

`ZhttpH2Hub.hh` stores all pending frames for all streams in one
`ZtArray<PendingFrame>`.  `startFrameDrain_()` resets `m_frameScan` to the
entire queue length whenever work arrives.  `drainFramesTx_()` then searches
the array for a frame whose stream ordering, connection window, stream window,
and header-continuation state permit transmission.

Every admitted frame is removed with `m_pendingFrames.splice(m_frameCursor,
1)`.  Removing a stream scans the complete array and performs one splice for
each matching frame.  With the default 4,096-frame limit:

- backpressure can cause repeated full-backlog searches;
- successful transmission moves O(n) array elements per frame; and
- resetting a stream with many queued frames can perform O(n^2) movement.

Replace the flat global array with per-stream intrusive frame FIFOs and a
runnable-stream queue.  `TxWindowHash` already provides the natural
per-stream owner.  Queue a stream only when its head frame becomes runnable,
while preserving per-stream order, connection flow control, fair scheduling,
and uninterrupted HEADERS/CONTINUATION blocks.  Delete the global cursor and
scan state rather than adding another index over the flat array.

Relevant code:

- `ZhttpH2Hub.hh:1466-1497` queues frames;
- `ZhttpH2Hub.hh:1537-1554` searches and splices during stream removal; and
- `ZhttpH2Hub.hh:1567-1630` repeatedly scans and splices during drain.

### 2. High: H2 completion and streaming fences search the frame backlog

`finishTx()` scans backward through every pending frame to find the terminal
frame for one stream.  `fenceTx()` repeats the search for the latest frame and
is used by asynchronous response-body chunking.  Their cost therefore scales
with the complete connection backlog rather than the subject stream.

As part of the frame-queue repair, retain direct latest-frame and
terminal-frame node references in `TxWindowEntry`.  Update or clear them when
nodes are transmitted, cancelled, or removed.  This should delete both
searches without creating a parallel lookup structure.

Relevant code: `ZhttpH2Hub.hh:750-775`.

### 3. High: H1 response admission searches past blocked connections

The server has one response FIFO per protocol profile.  For H1,
`drainResponses_()` removes each response whose connection already has an
active response, appends it to the same FIFO, and continues until it finds
runnable work or has examined the whole queue.

With pipelined requests across busy connections, each enqueue or response
completion can search most of the global H1 backlog.  The scan is unnecessary:
H1 ordering is a per-connection property, not a property of all H1 traffic.

Give each H1 link its own response FIFO and maintain a server queue of ready
links.  An idle link with queued responses appears in the ready queue at most
once.  Completion makes the link ready again if its FIFO remains non-empty.
H2 and H3 can retain simple global response FIFOs because their drain paths do
not search past connection-blocked responses.

Relevant code: `ZhttpServer.hh:968-1008`.

### 4. High: H3 connection reuse scans an indefinitely retained link array

Every H3 logical connection request searches `ClientHub::m_links` by host,
remote address, and port.  A physical endpoint that goes down is marked down
and drained but is not removed from `m_links`; the complete array is cleared
only during hub finalization.

Consequently:

- connection selection is O(all endpoints encountered during process
  lifetime);
- dead owner slots accumulate in a 24x7 client; and
- endpoint churn steadily increases request latency and memory consumption.

Use an endpoint-keyed `ZmHash`, following the existing H2 client-hub pattern.
Remove both the hash entry and owner slot deterministically when the endpoint
goes down.  Store the owner-array slot and use swap removal, updating the moved
slot.  Include any concrete link-type identity in the key if one `ClientHub`
can contain more than one `CliLink<App, Logical>` type.

Relevant code:

- `ZhttpH3Hub.hh:150-190` searches and appends links; and
- `ZhttpH3Hub.hh:416-447` marks a link down without removing its owner slot.

### 5. Medium: H3 stream completion searches active-stream arrays

Client stream completion searches `streams` before removing the stream.
Deferred client close repeats the search, and server deferred close performs
the same operation on `logical`.  Cancellation before stream assignment also
searches `pending` and `waiting` by logical-stream pointer.

Store an active-array slot in each native stream and use swap removal, updating
the moved stream's slot.  Use an intrusive FIFO for waiting logical streams.
Pending cancellation should use intrusive membership or another direct owner
handle rather than searching by pointer.

Relevant code:

- `ZhttpH3Hub.hh:517-535` performs immediate client removal;
- `ZhttpH3Hub.hh:575-599` performs deferred removal and cancellation search;
  and
- `ZhttpH3Hub.hh:911-925` performs deferred server removal.

### 6. Medium, exceptional path: client cancellation performs nested searches

Cancellation of an active request first searches every configured
`m_liveReqs` slot by application request key.  After locating the attempt, the
selected TCP, TLS, or QUIC pool searches its links or TLS pairs again by
`LiveReq *`.

Cancellation is not the successful-request path, but timeout or cancellation
storms make the doubled O(concurrency) cost material.  Maintain an active
request-key-to-slot hash and retain a direct pool/link cancellation handle in
`LiveReq`.  Delete both searches.

Relevant code:

- `ZhttpClient.hh:706-724` searches active attempts;
- `ZhttpClientPool.hh:217-224` searches TCP/QUIC logical links; and
- `ZhttpTLSClientPool.hh:354-375` searches TLS pairs.

### 7. Low: QPACK insertion planning is quadratic in planned headers

For every prospective dynamic-table insertion, QPACK planning first searches
all existing insertion plans for an exact name/value match.  After appending a
plan it searches backward again for a matching name.  Planning can therefore
be O(h^2) in the number of inserted fields.

Header sets and dynamic insertion plans are normally small, so an unconditional
hash may cost more than it saves.  First combine the exact-match and name-match
work into one reverse pass.  Benchmark realistic and adversarial header sets
before adding more indexing; retain the simpler scan if measured latency does
not justify extra state or allocation.

Relevant code: `ZhttpH3.hh:1330-1362`.

## Adjacent Linear Queue Movement

These operations are not searches, but they affect the same hot paths and
should be repaired with the corresponding queue changes:

- H3 removes the first waiting logical stream with `waiting.splice(0, 1)` for
  every newly opened stream (`ZhttpH3Hub.hh:537-548`), shifting all remaining
  waiters; and
- H2 client admission and requeueing remove prefixes from `m_pending` arrays
  with `splice(0, n)` (`ZhttpH2Hub.hh:2108-2134` and
  `ZhttpH2Hub.hh:3212-3249`), with a default pending limit of 1,024.

Use intrusive FIFOs or a head cursor with infrequent amortized compaction.
Prefer the intrusive FIFO where cancellation or deterministic node removal is
also required.

## Reviewed and Not Classified as Findings

The following linear work is proportionate, bounded, amortized, or outside the
runtime hot path and should not acquire extra indexing without evidence:

- HTTP, URL, Alt-Svc, Huffman, HPACK, and QPACK byte parsing, which necessarily
  examines input bytes;
- iteration over emitted or retained body buffers, which is proportional to
  output;
- HPACK/QPACK eviction and occasional table compaction/rebuild;
- the bounded recent-H2-stream ring and its hash;
- Alt-Svc alternative iteration bounded by configured `maxAlternatives`;
- diagnostic callbacks and start, stop, finalization, and whole-connection
  teardown traversal; and
- H2 active-stream and compression-table lookup, which already use hashes.

## Recommended Repair Order

1. Replace the H2 pending-frame array, folding `finishTx()` and `fenceTx()`
   tracking into the new per-stream queues.
2. Replace global H1 blocked-response rotation with per-link FIFOs and a ready
   link queue.
3. Add deterministic H3 physical-link indexing and removal.
4. Make H3 active and waiting stream removal constant-time.
5. Remove nested client cancellation searches if cancellation/timeout load is
   material in benchmarks.
6. Simplify QPACK plan scanning only after measuring representative header
   workloads.

Each repair must include queue-saturation, backpressure, cancellation,
stream-reset, connection-churn, and shutdown tests.  Benchmark latency against
queue depth and concurrent-stream count; average throughput alone will conceal
the worst-case scans identified here.
