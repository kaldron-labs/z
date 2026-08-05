# `Zhttp::Client` `ZmPQTx` Request Submission Plan

## Summary

Replace the finite-workload `Zhttp::Client::submit(Request *, unsigned)` API
and its private pending ring with an application-defined intrusive
`ZmPQueue` driven by `ZmPQTx`.

The application will allocate and enqueue request nodes incrementally.  A
request remains in the transmit queue while it is queued or in flight and is
retired after its terminal response has been processed.  Applications such as
`zhttp` can therefore bound the number of live request objects independently
of the total workload: a run of 100,000 requests with an outstanding limit of
32 needs approximately 32 request nodes rather than a 100,000-element array.

This is a breaking API change.  Remove `submit` and the old admission
configuration and migrate all direct consumers in the same change; do not add
a compatibility overload or an adapter which recreates whole-workload
pre-allocation.

## Current State and Constraints

`Zhttp::Client<Request_, ResParser_>` currently:

- allocates `m_pending` to `ClientConfig::maxPending()` during `init()`;
- accepts one contiguous array through `submit()`;
- walks that array in `admissionBatch()`-sized Rx-thread turns;
- seals the workload implicitly after the last array element;
- admits up to `concurrency()` requests into fixed `Attempt` slots and stores
  the remainder as raw pointers in `m_pending`;
- requires every submitted `Request_` and its body source storage to remain
  valid until `completed()`; and
- uses `m_sealed`, `m_active`, and `m_count` to release `wait()`.

The utility consequently allocates a `ZtArray<Request_>` of
`options.requests` elements before calling `submit()`.  The active direct
consumers are `zhttp/util/zhttp.cc`, `ZhttpClientCancelTest.cc`, and
`zhttpclientfallbacktest.cc`.

`ZmPQTx` adds several constraints which the migration must handle explicitly:

- the entire `ZmPQTx` base and its `ZmPQueue` are Tx-owned;
- `send()` and `resend()` advance a monotonically keyed sequence;
- `send_()` returning `false` is transient back-pressure and is resumed by a
  later `start()`;
- ordered `ackd(key)` is cumulative, while HTTP specializes `ZmPQTx` with
  `ZmPQTxOrdered<false>` so each response can acknowledge its exact key;
- unordered `ackd(key)` removes that exact node and passes it to the
  application's `archive_()` hook; and
- `abort(key)` removes only an unsent node and leaves a sequence gap.

The unordered specialization is a prerequisite for HTTP correctness.  A later
response must retire only its own node and must not destroy an earlier request
which is still in flight.

## Public Type and API Design

### Application-specialized `TxQ`

Change the client template to inherit an application-specialized `ZmPQTx`
type:

```c++
template <typename TxQ, typename ResParser_>
class Client : public TxQ {
public:
  using Tx = TxQ;
  using Request = typename Tx::Msg; // intrusive RequestQ::Node
  using Request_ = typename Request::T; // application Builder implementation
  using ResParser = ResParser_;
  // ...
};
```

The final application type is the `Impl` used to specialize `ZmPQTx`.  A
consumer has the following shape:

```c++
struct App;

struct Request_ : public ZmObject {
  uint64_t key() const;
  uint64_t length() const { return 1; }
  // Zhttp request Builder and lifecycle callback contract
};

using RequestQ = ZmPQueue<
  Request_,
  ZmPQueueOverlap<false,
    ZmPQueueNode<Request_,
      ZmPQueueHeapID<"zhttp.Request">>>>;
using Request = RequestQ::Node;
using TxQ = ZmPQTx<App, RequestQ, ZmPQTxOrdered<false>>;

struct App : public Zhttp::Client<TxQ, ResParser> {
  RequestQ *txQueue() { return &m_requests; }
  void archive_(Request *);
  ZmRef<Request> retrieve_(RequestQ::Key, RequestQ::Key);

private:
  RequestQ m_requests;
};
```

This arrangement is intentional:

- `Client` supplies `send_`, `resend_`, gap handling, and all scheduling
  callbacks inherited by `App`;
- the application supplies queue storage/access and persistence policy through
  `txQueue()`, `archive_()`, and `retrieve_()`;
- `Request_` implements the existing extended HTTP request Builder contract;
  it also supplies the queue key and a length of one;
