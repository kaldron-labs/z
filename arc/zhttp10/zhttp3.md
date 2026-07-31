# Zhttp stream-contract follow-on plan

## Objective

Complete three related `libZhttp` contracts:

1. ordered tunnels over H2 and H3;
2. application-produced request bodies through `ZiTxStream`; and
3. application-consumed request/response bodies through `ZiRxStream`.

The result must let applications implement PUT, POST, streaming body
consumption, and Extended CONNECT without naming native TCP, TLS, QUIC, H1,
H2, or H3 stream types.  Protocol selection may change concrete CRTP stream
types, framing, and topology, but not application control flow.

The `zhttp`/`zhttpd` integration corpus must exercise both GET and PUT.  On
both sides, request and response production uses `ZiTxStream`, and request and
response consumption uses `ZiRxStream`; an empty GET request body is still
finalized through the same typed Tx contract.  The PUT corpus serializes a
`ZtStruct` body with `ZtJSON` and decodes and validates the received JSON as
the corresponding `ZtStruct`.  The PUT corpus forces multiple bounded producer
turns and consumes every callback-scoped native payload span through
`ZiRxStream`.  Only bounded JSON parser/record state required by the workload
may retain copied bytes; Zhttp adds no application body queue.

This plan depends on the `ZiTxStream` final-boundary and synchronous
callback-scoped `ZiRxStream` work specified in `zi.md`.  Do not duplicate that
substrate in `libZhttp`.

## Baseline

- H2 advertises and parses `SETTINGS_ENABLE_CONNECT_PROTOCOL`.
- `H2Config::extendedConnect(true)` enables local Extended CONNECT.
- H2 parsing recognizes `:protocol`, tunnel state, remote half-close, and
  reset, but delivers tunnel DATA as borrowed `ZuBSpan`.
- `Zhttp::Tunnel<Link>` provides H2 Tx but has no H3-equivalent binding.
- H3 lacks RFC 9220 settings and Extended CONNECT state.
- H1, H2, and H3 builders can frame body data, but `Zhttp::Agent` instantiates
  bodyless `ClientMessage` specializations.
- Agent supplies request method, target, host, and headers, but no body
  description, body source, or per-attempt body cursor.
- Agent reduces response bodies to `ZuBSpan` callbacks.
- Service discards request bodies before invoking the workload.

## Normative boundaries

### Tx

- `ZiTxStream` is the application-facing byte-production contract.
- Applications receive a concrete, templated stream; do not add virtual
  streams or type erasure.
- `libZhttp` owns HTTP headers, chunk syntax, DATA frames, padding,
  end-of-stream mapping, QPACK/HPACK, and transport submission.
- Bodyless messages retain an allocation-free path.
- Body production is bounded by a named, tunable per-turn batch.  This is a
  scheduler-turn bound, not transport admission or flow control.
- Submit each produced buffer immediately through the existing transport
  path.  H1 has no flow control; H2 flow control remains wholly below the
  application stream in its H2-specific Zhttp layer; QUIC flow control remains
  wholly in Zquic.  Agent, Service, and workloads must not inspect or wait for
  flow-control credit or native queue drain.
- Preserve the established low-latency, minimal-copy H1/TCP, H1/TLS, and H3
  Tx paths.  Adopting `ZiTxStream` must not redesign their queueing, pacing,
  scheduling, or transport callbacks.

### Rx

- `ZiRxStream` is the application-facing body-consumption contract for
  ordinary messages and tunnels.
- Applications receive a concrete synchronous view of the decoder's current
  entity bytes.  HTTP framing and bytes from later messages are never exposed.
- The initial `zi.md` contract is synchronous and Rx-shard-affine.  An
  application may consume incrementally within the callback, but must consume
  or copy the offered input before returning.
- Do not retain borrowed spans after the callback.  Do not invent
  asynchronous retention until Zi provides a bounded native pause/resume
  contract.
- Native pooled `ZiIOBuf` storage remains authoritative.  Do not copy each
  fragment into a second queue to manufacture an application stream.
