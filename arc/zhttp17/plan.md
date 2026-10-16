# Push-Driven HTTP Body Streaming Plan

## Objective

Unify fixed and streaming response-body production under the existing
application-push `body(emit)` contract.

The application initiates every production turn by calling `emit(write)`.
`zhttp` synchronously invokes that turn's `write` callback with a suitable
protocol-neutral Tx stream. The application writes directly to the stream and
returns the turn outcome. `zhttp` never persists a `write` callback.

For a streaming body, returning `Stream` leaves the HTTP response open. The
application may retain `emit` and call it again when it is ready for another
turn. The queue backing the Tx stream provides buffering, ordering,
backpressure, and rate limiting; `zhttp` does not poll the producer and does
not call a retained writer repeatedly.

The redesign must:

- replace the pull-oriented `next(max, done)` path with application-pushed
  production turns;
- preserve pooled `ZiIOBuf` use and direct writes to uninitialized storage;
- keep every `write` invocation synchronous and Tx-shard-affine;
- permit `emit` to be retained only for an open streaming body;
- make a retained emitter safe after reset, disconnect, or shutdown;
- support an intentional stream abort distinct from build or Tx failure;
- preserve protocol-neutral application code across H1, H2, and H3;
- remove `ResponseOutcome` and the continuation ownership cycle.

This is an intentional repository-wide breaking API change. Implement the new
contract atomically and propagate it through every dependent in the same change.
Do not provide a legacy `bool` writer adapter, overload, forwarding shim, or
deprecated `next(max, done)` path.

## Prescribed Source Changes

Implement the change in these locations:

- `zhttp/src/ZhttpCore.hh`: declare `WriteOutcome` beside `BodyPolicy` and
  `BodyType`.
- `zhttp/src/ZhttpCore.cc`: add `ZtEnumImplStruct(WriteOutcome)`.
- `zhttp/src/Zhttp.hh`: change `invokeBodyWriter()` and every `MessageTx` body
  path to use `WriteOutcome::T`; fixed writers accept `End` or `Failed` and
  reject streaming-only outcomes.
- `zhttp/src/ZhttpTransport.hh` and `ZhttpTransport.cc`: replace
  `ResponseOutcome` transport completions with `bool`, then delete the enum and
  its implementation.
- `zhttp/src/ZhttpServer.hh`: replace `AsyncBody_`, `HasAsyncBody`,
  `BodyChunkFn`, and producer completion accounting with the push stream driver
  and emitter control prescribed below. Add the response-Builder lifecycle
  `close()` callback and rename the existing post-response connection-close
  query to `disconnect() const`. Keep template-invariant task/state declarations
  at `Zhttp` namespace scope rather than nesting them in `Server<App_>`.
- `zhttp/src/ZhttpH1.hh`, `ZhttpH2.hh`, and `ZhttpH3.hh`: retain their existing
  body-stream framing, adding only the terminal operations needed to map `End`
  and `Abort` exactly as specified below.
- `zhttp/src/ZhttpClient.hh`: propagate `WriteOutcome` through client request
  body transmission. Client request writers remain single-turn: `Stream` and
  `Abort` are rejected. Multi-turn streaming request bodies are explicitly out
  of scope for this change.
- `zhttp/util/zhttp.cc`, `zhttp/util/zhttpd.cc`, all `zhttp/test`,
  `zhttp/itest`, `zhttp/example`, `zhttp/bench`, and `zhttp/interop` Builders,
  `zrest/src/ZrestClient.hh`, `zrest/src/ZrestServer.hh`, and every transitive
  dependent in `zrest` or another module, including `example`, `bench`, and
  `interop` directories: replace
  successful writer `return true` with `WriteOutcome::End`, replace writer
  failure with `WriteOutcome::Failed`, and convert server
  streaming producers from `next(max, done)` to retained-emitter turns.
- `zhttp/README.md`: replace the pull-streaming documentation and examples with
  the normative push contract, and delete its contradictory outbound-trailer
  claim. Receive-side trailers remain documented and supported.

Run `rg` over the entire repository before completion. No body writer returning
`bool`, no `next(max, done)` body producer, and no `ResponseOutcome` reference
may remain.

## Control Flow

The streaming contract is push-oriented:

```text
application                         zhttp
    |                                 |
    |-------- emit(write) ----------->|
    |<------- write(txStream) --------|  synchronous on Tx
    |-------- WriteOutcome ---------->|
    |                                 |
    |  if Stream: retain emit          |
    |  and call it on a later turn     |
```

`emit` is the durable capability for an open stream. Each `write` lambda is a
single-use, callback-scoped production operation.

There is no library-owned wake callback and no persistent library-owned writer.
The application schedules its own later turn and invokes its retained emitter.

## Write Outcomes

Add:

```c++
ZtEnumStruct(ZhttpAPI, WriteOutcome, int8_t,
  End, Stream, Abort, Failed);
```

The outcomes have these meanings:

- `End`: the body completed successfully. For a fixed body this is the only
  successful outcome. For a streaming body, `zhttp` emits the native final
  boundary and completes the response.
- `Stream`: the current turn completed successfully and the response remains
  open. The application may invoke its retained `emit` for a later turn. This
  is valid only for a streaming body.
- `Abort`: intentionally abort an open stream without classifying the event as
  an application build failure or transport failure. This is valid only for a
  streaming body.
- `Failed`: the turn failed. Application code returns it for serialization,
  body construction, or an observed invalid Tx stream. `zhttp` also promotes
  queue refusal, an invalid stream, or failed flushing/sending to this effective
  outcome. Failure origin remains known to the path which detects it and may be
  used for existing diagnostics, but it is not part of the public outcome.