- `Request` is the intrusive `RequestQ::Node` actually allocated, referenced,
  enqueued, aborted, archived, and retrieved; and
- the final `App` type satisfies `ZmPQTx`'s CRTP dispatch without virtual
  calls.

Use a no-overlap request queue: request keys identify discrete messages and
must be unique, so clipping or overwriting a request is invalid.  Give the
outer request node an application heap ID.  Prefer a thread-affined intrusive
base when ownership is moved to the Tx shard and the Rx side uses only the raw
pointer protected by queue lifetime; use an atomic base only if an application
actually retains references concurrently across shards.

### Submission and cancellation API

- Remove `submit(Request_ *, unsigned)` and `submit_()`.
- Expose a thin app-facing `enqueue(ZmRef<Request>)` dispatcher and an
  on-Tx `enqueue_(ZmRef<Request>)` variant.  `enqueue_()` calls `TxQ::send()`;
  the public function only posts the movable reference to the configured Tx
  thread.  Do not make off-shard queue access acceptable by adding a lock.
- Expose the queue's key-based abort operation through the same dispatcher
  pattern.  The on-Tx form returns or passes the aborted `ZmRef<Request>` to
  the application so it can requeue it, classify it as cancelled, or discard
  it.  An aborted unsent request was never admitted and must not accidentally
  receive a normal response completion.
- Preserve active-request cancellation as a distinct client operation.  Make
  its public identity key-based (or node-based if local precedent proves
  cleaner), dispatch first to Tx to resolve the queued-versus-sent race, and:
  - abort an unsent node in `TxQ`; or
  - post an already-sent request to the Rx cancellation path.
  This retains exact-once terminal completion without scanning or mutating the
  Tx queue from Rx.
- Add explicit `seal()`/`seal_()` because removing `submit()` also removes its
  implicit end-of-workload signal.  Enqueueing remains valid until sealed.
  `wait()` completes only after sealing, terminal completion of every admitted
  request, and archival/retirement of the final queue node.  Long-lived clients
  which do not use finite-workload `wait()` need not seal.

Do not expose inherited `ZmPQTx` mutators as accidentally off-shard-safe public
APIs.  Application code already executing on the Tx thread may use the
underscored variants to elide another scheduler post.

### Configuration and diagnostics

- Retain `ClientConfig::concurrency()` as the HTTP in-flight limit.
- Remove `ClientConfig::maxPending()` and `admissionBatch()`.  Queue capacity
  and producer batching belong to the application-defined `TxQ`; retaining
  either setting would create two competing admission limits.
- Remove the corresponding initialization validation and the `m_pending`
  allocation.
- Remove or redefine `pending()`, whose old meaning was the private Rx ring.
  Prefer explicit telemetry names:
  - `active()` for requests assigned to HTTP attempts; and
  - application/`TxQ`-provided `outstanding()` for intrusive nodes retained in
    the queue, including queued and in-flight requests.
- Keep `completed()` and `failed()` as terminal-result telemetry.  They remain
  Rx-owned, with the same relaxed diagnostic-read expectations as today.

## Threading and Ownership

| State or operation | Owner | Rule |
| --- | --- | --- |
| `TxQ`, `RequestQ`, send/resend/archive cursors | Tx | Access only on the configured client Tx thread. |
| Admission credits | Tx | Updated by `send_()` and terminal Rx-to-Tx posts. |
| `Attempt`, discovery, request timers, response callbacks | Rx | Preserve the existing Rx ownership. |
| Request Builder wire construction | Tx | Preserve current synchronous Builder execution in `ClientMessage`. |
| Response parsing and terminal completion | Rx | Preserve current synchronous Parser/callback execution. |
| Request node lifetime | Tx queue | The queue retains the node until Rx terminal work has posted completion to Tx and archival retires it. |

An HTTP response is an Rx-side acknowledgement, but `TxQ::ackd()` is a
Tx-owned operation.  Therefore response-driven terminal completion must post
a continuation from Rx onto the client's Tx thread; that continuation, not the
Rx response path, calls `TxQ::ackd()`.  The same rule applies to every other
Rx-originated terminal outcome.

