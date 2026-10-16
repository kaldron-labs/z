# Reactive receive-stream refactor

This refactor restores the receive architecture to its intended reactive
model: transports append received `ZiIOBuf` nodes to `ZiRxStream`, then prompt
the next protocol layer to interpret the enlarged queue.  That layer consumes
and processes each complete frame it can recognize.  A trailing partial frame
remains unconsumed until a later append prompts interpretation again.  No read
operation asks a decoder or transport to produce more data.

`ZiRxLayer` has one separate purpose: composing protocol layers while
accumulating the headroom and tailroom required by those layers.  It is not a
logical-message stream, a refill source, a decoder callback interface, or an
owner of input/final/error state.

The change is intentionally breaking.  Propagate it through `zi`, `zhttp`,
`zws`, all dependent examples and tests, and the public comments and boundary
documents.  Do not retain `rxRefill_()`, `RxRefill`, `RxEvent`, the current
pull-style `ZiRxLayer`, or compatibility adapters which emulate them.

This plan supersedes the receive-side callback-scoped refill contract currently
described in `ZhttpMessage.hh`, `ZhttpStream.hh`, and related documentation.  It
does not change the direct, layered transmit design in `zhttp12.md`: Tx remains
incremental and uses accumulated head/tail room for in-place framing.

| Phase | Outcome |
| --- | --- |
| 1 | Restore queue-backed `ZiRxStream` and geometry-only `ZiRxLayer` contracts. |
| 2 | Add transactional framed queue transfer and a real reactive HTTP body stream. |
| 3 | Migrate H1 bodies and upgraded byte streams. |
| 4 | Migrate H2/H3 bodies, tunnels, termination, and flow control. |
| 5 | Migrate WebSocket framing and application message delivery. |
| 6 | Propagate the public API break and delete all refill/event residue. |
| 7 | Perform integrated validation and final interoperability testing. |

## Historical cause

The queue-backed `Zi::RxStream<Queue>` predates the defect.  Its contract is
push-driven: `push()` appends a node, `consume()` examines queued spans, and a
frame callback returning zero leaves the queue unchanged.

Commit `5b0f779` (`zhttp: add H2 and layered streams`, 28 July 2026) added a
second class named `Zi::RxLayer` to `ZiRxStream.hh`.  That class called
implementation callbacks `rxRefill_()`, `rxSpan_()`, `rxAdvance_()`, and
`rxCancel_()`, and introduced `RxRefill::{Wait,Input,Final,Error}`.  It did not
replace the original queue stream, but it put an unrelated pull abstraction
beside it under the receive-layer name.

Commit `55fa35b` (`zhttp: unify streaming transports`, later the same day)
introduced `Zhttp::BodyRx` and used the pull-style `ZiRxLayer` for HTTP body
callbacks.  HTTP parsers first consumed body bytes from the native queue, then
offered a borrowed span through `rxRefill_()`, requiring the application to
consume the complete offer synchronously.  Subsequent changes extended the
same mechanism to H1 upgraded streams, H2/H3 extended CONNECT, and WebSocket
messages.  `RxEvent` then became an application-visible substitute for
ordinary start/end/reset callbacks.

That evolution inverted receive ownership.  The application now pulls a span
which the protocol has already consumed, and failure to consume it is treated
as a protocol error.  Several parsers also consume and publish fragments of a
protocol frame before that frame is complete.  H2 returns flow-control credit
when a DATA frame is parsed rather than when its body bytes are consumed.  This
refactor removes those inversions rather than renaming or wrapping them.

## Target contracts

### `ZiRxStream` is an appended queue

The canonical receive stream remains `Zi::RxStream<Queue>`:

```c++
rx.push(ZuMv(buf));

int64_t n = rx.consume(frame, data);
// frame(span) == 0: continue across queued spans; if no frame completes,
//                   retain every byte and wait for another push/prompt
// frame(span) > 0: present each queued span in order, then remove exactly the
//                  completed prefix
// frame(span) < 0: report parse failure without consuming that prefix
```

The precise API may retain the existing `span()`, `advance()`, `consume()`, and
`extract()` names, but it must satisfy these invariants:

- `push()` is the only way ordinary received data becomes readable;
- `empty()`/boolean conversion inspect only the queue and never call outward;
- `consume()` never invokes a producer, decoder, refill, wait, or terminal
  callback;
- a framing operation is transactional: a zero/incomplete result preserves all
  queued bytes; the recognizer uses local/copyable cursor state and commits
  parser state only when the frame completes, or explicitly restores it on a
  zero result;
- all mutations occur on the owning Rx shard;
- queue nodes remain pooled `ZiIOBuf` objects and are moved by reference;
- no temporary contiguous buffer is created when the completed prefix already
  occupies one node, and `consume()` never gathers a multi-node frame;
- any byte-count accessor is maintained incrementally rather than rescanning
  the queue.

`input()`, `available()`, `complete()`, `failed()`, and `events()` are not part
of the queue contract.  Add a succinct `uint64_t length()` accessor maintained
on every queue mutation for pressure and flow-control accounting; retain
`empty()` and boolean conversion for ordinary read loops.  Failure and logical
completion belong to the parser/session which understands them.

### `ZiRxLayer` only composes buffer geometry

Restore `ZiRxLayer` solely as the receive-side geometry composer used while
constructing a protocol stack.  It may follow `ZiTxLayer`'s accumulated
head/tail-room vocabulary, but it is not a behavioral mirror of `ZiTxLayer`
and does not need an `Impl` callback contract merely for symmetry:

- a layer declares only its additional headroom and tailroom;
- stacked layers expose the accumulated requirement to the lowest
  pooled-buffer allocation boundary;
- it owns no queue and performs no parsing, transformation, publication, or
  callback;
- it owns no message availability, completion, failure, cancellation, or
  event state;
- it exposes no `consume()` facade and calls no `rxRefill_()`-style method.

Protocol interpreters separately advance, rewind, truncate, or otherwise
transform complete frames within the provisioned geometry and append decoded
buffers to the next queue.  None of that behavior belongs to `ZiRxLayer`.

Do not force protocols to instantiate `ZiRxLayer` when they need no additional
receive geometry.  A direct `ZiRxStream` remains the correct object in that
case.  Add focused `zi` tests which stack at least three synthetic geometry
layers and prove the lowest allocator provisions the cumulative head/tail
room; do not put parsing or buffer-publication callbacks in that test, and do
not use an HTTP test as the foundational contract test.

### Receive processing is reactive and frame-transactional

Every receive layer follows the same sequence:

1. the lower layer appends one or more buffers;
2. it prompts the current interpreter;
3. the interpreter scans only the queued bytes;
4. for each complete frame, it atomically consumes that frame, processes its
   protocol metadata, and appends any decoded payload to the next queue;
5. it stops on the first incomplete trailing frame without consuming it;
6. each append to the next queue reactively prompts the next interpreter.

Append, interpretation, decoded append, and the resulting upper-layer prompt
are synchronous on the owning Rx shard.  The fixed protocol-stack depth bounds
call-stack nesting; work performed within any one interpreter turn remains
batch-bounded so a stream cannot starve unrelated Rx work.

Thus each protocol consumes its own wire framing and appends only complete
decoded payload frames to the next queue:

```text
transport ZiRxStream
        |
        | parser consumes wire headers/control bytes
        v
HTTP body ZiRxStream
        |
        | WebSocket consumes frame headers/mask/control bytes
        v
WebSocket message ZiRxStream
```

HTTP bodies are framed too.  A non-chunked H1 body is bounded by its declared
content length; a chunked H1 body is a sequence of chunk-length-delimited
frames; an H2/H3 body is a sequence of DATA frames.  A layer must not publish
the available prefix of any such frame merely because some bytes arrived.
Content-length, chunk length, H2/H3 frame length, WebSocket frame length, and
equivalent protocol bounds are framing decisions made before consumption.

An application callback is a prompt to interpret an already-populated stream.
If its own framing function returns zero, those bytes remain queued.  When the
protocol later appends another complete decoded payload frame it calls the
application again, allowing the application frame to complete across
transport buffers, HTTP chunks/DATA frames, and WebSocket fragments without
making any lower layer pull for data.

Start, final, EOF, reset, and error are explicit protocol callbacks or parser
state transitions.  They are never synthesized by a read from an empty
stream.  The protocol must prompt the consumer after the last append before a
logical terminal callback.  Unread bytes after that final prompt are not a
wire-protocol failure: the terminal callback either deliberately receives the
queue so the consumer can drain/discard it, or the protocol discards the
remainder after the terminal callback returns.

This distinction applies only to unread decoded/application bytes.  EOF/FIN
with a partial wire-protocol frame still queued is a framing error; that prefix
is never published upward as a final payload fragment.

### Transactional framed transfer, copying, and ownership

Decoded payload must not be copied through a temporary span merely to cross a
protocol boundary.  Add one foundational framed transfer/splice operation to
`ZiRxStream` and reuse it from H1, H2, H3, and WebSocket parsing.  It first
scans with the normal frame callback without mutating either queue.  If the
frame is incomplete it returns zero and changes nothing.  Only after the exact
frame boundary is known does it atomically consume the framed prefix, strip or
retain the caller-specified framing bytes, and append decoded payload to the
destination:

- move complete intrusive `ZiIOBuf` nodes within the completed frame from the
  wire queue to the decoded queue by reference;
- adjust `skip`/`length` in place when a moved buffer contains only payload
  plus already-discarded framing;
- when payload and following wire bytes share one allocation, split once using
  the appropriate pooled allocator;
- obtain every boundary allocation before mutating either queue so allocation
  failure also leaves source, destination, counters, and parser state unchanged;
- preserve the larger useful region in the original allocation and copy the
  smaller side where that choice reduces data movement;
- never allocate a per-span reference/view node merely to avoid a small
  boundary copy;
- preserve the accumulated head/tail-room invariant on both resulting
  buffers;
- leave source and destination byte counts correct after every whole-node and
  split-node path.

Do not provide or use an unchecked operation which transfers “whatever is
currently available.”  Framing must precede queue mutation.  The common
operation may accept head/tail padding counts or a small in-place transform so
H1 chunk delimiters, H2 padding, H3 frame fields, and WebSocket framing are
removed without a second traversal or a protocol-specific copy path.

The operation is a queue mutation, not a decoder pull.  It is called while a
protocol parser already owns both streams on the Rx shard.  Extend the existing
`extract()` implementation or factor common internals rather than maintaining
two divergent node-splitting algorithms.

If the current transport-specific queue aliases prevent moving a node into a
common decoded queue, consolidate their identical `ZmList<ZiIOBuf>` intrusion
at the `zi` level.  Preserve role-specific `Zi::IOBufAlloc` heap IDs and pool
tuning; do not solve type friction with a heap-allocated wrapper node or an STL
container.

### HTTP application contract

The application-facing request/response Parser sink remains message-typed and
succinct:

```c++
template <typename Rx> void body(Rx &rx); // prompt after decoded bytes append
void complete(State::T);                 // framing terminal, delivered once
```

`Rx` is a real queue-backed `ZiRxStream`-compatible object owned by the HTTP
message/parser on the Rx shard.  Complete HTTP body frames are appended before
the callback is made.  The application applies the same reactive rule to its
own framing: consume each complete application frame and leave the trailing
partial frame untouched.  A raw body sink may deliberately treat each queued
span as a complete frame and drain it.  The application must not retain the
stream reference outside the callback, but unread queue nodes remain owned by
the message and are presented again after the next complete HTTP body frame is
appended.  Header and body spans remain mutable where the parser owns the
received buffers.

HTTP exposes the body queue as scatter/gather input; it does not coalesce body
nodes, retain a complete-body scratch buffer, or provide a body-level gather
facility.  Applications which require contiguous application records own and
manage that gather buffer themselves.  That policy, allocation, and copy cost
are application concerns and must not leak back into HTTP framing or
flow-credit accounting.  `ZiRxStream::consume()` likewise presents the native
spans of a completed application frame successively; it does not silently
allocate a contiguous scratch copy on the application's behalf.

The body owner brackets each synchronous `body(rx)` prompt with the stream's
maintained `length()`.  The decrease is the application-consumption delta used
for H2/H3 flow-control accounting; an explicit terminal discard accounts for
the remaining length.  Do not add a consume observer, virtual hook, or
transport callback to the foundational `ZiRxStream` merely to obtain this
delta.

### QUIC payload credit follows semantic consumption

Zquic's reactive delivery remains intact, but its receive-credit boundary must
change.  Removing contiguous bytes from `Zquic::Stream::m_rx` means only that
the H3 interpreter accepted ownership; it does not mean the body/tunnel
application consumed those bytes.  `MAX_DATA` and `MAX_STREAM_DATA` must not be
extended merely because a STREAM/RESET_STREAM frame was admitted,
`processRx_()` returned successfully, or the native queue became shorter.
Keep admitted/received accounting for validating peer flow-control limits
separate from retired accounting which replenishes those limits.

Add an Rx-shard operation which explicitly retires a known number of consumed
or deliberately discarded QUIC stream bytes.  The final succinct name should
follow Zquic vocabulary.  It updates checked per-stream and connection retired
byte counters and drives the existing batched flow-update scheduling.  It is
an accounting notification, not a producer callback, and must not read,
refill, or retain an application stream reference.

H3 assigns credit ownership while atomically processing a complete frame:

- frame type/length fields, HEADERS/QPACK bytes, and other protocol-only bytes
  are retired immediately after their parser state commits;
- DATA payload bytes transferred to a body or tunnel queue enter a deferred
  per-stream ledger instead of being retired;
- each synchronous upper-queue prompt retires exactly the decrease in
  maintained queue length, capped by that ledger;
- terminal discard, reset, cancellation, and parse failure retire the exact
  deferred remainder once before releasing the logical or QUIC stream.

A received RESET_STREAM final size still consumes connection admission credit
for newly accounted bytes.  Because reset discards the stream rather than
publishing that data, retire those newly admitted bytes and any queued/deferred
bytes through the same exactly-once terminal settlement; do not treat reset
receipt itself as evidence that previously delivered payload was consumed.

The connection retired total is the checked sum of unique stream bytes
retired.  Retransmission, overlap, reordering, repeated terminal calls, and
already-retired protocol bytes must never advance either counter twice.  A
stalled application may therefore leave Zquic's native queue empty while the
deferred ledger remains non-zero and both advertised receive windows remain
bounded.

Propagate the explicit retirement contract through Zquic's public stream
defaults, direct consumers, examples, and tests.  A direct application retires
the native-queue length delta it actually consumed, while a protocol layer
such as H3 partitions that delta into immediately retired protocol bytes and
deferred payload bytes.  Returning from `process()` without consuming input
retires nothing.  Do not preserve an automatic native-dequeue-equals-consumed
fallback for compatibility.

`bodyEach()` may remain only as an explicitly eager convenience over bytes
already in that queue.  Its loop uses `while (rx)`/`empty()` and a frame
callback returning the current span length.  It never calls `input()`, never
waits, and never causes decoding or transport activity.  `bodyDrain()` is the
discarding specialization.  Rename or delete either helper if its remaining
call sites become clearer as ordinary `consume()` loops; do not preserve it
solely for compatibility.

### Extended byte streams and WebSocket terminals

Replace stream-embedded `RxEvent` flags with explicit callbacks.  The final
names should follow the containing API's local vocabulary; the required
separation is:

- a data callback receiving the populated queue (`process(stream, rx)` for the
  current upgraded-stream facade, or the existing local equivalent);
- one graceful peer-end callback;
- one reset/error callback carrying the protocol reason where available;
- for WebSocket messages, explicit message start (including opcode), data
  prompt, and message end callbacks; connection close and protocol error remain
  distinct from a successful message end.

Do not pass an `RxEvent` bitmask as a new argument.  That would preserve the
combined lifecycle API while merely moving it out of `ZiRxLayer`.

## Build and test execution policy

Use only the existing clang/debug build.  If configuration is required, use
`./z.config -d -L <prefix>`.  Do not configure or validate gcc, release, or a
compiler/sanitizer matrix during this refactor.

Minimize rebuilding.  Batch each phase and build the smallest affected `src`
targets before their dependent test targets.  Do not clean the repository or
rebuild at a phase boundary merely because the phase ended.  A phase may be an
intentional propagation checkpoint at which the tree is temporarily
unbuildable; record the deferral and do not manufacture a compatibility shim
or run a knowingly unproductive build.  Resume validation at the next stable
checkpoint.

