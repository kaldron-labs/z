# `Zhttp::Client::Attempt` Cleanup Plan

## Summary

Refactor `Zhttp::Client::Attempt` from one flat collection of duplicated
pointers, counters, and Boolean flags into a small set of explicitly named
state groups.

The cleanup has three goals:

- remove the redundant cached `Request_ *request` pointer;
- group identity and body-accounting values by role; and
- represent the actual request-attempt lifecycle as an FSM while retaining
  separate state for orthogonal HTTP framing, redirect, failure, and event
  concerns.

This is an internal representation change.  Preserve request scheduling,
retry and redirect behavior, callback ordering, result accounting, and
Rx/Tx ownership.  Do not add compatibility members or parallel old/new state.

## Problems in the Current Structure

### Duplicate request pointers

`Attempt::node` is the intrusive `RequestQ::Node` whose lifetime is retained
by the Tx queue.  `Attempt::request` is always initialized from
`&node->data()` and both pointers are cleared together.

The second pointer:

- consumes storage in every concurrency slot;
- creates an unnecessary invariant that `request == &node->data()`; and
- makes it unclear whether queue-node identity or request-builder data is the
  authoritative object.

Keep only `Request *node`.  Add an internal accessor for the builder contract:

```c++
Request_ &request_() { return node->data(); }
const Request_ &request_() const { return node->data(); }
```

The accessor is valid only while the attempt is active.  Convert all direct
users of `attempt.request` in `Client`, `ClientPool`, and `ClientMessage` in
the same change.  Do not retain a forwarding data member.

### Flat accounting fields

The 64-bit fields are not evidence of an FSM.  Most are byte counts and must
remain 64-bit because a single HTTP body can exceed 4GiB and the values are
reported through `Result`.

Their current flat layout nevertheless obscures two separate accounting
domains:

- request body: produced, committed, reset, and discarded, together with
  header/final commit state; and
- response body: received, consumed, pending, reset, and discarded.

Store the request-side values in the existing `BodyCommit` type if its reset
and copy semantics exactly match the current snapshots.  Otherwise introduce
an equivalent named aggregate rather than duplicating the fields at the top
level.  Introduce a response-side aggregate such as:

```c++
struct ResponseBody {
  uint64_t received = 0;
  uint64_t consumed = 0;
  uint64_t pending = 0;
  uint64_t reset = 0;
  uint64_t discarded = 0;
};
```

Retain distinct reset and discarded counters unless all producers and public
result semantics prove that they are permanently identical.  Do not merge
them merely because current terminal paths commonly increment both.

Group the two IDs and document their different stability:

```c++
struct AttemptID {
  uint64_t request = 0; // logical request; stable across retries/redirects
  uint64_t attempt = 0; // wire generation; changes for each new attempt
};
```

Keep `unsigned` for ordinary CPU-local indexes and counters unless their
stored volume or an external representation justifies narrowing.  Narrow
`redirects` and `retries` only after checking configuration bounds and all
result conversions; this cleanup must not introduce silent truncation.

### Unclassified Boolean state

The existing flags do not all belong to one FSM.  They cover several
independent dimensions:

| Concern | Current members |
| --- | --- |
| lifecycle/progress | implicit idle state, `headersDone`, `responseStarted`, `responseDone` |
| endpoint selection | `endpointSet`, `selectionObserved` |
| failure | `failed`, `connectFailed`, `failureObserved`, `transient`, `txFailed` |
| redirect | `hasRedirect`, `invalidRedirect` |
| HTTP/1 framing | `connectionClose`, `connectionKeepAlive`, `http10`, `closeDelimited` |
| request commit | `requestHeadersCommitted`, `requestFinalCommitted` |
| terminal disposition | sentinel-valued `terminal` |

Forcing every combination into one enumeration would create a state-product
FSM and make transitions harder to audit.  Introduce one lifecycle state and
small orthogonal state types instead.

Use the `ZtEnum*` macros already used by `ZhttpConfig.hh`, `ZhttpH1.hh`, and
`ZhttpH3.hh`.  These states benefit from the same ordinal/name mapping used by
the rest of the library for diagnostics and assertions:

```c++
ZtEnumStruct(ZhttpAPI, AttemptPhase, int8_t,
  Idle, Resolving, Connecting, Sending,
  ReceivingHeaders, ReceivingBody, Closing);

ZtEnumStruct(ZhttpAPI, FailureKind, int8_t,
  None, Connect, Tx, Protocol, Body);

ZtEnumStruct(ZhttpAPI, RedirectState, int8_t,
  None, Valid, Invalid);

ZtEnumStruct(ZhttpAPI, Persistence, int8_t,
  Default, KeepAlive, Close);
```

Define non-template state types outside the `Client` template to avoid
per-specialization debug and symbol bloat.  Add the corresponding
`ZtEnumImplStruct(...)` definitions to a new `ZhttpClient.cc`, and add that
source to `libZhttp_la_SOURCES`; do not leave enum name maps instantiated or
implemented in the template header.

Use `int8_t` for these stored states, as required for compact Z enums.  Use
`ZtFlagsStruct` only for genuinely independent, simultaneously true bits such
as retained event-observation markers; lifecycle, failure kind, redirect
validity, and persistence are mutually exclusive values and must remain
`ZtEnumStruct` types rather than flag masks.

## Proposed State Groups

Refactor toward the following logical shape; exact names should follow nearby
`ZhttpClient.hh` precedent and member order should minimize padding:

```c++
struct Attempt {
  Request_ &request_() { return node->data(); }
  const Request_ &request_() const { return node->data(); }

  Request *node = nullptr;
  DiscoveryRequestRef discovery;

  AttemptID identity;
  AttemptRoute route;
  BodyCommit requestBody;
  ResponseBody responseBody;
  AttemptProtocol protocol;
  AttemptFailureState failure;

  ResultCode::T terminal = -1;
  unsigned slot = 0;
  unsigned redirects = 0;
  unsigned retries = 0;
  AttemptPhase::T phase = AttemptPhase::Idle;
};
```