`init()` must resolve and retain both `m_rxThread` and `m_txThread` from
`HubConfig`, using the multiplexer defaults when a named thread is absent.
Add `txRun_()` and debug shard assertions parallel to `rxRun_()`.

The Tx-to-Rx admission post may capture a raw `Request *`: the Tx queue owns
the intrusive reference until the matching terminal Rx-to-Tx completion has
been processed.  Shutdown and abort paths must preserve that ordering.  Do not
add a per-attempt reference back to `Client`, and do not archive/remove a sent
node merely because its Rx continuation has been posted.

## Queue-to-HTTP Flow Control

The private Rx pending ring disappears.  `ZmPQTx` itself becomes the only
pending-request queue.

1. `enqueue_()` inserts the node into `RequestQ`; `ZmPQTx` schedules `send()`.
2. `send_()` runs on Tx.  If all `concurrency()` credits are reserved, it
   returns `false`; `ZmPQTx` records transient `SendFailed` state without
   advancing permanently past the request.
3. With a credit available, `send_()` reserves it and posts the raw node
   pointer to Rx.
4. Rx consumes one already-reserved `Attempt` slot and starts the existing
   routing, discovery, connection, retry, redirect, and response machinery.
   There is no fallback Rx pending queue; lack of a slot is an invariant
   failure except during a stopping race.
5. Terminal Rx completion invokes `observed()`/`completed()` exactly once,
   frees the Rx attempt slot, and posts fixed metadata (key/range and result)
   to Tx.  It must not call `TxQ::ackd()` directly from Rx.
6. The posted Tx continuation returns the admission credit, calls the unordered
   `TxQ::ackd(requestKey)` to detach exactly that node, and calls `TxQ::start()`
   to resume a sender which stopped on capacity.
7. Unordered `ackd()` calls the application's `archive_()` with the detached
   node.  The hook persists it if required or simply performs application
   bookkeeping; releasing the hook's reference retires a non-persistent node.

The credit count is Tx-owned and must never exceed `concurrency()`.  Initialize
any per-credit bookkeeping once in `init()`; do not allocate a bookkeeping
object for each admission.  Each `send()`/`resend()` turn handles one request
and yields through `reschedule*()`, so a large queue cannot monopolize the Tx
thread.

`idleSend()` and `idleResend()` should normally be no-ops because `ZmPQTx`
already owns the `Sending`/`Resending` flags.  Add a client-local busy flag only
if needed to coalesce a separate cross-shard admission post, and clear it in
the corresponding idle callback.  Do not duplicate `ZmPQTx`'s state machine.

## `ZmPQTx` Callback Contract in `Client`

Implement the callbacks as follows:

| Callback | Client behavior |
| --- | --- |
| `send_(Request *, bool)` | Reserve an HTTP admission credit and post the request to Rx; return `false` only for transient capacity/stopping back-pressure. |
| `resend_(Request *, bool)` | Use the same admission path as `send_()`; both callbacks run serially on the client Tx thread. |
| `sendGap_(Span, bool)` | Perform no HTTP/network action and return `true`. |
| `resendGap_(Span, bool)` | Perform no HTTP/network action and return `true`. |
| `scheduleSend()` | `txRun_([this] { TxQ::send(); })`. |
| `rescheduleSend()` | Same scheduler post as `scheduleSend()`; do not recurse inline. |
| `idleSend()` | No-op unless a separate admission-post busy bit is required. |
| `scheduleResend()` | `txRun_([this] { TxQ::resend(); })`. |
| `rescheduleResend()` | Same scheduler post as `scheduleResend()`. |
| `idleResend()` | No-op unless a separate busy bit is required. |
| `scheduleArchive()` | `txRun_([this] { TxQ::archive(); })`. |
| `rescheduleArchive()` | Same scheduler post as `scheduleArchive()`. |
| `idleArchive()` | Re-evaluate sealed/drained state; otherwise no-op. |

The application-provided callbacks remain:

- `archive_(Request *)`: persist an acknowledged request if required, or do
  nothing for non-persistent operation; unordered `ackd()` has already removed
  the exact node from the live queue; and
- `retrieve_(Key key, Key head)`: return the archived node covering `key`, and
  optionally repopulate subsequent archived nodes as permitted by `ZmPQTx`.