Interim testing is limited to deterministic functional unit tests.  Defer the
full external-peer H1/H2/H3/WebSocket interoperability matrix until phase 7.
That matrix is the final validation action after the final repository search,
`GUIDELINES.md` audit and repair, clang/debug build, and functional tests.

## Phase 1 — Restore the `zi` receive contracts

Separate queue storage from protocol geometry in `ZiRxStream.hh` (or split the
geometry class into a directly named header if that produces cleaner direct
dependencies).  Preserve the existing queue-backed `RxStream` and delete from
the foundational receive API:

- `RxRefill` and its `Wait`, `Input`, `Final`, and `Error` states;
- `RxEvent` and stream-embedded `Start`, `Input`, `Final`, and `Error` flags;
- the current `RxLayer<Impl>` implementation;
- `refill_()`, `input()`, `available()`, `complete()`, `failed()`, and the
  `rxRefill_()`/`rxSpan_()`/`rxAdvance_()`/`rxCancel_()` implementation
  contract.

Implement the geometry-only `ZiRxLayer` contract described above.  Use the
same accumulated head/tail-room vocabulary and buffer-bound assertions as
`ZiTxLayer`, without copying its `prepareBuf_()`/behavioral callback role.
Ensure the lowest allocator receives the complete requirement before any
protocol writes into a buffer.  Do not add an `Impl` callback contract,
receive queue, completion, or error flags to the replacement layer.

Add an incrementally maintained queue byte length for backlog and flow-control
accounting.  Update every existing `RxStream` mutation (`push`, `advance`,
`consume`, `extract`, `clean`, move construction, and move assignment) in one
change so the count cannot drift.

Rewrite `ZiRxStreamTest` around push behavior:

- append one buffer, consume a complete prefix frame, and retain the suffix;
- return zero for an incomplete frame, append a second buffer, then consume
  the completed frame without losing the original prefix;
- consume a frame spanning multiple nodes;
- move/clean a non-empty stream and verify queue length;
- stack receive geometry layers and verify the allocation boundary receives
  the accumulated head/tail room without invoking a protocol callback;
- verify that reading an empty stream invokes no callback outside the supplied
  frame/data lambdas.

This phase may temporarily break `zhttp` and `zws`; do not retain the pull
types to keep them compiling.

### Phase 1 acceptance criteria

- `ZiRxStream` has one reactive appended-queue contract; empty reads cannot cause
  data production or state transitions.
- `ZiRxLayer` composes only protocol buffer geometry and contains no readable
  byte queue or logical lifecycle state.
- No foundational declaration of `RxRefill`, `RxEvent`, or `rxRefill_`
  remains.
- Queue length is exact after every mutation and is not computed by scanning.
- Focused `zi` unit tests pass at the first stable buildable checkpoint.
- Audit every phase 1 change against `GUIDELINES.md` and repair buffer pooling,
  head/tail-room arithmetic, overflow, move semantics, naming, layout,
  allocation, copying, and direct-header issues before accepting the phase.

## Phase 2 — Add framed queue transfer and HTTP body storage

Implement the transactional framed transfer/splice operation and its pooled
split-buffer path.  Exercise incomplete, full-node, partial-head-node,
multi-node, empty, exact-boundary, padded, and allocation-failure cases in
`ZiRxStreamTest`.  Explicitly prove that an incomplete frame mutates neither
queue, source bytes following a completed frame remain in source order, and the
destination preserves pre-existing partial application data.

Replace `Zhttp::BodyRx` with a small HTTP body owner whose readable member is a
real queue-backed stream.  It may track message-specific accounting such as
received bytes, queued bytes, body limit, and terminal state, but those values
must not be exposed as `ZiRxStream` refill/events.  It provides protocol-side
operations such as append/transfer, prompt, reset, and discard; the app sees
only the queue stream in `body(rx)`.

The body owner observes these rules:

- protocol framing is removed before a node enters the body queue;
- appending never calls back into the lower transport to obtain input;
- append and prompt only after the enclosing HTTP body frame is complete;
- preserve a trailing incomplete application frame across prompts and append
  later entity bytes after it;
- enforce `MaxBody` against bytes received on the wire, not bytes consumed by
  the application;
- prompt synchronously after every append; the last append's prompt is the
  final data prompt, after which message completion is delivered separately
  without an empty synthetic read;
- reset/cancel deterministically releases every queued node without invoking a
  fake read error;
- record consumption deltas where multiplexed transports need flow-control
  credit in phase 4.

Update the common Parser comments and `Zhttp::Stream_::IsRx` detection to the
queue contract (`consume`, `empty`/boolean conversion, and any deliberate
`length` accessor).  Do not detect the removed lifecycle members.

Rewrite `bodyEach()`/`bodyDrain()` now, before protocol migration, so their
semantics cannot accidentally reintroduce a pull.  Add tests showing that
`bodyEach()` consumes all currently queued spans, preserves callback order,
and returns immediately on an empty queue.

This is an intentional structural checkpoint: replacing `BodyRx` breaks the
still-unmigrated H1/H2/H3 parsers.  Build and run the independent `zi` framed
transfer tests, but do not build Zhttp or introduce old-shaped methods on the
new body owner merely to make this phase compile.  Author the body-owner tests
now and defer their execution through phase 3 to the stable Zhttp checkpoint
at the end of phase 4.