- Preserve the established low-latency, minimal-copy H1 and H3 Rx paths.
  Adopting `ZiRxStream` changes only the callback interface: it must not add
  retention, admission, delayed delivery, flow-control accounting, or parser
  scheduling.

### Ownership and lifecycle

- Routing, parsing, response state, retry decisions, and Rx body state are
  Rx-owned.
- Request-body cursors, production scheduling, and committed-byte accounting
  are Tx-owned.  Pending Tx buffers and flow-control state remain owned by
  their existing lower transport/protocol layers.
- Cross-shard entry points post fixed metadata or stable handles only.
  Variable body data is produced into destination-owned pooled buffers.
- Cancellation and teardown prevent new callbacks, drain Rx, drain Tx, return
  to Rx, and notify exactly once.
- Short-lived buffers and stream events use raw back-pointers where that is
  the local ownership pattern; do not add owner reference-count churn or
  cycles.

### Compatibility and scope

- These are deliberate breaking API changes.  Propagate them through all
  dependents; do not retain borrowed-span or bodyless compatibility shims.
- Keep WebSocket fields, framing, masking, opcodes, close codes, extensions,
  QUIC datagrams, and unordered delivery out of this work.
- Do not add concepts, `requires`, virtual protocol interfaces, locks around
  shard violations, polling, or timer-based test completion.
- No slice starts until the preceding slice satisfies its hand-off criteria,
  including its concluding `GUIDELINES.md` audit and repairs.

### Build and iteration discipline

- Use Clang exclusively for all builds and tests in this plan; configure
  through `z.config -L` and do not use GCC.
- Use `make -j8` by default.  Limit every `zquic` build or test invocation to
  `-j4` and every `zhttp` build or test invocation to `-j2`.
- Keep rebuilds and test runs infrequent.  Batch source updates, dependent
  propagation, audit repairs, and other related edits as much as possible
  before rebuilding.
- Likewise, batch the focused tests required by a slice and run them after a
  coherent source batch, not after each individual edit.  This batching does
  not relax any slice hand-off or final release-gate requirement.

## Slice 0: freeze contracts and Zi prerequisites

### Work

1. Complete and verify the `zi.md` changes:
   - `ZiTxStream::sendBuf_(buf, final)` propagation;
   - composed headroom/tailroom without payload movement; and
   - synchronous callback-scoped `ZiRxStream` views.
2. Add Zhttp detector/compile-time contract tests for:
   - concrete Tx streams supporting write and final flush;
   - concrete Rx views supporting callback-scoped consumption and refill;
   - link Tx/body and Rx/body operations without native transport names; and
   - one synthetic multiplexed non-H3 profile.
3. Specify the public callback ordering in installed headers:
   - headers before first body callback;
   - body completion after framing validation and payload consumption;
   - terminal result exactly once; and
   - no callback after terminal result.
4. Name the configuration field for Tx production batch size.  Its default
   must be a library constant with a maintenance comment, not an unexplained
   literal.  Do not expose a fictitious Rx-admission setting: Rx input is
   synchronous, callback-scoped, and not retained by Zhttp.

### Acceptance criteria and hand-off

- Focused Zi tests prove final-boundary signaling, nested headroom,
  callback-scoped refill, message-boundary refusal, and cleanup with partial
  input.
- Zhttp contract tests compile for H1/TCP, H1/TLS, H2/TLS, H3/QUIC, and the
  synthetic multiplexed profile.
- Installed headers document callback order, synchronous Rx lifetime, and
  shard ownership.
- No Zhttp application-facing contract mentions `Ztcp`, `Ztls`, `Zquic`, an
  H2/H3 frame, QPACK, HPACK, or native flow-control types.

### `GUIDELINES.md` alignment audit and repair

Re-read the sections on CRTP/detectors, constants, buffer ownership, sharding,
and teardown.  Audit every new contract and test for concepts, virtual or
type-erased streams, hard-coded capacities, hidden allocations, copied payload,
cross-shard access, and polling.  Repair every finding, rerun the focused Zi
and compile-time contract tests, and run `git diff --check`.  Slice 1 starts
only after this audit is clean.

## Slice 1: H2 callback-scoped tunnel Rx