An `Abort` turn may write bytes. Those bytes precede the intentional abort and
may still be dropped while queued when the protocol transport is disconnected
without draining. No transactional retraction or special accounting is needed.

`Reset` and `Cancelled` are not write outcomes. They describe external teardown
origin, not something produced by an application write turn.

Apply an outcome with one flat `switch`; do not duplicate H1/H2/H3 outcome
blocks or build a chained-`if` dispatcher. Factor protocol-specific terminal
operations behind the existing statically dispatched Tx operations.

## Builder Contract

`body(emit)` remains the only application-facing body-production entry point:

```c++
template <typename Emit>
void body(Emit &&emit)
{
  emit([this](auto &tx) {
    writeBody(tx);
    return tx ? Zhttp::WriteOutcome::End :
      Zhttp::WriteOutcome::Failed;
  });
}
```

Cardinality and lifetime rules:

- `BodyPolicy::None`: `body()` is not called.
- `BodyPolicy::Fixed`: `body()` must call `emit(write)` exactly once and the
  writer may return `End` or `Failed`; only `End` succeeds.
- `BodyPolicy::OptionalFixed`: `body()` may call `emit(write)` zero or one
  times; an emitted writer may return `End` or `Failed`.
- `BodyPolicy::Stream`: `body()` must call `emit(write)` at least once. After a
  writer returns `Stream`, the application may call `emit` again sequentially
  before `body()` returns or retain it for a later turn.
- `BodyPolicy::OptionalStream`: `body()` may emit no writer, producing no body,
  or emit an initial writer. After that writer returns `Stream`, the application
  may emit further sequential turns or retain and reuse `emit`.
- An initial or later `End` invalidates the emitter before `zhttp` emits the
  final HTTP boundary.
- An initial or later `Abort` or `Failed` invalidates the
  emitter and terminates the response through the corresponding path.
- Returning `Stream` from a fixed-body writer is a Builder contract failure.
- Returning `Abort` from any non-streaming writer is a Builder contract failure.
  These violations use `ZiAssert` and fail the operation in release builds.
- Emitting more than once before the preceding writer returns is a Builder
  contract failure. Reentrant emission is not supported.

The original `body()` call and every emitted `write` callback run synchronously
on the Tx shard. A retained emitter is Tx-shard-affine. An application event
arriving on another shard must post a thin continuation to Tx and call `emit`
there; `zhttp` must not retain the turn's arbitrary `write` type merely to
perform cross-shard dispatch.

This is a normative `Builder` contract and must be documented beside `body()`
in `Zhttp.hh`: `zhttp` performs no caller dispatch. The application must call a
retained emitter on the owning Tx shard, dispatching there itself when needed.
Wrong-shard, active/reentrant, and malformed-state calls are programmer errors:
use `ZiAssert` and also return `false` when assertions are compiled out. A late
call on an ordinarily closed emitter simply returns `false`; its writer is not
invoked.

The server `ResBuilder` base supplies a side-effect-safe no-op `void close()`
default, so the server calls it directly without a detector trait. A streaming
application which retains producer state overrides it. Required-body absence, invalid
outcomes for the selected policy, and duplicate/reentrant emission likewise use
`ZiAssert` and follow the documented release-build failure path.

## Retaining `emit`

The application may retain `emit` only when all of the following are true:

- the selected body policy is `Stream` or `OptionalStream`;
- an emitted writer has returned `Stream`;
- the response has not subsequently ended, aborted, failed, reset, disconnected,
  or entered server shutdown.

The application may provisionally move `emit` into its producer state before
the initial call so that the same concrete emitter is available afterward. It
commits that retained state only if the initial writer returns `Stream`; it must
drop the stored emitter immediately for every other outcome.

A streaming application which cannot name the generic emitter type puts it in a
concrete `Producer<ZuDecay<Emit>>` state allocated from an identified `ZmHeap`.
That state also owns every datum used by future writers. The application's
repeatable type-erased resume callback uses ordinary bound `ZmFn::fn` with one
`ZmRef<Producer>` and a stateless `Producer *` callback. `ZmFn::mvFn` is
one-shot and must not be used for a retained emitter that can be called more
than once. The producer therefore derives from `ZmPolymorph`, whose atomic
intrusive reference count permits callback copies to cross shards. Do not
capture the `ResBuilder_` or several owner references.

The event source copies its stored resume callback to a callback-local value
before invoking it. A later `emit(write)` may synchronously return `End`, causing
`zhttp` to call Builder `close()` and unsubscribe/clear the stored callback
while that local copy remains live; the initial turn defers `close()` until
`body()` returns. A queued copy consequently retains only the
producer state, not a destroyed `ResBuilder_`; its emitter has already been
invalidated and returns `false`. Builder `close()` calls the producer state's
idempotent close operation before dropping the Builder's producer reference.

`close()` must only mutate Tx-owned Builder/producer state. Cleanup owned by
another shard is posted to that shard with fixed metadata or one owning handle
and completed by its continuation; it is never accessed directly from Tx.

The application owns its scheduling, fairness, batching, and time slicing. A
producer can write one application frame, several frames, or as much as the Tx
stream accepts in a turn, then return `Stream` and schedule another turn later.
Immediate sequential turns are legal, but the application must bound work per
Tx turn and post a continuation for the remainder; it must not loop indefinitely
and starve other Tx work.

For SSE/MCP, the event source can retain a continuation containing `emit` and
invoke it for each available event or keepalive. No library polling is needed.

## Emitter State and Invalidation