### Phase 2 acceptance criteria

- An incomplete protocol frame leaves both source and destination queues
  unchanged.
- Boundary allocation failure has the same transactional result: neither
  queue, byte counter, nor parser cursor is changed.
- Whole-node decoded payload transfer for a completed frame is reference-only;
  split boundaries use one pooled allocation and the selected minimal copy.
- There is no per-span wrapper allocation and no temporary complete-body
  buffer.
- HTTP never gathers or coalesces decoded body nodes for the application; any
  application-required contiguous representation is owned above the HTTP API.
- The HTTP body stream is populated by protocol-side append/transfer and is
  read only from its own queue.
- A consumer can leave a trailing partial frame untouched, receive another
  append, and consume the completed frame on the next prompt.
- HTTP body limits and queue counts remain correct regardless of application
  consumption.
- `bodyEach()` is demonstrably eager over existing input only.
- Focused `zi` transfer tests pass.  Zhttp body-owner tests are present but
  explicitly deferred to phase 4 because protocol propagation is incomplete.
- Audit phase 2 against `GUIDELINES.md` and repair intrusive ownership, heap
  IDs, split-copy choice, hidden allocation, temporary contiguity, counter
  maintenance, and callback lifetime issues.

## Phase 3 — Migrate H1 body and upgraded-stream receive paths

Change the H1 parser so it no longer consumes an available entity span and
re-offers it through `BodyRx::offer()`.  Instead, parse the start line, headers,
chunk-size lines, chunk delimiters, trailers, and pipelined messages
reactively.  Each H1 framing state consumes only a complete frame and
transfers only the decoded entity bytes into the message body queue.

For fixed/content-length and EOF-delimited bodies:

- after headers establish a positive content length, treat exactly that many
  bytes as the body frame and leave its incomplete prefix in the native queue;
- reject a declared content length above `MaxBody` before waiting for that
  frame;
- transfer/process that body only when the complete content-length frame is
  queued;
- never expose bytes belonging to the next pipelined message;
- preserve an incomplete native body frame while additional transport buffers
  are appended;
- complete content-length accounting from bytes transferred, independently of
  application consumption;
- for a valid close-delimited response, use explicit peer EOF as the frame
  boundary and process the queued body once at EOF rather than publishing
  prefixes as they arrive;
- handle premature EOF explicitly rather than by making an empty body read
  return `Final`.

For chunked bodies:

- remove chunk-size lines, extensions, per-chunk CRLF, and trailers before
  body-queue insertion;
- after parsing a chunk length, leave the chunk data plus its required trailing
  CRLF unconsumed until that complete chunk frame is queued;
- reject a chunk whose length or cumulative entity length exceeds `MaxBody`
  before waiting for or transferring its data;
- atomically consume each complete chunk frame and append only its entity bytes
  to one continuous body queue;
- allow an application frame to span chunks without seeing their boundaries;
- retain incomplete chunk headers/trailers in the native queue until more
  bytes arrive;
- apply the transfer fast path when a body region occupies complete nodes and
  copy only the smaller side at a coalesced body/framing boundary.

Delete `ZhttpH1Stream::StreamRx`.  On a successful H1 Upgrade/CONNECT handoff,
the native receive queue already contains any bytes read beyond the HTTP
headers; pass that queue to the upgraded-stream consumer and continue appending
future transport input to it.  Replace `terminal(RxRefill::T, ...)` with
explicit peer-end and reset/error dispatch.  Ensure the handoff neither drops
nor duplicates bytes coalesced with the final HTTP header line.

Apply bounded backlog policy without pulling: prompt the body/stream consumer
after complete-frame appends, retain an incomplete protocol frame in the lower
queue and an incomplete application frame in the upper queue, and stop making
protocol progress when the configured/body maximum is reached.  Do not spin a
parser loop which repeatedly scans the same incomplete frame or prompts a
consumer when neither queue changed.

H2/H3 still use the deleted refill interface at this checkpoint, so phase 3 is
also intentionally not a Zhttp build checkpoint.  Do not compile Zhttp or add
a transitional H2/H3 adapter.  Carry the H1 and common body-owner functional
tests into the stable phase 4 build after the same break has been propagated
through every HTTP version.

### Phase 3 acceptance criteria

- H1 application body input is a queue populated with decoded entity bytes;
  `BodyRx::offer()` and the H1 `rxRefill_()` adapter are gone.
- Chunked bodies retain a trailing incomplete application frame across
  successive complete-chunk appends; fixed and EOF-delimited bodies receive
  one complete-body append and final prompt before the terminal callback.
- A partial content-length body remains entirely unconsumed; after a complete
  chunk-length line is processed, partial chunk data plus its CRLF remains
  unconsumed until that complete chunk frame is present.
- Chunk boundaries, extensions, CRLF, trailers, and pipelined messages never
  enter the body queue.
- An application frame can span TCP/TLS buffers and multiple HTTP chunks.
- H1 Upgrade transfers already-buffered post-header bytes exactly once and
  uses explicit peer-end/reset callbacks.
- Parser loops return without spinning when neither parsing nor application
  consumption makes progress.
- Focused H1 parser, boundary, message-engine, service-idle, and upgraded-stream
  functional tests are updated here and run at the phase 4 clang/debug
  checkpoint; no knowingly unbuildable phase 3 build is attempted.
- Audit phase 3 against `GUIDELINES.md`, focusing on Rx-shard ownership,
  receive-loop liveness, pipelining, EOF semantics, in-place buffer mutation,
  allocation, and boundary copies; repair every issue before acceptance.