### Work

1. Replace `tunnelData(ZuBSpan)` with a templated callback receiving the
   callback-scoped `ZiRxStream` view.
2. Keep H2 frame headers and DATA padding in the decoder.  Expose only ordered
   DATA payload backed by native pooled buffers.
3. Preserve the existing H2 lower-layer DATA delivery and flow-control
   behavior.  The callback must consume or copy all offered payload before
   returning; returning early is an application error, not a request to retain
   or delay credit.
4. Do not add a blocked-input queue or application-facing flow-control
   interface.  H2 window processing stays within the H2-specific lower layer.
5. Deliver remote end after all preceding payload is delivered.  Reset,
   cancellation, parser error, and link teardown cancel buffered input and
   notify exactly once.
6. Keep `Zhttp::Tunnel<Link>::send()` as the scoped Tx entry point.  Its
   callback receives the native pooled body stream, final-flushes before
   returning, maps `end()` to local half-close, and maps `reset()` to
   RST_STREAM.
7. Delete the borrowed-span callback and update all H2 tests and examples.

### Acceptance criteria and hand-off

- Tests prove no-copy single-buffer delivery and callback-scoped refill across
  multiple DATA frames/native buffers.
- Partial `consume()` calls within a callback cover the complete offered
  payload; returning with unconsumed payload fails deterministically.
- DATA followed by END_STREAM is ordered payload-before-end.
- Reset with buffered DATA, simultaneous close, GOAWAY, and session shutdown
  each produce one terminal callback and no shared-session leak.
- Finalization leaves no tunnel event, callback-scoped view, buffer reference,
  flow-control entry, or logical link.

### `GUIDELINES.md` alignment audit and repair

Re-read the Rx ownership, liveness, intrusive lifetime, buffer-pool, and
teardown guidance.  Audit H2 event ownership, window accounting, callback
turn length, container removal, and close ordering.  Repair copies,
allocations, owner references, locks, stale events, and unbounded turns; rerun
H2 tunnel tests under the consistent Clang debug build and run
`git diff --check`.  Slice 2
starts only after this audit is clean.

## Slice 2: H3 Extended CONNECT and ordered tunnels

### Work

1. Add the RFC 9220 H3 setting:
   - advertise it only when locally enabled;
   - reject duplicate settings and values other than zero or one;
   - track local enablement and peer capability independently; and
   - reject `:protocol` when the relevant capability is absent.
2. Transition a successful Extended CONNECT request/response into tunnel
   state without exposing QUIC stream objects to the application.
3. Reuse the Slice 1 callback-scoped `ZiRxStream` tunnel contract for H3 DATA.
   Hide H3 frame headers while preserving the existing H3 parser and Zquic
   delivery/flow-control behavior.
4. Map QUIC FIN to ordered remote half-close.  Map RESET_STREAM and
   STOP_SENDING to tunnel reset with exact-once notification.
5. Make `Zhttp::Tunnel<Link>` compile against one typed H2/H3 logical-stream
   contract.  H3 Tx uses the native request stream and pooled buffers.
6. Closing or resetting one tunnel must not close its shared H3 session.

### Acceptance criteria and hand-off

- Setting absent, disabled, enabled, duplicated, and invalid-value cases have
  deterministic parser/engine coverage.
- `:protocol` is accepted only with the correct negotiated capability.
- The common tunnel corpus passes unchanged over H2 and H3.
- H3 tests cover split DATA, callback-scoped partial consumption, FIN, both
  reset directions, simultaneous close, GOAWAY, and shared-session survival.
- No public tunnel callback mentions H2, H3, or QUIC-native types.
- Finalization leaves empty stream registries, flow-control queues, pending Tx
  buffers, timers, and connection/session references.

### `GUIDELINES.md` alignment audit and repair

Re-read compile-time dispatch, discrete branching, QUIC sharding, pooled I/O,
and lifecycle guidance, plus `zquic/GUIDELINES.md`.  Audit setting dispatch,
H2/H3 factoring, stream/session back-pointers, FIN/reset ownership, and
cross-shard captures.  Repair repeated protocol blocks, chained discrete
branches, payload copies, heap churn, locks, and reference cycles; rerun the
common tunnel corpus under the consistent Clang debug build and run
`git diff --check`.
Slice 3 starts only after this audit is clean.