For `zhttp`, there is no persistent archive: `archive_()` replenishes the
bounded work queue and otherwise does nothing; `retrieve_()` returns `nullptr`.
It must not call cumulative `archived(endKey)`, which could remove other live
nodes after an out-of-order response.

Treat a queue-level resend as a new HTTP admission of the same request, not as
the client's internal transport retry.  Internal retries and redirects remain
inside the existing `Attempt` and keep one terminal result.  No resend-specific
concurrency guard is needed: `send_()` and `resend_()` are serialized on the
same Tx thread.

## Unordered Completion and Archival

No client-local completion reorder structure is needed.  Each terminal Rx
post carries only the request key.  On Tx, unordered `ackd(key)` deletes the
exact queue node and invokes `archive_()` with a reference that preserves its
lifetime through application bookkeeping.  Other active nodes remain in the
queue regardless of response order.  An aborted unsent key needs no later
cumulative gap crossing; `sendGap_()` remains a no-op.

## Lifecycle and Shutdown

### Start

- Initialize queue/admission bookkeeping before starting the hubs.
- Start the protocol hubs first.
- Post `TxQ::start()` to the client Tx thread so requests queued before
  `Client::start()` begin only after transports are available.
- Make repeated `start()` calls idempotent in conjunction with the existing hub
  lifecycle.

### Seal and natural drain

`seal_()` prevents further enqueueing but does not cancel queued or active
requests.  Natural drain is complete only when:

- the application has sealed;
- no Rx attempt is active;
- no Tx admission credit is reserved;
- `RequestQ` contains no unarchived node; and
- no send, resend, archive, or cross-shard continuation can recreate work.

Release `Runtime::wait()` from a continuation after those conditions become
true.  A momentarily empty producer queue before `seal()` is idle, not complete.

### Stop and final

Preserve exact-once completion and use asynchronous shard fences:

1. gate new enqueue/admission and run the existing Rx `stopIngress_()` path;
2. cancel discovery, timers, and active links on Rx, producing the normal
   terminal Rx-to-Tx posts;
3. after the Rx fence, post to Tx, stop `TxQ`, and resolve queued unsent nodes
   according to cancellation policy without touching active nodes;
4. fence Rx again for queued cancellation callbacks, then fence Tx so terminal
   acknowledgements and immediate archives are drained;
5. stop the protocol hubs using their existing continuation; and
6. in `final()`, assert/verify that the queue, admission credits, attempts,
   timers, and scheduler activity are empty before releasing storage.

Never clear the Tx queue while an Rx attempt still holds a raw request pointer.
Do not block on an I/O shard; retain `ZmBlock` only for the main-thread
synchronous `stop()` wrapper.

## `zhttp` Utility Migration

Refactor `zhttp/util/zhttp.cc` as the reference application:

- rename the present Builder/callback struct from `Request` to `Request_`;
- add its monotonic request key and `length() == 1` queue contract;
- define the no-overlap intrusive `RequestQ`, `Request = RequestQ::Node`, and
  `TxQ = ZmPQTx<App, RequestQ, ZmPQTxOrdered<false>>` types;
- derive the utility application from `Zhttp::Client<TxQ, ResParser>` and own
  `RequestQ` there;
- implement `txQueue()`, non-persistent `archive_()`, and null `retrieve_()`;
- replace the full `ZtArray<Request_>` allocation with a Tx-owned producer
  which creates nodes until the configured outstanding limit is reached;
- after archival frees capacity, create the next logical request and enqueue
  it until `options.requests` have been generated;
- seal after generating the final request, including the zero-request edge
  case if it remains accepted by option validation; and
- keep output naming, PUT record IDs, result validation, diagnostics, timeout,
  and exit-status behavior unchanged.

Use `--jobs`/`options.concurrency` as the initial outstanding bound unless a
separate queue-depth option is deliberately introduced.  With queue nodes
retained through response completion, this gives at most `jobs` live request
objects and naturally replenishes one node as another is archived.  If a
larger pending reservoir is desired later, expose it as an application option,
not `ClientConfig`.

The producer must cap work per Tx scheduler turn if the queue bound can be
large.  Unordered `ackd()` removes the completed node before calling
`archive_()`, so refill observes the released queue capacity.