The aggregates have these responsibilities:

- `AttemptRoute`: active URL, redirect URL, endpoint collection, selected
  endpoint, endpoint index, and redirect state;
- `BodyCommit`: request-side byte and commit accounting;
- `ResponseBody`: response-side byte accounting;
- `AttemptProtocol`: transport, HTTP version, status, and HTTP/1 persistence
  and framing state; and
- `AttemptFailureState`: a `FailureKind::T` plus only genuinely orthogonal
  properties such as transient classification and event-observed state.

`failed`, `connectFailed`, and `txFailed` become tests of `FailureKind::T`.
`hasRedirect` and `invalidRedirect` become tests of `RedirectState`.
`connectionClose` and `connectionKeepAlive` become one `Persistence` value.
Keep `closeDelimited` independent because response framing and connection
persistence are related but not interchangeable.

Evaluate whether `selectionObserved` and `failureObserved` can be eliminated
by emitting events in centralized transitions.  If event delivery can be
re-entered or deferred independently of the lifecycle state, retain them in a
small event-observation group rather than encoding them in `AttemptPhase`.

## Lifecycle FSM

Introduce trailing-underscore transition helpers rather than allowing
callers to set the lifecycle state and related fields independently.  The
helpers should express the existing flow, including retry and redirect loops:

```text
Idle -> Resolving -> Connecting -> Sending
                    ^                |
                    |                v
                    +--- retry --- ReceivingHeaders -> ReceivingBody -> Closing
                    +-- redirect -------------------------------------+
Closing -> Idle
```

The exact transition graph must be derived from the current code before
replacement; in particular, cached endpoint selection may bypass resolving,
an error may close from any active phase, and responses without bodies may
advance directly from headers to closing.

Use the lifecycle state to replace progress flags only where the state fully
answers the same question.  For example, `responseStarted` may also determine
whether a request is safe to replay.  Replace it with a phase test only after
confirming that every transition preserves that replay-safety boundary.

Keep terminal reason separate from phase.  Cancellation or timeout can be
latched while transport teardown and callback completion are still in
progress.  Replace the `-1` sentinel only if `ResultCode` has, or can cleanly
gain, an explicit non-terminal value without distorting its public result
semantics.

## Reset and Invariants

Replace the long sequence of individual field resets with reset operations at
the ownership boundary:

- a full reset when an attempt slot returns to `Idle`;
- a wire-attempt reset for retry/redirect which preserves logical request ID,
  request node, and cumulative retry/redirect accounting; and
- narrowly scoped protocol/body resets where the current behavior requires
  them.

Do not use whole-structure assignment if it would unnecessarily destroy and
reallocate URL, endpoint, discovery, or other reusable storage.  Each reset
helper must make its preservation rules explicit.

Add debug assertions for the resulting invariants:

- `Idle` implies `node == nullptr` and no active discovery request;
- every non-idle attempt has a request node;
- resolving state owns the corresponding discovery operation;
- response progress is monotonic within one wire attempt;
- redirect state and redirect URL agree;
- failure kind `None` implies no failure-only metadata is active; and
- terminal acknowledgement cannot retire the Tx-owned node until all Rx work
  using the raw pointer has completed and posted `ackd()` to Tx.

## Implementation Sequence

1. Inventory every read and write of each current `Attempt` member, including
   template-dependent uses in `ClientPool` and `ClientMessage`, and record
   which values survive retry, redirect, cancellation, and terminal cleanup.
2. Remove `Request_ *request`, add the node-data accessor, and migrate all
   consumers.
3. Declare the non-template state enums with `ZtEnumStruct` outside `Client`,
   implement them with `ZtEnumImplStruct` in `ZhttpClient.cc`, and add that
   source to `libZhttp_la_SOURCES`.
4. Introduce identity, request-body, response-body, route, protocol, failure,
   and event-observation aggregates without changing behavior.
5. Consolidate redirect, persistence, and failure Boolean clusters into their
   small state types.  Preserve multiple simultaneous failure information
   only where current result or retry policy actually consumes it; otherwise
   define and document a deterministic failure precedence.
6. Introduce `AttemptPhase` and transition helpers, then remove lifecycle
   flags one at a time after each flag's non-lifecycle uses have been audited.
7. Replace field-by-field initialization and teardown with full-attempt and
   wire-attempt reset helpers which explicitly preserve reusable allocations
   and logical-request state.
8. Reorder members largest-to-smallest within logical ownership groups and
   compare `sizeof(Attempt)` before and after.  Size reduction is desirable,
   but eliminating invalid state combinations and preserving hot-path locality
   take precedence over cosmetic packing.
9. Extend focused client tests to cover cached and discovered endpoints,
   connect failure, Tx failure, retry before and after response start,
   redirects, HTTP/1.0 persistence, close-delimited responses, cancellation,
   timeout, and out-of-order response acknowledgement.

## Acceptance Criteria

- `Attempt` stores only the intrusive request node, not a second derived
  request pointer.
- Request and response byte accounting are named groups, with all existing
  `Result` values preserved.
- Lifecycle progression is represented by one explicit phase state with
  auditable transitions.
- Redirect, failure, persistence, framing, commit, and event-observation state
  remain orthogonal and do not form a combinatorial FSM.
- Retry and redirect resets preserve exactly the state they preserve today.
- No compatibility aliases or duplicated legacy state remain.
- Rx ownership of `Attempt` and Tx ownership of the request queue remain
  unchanged.
- The final layout has no avoidable padding or redundant pointer storage.