## Slice 3: protocol-neutral client request-body Tx

### Work

1. Add compile-time request-body policy parameters to `Agent`, `ClientPool`,
   `TLSClientPool`, and `ClientMessage`; propagate them through every H1, H2,
   and H3 instantiation.
2. Define one application request description:
   - no body;
   - known content length; or
   - streaming body, using H1 chunked framing and H2/H3 DATA semantics.
3. Define a templated body producer receiving the builder's concrete
   `ZiTxStream`-compatible body stream and an attempt-local source/cursor.
4. Refactor `ClientMessage::send()` to:
   - acquire `link.transmit(builder)`;
   - emit headers;
   - obtain `builder.body(tx)` when a body is present;
   - invoke the producer;
   - validate known-length production;
   - final-flush the builder/body stream; and
   - map finalization to H1 completion or H2/H3 END_STREAM.
5. Reject conflicting application-supplied `content-length` or
   `transfer-encoding` headers.  `libZhttp` alone derives framing headers from
   the body description.
6. Preserve the bodyless request path without a cursor, body callback, or
   extra allocation.
7. Propagate producer success, short source, over-production, failure, and
   cancellation as typed request outcomes.

### Acceptance criteria and hand-off

- One Agent callback implementation sends known-length PUT and POST over
  H1/TCP, H1/TLS, H2/TLS, and H3/QUIC.
- Tests cover zero-length, exact-buffer, multi-buffer, and configured maximum
  bodies, plus short and over-producing sources.
- Wire assertions prove H1 fixed/chunked syntax, H2/H3 DATA framing, and one
  correctly ordered end-of-stream.
- Bodyless GET retains its existing callback sequence and allocation count.
- Source/producer failure prevents successful completion and leaves no pending
  body buffer or logical stream.
- Applications never write HTTP framing or name protocol-native stream types.

### `GUIDELINES.md` alignment audit and repair

Re-read Tx sharding, uninitialized destination storage, pooled allocation,
CRTP defaults, and hot-path guidance.  Audit body-source storage, builder
layering, final flush, content-length accounting, bodyless fast-path codegen,
and all new captures.  Repair temporary copies, default-before-overwrite,
unidentified heaps, type erasure, off-shard writes, and duplicate H1/H2/H3
send paths; rerun request-builder and engine tests and run `git diff --check`.
Slice 4 starts only after this audit is clean.

## Slice 4: bounded Tx turns, replay, retry, and cancellation

### Work

1. Execute body production on Tx.  Limit each turn only by the named
   production batch and resume through a Tx continuation when the source has
   more data.  Submit produced buffers immediately; do not inspect transport
   credit or wait for queue capacity.
2. Keep the source cursor, produced count, and committed count attempt-local
   and Tx-owned.  Report fixed completion metadata to Rx.  Queued buffers
   remain exclusively owned by the existing lower layer.
3. Separate:
   - semantic permission to replay the request; and
   - mechanical ability to create a fresh body source at byte zero.
4. Require every replayed attempt to create a new cursor.  Never rewind or
   share a partially consumed cursor.
5. Track header commitment and body-byte commitment.  Apply this state to
   redirects, connection retry, H2 retry, H3-to-TLS fallback, cancellation,
   and disconnect.
6. Return `ReplayUnsafe` when replay is required but not permitted or
   reproducible.  Return `Indeterminate` when a non-replayable request may
   have been committed.  Never silently resend it.
7. On cancellation/teardown, prevent new producer calls, drain already-posted
   producer continuations, then let the existing lower layer perform its
   normal queued-buffer teardown before returning to Rx and completing exactly
   once.
8. Do not infer replayability only from GET/PUT/idempotence.  The side-effect-
   safe default for an opted-in body source is non-replayable.

### Acceptance criteria and hand-off