Update `zhttp/util/zhttpboundary.md` to classify queue ownership, incremental
workload generation, and non-persistent archival as application
responsibilities.

## Source Changes

### `zhttp/src/ZhttpClient.hh`

- Change the template/type aliases and inherit `TxQ`.
- Add `ZmPQueue` dependencies required by the public contract.
- Replace `Request_ *` submission storage with intrusive `Request *` node
  identity plus access to `Request::data()` for the Builder.
- Remove `Requests`, `m_pending`, `m_head`, `m_tail`, `m_count`, `shift_()`,
  `request_()`, `submit()`, and `submit_()`.
- Add Tx-thread resolution/dispatch, admission credits, exact unordered
  completion, explicit sealing, queue scheduling callbacks, and
  queue-aware cancellation.
- Change `Attempt` to retain the raw node identity needed for terminal key
  handoff while exposing `Request_ *` to `ClientPool`/`ClientMessage`.
- Modify `finish_()` and `completeAttempt_()` to release the Rx slot without
  pulling another request from an Rx ring; post terminal metadata to Tx
  instead.
- Update `stopIngress_()`, `idle_()`, `start()`, `stop()`, and `final()` for the
  two-shard queue lifecycle.

### `zhttp/src/ZhttpClientPool.hh` and message plumbing

- Continue instantiating `ClientPool`/`ClientMessage` with `Request_`, not the
  container node type.
- Bind the Builder/Parser to `node->data()` while keeping the node pointer in
  the owning `Attempt`.
- Remove the unused `Admission` ring helper and its stale source comment if the
  repository-wide reference audit confirms that only `ZhttpAdmissionTest`
  uses it.  The new `ZmPQTx` path supersedes it.

### `zhttp/src/ZhttpConfig.hh`

- Remove `maxPending`, `admissionBatch`, their setters, and backing members.
- Retain `concurrency` and all routing, replay, timeout, cache, and transport
  configuration.

### Consumers and documentation

- Migrate `ZhttpClientCancelTest.cc` and `zhttpclientfallbacktest.cc` to
  intrusive request nodes and incremental enqueue/seal.
- Update `zhttp/README.md` and the extended Client contract in `Zhttp.hh` to
  distinguish `Request_` (Builder/callback data) from `Request` (queue node),
  document application queue/archive hooks, and replace the `submit()` example.
- Search the full repository for `Zhttp::Client<`, `.submit(`,
  `.maxPending(`, `.admissionBatch(`, and the removed telemetry.  Propagate all
  active dependents directly and add no shims.
- Delete `ZhttpAdmissionTest.cc` and its Automake entry if it only covers the
  removed private-ring design; replace that coverage with the queue-aware
  client tests below.

## Phased Implementation

### Phase 1: Establish the intrusive type boundary

- Add `TxQ` inheritance and derive `Request`/`Request_` aliases.
- Thread the node/data distinction through `Attempt`, `ClientPool`, and
  `ClientMessage` while temporarily retaining the old admission path only long
  enough to keep the build reviewable.
- Add the queue/application type skeletons to one focused test.

Acceptance: one request can traverse H1/H2/H3 with the Builder and Parser bound
to `Request::data()`, and node lifetime extends through `completed()`.

### Phase 2: Replace admission with Tx-owned `ZmPQTx`

- Add Tx scheduling callbacks, credits, `send_()`/`resend_()`, and no-wire gap
  callbacks.
- Remove the Rx pending ring and configuration.
- Post each terminal key to Tx and acknowledge it with exact unordered
  `ackd(key)`.

Acceptance: queue depth greater than concurrency applies back-pressure without
failure, responses completing out of order retire only their exact nodes, and
a freed credit resumes sending.

### Phase 3: Complete lifecycle, abort, resend, and persistence seams

- Add explicit seal/natural drain.
- Implement queued abort versus active cancellation ordering.
- Complete archive/retrieve dispatch, gap retirement, and asynchronous
  stop/final fences.

Acceptance: all queued/active requests receive at most one terminal callback;
stop leaves no nodes, timers, attempts, or scheduled queue work; abort gaps do
not require cumulative retirement.

### Phase 4: Migrate the utility and remaining consumers

- Convert `zhttp` to the bounded incremental producer.
- Convert cancellation/fallback tests and public documentation.
- Remove dead admission code and compatibility residue.

