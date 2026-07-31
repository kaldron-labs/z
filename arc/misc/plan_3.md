# Plan 3: qlog Event Tracing

## Goal

Implement `audit.md` Priority work item 1: add qlog/event tracing before deeper
interop work.

Current state:

- `zquic` has runtime diagnostics counters and debug logs, but no qlog writer or
  qlog event stream consumable by QUIC tooling.
- Packet, recovery, congestion, path, Retry/NEW_TOKEN, PMTUD, key update, and
  close behavior are sufficiently complete that structured traces will materially
  improve interop debugging.

The target is an optional qlog facility with production-safe hot-path behavior:
disabled by default, compiled out when `Zquic_DEBUG` is not defined, and
offloaded to a dedicated qlog thread when enabled in `Zquic_DEBUG` builds.

## Design

Add `zquic/src/ZquicLog.hh` and `zquic/src/ZquicLog.cc` as the home for all
qlog-related code.  Do not scatter writer implementation, JSON formatting, or
thread lifecycle logic through the packet path.

Use `ZiLog` as the template structure:

- singleton construction and cleanup
- explicit `init()` / `final()` lifecycle
- explicit `start()` / `stop()` lifecycle
- a dedicated low-priority logging thread
- inter-thread communication via `ZmRing`
- `ZmRingFn`-style function records posted to the logging thread
- hot-path enqueue APIs that capture only immediately available primitive
  values, small fixed metadata, and bounded value copies
- all qlog formatting and writing inside the posted lambda body on the qlog
  thread

The qlog thread must be configured alongside the existing `rx` and `tx` thread
configuration.  Treat it as a first-class runtime thread, not a side effect of
the application callback layer.

If qlog is enabled at runtime:
- `Zquic` `Engine` `init`/`start_`/`stop_`/`final` must call `ZquicLog`
  corresponding functions to initialize, start, stop, and finalize it.
- qlog is controlled by `Zquic::Engine`, not by individual links, streams,
  endpoint sockets, or application callbacks.
- `Client` and `Server` inherit the same qlog behavior through
  `EngineParams`; do not add separate client/server qlog configuration unless
  a real semantic difference appears during implementation.

Builds without `Zquic_DEBUG` must compile qlog out.  The disabled path should
reduce to an empty inline/no-op test with no event object construction, no
formatting, no heap allocation, and no cross-thread post.  `Zquic_DEBUG` builds
should still default qlog off unless explicitly configured.

## Depended API Review

Review these APIs before implementation and use them only as described here:

- `ZiFileSink`
  - Actual behavior: `ZiFileSink` is a `ZiSink` implementation for ordinary
    `ZiLog` events.  Its public write path is `pre(ZeLogBuf &, ZeEventInfo &)`
    plus `post(ZeLogBuf &, ZeEventInfo &)`.  `pre()` prepends timestamp,
    thread, severity, component, and function text; `post()` appends newline and
    writes the buffer to its private `ZiFile`.  It does not expose a raw JSON
    record write API.
  - Intended qlog use: do not pipe qlog records through `ZiFileSink::pre()` /
    `post()` because that would corrupt JSON-SEQ with ordinary log prefixes.
    Create `ZquicLogSink`, based on the `ZiFileSink` example, with only the file
    behavior qlog needs: path option, default path handling if needed, file aging
    if enabled, `ZiFile::open(..., ZiFile::Write | ZiFile::GC)`, fallback
    policy, close/reopen on age, and `ZiFile::write()` from the qlog thread.
    Do not add generic sinks or sink fanout to qlog.

- `ZiFile`
  - Actual behavior used here: `ZiFile` provides the raw file open/write/close
    operations used by `ZiFileSink`.
  - Intended qlog use: own the file exclusively on the qlog thread.  Open during
    `ZquicLog::init()`/`start()` on that thread, write only from posted qlog
    lambdas or qlog-thread helper functions, and close during `stop()`/`final()`.
    Rx/Tx threads must never call `ZiFile::write()` for qlog.