- Large bodies require multiple bounded producer turns without changing H1,
  H2, H3, TLS, TCP, or QUIC transport scheduling.  Instrumentation proves no
  producer call exceeds the configured batch.
- Instrumented sources prove every redirect/retry/fallback replay starts at
  byte zero with a new cursor.
- Non-reproducible, disallowed, and partially committed cases return the
  specified typed result without resend.
- Cancellation before headers, after headers, mid-body, between turns, and
  after final flush produces one result and no later producer callback.
- Shutdown drains all cursors, continuations, buffers, links, attempts, and
  lower-layer transport state.
- No retry decision reads Tx-owned mutable state directly from Rx.

### `GUIDELINES.md` alignment audit and repair

Re-read async continuations, sharding, liveness, callback captures, owner
lifetime, and teardown.  Audit every Rx/Tx hand-off, attempt-generation check,
  cursor owner, retry branch, continuation, and cancellation path.  Repair
locks, atomics used to mask ownership, variable-size captures, owner refs,
unbounded turns, stale continuations, and duplicate retry logic; rerun Agent
retry/fallback/cancel tests under the consistent Clang debug build and run
`git diff --check`.  Slice 5 starts only after this audit is clean.

## Slice 5: H1 ordinary message-body Rx

### Work

1. Replace parser-to-application body spans with a callback-scoped
   `ZiRxStream` entity-body view.
2. Decode fixed-length, chunked, and close-delimited bodies in place:
   - hide chunk-size lines and delimiters;
   - hide trailers until header/trailer callbacks;
   - expose only entity bytes; and
   - enforce body limits against decoded bytes received.
3. Preserve the native single-buffer fast path.  Gather only when the
   application explicitly requests a contiguous framed view across buffers.
4. Invoke the application synchronously on Rx.  It must consume or copy the
   offered bytes before returning; returning with offered input unconsumed is
   an application error, not permission for unbounded retention.
5. Do not parse a subsequent H1 message until the preceding body, trailers,
   and completion are finalized.
6. For close-delimited bodies, order final payload before EOF completion.
   Content-length mismatch, malformed chunking, cancellation, and disconnect
   each terminate exactly once.

### Acceptance criteria and hand-off

- The same Rx callback consumes fixed-length, chunked, close-delimited, and
  zero-length H1 bodies.
- Tests cover one buffer, many buffers, chunk metadata split at every byte,
  trailers, partial `consume()` calls within one callback, and EOF.
- No body callback exposes chunk syntax, trailers, or bytes from the next
  message.
- Sequential keep-alive messages cannot overtake a body callback or terminal
  notification.
- Body limits, short content length, excess bytes, malformed chunking,
  cancellation, and disconnect produce deterministic typed failure.
- Finalization leaves no Rx buffer, view/refill state, parser continuation, or
  link reference.

### `GUIDELINES.md` alignment audit and repair

Re-read Rx stream, scratch-storage, parser control-flow, liveness, and
teardown guidance.  Audit chunk parsing, decoded-byte counters, scratch
gathering, callback loops, sequential-message state, and EOF cleanup.  Repair
hidden copies, fixed scratch arrays, nested control flow, overlong Rx turns,
borrowed-span retention, and stale buffers; rerun H1 parser/engine tests under
the consistent Clang debug build and run `git diff --check`.  Slice 6 starts
only after this
audit is clean.

## Slice 6: H2/H3 ordinary message-body Rx

### Work

1. Apply the Slice 5 application contract to H2 and H3 request and response
   bodies without changing callback shape.
2. H2:
   - hide frame headers and DATA padding;
   - validate content length against decoded DATA;
   - preserve the existing lower-layer stream/connection window behavior; and
   - do not retain application-blocked input after the callback returns.
3. H3:
   - hide DATA frame headers;
   - validate content length against decoded DATA;
   - preserve the existing Zquic flow-control behavior; and
   - order FIN/reset after preceding payload.
4. Empty DATA frames do not cause synthetic body callbacks.  Trailing HEADERS
   are delivered as trailers only after preceding body consumption.
5. Returning from a callback with offered input unconsumed is an application
   error.  Do not introduce a blocked logical-body state or retention queue.