A retained emitter must not strongly retain the response driver. Otherwise an
application-held emitter would prevent teardown after a reset or shutdown.

Implement the emitter as `Server::BodyEmit_<Driver, Heap>`, deriving from
`Heap` and `ZmObject`. Its atomic reference count is intentional: application
callback copies may be retained or destroyed off-shard. Only its reference
count is shared; its driver pointer remains Tx-owned. Use `ZuDerive` for the
complex public type rather than an alias:

```c++
template <typename Driver>
ZuDerive(BodyEmit, (BodyEmit_<Driver,
  ZmHeap<"Zhttp.Server.BodyEmit", BodyEmit_<Driver, ZuVoid>>>));
```

It is the only object an application-retained emitter captures:

```text
application continuation -> Emit control -> non-owning active driver
                                            ^
                                            |
                                       response driver
```

`BodyEmit_` contains an immutable copy of the Tx thread ID and one Tx-owned raw
`Driver *`; order the final members according to their measured alignment to
avoid padding. The response driver owns one
`ZmRef<BodyEmit<Driver>>` while active. The concrete emitter captures another
`ZmRef<BodyEmit<Driver>>`. `BodyEmit_` never owns the driver. This exact
ownership direction prevents an application-retained emitter from retaining
the response, link, or server.

Give `BodyEmit_` a templated internal `emit_(Write &&)` member. It first compares
the immutable thread ID with the current thread, using `ZiAssert` and returning
`false` in release builds before touching the Tx-owned pointer on a mismatch.
It then checks its raw driver and synchronously calls
`driver->turn_(ZuFwd<Write>(write))` while `write`
remains on the caller's stack. The emitter passed to `body()` is a generic
lambda capturing the `BodyEmit_` reference and forwarding each writer into that
member. Do not add a virtual writer, `ZmFn` wrapper, or heap allocation for an
individual write turn.

The emitter returns `bool`:

```c++
bool accepted = emit(write);
```

`true` means the writer was synchronously invoked. `false` means the stream was
already terminal or the emitter was otherwise invalid, and the writer was not
invoked. This lets an application discard an obsolete continuation without a
separate cancellation callback.

On every terminal path, invalidate the control before releasing the driver or
application response. A late emitter call then returns `false` without invoking
its writer or accessing destroyed response state.

When a later writer returns `End`, invalidate the emitter and invoke Builder
`close()` synchronously, before the final bytes are acknowledged. For the
initial writer, invalidate synchronously but invoke Builder `close()` immediately
after `body()` returns, as prescribed below. Further application turns are
rejected from the terminal outcome; successful response completion remains
pending until the existing Tx completion reports that the final boundary was
sent.

Implement invalidation as `BodyEmit_::close_(Driver *)`. It asserts Tx ownership,
compares the supplied pointer with the active raw pointer, clears it, and is
idempotent for subsequent terminal paths. The driver calls `close_(this)` before
dropping its own control reference. A non-null pointer mismatch is a programmer
error: use `ZiAssert` and leave the actual pointer untouched in release builds.

Also add a server-response `close()` lifecycle callback to the `ResBuilder`
contract. Because `ResBuilder::close() const` currently means "disconnect the
connection after a successful response", rename that query to
`disconnect() const` throughout the repository as part of this intentional
breaking change. The new non-const `void close()` callback means "release the
application producer for this response". On every terminal streaming path,
after invalidating `BodyEmit_` and before releasing the response Builder, call
`appResponse->data().close()` exactly once. A streaming Builder which retains
the emitter must destroy it in this callback and must synchronously initiate
removal of its event subscription; shard-owned subscription cleanup may finish
in the posted owner-shard continuation described above. Early
destruction of an unstarted application `ResBuilder_` naturally drops its
retained-emitter member as part of member destruction. For an active response,
ordered teardown must call `close()` first; the destructor asserts that no active
subscription remains and does not serve as the primary cleanup mechanism. The
callback runs synchronously on Tx and must not call the emitter.

Emitters are strictly Tx-shard-affine. Invalidation and emission are serialized
on Tx, so `BodyEmit_` contains no lock and no atomic state beyond the
cross-shard-safe `ZmObject` reference count. Applications must use their own
thin `txRun`/`txInvoke` dispatcher before calling a retained emitter from a
different shard.

## Stream Driver Ownership

Replace `AsyncBody_` with `Server::StreamBody_<Profile, Link_, Heap>`. It is
strictly Tx-affine, so derive it from `ZuObject` and hold it with `ZuRef`; do not
pay for an atomic `ZmObject`/`ZmRef`. Preserve the tunable fixed-size allocation
model and expose a thin derived public type:

```c++
template <typename Profile, typename Link_>
struct StreamBody : public StreamBody_<Profile, Link_,
    ZmHeap<"Zhttp.Server.StreamBody",
      StreamBody_<Profile, Link_, ZuVoid>>> {
  using Base = StreamBody_<Profile, Link_,
    ZmHeap<"Zhttp.Server.StreamBody",
      StreamBody_<Profile, Link_, ZuVoid>>>;
  using Base::Base;
};
```

Inside `StreamBody_`, define `using Self = StreamBody<Profile, Link_>;` and
`using Emit = BodyEmit<Self>;`.

The active-stream registry is the sole long-lived owner of each `StreamBody_`.
`StreamBody_` is a shorter-lived dependent and uses raw back-pointers to its
longer-lived owners. It contains exactly:

- raw `Server *server`;
- raw `Link_ *link`;
- raw `ResBuilder *appResponse`;
- `ResponseTx<Profile, ResBuilder_, true, true> response`;
- `ZmRef<Emit> emit`;
- a raw pointer to its owning active-stream task node;
- one `int8_t state` holding the `StreamState` value.