- `ZtStruct`
  - Actual behavior: `ZtStruct` provides compile-time field metadata and facets
    consumed by `ZtJSON`.  JSON field names can be controlled with
    `ZuFieldProp::JSON::ID`; save/load filtering is controlled through the
    established Zt field properties.
  - Intended qlog use: define small qlog header/event structs with the JSON
    facet, lower-case qlog field names, and explicit JSON IDs when C++ member
    names differ from qlog names.  Prefer value fields and bounded arrays/spans
    that are already owned by the qlog-thread lambda.

- `ZtJSON`
  - Actual behavior: `ZtJSON::save<Facet>(stream, object)` serializes
    `ZtStruct`-described objects to JSON using stream-style output.  It handles
    JSON field names, quoting/escaping, byte formatting, number formatting, and
    time formatting based on field properties.  `ZtJSON::scan()` is a parser and
    can mutate input while decoding strings; it is for tests/parsing, not qlog
    emission.
  - Intended qlog use: on the qlog thread, build JSON-SEQ records by writing
    ASCII record separator `0x1e`, then `ZtJSON::save<ZuFacet::JSON>(buf,
    record)`, then newline.  Use `ZtJSON::scan()` only in tests that validate
    emitted records, and pass it mutable test buffers.

- `ZtLocalArray`
  - Actual behavior: `ZtLocalArray(Array, size)` constructs an `Array` with an
    initial `ZmVAlloc` stack buffer and heap fallback; `ZtLocalArray(Array,
    length, size)` starts with the specified length.  It extends `ZtArray`-style
    containers and is empty unless constructed with a length.
  - Intended qlog use: use it only on the qlog thread for temporary JSON output
    buffers or bounded value arrays.  For stream-style JSON output, use an array
    type that supports `operator <<`, for example a `ZtArray<char, ...>`-derived
    buffer or `ZeLogBuf` if its fixed builtin capacity is sufficient.  Check
    allocation failure via the container boolean state before writing.

- `ZmRing` and `ZmRingFn`
  - Actual behavior: `ZmRingFn` stores callable records in the ring; captured
    lambdas may allocate through the configured heap/cache if they do not fit in
    the ring record path.  `Ring::push(size)` can block/wait depending on ring
    parameters, while `tryPush(size)` is the non-blocking path.  `push()` returns
    `nullptr` on EOF, no reader, or non-blocking full failure.
  - Intended qlog use: use `ZmRing<ZmRingMW<true>>` or the closest `ZiLog`
    pattern for multi-producer Rx/Tx posts to a single qlog reader.  Use
    `tryPush()` or an explicitly non-blocking ring configuration for qlog event
    enqueue so Rx/Tx never wait behind qlog.  Increment drop/backpressure
    diagnostics if enqueue fails.  Keep lambda captures small enough to avoid
    `ZmRingFn` heap allocation in normal packet-path events.

- `ZmThread`
  - Actual behavior: constructing or running a `ZmThread` starts the target
    lambda; `join()` waits for completion.  Capturing lambdas are heap-allocated
    into the thread context.
  - Intended qlog use: start one qlog thread from `ZquicLog::start()` with
    `ZmThreadParams().name(qlogThreadName).priority(ZmThreadPriority::Low)`.
    Stop by setting ring EOF, joining the thread, and closing the ring, following
    the `ZiLog::stop_()`/`work_()` pattern.

## Phased Implementation

Complete the phases in order.  Do not add broad packet-path instrumentation
before the disabled fast path, qlog thread, non-blocking enqueue path, and file
output path are proven, because later phases depend on those guarantees.

### Phase 1: Compile-Time Gating and No-Op Surface

Dependencies: none.

Implementation:

1. Add build-time qlog gating.
   - Define qlog implementation code under `#ifdef Zquic_DEBUG`.  A secondary
     `ZQUIC_QLOG` alias is acceptable only if it is derived from
     `Zquic_DEBUG`.
   - When `Zquic_DEBUG` is not defined, expose no-op inline APIs in
     `ZquicLog.hh` so call sites compile away.
   - Keep the qlog enabled predicate cheap enough for packet-path use.  It
     should be compile-time false when `Zquic_DEBUG` is not defined and a simple
     runtime flag check when `Zquic_DEBUG` is defined.
   - Add `ZquicLog.hh` to `pkginclude_HEADERS` because `Zquic.hh` call sites and
     public `EngineParams` qlog setters need the declarations.
   - Add `ZquicLog.cc` to `libZquic_la_SOURCES`; compile it to an empty or
     minimal no-op implementation when `Zquic_DEBUG` is not defined if
     conditional automake source lists would add more build churn.

Verification:

- Build with `Zquic_DEBUG` undefined and confirm qlog APIs compile to no-ops.
- Verify disabled qlog call sites do not instantiate event objects, perform
  formatting, allocate, or post to a ring.
- Run focused compile/build tests for `zquic` headers and existing users before
  moving to Phase 2.

Acceptance:

- `ZquicLog.hh` and `ZquicLog.cc` exist and are build-integrated.
- `!defined(Zquic_DEBUG)` builds have a compile-time false qlog enabled
  predicate and no qlog runtime side effects.

### Phase 2: Lifecycle, Engine Configuration, and Thread Ownership

Dependencies: Phase 1 complete.

Implementation:

1. Add `ZquicLog` lifecycle.
   - Implement a singleton modeled on `ZiLog::instance()`.
   - Provide `init(params)`, `final()`, `start()`, and `stop()` entry points.
   - Own a `ZmRing` and logging `ZmThread`.
   - Use deterministic teardown: close/eof the ring, join the qlog thread, flush
     or drop pending records according to explicit stop policy, and release
     writer state.
   - Keep construction/deletion private and singleton-owned.
   - Make repeated `init`/`final` and `start`/`stop` behavior explicit.  Follow
     `ZiLog` where practical, but avoid silently carrying old qlog output paths,
     file state, or ring sizing across a new `Engine::init()`.

2. Add qlog thread configuration.
   - Extend `EngineParams` with qlog options next to the existing constructor
     `rxThread` and `txThread` configuration and the `asyncThread()` setter.
   - Include at minimum:
     - enable/disable flag, default disabled
     - qlog thread name or suffix
     - thread priority, default low
     - ring size
     - output file path
   - Suggested API shape:
     - `EngineParams &&qlog(bool)`
     - `EngineParams &&qlogPath(ZuCSpan)`
     - `EngineParams &&qlogThread(ZuCSpan)`
     - `EngineParams &&qlogRingSize(unsigned)`
   - Store the resolved qlog configuration in `Engine`, just as `rxThread`,
     `txThread`, `asyncThread`, key paths, and transport limits are stored now.
   - Resolve the qlog thread name during `Engine::init_()` and validate that it
     is configured consistently with `ZiMultiplex`/thread configuration.
   - Ensure `Engine::init_()` calls `ZquicLog::init(...)` only when qlog is
     enabled and compiled in.
   - Ensure `Engine::start_()` starts qlog before Rx/Tx can enqueue records.
   - Ensure `Engine::stop_()` prevents new qlog enqueues and stops qlog only
     after Rx/Tx shutdown continuations can no longer post records.
   - Ensure `Engine::final()` clears stored qlog params and calls
     `ZquicLog::final()` after the engine is stopped.

Verification:

- Unit-test lifecycle ordering: `init`, `start`, `stop`, and `final` are
  idempotent where intended and reject or report invalid state transitions where
  not intended.
- Verify `Engine::init_()` owns qlog configuration and `Engine::start_()` /
  `stop_()` / `final()` own qlog thread lifecycle for both `Client` and
  `Server`.
- Verify the qlog thread is started only when qlog is enabled in a
  `Zquic_DEBUG` build.
- Verify stopping sets ring EOF, joins the qlog thread, and closes the ring
  without leaking a thread.

Acceptance:

- qlog is configured through `EngineParams`, inherited by `Client` and `Server`,
  and not controlled by links, streams, endpoints, or app callbacks.
- qlog starts before Rx/Tx can enqueue records and stops only after Rx/Tx can no
  longer post records.