6. Share body-view mechanics with tunnels where semantics are identical;
   retain separate HTTP message/tunnel state only where completion rules
   differ.

### Acceptance criteria and hand-off

- One Rx callback implementation consumes the same corpus over H1, H2, and
  H3.
- H2 tests cover padding, empty DATA, split frames, trailers, multiple
  `consume()` calls within one callback, reset, and GOAWAY.
- H3 tests cover split frame headers/payload, empty DATA, trailers, partial
  consumption within one callback, FIN, both reset directions, and GOAWAY.
- Content-length mismatch and body-limit failures are identical at the public
  result boundary for H1, H2, and H3.
- Finalization leaves empty body-view state, native buffer refs,
  flow-control queues, logical-stream registries, and sessions.

### `GUIDELINES.md` alignment audit and repair

Re-read buffer preservation, lower-layer flow-control ownership, multiplexed
liveness, template factoring, and teardown guidance, plus
`zquic/GUIDELINES.md`.  Audit DATA/trailer ordering, H2/H3 duplication,
callback-scoped lifetime, event ownership, and session close.  Repair copying
queues, per-fragment heap objects, locks, atomic ownership workarounds,
unbounded per-stream work, and reference cycles; rerun the common body corpus
and H2/H3 engine tests under the consistent Clang debug build and run
`git diff --check`.  Slice 7 starts only after this
audit is clean.

## Slice 7: Agent and Service application migration

### Work

1. Agent:
   - replace `responseBody(Request &, ZuBSpan)` with the templated
     `ZiRxStream` callback;
   - order status and headers before the first body callback;
   - count received and consumed bytes separately;
   - delay redirect, retry, fallback, reuse, and terminal result until body
     completion; and
   - implement response discard by draining the same stream path.
2. Service:
   - deliver immutable request metadata immediately after headers;
   - invoke the workload's templated `ZiRxStream` request-body callback;
   - deliver exact-once body/message completion after consumption; and
   - stop discarding request bodies internally.
3. Migrate `zhttp`, `zhttpd`, examples, boundary manifests, and source scans.
   Delete old span callbacks and program-local body buffering used only to
   compensate for the old contract.
4. Document that Rx views/spans are shard-affine and callback-scoped, while
   submitted request/body-source storage remains valid until `completed()`.
5. Extend `zhttp.cc` with a PUT integration workload:
   - describe the request and response JSON records with `ZtStruct`;
   - encode the request body with `ZtJSON` directly into buffers submitted
     through the request `ZiTxStream`;
   - consume and decode the response body through `ZiRxStream`; and
   - validate the decoded response against the submitted record.
6. Extend `zhttpd.cc` to handle that PUT workload:
   - consume and decode the request JSON through `ZiRxStream`;
   - validate it as the corresponding `ZtStruct`;
   - encode the response with `ZtJSON` into buffers submitted through the
     response `ZiTxStream`; and
   - retain GET handling on the same Rx/Tx stream contracts.
7. Preserve direct pooled-buffer delivery from HTTP decoding to application
   consumption and direct application production into HTTP Tx buffers.  Force
   the PUT integration through multiple named, bounded producer turns.
   `zhttpd.cc` consumes each offered span during its callback; JSON parser
   scratch or record storage is permitted only where `ZtJSON` requires it and
   remains bounded by the configured request-body maximum.  Do not add an
   application body queue or change established response scheduling.

### Acceptance criteria and hand-off

- `zhttp.cc` sends both GET and PUT requests through `ZiTxStream`, consumes
  both responses through `ZiRxStream`, and uses the same parser and
  completion path for response discard.
- `zhttpd.cc` consumes both GET and PUT requests through `ZiRxStream` and
  produces both responses through the established direct `ZiTxStream` path.
- The PUT integration test round-trips a `ZtStruct` JSON record:
  `zhttp.cc` encodes it with `ZtJSON`, `zhttpd.cc` decodes and validates it,
  the server encodes the response with `ZtJSON`, and the client decodes and
  validates the result.