Acceptance: the utility completes a workload much larger than its queue bound
without whole-workload allocation, and every active consumer builds against
the new API.

## Detailed Test Plan

Add focused `Zhttp::Client` queue tests using deterministic semaphores/blocks,
not sleeps or polling:

1. **Type/lifetime:** enqueue an intrusive node, complete it, verify
   `completed()` precedes `archive_()`/destruction and occurs exactly once.
2. **Capacity:** configure concurrency `N`, enqueue more than `N`, and verify
   no more than `N` attempts are active while later nodes remain solely in
   `RequestQ`.
3. **Resume:** complete one active request and verify a sender stopped by
   capacity resumes on Tx and admits exactly one replacement.
4. **Out-of-order completion:** complete keys `2`, `3`, then `1`; verify keys
   `2` and `3` retire independently while key `1` remains live, then retires
   exactly once without use-after-free.
5. **Abort gap:** abort an unsent middle key, send later keys, and verify the
   no-wire gap does not stall terminal acknowledgement/archive.
6. **Resend:** request a queue resend after completion/archive retrieval and
   verify it uses the same HTTP admission path and runs on the Tx thread after
   any preceding send callback.
7. **Seal:** let the queue become temporarily empty before producer refill and
   verify `wait()` does not complete until explicit seal and final archival.
8. **Stop races:** stop with requests queued, resolving, transmitting, and
   awaiting responses; verify exact-once cancellation/completion and empty
   Tx/Rx state at finalization.
9. **Shard assertions:** invoke app-facing enqueue/cancel off-shard and verify
   the actual queue operations and all `schedule*()` callbacks execute on the
   configured Tx thread.
10. **Bounded large workload:** run at least 100,000 logical loopback requests
    with a small queue bound; verify request-node allocation high-water remains
    proportional to that bound, not total request count.
11. **Protocol regression:** retain cancellation, timeout, retry, redirect,
    H3-to-H2 fallback, Alt-Svc, body accounting, and result/event sequencing
    coverage for H1, H2, and H3.

Build `zhttp/src` before dependents, build the default test target before
running it, then run the enabled `zhttp/test` and `zhttp/itest` suites.  Run the
utility under ASan/LSan and valgrind for a smaller bounded workload to verify
queue/archive shutdown and intrusive node lifetime.

## Acceptance Criteria

- `Zhttp::Client` is parameterized by and inherits `TxQ`; `Request_` is the
  application Builder implementation and `Request` is the intrusive queue
  node.
- No active code calls or defines `Zhttp::Client::submit`.
- The client has no private request pending ring and `ClientConfig` has no
  `maxPending` or `admissionBatch`.
- All `ZmPQTx` and `RequestQ` state is Tx-owned; public off-shard operations are
  thin dispatchers and no lock is added to permit wrong-shard access.
- HTTP concurrency provides transient `ZmPQTx` back-pressure and resumes
  without dropping or failing queued requests.
- Out-of-order responses, abort gaps, retries, redirects, queue resends,
  cancellation, and stop cannot archive a live request or produce duplicate
  terminal callbacks.
- Application `archive_`/`retrieve_` hooks are functional; `zhttp` performs
  immediate non-persistent retirement and returns null on retrieval.
- `zhttp -n 100000 -j N ...` allocates only a bounded number of request nodes
  proportional to `N` (plus fixed client/transport state), not 100,000 request
  objects.
- Explicit seal and natural drain preserve `wait()` semantics for finite
  workloads; temporary queue idle is not mistaken for completion.
- Stop/final drain both shards cleanly with no queued nodes, active attempts,
  timers, continuations, leaks, or stale raw pointers.
- All enabled `zhttp` unit/integration tests pass under gcc and clang, with
  ASan/LSan and valgrind lifecycle coverage.

## Non-goals

- Preserving source compatibility with `submit`, the old client template
  arguments, or old admission configuration.
- Retaining a second pending queue inside `Client`.
- Adding persistent archival to the `zhttp` executable.
- Replacing the existing HTTP retry, redirect, discovery, Alt-Svc, pool, or
  protocol-selection machinery.
- Changing the request Builder or response Parser wire contracts beyond
  distinguishing the intrusive node from its `Request_` data.