## Phase 4 — Migrate H2/H3 messages, tunnels, and flow control

Give every active H2/H3 logical message/stream its own decoded receive queue.
Header decompression and semantic parsing remain protocol state; DATA payload
nodes are transferred into the decoded queue only after the complete DATA
frame is queued, with frame headers, prefixes, and padding removed atomically.
An incomplete frame remains in the native connection/QUIC stream queue.
Trailers and subsequent protocol frames remain there until their own framing
is complete.

Apply the complete-frame rule to every H2/H3 frame type, not only DATA.
HEADERS plus CONTINUATION and QPACK instruction sequences may have
higher-level multi-frame state, but each individual wire frame/instruction is
consumed only after its own declared production is complete.  Unknown frames
are likewise skipped only when their complete declared payload is queued.

For H2:

- change frame parsing so DATA payload ownership can be transferred rather
  than consumed into a borrowed `Event::value` span;
- parse the nine-byte header to establish the exact frame length, then leave an
  incomplete DATA frame unconsumed until its complete payload and padding are
  queued;
- validate the declared H2 frame size and prospective cumulative body limit
  immediately after recognizing the header, before waiting for its payload;
- do not queue `Event` objects containing receive-buffer spans beyond their
  synchronous owner;
- append complete DATA-frame payloads for one stream in order even when
  connection frames for other streams are interleaved; native-buffer
  fragmentation remains invisible until the complete frame is queued;
- count the entire DATA payload for RFC flow control, but return credit for
  padding/protocol-only bytes immediately and body credit only as the
  application consumes body-queue bytes;
- keep connection and stream receive windows bounded when an application
  leaves input queued;
- batch WINDOW_UPDATE and scheduler work using the existing bounded-turn
  discipline rather than posting per byte/span;
- on reset, discard queued nodes deterministically and send one explicit reset
  notification; return any required connection-level credit for discarded
  entity bytes, and do not make an empty read fail.

When a message ends normally with unread body bytes, prompt the application
once, then discard any remainder and settle both connection and stream credit
before releasing the logical stream.  Apply the same credit settlement when
DATA is discarded for a closed/reset stream.

For H3:

- retain incomplete type/length varints in the QUIC stream queue;
- inspect type/length with non-mutating cursor state and retain an incomplete
  frame until all declared payload bytes are queued;
- reject an invalid/oversized H3 frame or prospective cumulative body length
  immediately after recognizing its type/length, before waiting for its
  payload;
- atomically consume each complete frame and transfer only DATA payload into
  the decoded queue;
- keep HEADERS/trailer/QPACK state outside the body queue;
- preserve DATA continuity across H3 frames and QUIC receive buffers;
- distinguish delivery from the Zquic native queue into the H3 interpreter
  from consumption by the body/tunnel application;
- carry a deferred byte-credit ledger with payload ownership when a complete
  H3 DATA frame is transferred upward; release that credit only as the upper
  queue is consumed or explicitly discarded;
- remove receive-window extension from STREAM/RESET_STREAM admission and
  native `processRx_()` completion; none of those events by itself proves
  application consumption;
- make Zquic extend `MAX_DATA` and `MAX_STREAM_DATA` from explicit checked
  retired-byte accounting, using its existing batching thresholds; protocol
  bytes retire immediately and deferred DATA payload retires only through the
  body/tunnel consumption or discard path;
- cap each H3 DATA transmit buffer at half the peer's initial QUIC stream-data
  window (including accumulated upper-layer headroom), so a complete H3 frame
  can cross the remaining window and trigger the existing half-window
  credit-update threshold without deadlocking;
- settle RESET_STREAM final-size admission, queued native bytes, and deferred
  payload through one exactly-once discard path, extending connection credit
  only from the resulting retired count;
- propagate the explicit retire/discard operation through Zquic's CRTP
  defaults, raw-stream examples, fixtures, and flow-control tests; do not keep
  automatic native-dequeue credit as a legacy default;
- on FIN, reset, cancellation, or parse failure, settle deferred credit for
  every body byte actually consumed or deliberately discarded before releasing
  the QUIC stream;
- map FIN and reset to explicit logical callbacks after the final data prompt.

Replace `H2::Parser::m_bodyRx`, `H3::Parser::m_bodyRx`, `start()`, `offer()`,
`finish()`, and `fail()` with the common push body owner.  Content-length and
maximum-body validation uses bytes appended, not queue-drain progress.

Extended CONNECT/tunnel delivery uses the same populated queue but a distinct
logical-stream terminal contract.  Replace `RxEvent::Start/Final/Error` with
explicit stream establishment, data prompt, peer end, and reset/error calls.
Preserve the existing `Zhttp::Stream` Tx facade and its end/reset operations;
this phase changes only Rx lifecycle signaling and ownership.

### Phase 4 acceptance criteria

- H2 and H3 never deliver DATA through a borrowed refill span; each logical
  stream owns a push-populated queue.
- Neither H2 nor H3 gathers DATA payload chunks into a contiguous HTTP buffer;
  whole nodes move by reference and only an unavoidable coalesced wire-boundary
  split may copy the smaller side.
- Neither protocol consumes or publishes a partial DATA frame; later appends
  resume interpretation from the unchanged queued prefix.
- Every non-DATA frame/instruction obeys the same complete-frame/instruction
  rule; multi-frame header-block state does not justify consuming a partial
  wire frame.
- A trailing incomplete application record remains untouched across H2/H3
  DATA-frame and native-buffer boundaries and resumes after later appends.