- The multi-turn PUT test proves application Tx buffers remain the payload
  storage accepted by HTTP framing and native decoded payload buffers back the
  callback-scoped `ZiRxStream` views.  Any retained JSON bytes are bounded
  workload parser state, not Zhttp buffering or transport admission.
- Agent and Service use identical callback implementations over H1/TCP,
  H1/TLS, H2/TLS, and H3/QUIC.
- Boundary scans reject `ZuBSpan` body callbacks, native stream names,
  protocol-conditioned workload code, and program-local parser/body queues.
- Callback-order tests prove headers, body availability, body completion,
  terminal result, and disconnection ordering with exact-once counts.
- No callback, refill, borrowed span, cursor, or body buffer survives terminal
  completion.

### `GUIDELINES.md` alignment audit and repair

Re-read application/library boundaries, shard ownership, file I/O, callback
captures, lifecycle, and dead-code guidance.  Classify every changed
application declaration under the established executable allow-list.  Audit
file-body handling, response discard, callback order, and
program source closure.  Repair copied body queues, native-type leakage,
protocol branches, off-shard file access, stale callbacks, and compatibility
code; rerun boundary and application tests and run `git diff --check`.  Slice
8 starts only after this audit is clean.

## Slice 8: documentation, diagnostics, and release gate

### Work

1. Update installed-header documentation and `zhttp/README.md` with:
   - replayable known-length PUT;
   - non-replayable streaming POST;
   - incremental Agent response consumption;
   - incremental Service request consumption;
   - H2/H3 Extended CONNECT tunnel use; and
   - callback lifetime, sharding, and cancellation.
2. Add diagnostic counters, using owner-shard primitive fields, for body bytes
   received, consumed, produced, committed, reset, and discarded.
   Diagnostics must not control behavior or require stable atomic snapshots.
3. Run the complete deterministic matrix:
   - H1/TCP, H1/TLS, H2/TLS, and H3/QUIC;
   - `zhttp.cc`/`zhttpd.cc` GET and `ZtJSON`/`ZtStruct` PUT integration;
   - client request Tx and response Rx;
   - server request Rx and response Tx;
   - `ZiTxStream` production and `ZiRxStream` consumption for both GET and PUT
     on both client and server;
   - multi-turn typed PUT production and callback-scoped server consumption;
   - ordered tunnels over H2 and H3;
   - zero, one-buffer, multi-buffer, window-sized, and limit-sized bodies;
   - retry, redirect, fallback, cancellation, reset, GOAWAY, EOF, and
     shutdown; and
   - no-copy, unchanged-lower-layer, and final empty-state assertions.
4. Verify debug, release, ASAN/LSAN, and Valgrind builds using Clang.
   Reconfigure release through `z.config -L`, then run top-level `make clean`,
   `make -j8`, and the complete test suite so no stale mixed-build objects
   remain.

### Acceptance criteria and final hand-off

- README examples compile against installed public headers and exercise the
  actual Agent, Service, and Tunnel APIs.
- Documentation and implementation agree on callback order, replay,
  synchronous Rx lifetime, and terminal results.
- All focused and top-level tests pass without warnings in debug and release.
- ASAN/LSAN and Valgrind report no leak, use-after-free, invalid access, or
  reachable request/stream state after finalization.
- Diagnostic counters add no lock, required atomic accuracy, timer, polling
  loop, or behavior dependency.
- Source scans find no old body-span callback, copying compatibility shim,
  native application stream, or protocol-conditioned application body path.

### `GUIDELINES.md` alignment audit and repair

Perform a complete audit of every file changed by this plan against
`AGENTS.md`, all of `GUIDELINES.md`, and `zquic/GUIDELINES.md` where
applicable.  Inspect allocation/heap IDs, buffer copies, initialization,
constants, containers, CRTP factoring, branching, Rx/Tx ownership, captures,
liveness, teardown, diagnostics, naming, headers, tabs, and dead code.  Repair
every finding and rerun the affected focused tests.  Then repeat the release
top-level clean rebuild, complete test suite, ASAN/LSAN, Valgrind,
source-boundary scans, and `git diff --check`.  The plan is complete only when
this final audit and all reruns are clean.