Declare the template-invariant internal tag at `Zhttp` namespace scope without
runtime enum metadata:

```c++
struct StreamState {
  enum {
    Initial, InitialWriting, InitialOpen, InitialTerminal,
    Writing, Open, Terminal
  };
};
```

Store it in signed `int8_t`. Do not represent it with separate
`initial`, `active`, and `terminal` booleans, and do not add a cumulative
`produced` diagnostic member when the per-turn Tx stream already reports the
only byte count required by the state transition. Order members by alignment
and size, with immutable owner pointers first and the compact Tx state last;
audit the final layout for avoidable padding.

Retain the existing type-erased task registry but rename it `StreamTaskQ`.
Each move-only task directly type-erases the registry's sole
`ZuRef<StreamBody_>` as one driver pointer plus static stop/release function
pointers. Its templated constructor steals the `ZuRef`; its destructor releases
that reference. This avoids a type-erased callback and its possible allocation.
The driver
does not own the server, link, response node, or task. The corresponding
`Responses`/link owner keeps the response node alive and may not release it or
the link until its stream task has been drained. Remove the task immediately on
`End`, `Abort`, `Failed`, peer reset, or ordinary response teardown.
`Server::stop_()` prevents new response ingress and begins draining stream tasks
on Tx without waiting for application activity. It must not drain an unbounded
list in one scheduler turn.

Declare these template-invariant types once at `Zhttp` namespace scope, not once
per `Server<App_>` instantiation. Use a derived Z container type to keep the
complex instantiation out of dependent symbols:

```c++
struct StreamTask {
  void *driver;
  void (*stopFn)(void *);
  void (*releaseFn)(void *);
  // move-only construction, stop_(), release_(), and destruction as above
};
ZuDerive(StreamTaskQ, (ZmList<StreamTask,
  ZmListNode<StreamTask, ZmListHeapID<"Zhttp.StreamTask">>>));
```

The separate task node is a deliberate exception to allocation consolidation:
one type-erased queue must own drivers of several `Profile`/`Link_` concrete
types, while the driver must remain non-polymorphic. Both allocations use
identified recycling heaps; direct function-pointer type erasure avoids a third
callback allocation, and no control/callback allocation occurs per emitted body turn. Body bytes
continue to use only the existing pooled `ZiIOBuf` allocation path.

Add `enum { StreamStopBatch = 64 };` beside the existing zhttp work-batch
constants, with a comment that it matches the established 64-item H2/H3/Client
scheduler fairness bound. `stopStreams_()` shifts at most
that many tasks. For each shifted node it invokes `task->stop_()`; the local
node remains alive during that call and releases its sole owning driver
reference when destroyed. `StreamBody_::stopTask_()` first clears its
now-unlinked raw task pointer and never calls `delNode()` for that path. Post
another Tx continuation when tasks remain, and invoke the final
transport-stop continuation only after the queue is empty. Do not snapshot the
queue count, retain an iterator across callbacks, or use a blocking wait.

Delete `m_bodyPending`; a push stream has no library request outstanding at the
application. `final()` asserts that `StreamTaskQ` is empty, all link/response
owners have already drained their dependent stream drivers, and retained
response accounting is zero. Owners must not delete drivers from destructors;
their destructors assert that the earlier teardown phases left no dependents.
Replace the existing `alignas(Zm::CacheLineSize) BodyTaskQ` member in place with
the Tx-owned `StreamTaskQ`, preserving the cache-line boundary from shared/server
state and the default `ZmNoLock` container policy.

`StreamBody_::start_()` is Tx-only, begins with `ZiAssert` for the owning shard,
and performs this exact sequence:

1. Register the stream task as the sole owner of the driver and install only a
   non-owning link response-cancellation back-pointer/callback.
2. Allocate its `BodyEmit_`, set the control's raw driver pointer, and create the
   generic emitter lambda capturing only the control reference.
3. Call `appResponse->data().body(emitter)` exactly once on Tx.
4. Use the `StreamState` transition itself to record whether an initial emission
   was accepted; do not maintain a parallel count or boolean.
5. For required `Stream`, terminate with `Failed` if no initial emission
   occurs.
6. For `OptionalStream`, if no initial emission occurs, invalidate the emitter,
   call Builder `close()` to discard any provisionally retained emitter, remove
   the task, and send the existing no-body response path.
7. An initial writer returning `Stream` enters `InitialOpen`. Accept another
   sequential emission from that state by entering `InitialWriting` again;
   immediate turns are allowed while earlier bytes remain queued. `start_()`
   changes `InitialOpen` to `Open` only after `body()` unwinds.
8. If an initial writer is terminal, use `InitialTerminal` to invalidate/detach
   immediately but defer only the application Builder `close()` call until
   `body()` returns. This prevents reentrant destruction of producer state while
   its `body()`/writer frame is active; it does not persist or reinvoke `write`.
9. After `body()` returns, retain no application callback other than copies of
   the emitter which the application itself chose to keep.

The existing `Responses` object remains the sole queue owner of the active
`ResBuilder` node. It must retain that node while its raw pointer is installed
in a stream driver. Terminal response handling first detaches all raw callbacks
and closes the application producer, then removes the stream task, and only
then releases the response node. There is no reference-count edge from a
shorter-lived driver back to `Responses`, its link, or the server.

Change `startResponse_()` so every server response whose policy satisfies
`BodyPolicy::streaming(policy)` is handled by `StreamBody_`; remove callback
detection entirely. Change `sendResponse_()` to handle only no-body and fixed
policies. Keep `MessageTx::streaming()` solely for single-turn outbound paths
which cannot retain an emitter, including current client requests, and require
their writer to return `End`. A `Stream`, `Abort`, or `Failed`
result from that single-turn path fails the operation immediately through its
existing owner-specific failure path.