### Phase 3: File Output, JSON-SEQ Serialization, and Sink Diagnostics

Dependencies: Phase 2 complete.

Implementation:

1. Add qlog file writer.
   - Do not add a general sink abstraction, sink fanout, syslog/debug sinks, or
     application-provided sink callbacks.  qlog always writes to a file.
   - Do not use `ZiFileSink::pre()` / `post()` for qlog records.  Those methods
     format ordinary log lines and would prepend non-JSON text.
   - Add `ZquicLogSink`, based on the `ZiFileSink` example: path/options
     storage, file open in init/start, file close in final/stop, optional aging
     if needed, and `ZiFile::write()` from the qlog thread.
   - Model qlog records as `ZtStruct`-described event/header structs where that
     keeps JSON field definitions explicit and reusable.
   - Use `ZtJSON::save<ZuFacet::JSON>(buf, record)` for JSON object
     construction, field naming, escaping, and serialization on the qlog thread.
   - The qlog thread may use `ZtLocalArray` on-stack scratch buffers, with heap
     fallback, when building JSON-SEQ records before writing to `ZquicLogSink`.
   - Use `ZtJSON`, `ZquicLogSink`/`ZiFile`, `ZtLocalArray`, and existing Z
     string/buffer types rather than STL.
   - Open and close output files on the qlog thread.
   - Emit streaming qlog as JSON-SEQ: write `0x1e`, one JSON object, then
     newline for each record.
   - Keep per-connection trace identity stable and include vantage point,
     connection IDs, packet numbers, packet spaces, and timestamps.
   - Keep file open/write failures visible through diagnostics but do not let
     qlog failure perturb transport behavior.
   - Prefer one qlog trace per connection with shared writer state owned by the
     qlog thread.  If the initial implementation writes a single process file,
     include per-connection trace IDs so traces can still be split later.

Verification:

- Unit-test `ZquicLogSink` file open/write/close behavior, fallback behavior,
  and optional aging if implemented.
- Unit-test JSON-SEQ output shape: each record starts with `0x1e`, contains one
  valid JSON object serialized by `ZtJSON::save<ZuFacet::JSON>()`, and ends with
  newline.
- Verify emitted field names are lower-case and qlog records do not contain
  ordinary `ZiLog` prefixes from `ZiFileSink::pre()` / `post()`.
- Verify writer failures update qlog diagnostics and do not affect transport
  state.

Acceptance:

- A minimal enabled qlog run writes a parseable `.sqlog` file with valid header
  and trace records.
- qlog file output is performed only on the qlog thread through `ZquicLogSink`.

### Phase 4: Non-Blocking Enqueue API and Hot-Path Guarantees

Dependencies: Phase 3 complete.

Implementation:

1. Add hot-path enqueue API.
   - Provide narrowly typed helper functions/macros for qlog events such as
     packet received/sent, frame parsed/written, ACK processed, loss declared,
     PTO armed/fired, congestion update, path validation, PMTUD probe, key
     update, Retry/NEW_TOKEN, stateless reset, and close.
   - Each call site must first check the cheap enabled predicate.
   - Capture only values already available at the event origin: primitive types,
     enums, packet numbers, lengths, counters, timestamps, short CIDs, path IDs,
     token status, and small bounded spans copied into fixed/bounded storage.
   - Do not capture pointers, references, `ZiIOBuf`, stream objects, path
     objects, link objects, or mutable spans whose lifetime is owned by Rx/Tx.
   - Do all JSON/qlog formatting and file writing inside the posted lambda on the
     qlog thread.
   - Do not construct `ZtStruct`/`ZtJSON` formatting objects on Rx/Tx hot paths;
     capture the raw values and instantiate/serialize the qlog record on the
     qlog thread.
   - For CIDs, tokens, reset tokens, ACK ranges, and frame payload summaries,
     copy only bounded metadata required by qlog.  Do not copy full packet
     buffers or stream payload bytes.
   - If an event needs derived text, names, or JSON field selection, capture the
     enum/integer state and derive the text on the qlog thread.

