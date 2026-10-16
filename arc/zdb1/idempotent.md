# Zdb exact-UN idempotence implementation

## Status and decision

Implement this precursor before `ZdbSaga`.  Correct the existing UN-bearing
insert, update, and delete overloads; do not add a saga-specific mutation API
such as `updateOnce()`.

This is an intentional breaking change.  A repository-wide call-site audit
currently finds no users of the UN-bearing CRUD overloads outside their
definitions in `Zdb.hh`.  Propagate the new callback contract to any dependent
found during implementation.  Do not add compatibility overloads, wrappers,
or forwarders.  This follows the explicit dependent-compatibility rule in
[`GUIDELINES.md`, lines 9-18](GUIDELINES.md#L9).

The scrutinized design deliberately has no exact-operation reservation, no
per-table/per-shard reservation array, no object-aware `allocUN()`, no hidden
waiter, and no result wrapper.  Those mechanisms are unnecessary for a
synchronous shard-turn operation and would add storage, branches, ownership,
and failure modes to ordinary CRUD.  The only new API type is the result enum;
the only common utility is a pure UN comparison.

## Existing defect

The current idempotent update overload is representative:

```c++
if (un != nullUN() && nextUN(shard) > un) {
  l(nullptr);
  return;
}
update<KeyIDs_>(ZuMv(object), ZuFwd<L>(l));
```

Insert and delete have the same one-sided comparison:

```text
nextUN > U  -> skip
nextUN = U  -> execute at U by coincidence
nextUN < U  -> incorrectly execute at nextUN, not U
```

When `nextUN < U`, missing earlier operations must advance that table/shard
stream first.  Executing at the current UN loses the durable association
between the operation and `U`.

The existing null callback is also ambiguous: it can mean already applied,
missing row, invalid object state, or a UN gap.  Finally, accepting `nullUN()`
silently changes an explicitly numbered operation into an ordinary allocated
operation.

## Exact intended-UN contract

For intended UN `U`, every UN-bearing insert, update, and delete classifies on
the owning DB shard:

```text
nextUN < U  -> NotReady; do not look up or mutate
nextUN = U  -> prepare the object at U and dispatch Executed
nextUN > U  -> Skipped; do not look up or mutate
```

`U` identifies one position in one `(table, shard)` stream.  It is not a row,
saga, or DB-wide operation identifier.

Consequently, `Skipped` proves only that the stream has consumed position
`U`; it does not by itself prove which semantic operation consumed it.  The
caller must durably bind one intent to `U` and prevent a different operation
from taking that position.  The saga step journal and its non-intervenable
journal/effect turn provide that proof for `ZdbSaga`.

The equality path passes `U`, not a newly read `nextUN`, to
`AnyObject::insert_()`, `update_()`, or `del_()`.  `commit()` retains its final
check through `allocUN(shard, U)`.  `NotReady` and `Skipped` return before any
object transition, UN-cache eviction, stale-buffer marking, or application
callback.

The UN-bearing overloads reject `nullUN()` as `Invalid`.  The ordinary
overloads remain the only interface which allocates the current UN.

No scheduler callback can interleave while an operation body runs
synchronously on its owning shard.  The operation body must not yield and must
not commit another mutation to the same table/shard before committing or
aborting the supplied object.  Such re-entrancy is a contract violation, not a
case worth taxing every ordinary CRUD call with a reservation lookup.  If it
does consume `U`, the existing `allocUN(shard, U)` check rejects the original
commit.  `ZdbSaga` controls its update body and can enforce the narrower
contract; a saga failure after its step row committed is fatal to activation.

This relies on thread-affined shard ownership rather than locks or shared
state, as prescribed by [`GUIDELINES.md`, lines 440-444](GUIDELINES.md#L440)
and [lines 598-606](GUIDELINES.md#L598).  Avoiding the reservation array and
ordinary-CRUD branch follows the hot-path efficiency and storage audit at
[`GUIDELINES.md`, lines 211-236](GUIDELINES.md#L211) and
[lines 294-296](GUIDELINES.md#L294).

## Result and callback contract

Add the public result enum beside `ObjState` in `ZdbTypes.hh`:

```c++
ZtEnumNS(ZdbAPI, OpResult, int8_t,
  Invalid,
  Executed,
  Skipped,
  NotReady,
  Missing);
```

Add `ZtEnumImplNS(OpResult)` to `ZdbTypes.cc` and export the established API
alias:

```c++
namespace ZdbOpResult = Zdb_::OpResult;
```

`OpResult` is an external API enum, so `ZtEnumNS` is appropriate above `zm`;
`int8_t` is the prescribed compact signed representation.  `Invalid` is zero
and first deliberately: value-initialization must not manufacture a success
result.  See
[`GUIDELINES.md`, lines 132-142](GUIDELINES.md#L132),
[lines 144-152](GUIDELINES.md#L144), and
[lines 173-182](GUIDELINES.md#L173).

Do not add `UNResult` or any other aggregate.  Every UN-bearing CRUD callback
receives three direct parameters:

```c++
// internal spelling
l(OpResult::T result, Object<T> *object, UN nextUN);

// external spelling
l(ZdbOpResult::T result, ZdbObject<T> *object, ZdbUN nextUN);
```

The public method's local `ZmRef<Object<T>>` owns the object through the
synchronous invocation.  The callback receives a borrowed pointer, matching
the existing non-UN CRUD callback convention.  It must not retain that pointer
after returning.  Passing another `ZmRef` would add avoidable reference-count
traffic without extending a useful lifetime.  This applies the short-lived
ownership and callback-capture guidance in
[`GUIDELINES.md`, lines 338-347](GUIDELINES.md#L338),
[lines 417-426](GUIDELINES.md#L417), and
[lines 481-484](GUIDELINES.md#L481).

The results mean:

- `Executed`: `object` is non-null and has entered the requested mutation
  state at exactly `U`; the callback is the operation body.  It may commit or
  abort.  `Executed` means the body was dispatched, not that it committed.
- `Skipped`: the observed `nextUN` is greater than `U`; `object` is null.
- `NotReady`: the observed `nextUN` is less than `U`; `object` is null.  The
  caller may park its durable description and retry later.
- `Missing`: an equality-path `findUpd()` or `findDel()` found no row;
  `object` is null.
- `Invalid`: `U == nullUN()` or a supplied object could not enter the requested
  mutation state; `object` is null.

For `Executed`, `Missing`, and object-state `Invalid`, `nextUN` equals `U`.
For `NotReady` and `Skipped`, it is the snapshot which caused the result.  It
is diagnostic data, not a live reference.

Do not encode results as special pointer values.  Every non-fatal attempt
invokes its callback exactly once.  Existing fatal store lookup errors still
fail the DB through the current retrieval path and do not become `OpResult`
values.  Keeping the enum and pointer orthogonal follows the sentinel/state
guidance in [`GUIDELINES.md`, lines 307-319](GUIDELINES.md#L307).

## Public API surface

Retain the existing method names and argument order, changing only the
callback result contract:

```c++
// l(OpResult::T, Object<T> *, UN)
template <typename L>
void insert(UN, ZmRef<Object<T>>, L &&l);

template <typename KeyIDs_ = ZuSeq<>, typename L>
void update(ZmRef<Object<T>>, UN, L &&l);

template <unsigned KeyID, typename KeyIDs_ = ZuSeq<>, typename L>
void findUpd(Shard, Key<KeyID>, UN, L &&l);

template <typename L>
void del(ZmRef<Object<T>>, UN, L &&l);

template <unsigned KeyID, typename L>
void findDel(Shard, const Key<KeyID> &, UN, L &&l);
```

Correct the current UN-bearing `del()` parameter from `ZmRef<AnyObject>` to
`ZmRef<Object<T>>`; typed table machinery should not require a cast at this
boundary.  The non-UN overloads retain their current object-or-null callback.
This follows the type/API audit in
[`GUIDELINES.md`, lines 321-336](GUIDELINES.md#L321).

## Implementation prescription

### One pure UN comparison

Add one non-template, protected internal `AnyTable::cmpUN_()` utility so
`Table<T>` can use it without exposing it as public API:

```c++
static OpResult::T cmpUN_(UN next, UN un) {
  if (ZuUnlikely(un == nullUN())) return OpResult::Invalid;
  switch (ZuCmp<UN>::cmp(next, un)) {
    case -1: return OpResult::NotReady;
    case  0: return OpResult::Executed;
    default: return OpResult::Skipped;
  }
}
```

Each caller asserts shard ownership, snapshots `nextUN(shard)` once, and then
calls `cmpUN_(next, un)`.  The utility does not read table state, allocate,
look up a row, mutate an object, or invoke a callback.

Do not add a `UNCheck { result, nextUN }` aggregate merely to return two local
values.  Do not duplicate the comparison in five templates.  Keeping the
utility outside template-dependent declarations avoids template/debug-info
bloat; using one cached snapshot, `ZuCmp`, and `switch` follows
[`GUIDELINES.md`, lines 199-209](GUIDELINES.md#L199),
[lines 266-269](GUIDELINES.md#L266),
[lines 297-302](GUIDELINES.md#L297), and
[lines 360-368](GUIDELINES.md#L360).

### Share the existing mutation body

Do not duplicate the ordinary insert/update/delete preparation blocks in the
UN-bearing overloads.  Factor each existing synchronous mutation body into a
private trailing-underscore overload of `insert_()`, `update_()`, or `del_()`
which:

1. accepts the already chosen UN and the owning `ZmRef<Object<T>>`;
2. attempts the corresponding `AnyObject` state transition;
3. returns `false` without invoking the body when the transition fails;
4. performs the existing cache and stale-buffer preparation;
5. invokes a caller-supplied body by reference under the existing
   exception/abort guard; and
6. aborts the object on callback return if it was not committed.

The ordinary public overload selects `nextUN(shard)` and preserves its
existing null-object result.  The exact public overload first classifies the
snapshot, passes `U` to the same private mutation body on equality, and maps a
failed state transition to `Invalid`.  The adapter invokes the exact callback
as `l(OpResult::Executed, object, next)` only when the body actually runs.

This creates one implementation of cache, stale-buffer, exception, and abort
handling for ordinary and numbered operations.  It uses a stack callback
adapter with specific captures; it does not add a type-erased callback,
operation context, allocation, or copy.  This follows the DRY and allocation
audits in [`GUIDELINES.md`, lines 238-250](GUIDELINES.md#L238) and
[lines 266-269](GUIDELINES.md#L266), plus the prohibition on global captures
at [lines 116-123](GUIDELINES.md#L116).

Before changing shared control flow, fix two existing preparation defects:

- ordinary `insert()` must check the boolean result of
  `AnyObject::insert_()` and invoke its failure callback instead of running the
  body on an invalid object; and
- `Table::update_()` and `Table::del_()` must save `prevUN`, perform the object
  state transition first, and evict `prevUN` only after it succeeds.

The second change ensures an `Invalid` object has no UN-cache side effect.
Preserve the established abort and stale-buffer behavior; do not expand this
precursor into row-data rollback, a copied before-image, or a heap transaction
context.  `prevUN` uses the prescribed project abbreviation.  See the
initialization/copy audit in
[`GUIDELINES.md`, lines 252-264](GUIDELINES.md#L252) and the naming rules at
[lines 679-714](GUIDELINES.md#L679).

### Object-bearing exact operations

`insert(U, object, l)`, `update(object, U, l)`, and `del(object, U, l)` each:

1. obtain the object shard and validate the owning shard context with
   `ZiAssert` before accessing table state;
2. snapshot `nextUN(shard)` once and call `cmpUN_()`;
3. on `NotReady`, `Skipped`, or null-UN `Invalid`, invoke
   `l(result, nullptr, next)` and return;
4. on equality, call the shared private mutation body with exactly `U`;
5. if its state transition fails, invoke
   `l(OpResult::Invalid, nullptr, next)`; otherwise the adapter invokes
   `l(OpResult::Executed, object, next)` inside the normal operation guard.

Use early returns and a `switch` on `OpResult::T`; do not nest the mutation
path under multiple conditions.  This aligns with the control-flow audit in
[`GUIDELINES.md`, lines 297-302](GUIDELINES.md#L297).

Example application call:

```c++
table->findUpd<0>(shard, key, un,
    [targetState, done = ZuMv(done), fail = ZuMv(fail)](
      ZdbOpResult::T result, ZdbObject<Account> *object,
      ZdbUN) mutable {
  switch (result) {
    case ZdbOpResult::Executed:
      object->data().state = targetState;
      if (ZuUnlikely(!object->commit())) {
	fail();
	return;
      }
      done(ZdbOpResult::Executed);
      return;
    default:
      done(result);
      return;
  }
});
```

The capture list is explicit, and the discrete result uses `switch`, matching
[`GUIDELINES.md`, lines 116-123](GUIDELINES.md#L116) and
[lines 297-299](GUIDELINES.md#L297).

### Key-bearing exact operations

`findUpd(..., U, l)` and `findDel(..., U, l)` use this sequence:

1. Validate the owning shard with `ZiAssert`, snapshot `nextUN(shard)`, and
   classify before the lookup.  Return `NotReady`, `Skipped`, or `Invalid`
   immediately.
2. Only equality starts the existing `findUpd_()` lookup.
3. If the lookup returns an object, call the corresponding object-bearing
   exact overload.  It performs the required post-yield classification before
   preparing the object.
4. If the lookup returns no object, take one new `nextUN(shard)` snapshot and
   classify it.  Return `Missing` only if it is still equal; otherwise return
   the new `Skipped` or `NotReady` result.

There is no redundant second classification followed immediately by a third:
the object-bearing exact overload is the final classifier for the found path,
while the not-found path performs its own final classifier.  Reclassification
is mandatory because a backing-store lookup can yield and another operation
can advance the stream before its continuation runs.

Do not retain the key, returned object, or application callback after a
`NotReady` result.  A caller parks its durable operation description and
reissues the key lookup later.  The exact overload adds no allocation or queue;
an uncached lookup continues to use the existing named `Find` heap and
continuation.  This follows the continuation rules in
[`GUIDELINES.md`, lines 270-281](GUIDELINES.md#L270), the heap rules at
[lines 238-250](GUIDELINES.md#L238), and the avoidable-work audit at
[lines 294-296](GUIDELINES.md#L294).

## Deliberately deleted mechanisms

### No exact-operation reservation

Delete the proposed `m_exactObject` array and all associated construction,
cleanup, `allocUN()` parameter changes, ordinary-CRUD checks, and DB-failure
logging.  A reservation would:

- allocate and initialize one pointer per table/shard;
- add a branch to every ordinary insert, update, and delete;
- create new cleanup paths around exceptions and aborts;
- add a second owner-like protocol for an object already owned by a stack
  `ZmRef`; and
- defend only against prohibited same-table re-entrancy during a synchronous,
  non-yielding callback.

The existing commit-time equality check is sufficient as the final invariant
guard.  The saga runner, not primitive CRUD, treats failure after journaling as
fatal.  Deleting the reservation aligns with the storage, algorithm, scope,
and unnecessary-latency audits in
[`GUIDELINES.md`, lines 211-250](GUIDELINES.md#L211),
[lines 294-296](GUIDELINES.md#L294), and
[lines 338-352](GUIDELINES.md#L338).

### No hidden parking

Primitive CRUD reports `NotReady`; it does not retain the mutable object,
callback, or key and does not create a table-level UN waiter.  Hidden parking
would add allocation, cancellation, activation-epoch, teardown, and fairness
requirements to the hot path.

The caller parks a durable description and retries.  `ZdbSaga` keeps its FIFO
of incomplete sagas.  During recovery, `NotReady` moves the current saga to
the tail and immediately drives the next saga.  A full FIFO rotation without
progress fails activation rather than polling.  This follows the no-polling
and bounded-turn rules in [`GUIDELINES.md`, lines 270-281](GUIDELINES.md#L270)
and [lines 354-357](GUIDELINES.md#L354).

### No rollback expansion

Do not add cache before-images, row copies, a rollback context, or new stale
buffer ownership.  This precursor must preserve the current mutation/abort
semantics and change only ordering needed to make an invalid state transition
side-effect-free.  Full transactional rollback is neither required for exact
UN dispatch nor offered by Zdb.  This keeps the work aligned with scope and
copy/allocation guidance at
[`GUIDELINES.md`, lines 238-264](GUIDELINES.md#L238) and
[lines 338-344](GUIDELINES.md#L338).

## Saga and replication use

The saga operation journal stores `(type, id, step) -> U` and calls the
corrected existing update API:

```text
no SagaStep row
  -> live path selects current U, journals U, then commits update at U

SagaStep row with U
  -> findUpd(..., U, ...)
       Executed -> apply mutation and commit at U
       Skipped  -> advance without invoking mutation
       NotReady -> move the saga to the recovery FIFO tail
       Missing  -> application/recovery invariant failure
       Invalid  -> application/API contract failure
```

The live saga path resolves the target before its non-yielding critical turn.
Within that turn it prepares the exact target, applies the mutation, commits
the step row on the same DB shard, and then commits the target.  It must not
invoke another mutation on that target table/shard in between.  `ZdbSaga`
owns journaling, FIFO policy, activation gating, and fatal handling; Zdb owns
the common comparison and mutation path.

Replication and recovery callers may use the same contract: `Skipped`
discards a duplicate prefix record, `Executed` applies the next record,
`NotReady` exposes an out-of-order stream, and `Missing`/`Invalid` exposes
incompatible state.  Do not maintain a second UN comparison definition.

## Naming review

All new names satisfy [`GUIDELINES.md`, lines 679-761](GUIDELINES.md#L679):

| Name | Disposition |
| --- | --- |
| `OpResult` / `ZdbOpResult` | Concise API type; the external alias has the module prefix. |
| `Executed`, `Skipped`, `NotReady`, `Missing`, `Invalid` | Precise camel-case outcomes; no ambiguous abbreviations. |
| `cmpUN_` | Concise internal utility; `UN` follows established Zdb spelling and `_` marks shard-internal use. |
| `insert_`, `update_`, `del_` | Reuse the public operation vocabulary; `_` marks the shared internal bodies. |
| `prevUN` | Uses the prescribed `previous` to `prev` abbreviation. |
| `result`, `object`, `next`, `nextUN`, `un` | Short, conventional parameter/local names. |
| `UNCheck`, `m_exactObject` | Deleted; neither type nor member is needed. |

No new name approaches the 28-byte limit.  `insert`, `update`, `findUpd`,
`del`, `findDel`, `nextUN`, and `allocUN` are existing Zdb vocabulary and are
not renamed for this change.

## Audit-flag disposition

The following checks cover every audit category in
[`GUIDELINES.md`, lines 199-357](GUIDELINES.md#L199):

| Audit category | Intended implementation | Guideline |
| --- | --- | --- |
| Code structure | `cmpUN_` is non-template; each caller caches one `nextUN` snapshot.  No invariant array access is added. | [Lines 199-209](GUIDELINES.md#L199) |
| Storage/capacity | No container, array, capacity, scan, member, padding, or cache-line concern is added. | [Lines 211-236](GUIDELINES.md#L211) |
| Heap allocation | Exact object paths allocate nothing.  Key misses reuse the existing named-heap lookup context. | [Lines 238-250](GUIDELINES.md#L238) |
| Data movement/initialization | Scalars pass by value; the callback borrows the already-owned object.  No row, key, or byte buffer is copied. | [Lines 252-264](GUIDELINES.md#L252) |
| Algorithm/control flow | One comparison utility, `switch` dispatch, and early exits; no duplicated mutation body, polling, blocking, iterator, loop, or hand hash. | [Lines 266-305](GUIDELINES.md#L266) |
| Framework fit | Uses `ZtEnumNS`, `ZuCmp`, existing `ZmRef` ownership, existing table callbacks, and existing Zdb mutation machinery. | [Lines 307-319](GUIDELINES.md#L307) |
| Type/API friction | Three direct callback parameters; no wrapper, special pointer, cast, enum mapping, or compatibility shim. | [Lines 321-336](GUIDELINES.md#L321) |
| Scope/cleanup | Deletes the proposed reservation and rollback expansion; propagates the intentional API break. | [Lines 338-347](GUIDELINES.md#L338) |
| Atomics/locks/copies | Adds none; correctness comes from the existing owning shard and scheduler turn. | [Lines 349-352](GUIDELINES.md#L349) |
| Liveness | Primitive CRUD never waits.  Saga recovery yields on `NotReady` and bounds a no-progress rotation. | [Lines 354-357](GUIDELINES.md#L354) |

Formatting during implementation must preserve hard tabs, local visual
alignment, pointer placement, and class/struct layout per
[`GUIDELINES.md`, lines 534-596](GUIDELINES.md#L534).  Runtime ownership checks
use `ZiAssert` for new run-time ownership checks; do not introduce raw `assert`,
`ZmAssert`, or a bespoke error channel.  A wrong-shard call is an API contract
failure before an attempt begins, not an `OpResult`; its graceful assertion
return is therefore outside the exactly-once result callback guarantee.  See
[`GUIDELINES.md`, lines 359-368](GUIDELINES.md#L359).

## Implementation sequence

1. Add `OpResult`, its implementation macro, and `ZdbOpResult`.
2. Add `AnyTable::cmpUN_()`.
3. Factor the existing synchronous insert/update/delete bodies so ordinary and
   exact overloads share preparation, callback, and abort handling.
4. Fix the ignored insert transition result and post-transition `prevUN`
   eviction ordering.
5. Replace all five UN-bearing overloads with the three-parameter result
   contract; correct the typed `del()` argument.
6. Add pre-lookup and post-yield classification to `findUpd()` and
   `findDel()`.
7. Propagate the intentional callback break and add focused tests.  Do not add
   compatibility paths.

## Verification

Use `ZuTestUtil`/`ZuTest`, deterministic scheduler barriers, and the deferred
mock store.  Do not use sleeps or polling, per
[`GUIDELINES.md`, lines 486-489](GUIDELINES.md#L486).

Verify:

- `nextUN < U` returns `NotReady`, invokes no mutation body, performs no
  lookup, and changes no object, cache, count, UN, replication, or store state;
- equality returns `Executed` with the borrowed object and snapshot `U`; a
  successful commit consumes exactly `U`;
- `nextUN > U` returns `Skipped` with a null object and no lookup or mutation;
- `findUpd()` and `findDel()` distinguish `Missing`, `Skipped`, and
  `NotReady` before and after a deferred lookup;
- `nullUN()` and invalid object states return `Invalid` without cache effects;
- ordinary insert rejects an invalid object state instead of invoking its body;
- an exact lookup which starts equal but resumes after advancement returns
  `Skipped`, including when no row was found;
- every non-fatal attempt invokes its callback once and supplies an object only
  for `Executed`;
- an uncommitted or throwing callback follows the existing abort behavior;
- a prohibited nested same-table mutation causes the outer exact commit to
  fail its existing `allocUN` check, without any reservation machinery;
- two out-of-order exact operations can be parked and retried in order without
  renumbering;
- saga recovery moves `NotReady` to the FIFO tail, makes progress through
  another saga, and detects a full no-progress rotation;
- callbacks remain on the owning DB shard; and
- debug and release builds expose identical result states.

## Non-goals

- UN idempotence does not identify a saga or row; the caller owns the durable
  association with `U`.
- It does not make multiple mutations transactional or isolated.
- It does not infer application update semantics.
- It does not add permanent operation history, audit storage, object retention,
  automatic retry, or application-data rollback.