## Production Turn

For each accepted `emit(write)` call:

1. Switch on `StreamState`: accept `Initial` or `InitialOpen` by entering
   `InitialWriting`, and accept `Open` by entering `Writing`. Diagnose either
   writing state as reentrant and return `false` for either terminal state.
   Programmer-error states use `ZiAssert` and still return `false` in release
   builds.
2. Preserve whether this is the initial or a later turn in the state value; do
   not add a companion boolean.
3. Construct the appropriate protocol body Tx stream over the existing pooled,
   queue-backed transport path.
4. Invoke `write(tx)` exactly once and synchronously.
5. Flush the Tx stream before inspecting its final validity.
6. If the bounded Tx queue refuses output or the Tx stream is invalid, override
   the returned outcome with `Failed` and fail the stream.
7. Apply the effective outcome with one `switch`, setting `InitialOpen`/`Open`
   for `Stream` and `InitialTerminal`/`Terminal` before a terminal callback:
   - `End`: finish the protocol body and response;
   - `Stream`: leave the response and emitter active for a later application
     call;
   - `Abort`: intentionally abort the stream after any bytes written by that
     turn;
   - `Failed`: terminate the stream; the detecting path records its known
     application/build or transport origin in the existing diagnostics.

The Tx stream and all spans obtained from it are valid only for the synchronous
writer call. The application must not retain them after `write` returns.

The queue below the Tx stream is bounded and remains responsible for buffering,
ordering, backpressure, and rate limiting. The response driver must not
introduce an additional retained body buffer or schedule speculative producer
turns. Saturation/refusal fails the stream; there is no retry outcome or
capacity-ready callback in this contract.

After a writer returns `Stream`, the application may immediately invoke another
turn while bytes from previous turns remain queued. Turns are still synchronous,
non-reentrant, and ordered. The existing transport queue bound is the hard
aggregate bound; crossing it makes the current turn `Failed` and terminates the
stream.

The first accepted streaming turn commits and flushes the response headers
immediately, even when it produces zero body bytes and returns `Stream`. Later
zero-byte `Stream` turns are valid no-ops. For `OptionalStream`, body absence is
decided only when the initial `body(emitter)` call returns without any accepted
emission. Once its first writer is invoked, including a zero-byte `Stream`
writer, the response is body-bearing and cannot revert to the no-body path.

Streaming turns do not call `retainResponse_()` and do not accumulate their
produced bytes in `Responses::m_retainedBytes`. That budget remains for fixed
messages whose headers/body must be retained transactionally. Streaming bytes
belong to the existing bounded transport Tx queue and are released by its normal
send completion. Queue refusal or an invalid Tx stream produces `Failed`.

Streaming body policies and an application-supplied `Content-Length` are
mutually incompatible on every protocol. Reject that Builder combination with
`ZiAssert` and fail the response in release builds. H1 always uses chunked
framing for a stream; H2/H3 never accept a promised fixed length for a
multi-turn stream.

## Protocol Mapping

Successful streaming remains protocol-neutral at the Builder layer:

- H1 uses chunked transfer encoding. `Stream` leaves the chunked response open;
  `End` emits the final zero chunk.
- H2 maps each turn's body bytes to DATA frames. `End` emits the native
  end-of-stream boundary.
- H3 maps each turn's body bytes to DATA frames. `End` finishes the QUIC request
  stream normally.

Intentional `Abort` maps to:

- H1: call `ZiConnection::close` and disconnect the TCP/TLS connection without
  draining queued bytes, because an individual H1 response cannot be reset
  independently. Cancel queued responses belonging to that connection.
- H2: disconnect/reset the affected HTTP/2 stream without draining it while
  preserving sibling streams.
- H3: disconnect/reset the affected QUIC request stream without draining it
  while preserving sibling streams, including the appropriate receive-side
  stop when the request direction remains open.

Use the existing protocol error appropriate for application-requested response
abortion: H2 uses `H2::Error::Cancel` and H3 uses
`H3::RequestCancelled`. Use the protocol implementation's most appropriate
RESET_STREAM/STOP_SENDING combination for the stream state. Do not count
`Abort` as `Failed`.

Keep this mapping shard-correct. The response reset and queued-Tx cancellation
run only through existing Tx-owned protocol operations. If H2/H3 must change
request-receive state or initiate a receive-direction stop, post a thin Rx
continuation carrying only the fixed stream ID/error code; do not inspect or
mutate Rx state from the Tx terminal function. Conversely, a peer reset detected
on Rx posts through the link's existing Rx-to-Tx path; Tx resolves the currently
installed raw cancellation callback after the handoff rather than capturing a
`ZuRef<StreamBody_>` across shards.

The same disconnect-without-drain rule applies after a `Failed` turn, including
after earlier bytes reached the peer: H1 closes the socket; H2/H3 disconnect
only the affected stream. Never attempt to replace a partially transmitted
response with an HTTP error or complete it normally. Increment exactly one
origin-appropriate existing failure counter at the point which detects the
failure.

## External Teardown

Peer reset/disconnect, local server shutdown, transport destruction, and queued
response cancellation do not invoke a synthetic writer and do not manufacture
an `Abort` outcome.

For an open streaming response:

1. Mark the response driver terminal on Tx.
2. Invalidate its emitter control.
3. Detach every non-owning link/transport callback and completion hook.
4. Call the application response Builder's `close()` exactly once so it drops
   its retained emitter and event subscription.