- H2 padding, H3 frame encoding, trailers, QPACK/HPACK storage, and adjacent
  stream data cannot leak into the entity stream.
- H2 receive-window credit for entity bytes follows application consumption,
  with immediate and correct accounting for non-entity DATA bytes.
- H3/QUIC advertises new connection/stream credit from the deferred
  application-consumption ledger, not merely from native-queue removal; a
  deliberately stalled consumer has focused coverage.
- With a complete H3 DATA frame transferred and Zquic's native queue empty, an
  unconsumed payload advances neither the stream nor connection retired-byte
  basis; partial application consumption advances each basis by exactly that
  payload amount.
- H3 protocol-only bytes retire immediately, while duplicate/reordered QUIC
  input and repeated terminal cleanup cannot retire any stream or connection
  byte twice.
- Normal completion with unread body data, FIN, reset, cancellation, malformed
  content length, and oversized-body paths each produce exactly one explicit
  terminal outcome, release queued nodes, and settle deferred flow-control
  credit exactly once.
- Interleaved H2 streams cannot block, consume, or reorder one another except
  through the configured connection-level flow-control limit.
- The phase 2 body-owner tests, phase 3 H1 tests, and focused H2/H3 frame,
  message, engine, cancellation, QPACK/HPACK, flow-control, and
  extended-CONNECT functional tests pass at this stable clang/debug checkpoint.
- Audit phase 4 against `GUIDELINES.md`, especially Rx/Tx sharding,
  cross-shard captures, flow-control arithmetic, bounded scheduler turns,
  buffer lifetime, retained compression-table spans, and copy/allocation
  behavior; repair all findings.

## Phase 5 — Migrate WebSocket receive framing

Make `Zws::Codec` consume the push-populated HTTP upgraded/tunnel queue. Move
each HTTP append by node reference into a codec-owned wire queue immediately,
so HTTP/QUIC credit can follow delivery into the dependent protocol without
copying or waiting for a complete WebSocket frame. Establish the complete
WebSocket frame length from that wire queue and leave the entire incomplete
frame unconsumed there when more bytes are needed. Only a complete frame is
atomically consumed and interpreted. Remove
`Codec::rxRefill_()`, `rxSpan_()`, `rxAdvance_()`, `rxCancel_()`, `m_input`,
`m_layerActive`, and the pull-style `RxLayer` member.

Add one real queue-backed message stream owned by the codec.  For data frames:

- consume frame headers and masking keys as WebSocket protocol bytes;
- after the complete declared frame is queued, unmask client payload in place
  while the codec owns mutable receive buffers;
- atomically strip the complete frame and transfer decoded payload nodes into
  the message queue;
- concatenate continuation frames into the same logical message queue;
- permit application framing to span HTTP chunks/DATA frames, WebSocket
  fragments, and native receive buffers;
- keep control frames out of the message queue and process interleaved
  ping/pong/close frames without disturbing queued message bytes;
- validate UTF-8 incrementally over decoded text bytes without constructing a
  complete message copy.

Replace `RxEvent`-driven application behavior with explicit callbacks:

- message start, carrying or exposing the selected text/binary opcode;
- data prompt with the populated message queue;
- successful message end after the final append/prompt;
- existing `pong`, `closed`, and `error` callbacks for their distinct
  connection/control outcomes.

If the application rejects a message/data prompt, stop ingress and perform the
existing close/reset policy exactly once.  A consumer returning without
consuming a trailing incomplete application frame is not itself rejection and
must not be turned into a protocol error.  After the final data prompt and
message-end callback, discard any unread message bytes before starting the next
message; they must neither leak across the message boundary nor be reported as
a WebSocket wire error.  Enforce `maxQueuedInput` from the maintained queue
byte length; never call a refill method to discover pressure.

Propagate this contract through H1, H2, and H3 WebSocket engines, sample
clients/servers, Autobahn programs, benchmarks, and protocol fixtures.  Update
CRTP defaults to drain the already-populated queue and implement harmless
start/end no-ops; do not preserve event-mask fallbacks.

### Phase 5 acceptance criteria

- `Zws::Codec` contains no `ZiRxLayer`, `RxRefill`, or stream event state.
- A partial WebSocket frame remains unconsumed in the codec wire queue until
  its complete declared payload is queued; moving HTTP nodes into that queue
  performs no copy or gather and releases lower-layer flow credit.
- WebSocket message input is appended to a queue; an incomplete application
  frame remains untouched across every lower/protocol fragmentation boundary.
- Masking is performed in place and message delivery does not materialize a
  complete temporary body.
- Message start/end, peer close, reset, and protocol error are distinct and
  delivered exactly once in valid order.
- Unread bytes are discarded after message end and cannot become a prefix of
  the next WebSocket message.
- Interleaved control frames do not enter or corrupt the message queue.
- Queue pressure is measured without scanning and a stalled consumer is
  bounded by configuration/transport flow control.
- Focused WebSocket protocol, H1 engine, H2/H3 engine, masking, UTF-8,
  fragmentation, control-frame, close, and pressure tests pass in clang/debug.
- Audit phase 5 against `GUIDELINES.md`, focusing on mutable-buffer ownership,
  queue limits, UTF-8 state, timer/shutdown ordering, CRTP defaults, callback
  capture lifetime, allocation, and copying; repair all findings.

## Phase 6 — Propagate the API break and delete residue

Update every application, fixture, example, benchmark, interop program,
README, public contract comment, boundary manifest, and test to the
reactive/explicit-terminal model.  In particular:

- replace `while (rx.input())` with ordinary queue-driven loops;
- replace `available()` with the deliberate queue `length()` only where a byte
  count is semantically required;
- replace `events()`, `complete()`, and `failed()` with parser/session outcomes
  or explicit protocol callbacks;
- remove fake Rx types which implement the old pull interface from tests;
- update `ZhttpStream.hh` detection and comments;
- update the application contracts adjacent to the relevant templates in
  `Zhttp.hh`, `ZhttpClient.hh`, and `ZhttpService.hh`;
- delete `ZhttpBody.hh` if no body-specific code remains, or retain it only for
  the real body owner/eager queue utilities under accurate comments;
- remove unused enum/flag includes and build-manifest entries.

Perform repository-wide searches for:

```text
rxRefill_
rxSpan_
rxAdvance_
rxCancel_
RxRefill
RxEvent
ZiRxLayer<
.input()
.events()
.available()
.failed()
```

Classify matches rather than blindly replacing names: unrelated device/input
APIs may legitimately use `input()` or `failed()`.  There must be no match
which preserves receive-stream pull, refill, or embedded logical lifecycle
semantics.  `ZiRxLayer` matches are permitted only for geometry stacking.

Review all receive loops for a progress invariant.  A loop may continue after
bytes were consumed, a node was transferred, or parser state advanced without
input; it must return when input is incomplete and no progress occurred.  Cap
mixed-workload I/O turns where a peer can supply an unbounded sequence.

### Phase 6 acceptance criteria

- The public and internal receive APIs describe append/prompt/consume
  semantics consistently.
- Repository searches find no `RxRefill`, `RxEvent`, `rxRefill_`, pull-style
  `ZiRxLayer`, or compatibility adapter.
- Every remaining `ZiRxLayer` use is solely a head/tail-room layer.
- No application is required to drain the populated queue before returning; a
  trailing partial application frame remains queued and is presented after a
  subsequent complete decoded-frame append.
- All terminal behavior is owned by the relevant HTTP/WebSocket parser or
  session and is delivered separately from empty reads.
- Build manifests and public comments match the new file/type ownership.
- All affected deterministic functional tests pass in the existing
  clang/debug build.
- Audit phase 6 against the complete `GUIDELINES.md` and repair dead code,
  compatibility residue, naming, header layout, loop liveness, sharding,
  storage, allocation, copying, and test synchronization issues.

## Phase 7 — Integrated validation

First repeat the repository-wide residue searches and the complete
`GUIDELINES.md` audit.  Repair every finding.  Then perform the smallest
dependency-ordered clang/debug rebuild which proves all changed `zi`, `ztcp`,
`ztls`, `zquic`, `zhttp`, and `zws` library/test targets use current headers
and libraries.  Run the complete deterministic functional test set with
specific coverage for:

- partial consumer framing across two or more appended buffers;
- unchanged source/destination queues and uncommitted parser state after every
  incomplete protocol-frame scan;
- exact-boundary and coalesced-boundary queue transfer;
- H1 content-length, EOF, chunked, trailers, and pipelining;
- H1 Upgrade with post-header bytes in the same receive buffer;
- H2 padded/interleaved DATA, stalled consumers, window exhaustion, resumed
  consumption, FIN, and reset;
- H3 DATA varint fragmentation, trailers, stalled QUIC consumption, FIN, and
  reset;
- H3 native-queue drain with a non-empty deferred payload ledger, partial body
  consumption, immediate protocol-byte retirement, and exact per-stream plus
  connection credit release;
- client response bodies and server request bodies;
- extended CONNECT in H2/H3 and ordinary upgraded H1 streams;
- WebSocket messages fragmented simultaneously at transport, HTTP, and
  WebSocket layers;
- WebSocket control-frame interleaving, masking, UTF-8, close, timeout, and
  pressure limits;
- cancellation and shutdown with unread queued nodes;
- normal and abnormal terminal discard with exact H2/H3/QUIC credit settlement;
- EOF/FIN with an incomplete lower-layer frame versus unread decoded bytes at
  a valid logical terminal;
- pooled-buffer accounting and leak-free teardown;
- geometry accumulation through multiple receive and transmit layers.

After the functional checkpoint is stable, use `libtool exec` for focused
valgrind checks of affected targets.  Run sanitizer checks only if the existing
clang/debug configuration is already instrumented; do not reconfigure or
create another build variant during this refactor.

If any build, functional test, sanitizer, or audit exposes a defect, repair it
and repeat the affected dependency build, functional tests, repository search,
and `GUIDELINES.md` audit until clean.

Only then run the full H1/H2/H3 and WebSocket external-peer interoperability
matrix.  It must be the final validation action.  If interoperability exposes
a defect, repair it, repeat the affected audit/build/functional checks, and
make a successful full matrix the new final action.

### Phase 7 acceptance criteria

- The dependency-ordered clang/debug build completes without warnings.
- All deterministic functional tests pass.
- Stalled consumers and consumers retaining a trailing incomplete application
  frame preserve queued data without polling, spinning, hidden refill, loss,
  duplication, or cross-message leakage.
- Buffer movement uses pooled whole-node transfer on the hot path and only the
  audited minimal boundary copy when splitting is unavoidable.
- H2/H3/QUIC flow control and queue limits bound unread application data.
- Shutdown, cancellation, reset, and error paths release all queued buffers and
  deliver one terminal outcome.
- Audit phase 7 against the complete `GUIDELINES.md`, repair every finding,
  and confirm the final repository searches and repeated audit are clean.
- The final external-peer interoperability matrix passes and is the last
  validation action performed.