2. Add diagnostics and backpressure policy.
   - Track qlog records enqueued, written, dropped, file writer failures, ring
     backpressure, and bytes written.
   - If the qlog ring is full, drop qlog events and increment diagnostics rather
     than blocking Rx or Tx.
   - Make drop/backpressure events visible in qlog when feasible, but never
     recursively enqueue unbounded diagnostics.
   - Keep qlog diagnostics readable from normal zquic diagnostics so tests and
     operators can confirm whether traces are complete or lossy.

Verification:

- Test enqueue from Rx and Tx contexts and verify formatting/writing executes on
  the qlog thread.
- Test full-ring/backpressure behavior with a small qlog ring: event posts are
  dropped, diagnostics increment, and Rx/Tx do not block.
- Test disabled qlog adds no packet-path heap allocations and no formatting.
- Audit representative event call sites for capture rules: no pointers,
  references, `ZiIOBuf`, link/stream/path objects, or mutable spans crossing to
  the qlog thread.

Acceptance:

- Enqueue is non-blocking for Rx/Tx.
- Normal packet-path event captures stay within bounded value metadata and avoid
  `ZmRingFn` heap allocation in the expected/common case.

### Phase 5: Reference Coverage Matrix

Dependencies: Phase 4 complete.  The matrix must be complete before broad
instrumentation begins.

Implementation:

1. Build the event coverage matrix.
   - Before placing instrumentation, review mainstream qlog event output and
     instrumentation points in the reference implementations:
     - `../zngtcp2` primary reference, especially `ngtcp2_qlog` event coverage
     - `../msquic` Microsoft QUIC structured/qlog-style trace coverage
     - `../quiche` Google Quiche qlog trace coverage
     - `../mvfst` Facebook mvfst qlogger coverage
   - Build a coverage matrix mapping qlog event names/categories expected by
     mainstream qlog tooling to `zquic` instrumentation points.  Include packet,
     transport, recovery, congestion, security, path, stream, and connection
     lifecycle events.
   - Identify every `zquic` Rx/Tx/recovery/timer/path/security point needed to
     generate the matrix events that apply to implemented `zquic`
     functionality.  If a qlog event is omitted because the underlying QUIC
     feature is not implemented, document that omission in the matrix.

Verification:

- Cross-check the matrix against `../zngtcp2` first, then `../msquic`,
  `../quiche`, and `../mvfst`.
- Confirm every matrix row has a concrete `zquic` instrumentation point, a test
  plan entry, or a documented omission tied to an unimplemented QUIC feature.
- Review the matrix against mainstream qlog tooling expectations before adding
  remaining instrumentation.

Acceptance:

- The matrix is checked in or generated by tests.
- The matrix is the source of truth for Phase 6 instrumentation and Phase 7
  qlog output tests.

### Phase 6: Event Instrumentation

Dependencies: Phase 5 complete.

Implementation:

1. Add instrumentation in coverage order.
   - Insert logging at the `zquic` Rx/Tx/recovery/timer/path/security points
     identified by the Phase 5 matrix.
   - Coverage group 1: connection start/close, packet sent/received/dropped,
     space, packet number, datagram size, and basic frame summaries.
   - Coverage group 2: ACK ranges, RTT sample, loss declaration, PTO/loss timer
     scheduling and expiry.
   - Coverage group 3: congestion bytes-in-flight, cwnd, ssthresh, persistent
     congestion, and ECN counters.
   - Coverage group 4: path validation, PMTUD, Retry, NEW_TOKEN, stateless
     reset, key update, and key discard.
   - Keep existing diagnostics counters; qlog supplements them.

Verification:

- After each coverage group, run its focused qlog output tests before adding the
  next group.
- After each coverage group, run the relevant existing focused tests for touched
  behavior, for example packet/frame, recovery, stream, runtime, path/PMTUD, and
  Retry/NEW_TOKEN tests.
- Re-audit each new call site against capture and allocation rules before
  proceeding to the next group.

Acceptance:

- Every matrix event for implemented functionality has an instrumentation point
  and at least smoke-level output validation.
- No existing diagnostics counter is removed or replaced by qlog-only behavior.

### Phase 7: Test Hardening and Final Verification

Dependencies: Phase 6 complete.

Implementation:

1. Add and harden tests.
   - Add unit tests for the qlog disabled fast path when `Zquic_DEBUG` is not
     defined and for runtime-disabled mode when `Zquic_DEBUG` is defined.
   - Test lifecycle: init/start/stop/final are idempotent where intended and do
     not leak the qlog thread.
   - Test ring enqueue executes formatting/writing on the qlog thread.
   - Test generated qlog is parseable for a handshake and a stream transfer.
   - Test packet, ACK/loss, congestion, path validation/PMTUD, Retry/NEW_TOKEN,
     key update/discard, stateless reset, and close events at least at smoke
     level.
   - Test disabled qlog adds no packet-path heap allocations using available
     allocation instrumentation or a narrow test hook.
   - Test qlog ring backpressure drops events without blocking transport.

Verification:

- Validate qlog output with both local JSON-SEQ parsing and, where available,
  mainstream qlog tooling.
- Run full `zquic` and dependent `zhttp` regression suites.
- Review the final implementation against `GUIDELINES.md`.

Acceptance:

- All final acceptance criteria below are satisfied.

## Acceptance Criteria

- qlog code is isolated in `ZquicLog.hh` and `ZquicLog.cc`.
- qlog is disabled by default.
- qlog is compiled out when `Zquic_DEBUG` is not defined.
- When enabled in a `Zquic_DEBUG` build, qlog runs on a dedicated thread
  configured alongside Rx and Tx threads.
- `EngineParams` owns qlog configuration and `Engine::init_()`/`start_()`/
  `stop_()`/`final()` own the `ZquicLog` lifecycle.
- Rx/Tx event origins enqueue compact lambdas through `ZmRing`; all formatting
  and writing happen on the qlog thread.
- No qlog hot-path call captures pointers, references, mutable buffers, or
  transport-owned objects whose lifetime does not cross threads.
- Full qlog/event tracing does not replace existing diagnostics counters.
- Ring backpressure drops qlog events instead of blocking transport.
- Generated qlog for a handshake and stream transfer is parseable by standard
  qlog tooling or a local schema/JSON-SEQ parser.
- qlog output tests verify JSON-SEQ framing, required qlog header fields, event
  ordering for handshake/packet/ACK/loss/close paths, lower-case JSON field
  names, and absence of ordinary `ZiLog` text prefixes.
- Event coverage is aligned with mainstream qlog tooling expectations and
  cross-checked against `../zngtcp2` as the primary reference plus `../msquic`,
  `../quiche`, and `../mvfst` as secondary references.
- A checked-in or test-local event coverage matrix maps expected qlog
  events/categories to concrete `zquic` instrumentation points and documents
  any omitted events whose underlying QUIC feature is not implemented.
- Disabled qlog performs no heap allocation and no formatting on packet hot
  paths.
- The full `zquic` regression suite passes: `make -C zquic/test test`.
- The full dependent `zhttp` regression suite passes:
  `make -C zhttp/test test`.
- The implementation is reviewed against `GUIDELINES.md`, especially hot-path
  allocation/copy rules, thread ownership, shutdown behavior, and Z Framework
  API usage.

## Notes

- Follow `ZiLog` closely for singleton shape, ring posting, worker thread
  lifecycle, and `ZiFileSink`-style file handling in `ZquicLogSink`.  Do not add
  `ZiLog`'s general sink abstraction or multiple-sink behavior to qlog.
- Keep the first implementation intentionally compact.  Add enough event
  coverage to debug interop and recovery timelines before adding every optional
  qlog field.
- Prefer lost qlog events over transport latency.  qlog must never become a
  reason for Rx or Tx to block.
- The qlog implementation should be suitable for `Zquic_DEBUG` developer and
  staging builds; builds without `Zquic_DEBUG` should compile it out unless a
  future explicit requirement changes that policy.