5. Remove the driver from the active-stream registry.
6. Release the response node from its `Responses` owner.
7. Continue the protocol-specific connection or stream teardown.

The application remains responsible for its own event subscriptions and stored
continuations, but the Builder `close()` callback gives it deterministic
notification and requires it to destroy the retained emitter. A continuation
already queued before `close()` may still run afterward; its copied emitter is
invalid and returns `false` without invoking the writer. Producer state captured
by an emitter must therefore either be owned by the response Builder or be
separately lifetime-managed until queued continuations drain; it must never rely
on a destroyed `ResBuilder_`.

Server stop must invalidate and release all active streaming drivers without
waiting for another application emission.

Implement three terminal driver functions and route every terminal edge through
exactly one of them:

First implement a shared, idempotent `closeProducer_()` prefix which marks the
driver terminal, invalidates `BodyEmit_`, and detaches raw callbacks/completions.
It invokes the application Builder's `close()` exactly once unless state is
`InitialTerminal`; `start_()` performs that one deferred call immediately after
`body()` returns. Every terminal function and external teardown path uses this
common sequencing before releasing task or response ownership. The state
transition itself guarantees exactly-once close; do not add a `closed` boolean.

- `end_()`: run `closeProducer_()`, remove the task, emit the successful final
  boundary, and arm ordinary successful response completion;
- `abort_()`: run `closeProducer_()`, remove the task, cancel the response Tx
  completion, perform the protocol mapping above, and release the response
  without incrementing failure counters;
- `failed_(ZmAtomic<uint64_t> &counter)`: run `closeProducer_()`, remove the
  task, increment the exact existing `responseBuildFailures` or
  `transportFailures` counter supplied by the detecting path, disconnect the
  affected H1 connection or H2/H3 stream without draining, and release the
  response. Do not add a parallel failure-source flag or enum.

`stop_()` and peer-reset teardown close the emitter, remove the task, cancel Tx
completion, and release the response without calling any of those outcome
functions or incrementing build/Tx failure counters. Each function begins with
a terminal-state guard, making duplicate transport and link callbacks no-ops.

Teardown must follow the `GUIDELINES.md` ownership and sharding sequence. First
prevent new ingress, timers, and I/O work and post the Rx drain continuation.
That continuation drains late Rx callbacks and posts the Tx drain continuation.
Tx then detaches raw callbacks, invokes Builder `close()`, and drains owning
stream tasks in bounded `StreamStopBatch` turns. Only the final Tx drain turn may
invoke `stopTransports_()`; preserve the existing transport completion handoffs
which release their owner-shard state and ultimately call `Engine::stopped()`.
Do not block outside the main thread.

The shifted callback invokes `stop_()`, whose first operation clears the raw
task pointer; it must not try to remove that already shifted node. Normal
terminal cleanup may ask the owner to remove its still-linked task, but holds a
temporary Tx-local `ZuRef<Self>` until the method returns so releasing the
task's sole owning reference cannot destroy an executing object. Owners drain
dependents before destruction and merely assert empty dependent queues in their
destructors. This makes the ownership graph acyclic and teardown order
unambiguous.

## Removing the Pull Producer

Delete the asynchronous `ResBuilder_::next(max, done)` contract and its support:

- `HasAsyncBody` detection;
- `BodyChunkFn` and application-held completion callbacks;
- `AsyncBody_::next_()`, `chunk_()`, and `chunkTx_()` pull sequencing;
- `m_bodyPending` and shutdown waiting for producer completion;
- continuation ownership that lets an idle application callback retain the
  response driver.

Replace it with `StreamBody_` reached through `BodyEmit_` and registered in
`StreamTaskQ` exactly as prescribed above. Removal remains immediate when a
driver becomes terminal; do not add background scanning or timer-based garbage
collection.

## Removing `ResponseOutcome`

Delete the public `ResponseOutcome` enum after converting all transport and
server users.

Separate the concerns currently combined by that enum:

- `WriteOutcome` describes the result of an application-pushed body turn and
  includes intentional `Abort`;
- transport completion needs only a private success/failure signal;
- peer reset paths already know that a reset occurred;
- local stop/destructor paths already know that cancellation is local;
- build and transport statistics are incremented where those failures originate.

Replace transport completion callbacks with this exact private boolean
signature:

```c++
using TxCompleteFn = ZmFn<void(bool),
  ZmFnHeapID<"Zhttp.TxComplete">>;
```

Then audit every existing `ResponseOutcome` use in `ZhttpTransport.hh`,
`ZhttpServer.hh`, and their implementation files:

- `Success` becomes `true`;
- Tx failure and peer reset invoke the completion with `false`; reset accounting
  remains in the reset path;
- local object destruction or cancellation clears the completion without
  invoking it; the owning response/link teardown path is already authoritative;
- application/build failure is handled by the body driver before transport
  completion;
- application `Abort` takes its explicit protocol-specific path.

This audit is a required implementation step. Prove for every local
cancellation/destructor path that a separate authoritative owner releases the
response after clearing the callback. Peer reset must invoke `false` exactly
once without double-accounting a transport failure already recorded by the
reset path. Convert or repair any path which currently depends on receiving
`Cancelled` before deleting `ResponseOutcome`.

Delete the enum declaration, its `ZtEnumImplStruct`, and any logging or tests
which expose it as public response state.

## Implementation Audit Constraints

Apply these constraints while implementing and reviewing the change:

- Preserve each edited header's modelines, include guard, component-library
  include, and ordered direct dependencies. Add only direct includes and do not
  introduce tail inclusion or a header cycle.
- Use GNU C++2b Z idioms: no concepts/`requires`, STL containers, `enum class`,
  anonymous `.cc` namespaces, non-specific lambda captures, forwarding wrappers
  to bases, or unnecessary casts. Use `using` declarations for inherited APIs
  and `ZuFwd`/`ZuMv`/`ZuDecay` at the relevant boundaries.
- Keep all template-independent declarations outside templates. Use CRTP,
  templates, and existing profile traits for static dispatch; do not introduce
  a vtable merely to unify H1/H2/H3 terminal handling.
- Follow local hard-tab, two-column indentation and class/struct shape. Internal
  and Tx-only functions have trailing underscores and begin with the appropriate
  `ZiAssert`. Order data members by immutable/shared/owner group and decreasing
  alignment within each group; inspect the resulting layouts for padding.
- Construct state directly in member initializers. Do not add default
  initialization followed by overwrite, temporary contiguous body copies,
  fixed-size arrays/tables, hard-coded queue capacities, or scratch heap
  allocations. Continue writing directly to the layered Tx stream and moving
  pooled `ZiIOBuf` references through the existing queue.
- The only new persistent `zhttp` allocations are one `BodyEmit_`, one
  `StreamBody_`, and the justified heterogeneous registry node per open stream. They use the
  prescribed identified recycling heaps. No allocation, type erasure, buffer
  copy, lock, or atomic operation is added by the stream driver per
  `emit(write)` turn; existing pooled I/O-buffer acquisition remains unchanged.
- Keep callback captures explicit and minimal. The registry task owns one
  type-erased `ZuRef`; repeatable application resume callbacks use ordinary
  bound `ZmFn::fn` with one producer reference, while `ZmFn::mvFn` is reserved
  for genuinely one-shot callbacks.
  Any `ZiLOG` added during implementation captures stable diagnostic values by
  copy, never pointers or references.
- Use sentinel/null pointer state where it naturally represents absence, and
  the single `StreamState` enum for driver progression. Do not add parallel
  booleans, cached container counts, background scans, polling, timers, or
  speculative wakeups.
- Factor shared terminal sequencing once and dispatch discrete outcomes with
  `switch`. Keep control flow flat with early terminal exits. Do not repeat
  near-identical protocol blocks, retain iterators across callbacks, or call an
  invariant accessor repeatedly in a loop condition.
- Do not add new diagnostic atomics, locks, or synchronized snapshots. Existing
  counters are updated once at their authoritative failure origin; unneeded
  cumulative per-stream telemetry is omitted.
- Keep all stream-driver, task-registry, link callback, and response-node state
  Tx-owned. Rx observes or mutates only Rx state and hands fixed metadata to Tx.
  No function opportunistically touches both shards, and no lock is used to
  legitimize non-owner access.
- Deterministically remove every terminal stream from its owner and recycling
  heap. Destructors assert already-drained state; they do not perform the primary
  deletion. Shutdown is continuation-driven, bounded per scheduler turn, and
  follows the prescribed Rx-to-Tx drain order without blocking an I/O shard.
- Delete all dead pull-streaming and compatibility code in the same change.
  Do not leave detectors, aliases, comments, tests, or documentation for the
  superseded contract.

## Migration Sequence

1. Introduce `WriteOutcome` with `End`, `Stream`, `Abort`, and `Failed`.
2. Change writer invocation from `bool` to `WriteOutcome::T` for fixed and
   synchronous streaming bodies, propagating the breaking API through all
   dependent Builders.
3. Implement the persistable emitter control with Tx-shard-affine synchronous
   `emit(write)` and deterministic invalidation.
4. Implement the push streaming driver and protocol mappings for `End`,
   `Stream`, and `Abort`.
5. Convert every asynchronous server streaming Builder and test from
   `next(max, done)` to an application-retained emitter.
6. Delete the pull producer, body completion callbacks, and body-pending
   shutdown accounting.
7. Replace the task/driver reference-count graph with the acyclic owner/raw-
   backpointer structure and ordered teardown prescribed above.
8. Convert transport completion to a private success/failure signal and move
   reset/cancellation handling to the paths which originate those events.
9. Delete `ResponseOutcome` and its enum implementation.
10. Update `zhttp/README.md` with fixed, multi-turn response stream,
   intentional abort, late-emitter, thread-affinity, and shutdown examples.
11. Delete documentation claiming that application Builders can emit outbound
   trailers. Receive-side trailer parsing remains supported; the Builder
   contract has no outbound trailer API, and `End` emits only the native body
   boundary.

No compatibility adapter or forwarding shim remains after migration.

## Verification

Add focused, TAP-emitting `ZuTest`/`ZuTestUtil` unit tests for Builder and driver
behavior under `zhttp/test` using protocol fakes rather than live sockets:

- fixed writer returning `End`;
- fixed writer incorrectly returning `Stream` or `Abort`;
- optional body emitting no writer;
- duplicate fixed-body emission;
- reentrant emission from inside a writer;
- multiple sequential emissions from `body()` accepted only after each prior
  writer returns `Stream`;
- streaming writer returning `Stream`, followed by later emitted turns and
  eventual `End`;
- multiple application time slices preserving exact byte order;
- another turn being accepted while prior-turn bytes remain queued;
- bounded queue saturation producing `Failed` and the prescribed disconnect
  without drain;
- a high-turn-count stream showing, through the identified heap telemetry, one
  emitter/driver/task allocation per open stream, no control allocation per
  turn, bounded queue memory, and prompt recycling at terminal state;
- a long-idle stream causing no polling, timer, allocation, or CPU activity in
  `zhttp`;
- `Abort` before any body bytes and after writing bytes in the aborting turn;
- `Failed` before and after partial streaming output;
- immediate and asynchronous Tx failure becoming `Failed`;
- initial zero-byte `Stream` committing/flushing headers and later zero-byte
  `Stream` turns acting as no-ops;
- rejection of `Content-Length` with `Stream` and `OptionalStream`;
- late emission after `End`, `Abort`, reset, disconnect, and server shutdown
  returning false without invoking the writer;
- wrong-shard, active/reentrant, and malformed-state emission asserting in
  assertion builds and returning false in release builds;
- emitter invalidation not retaining the response driver;
- Builder `close()` destroying its retained emitter and initiating subscription
  removal exactly once;
- server stop completing without another application emission;
- ordered Rx/Tx teardown draining all raw callbacks and stream tasks before
  owners are destroyed;
- retained bytes, response nodes, active drivers, requests, and connections
  returning to zero on every terminal path.

Add protocol-specific assertions:

- H1 `Stream` preserves an open chunked response and `End` emits one final zero
  chunk;
- H1 `Abort` closes the connection and deterministically cancels its queued
  responses;
- H2/H3 `End` emits the native final boundary exactly once;
- H2/H3 `Abort` resets only the affected stream;
- H2/H3 peer reset invalidates only the affected emitter and preserves sibling
  streams;
- application streaming code remains protocol-independent across all supported
  profiles.

Keep the existing non-streaming idle, admission, request-limit, synchronous
response, and heterogeneous-Builder coverage in
`zhttp/test/ZhttpServerIdleTest.cc`. Mechanically migrate that test to
`WriteOutcome` and `disconnect()`, and remove only its obsolete pull-producer
case. Add the lowercase integration-test program
`zhttp/itest/zhttpserverstreamtest.cc` for streaming-specific socket coverage.
In that integration test:

- remove `ProducerDone`, `producerDone`, and manual completion release;
- retain the emitter in the test application after the initial writer returns
  `Stream`;
- drive later `Stream`, `End`, and `Abort` turns explicitly on Tx;
- verify server stop invalidates an idle emitter and completes immediately;
- verify a late test emission returns `false` and never invokes its writer;
- cover one representative profile for each wire protocol: H1 TCP, H2 TLS,
  and H3 QUIC. H1 TLS framing is already the H1 implementation and does not
  warrant duplicating every stream-lifecycle scenario.

Keep the socket matrix compact. Exercise normal multi-turn completion and
intentional abort once per wire protocol, teardown cancellation once per wire
protocol, and common immediate-turn and failure behavior on one representative
transport. Leave fixed bodies, idle expiry, request limiting, optional-body
policy, heterogeneous Builders, and synchronous response construction in their
existing focused tests. Do not multiply full socket scenarios for internal
state-machine assertions that can be established by the shared implementation
or focused protocol contract tests.

All concurrent tests synchronize with `ZmBlock` or, where it fits better,
`ZmSemaphore`; do not poll, sleep, or infer completion from elapsed time. Post
work and signal completion from asynchronous continuations rather than blocking
an I/O shard. Keep container traversal/callback scopes short and do not retain
iterators while terminal callbacks mutate queues.

Extend `ZhttpTransportContractTest.cc` for boolean completion semantics and
destructor cancellation. Extend the H1/H2/H3 hub/message tests for exact `End`
and `Abort` wire behavior. Update `zhttp/util/zhttpd.cc` so its generated-body
mode exercises multiple retained-emitter `Stream` turns instead of
`next(max, done)`.

Run streaming socket-level message and lifecycle coverage for:

- HTTP/1.1 over TCP;
- HTTP/2 over TLS;
- HTTP/3 over QUIC.

Keep the existing clang debug configuration. Do not reconfigure, clean the
build, or switch toolchains/build types for this change. Batch the complete API
and dependent migration before rebuilding; do not rebuild after each edited
file or intermediate migration step.

After implementation is complete, perform one top-level incremental build so
all changed libraries, utilities, tests, and dependents are refreshed together:

```sh
make -j8
```

If that build exposes compile failures, repair them as a batch and rerun the
same incremental command only after the batch is ready. Do not use repeated
speculative rebuilds as a substitute for completing the repository-wide source
audit.

Once the incremental build is clean, run comprehensive testing at the end—not
piecemeal during implementation:

```sh
make test
```

This final test pass must include the affected unit, utility, integration,
example, and dependent-module targets reached by the repository test traversal.
Do not run `.libs` binaries directly; use `libtool exec` for any debugger launch
from the source tree.

Run `zhttp/itest/zhttpmatrix` interoperability cases separately when their
Caddy/curl dependencies are available.

Acceptance requires the incremental clang debug build to be warning-free, the
final comprehensive test pass to succeed, the three wire protocols to pass
their push-stream lifecycle cases, and these repository searches to produce no
legacy API matches:

```sh
rg -n 'ResponseOutcome|HasAsyncBody|BodyChunkFn|BodyCancelFn|BodyTaskQ|'\
'AsyncBody_|m_bodyPending|WriteOutcome::(BuildFailed|TxFailed)|'\
'next\(unsigned.*done' \
  --glob '*.{hh,cc}' .
rg -n -U 'body\([^\n]*emit[\s\S]{0,500}?return (true|false)' \
  --glob '*.{hh,cc}' .
rg -n 'bool close\(\) const' --glob '*.{hh,cc}' zhttp zrest
```

Although verification remains debug-only, every `ZiAssert` contract check must
still have the prescribed explicit `false`/failure fallback in adjacent control
flow; assertions must not be required for correctness.
