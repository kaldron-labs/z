# Zdb saga step declaration overhaul

## Outcome

Replace the separate `Steps` typelist plus run-time `switch (saga->step())`
with the declaration form demonstrated by `zdbsaga.proposed.cc`:

```c++
struct BalanceTransfer {
  using Type = ZuStringT<"balanceTransfer">;
  enum { NSteps = 4 };

  // saved fields

  ZdbSagaStep(0, transfer, Insert) {
    // schedule on the transfer shard; call complete(true) after commit()
    return {};
  }

  ZdbSagaStep(1, account, Update) {
    // schedule the find and debit; call complete(true) after commit()
    return {};
  }

  ZdbSagaStep(2, account, Update) {
    // schedule the find and credit; call complete(true) after commit()
    return {};
  }

  ZdbSagaStep(3, transfer, Update) {
    // schedule the find and update; call complete(true) after commit()
    return {};
  }
};
```

`MSaga` derives the persisted table/operation sequence from these overloads,
dispatches the overload selected by the current saga step with a movable
completion continuation, and completes the saga automatically when the current
step reaches `NSteps`. The application invokes the continuation with `true` or
`false`; the returned descriptor exists for compile-time metadata and is not a
run-time result. There is no run-time descriptor, registry, allocation,
container, or virtual dispatch added by this change.

This is an intentional breaking API change. Delete `ZdbSagaSteps`, rename the
descriptor template from `ZdbSagaStep` to `ZdbSagaStep_`, and migrate all
dependents. Do not retain aliases, compatibility macros, forwarding overloads,
or support for `U::Steps`; dependent compatibility is a non-goal under
[`GUIDELINES.md:8-16`](GUIDELINES.md#L8) and dead compatibility code is a red
flag under [`GUIDELINES.md:330-344`](GUIDELINES.md#L330).

## 1. Define the step macro

Use the descriptor itself as the SFINAE-selected return type, as proposed. Its
type exposes the compile-time metadata; its value carries no execution result:

```c++
template <typename TableID_, ZdbSagaOp::T Op_>
struct ZdbSagaStep_ {
  using TableID = TableID_;
  enum { Op = Op_ };
};

#define ZdbSagaStep(step_, table, op) \
  template <unsigned Step, typename Complete> \
  ZuIfT<Step == step_, \
    ZdbSagaStep_<ZuStringT<ZuPP_Q(table)>, ZdbSagaOp::op>> \
  operator ()(void *context_, ZdbSaga *saga, Complete &&complete)
```

Each macro expansion defines one SFINAE-selected `operator()<N>`. Its returned
descriptor is an empty compile-time metadata carrier; it is not stored,
allocated, or interpreted at run time. `MSaga::run` must not use the return
value: the body can post to a shard and return before it knows whether the step
was successfully initiated. Bodies return `{}` solely to satisfy the
metadata-bearing return type.

The forwarded `complete` continuation is the run-time result and lifetime
channel. The runner constructs a small concrete completion callable containing
an owning reference to the enclosing `MSaga` plus the captured saga epoch and
step. Application code moves it through any shard post or asynchronous read,
then either invokes it or moves it into the saga mutation callback:

- `complete(true)` is called only after the target object's `commit()` has
  returned successfully and is the sole trigger that permits the runner to
  advance to the next step;
- `complete(false)` fails the saga and never advances it.

Carrying `complete` keeps both the captured saga-definition `this` and the raw
`ZdbSaga *saga` base pointer alive. No separate `ZmRef<ZdbSaga>` is required on
that continuation chain. `complete(true)` transfers the runner's lifetime hold
to the DB-SID completion post, which records the completed step and schedules
the next runner dispatch. The saga mutation wrapper stages its shard for
cleanup, but it must not advance or invoke `sagaRun` independently.

The callable is one-shot: invoking it moves out its owning saga reference.
The exactly-once call requirement applies when application code retains the
continuation and its mutation callback is invoked. A recovered operation can
return `Skipped` without invoking that callback or performing another commit.
In that case the saga wrapper constructs/invokes the same concrete completion
callable with `true` on behalf of the already-committed operation, without
invoking the application mutation callback or re-executing its body. The
application-captured handle is then destroyed uncalled. This internal completion
is the only recovery exception to the rule that application code calls
`complete(true)` after its own `commit()`.

Keep `table` and `op` as macro parameters so the caller writes bare tokens.
`ZuPP_Q(table)` produces the compile-time table string and `ZdbSagaOp::op`
selects the existing enum. Keep `ZdbSagaStep_` trailing-underscored because it
is internal machinery. All names remain below the
28-byte limit and follow the macro/type/internal naming rules in
[`GUIDELINES.md:679-747`](GUIDELINES.md#L679). `ZdbSaga.hh` already directly
includes `ZuPP.hh`, `ZuInt.hh`, and the other required type machinery; preserve
direct dependency discipline.

Delete these obsolete definitions:

- the public `ZdbSagaStep` class template;
- `ZdbSaga_Step_` and `ZdbSaga_Step`; and
- `ZdbSagaSteps`.

## 2. Derive the step typelist from each saga type

Add the proposed internal transformation directly over each overload's return
type:

```c++
namespace Zdb_ {

template <typename U>
struct SagaSteps_ {
  template <typename I>
  using Step = decltype(
    ZuDeclVal<U &>().template operator()<I{}>(
      nullptr, nullptr, [](bool) { }));

  using T = ZuTypeMap<Step, ZuSeqTL<ZuMkSeq<U::NSteps>>>;
};

template <typename U>
using SagaSteps = typename SagaSteps_<U>::T;

} // namespace Zdb_
```

`ZuMkSeq` creates `[0, NSteps)`, `ZuSeqTL` converts it to `ZuUnsigned` tags,
and `ZuTypeMap` obtains the descriptors. A missing numbered overload makes the
saga definition ill-formed at compile time. A duplicate numbered step is an
ambiguous/redeclared overload and also fails compilation. Numbering therefore
remains dense and ordered without a separately maintained typelist.

Keep `enum { NSteps = N };` as the only declared count. Require `NSteps > 0`
in the existing saga-definition validation. Feed `SagaSteps<S>` to
`SagaStepsValid_` so every derived operation remains in `Insert..Delete`.
Continue to require non-empty, unique saga type discriminators at the bundle
level. Do not introduce concepts or `requires`; use the existing detector,
SFINAE, `ZuIfT`, `ZuTypeList`, and `ZuSeq` conventions described in
[`GUIDELINES.md:140-164`](GUIDELINES.md#L140) and
[`GUIDELINES.md:388-405`](GUIDELINES.md#L388).

An extra declaration whose number is outside `[0, NSteps)` is dead application
code and is not discoverable from C++ overloads without adding counters or a
second declaration source. Do not add `__COUNTER__`, registration state, or
run-time validation to police it. Missing in-range steps are rejected; unused
out-of-range members have no persisted or run-time effect.

## 3. Use the derived metadata everywhere

Replace every `typename Def::Steps`/`S::Steps` use in `MSaga` with
`SagaSteps<Def>`:

- `SagaDefValid` validates `Def::NSteps` and `SagaSteps<Def>`;
- `MSaga::catalog()` indexes `SagaSteps<Def>` to emit the same
  `SagaTypeStep(type, table, step, op)` rows;
- `MSaga::stepCount()` returns `Def::NSteps`; and
- `MSaga::stepDef_()` and `stepDef()` index `SagaSteps<Def>`.

The stored `saga_type` rows and startup reconciliation algorithm do not change.
An application that migrates only syntax produces byte-for-byte equivalent
type/step/table/operation metadata, so an existing matching catalog remains
valid. Changing a declaration still requires a new saga type name. Do not
alter any FlatBuffers schema, saga table, store API, recovery hash, or journal
format.

As stated in the proposal, the macro metadata describes the application's
declared mutation. The application remains responsible for calling the
matching saga mutation variant and table in the body. Keep the existing cold-
recovery target check against saved journal rows, but do not add steady-state
per-step validation or table lookup. That would tax the normal path and revive
the validation design previously rejected for sagas.

## 4. Dispatch step overloads and complete automatically

Change `MSaga<Sagas>::run()` from calling one untemplated application
`operator()` to this sequence after narrowing the `ZuUnion`:

Use a concrete, non-allocating internal callable rather than storing a
type-erased callback in the saga:

```c++
struct SagaStepComplete {
  ZmRef<Saga>	saga;
  uint64_t	epoch;
  uint32_t	step;

  void operator ()(bool);
};
```

`operator()` is one-shot and moves `saga` into the DB-SID result post. For a
successful executed operation, the on-shard wrapper first writes the target
shard into the current `m_locs[step]`; `SagaStepComplete::operator()(true)` reads
and captures it before posting. A recovery `Skipped` path does the same and
invokes a newly constructed `SagaStepComplete` directly. Do not store an
owning completion callback inside `Saga`; that would create a reference cycle.
Move the runner's existing saga reference into `SagaStepComplete`, and on
`Skipped` move the wrapper-held reference into the replacement callable; do
not introduce an avoidable reference-count increment.

1. Read `saga->step()` once.
2. If it is less than the concrete definition's `NSteps`, use
   `ZuSwitch::dispatch<Def::NSteps>` and invoke
   `def.template operator()<I>(context, saga.ptr(), ZuMv(complete))`, where
   `complete` is a movable continuation owning the incoming saga reference.
   Discard the returned descriptor.
3. If it equals `NSteps`, call the framework's `done()` path immediately.
4. If it exceeds `NSteps`, fail the saga as an internal invariant violation;
   during recovery this remains a fatal activation failure.

The `complete` continuation may run on any shard. On `false`, it posts the
generic step-failure exception through `DB::sagaFail` and never invokes the
next step. On `true`, it snapshots the target shard staged by the mutation
wrapper, posts to the DB SID, validates its captured saga epoch and step,
removes any recovered step/UN reservation, records the step location,
increments the step exactly once, and calls `sagaRun`. Preserve the existing
epoch behavior: a callback made stale by deactivation is discarded. A repeated
or wrong-step callback in the current epoch is an application invariant
violation, not a second advancement.
Add a distinct `DB::sagaStepComplete(...)` handler for this transition; do not
overload the existing `DB::sagaComplete(...)`, which denotes completion of the
entire saga after journal cleanup. After this refactor, only
`sagaStepComplete` may call `sagaRun` to advance the current saga to its next
step. The recovery `NotReady` path may call `sagaReplay` to run a different
saga at the queue head, but it does not advance the deferred saga.

Keep `SagaRunFn` itself unchanged; `MSaga::run` creates and forwards this
concrete callable while servicing that erased callback. Do not expose another
application callback or allocate a second result object.

In steady state, `sagaFail` calls the application's `errorFn` and fails the DB;
during rebuilding it fails activation and leaves the durable saga row for the
next recovery.

Invoking `complete(true)` without first committing the declared mutation is an
application bug: it advances a step whose effect is not established. Invoking
it before `commit()` can also end the guaranteed lifetime of captured `this`
too early. The application must make `complete(...)` its last use of both
captured `this` and `saga`. Failure to invoke a retained continuation is an
application coding error and is outside the framework's scope; do not add a
timeout, watchdog, registry, or other machinery for it. The framework must not
infer progress from the function return.

The final step's `complete(true)` increments the current step and reposts
`sagaRun`; that dispatch observes `NSteps` and performs normal main-row/step-row
cleanup. No application step calls `done()`, and no synthetic fifth step or
completion descriptor is persisted. Move `Saga::done()` out of the public
application interface (protected access for the typed `MSaga` path is
sufficient); retain `fail()` for application failures.

The application owns shard placement for the entire step. Saga mutation
wrappers have the same on-shard precondition as the corresponding ordinary
`Table` mutations. The application may need to perform an ordinary `find` and
then choose or construct the object passed to `saga->insert`, `update`, or
`del`; posting from the mutation wrapper would split that application-defined
on-shard sequence and permit intervening shard work. It would also add an
unnecessary context switch when the caller is already correctly placed.

`MSaga::run` cannot choose the shard because only application code identifies
the next table, key, and operation, and one saga can move across shards between
steps. A step therefore uses the restored `Table::run(shard, ...)` convention
to place the whole unit of work, moves the runner continuation (which owns a
`ZmRef<ZdbSaga>`) across that post, performs any required reads on the target
shard, and calls the saga mutation wrapper from the on-shard continuation. If
application control is already on that shard, it calls the step logic directly.
The wrapper asserts `shardInvoked(shard)`,
commits any required `saga_step` intent, and immediately calls the ordinary
`Table` operation without posting:

```c++
context->accounts->run(shard, [
  this, context, saga, complete = ZuMv(complete), shard, key
]() mutable {
  context->accounts->find<0>(shard, key,
    [this, context, saga, complete = ZuMv(complete)](auto object) mutable {
      if (!object) { complete(false); return; }
      // This callback is on shard.  The saga wrapper must remain inline.
      saga->update(context->accounts, ZuMv(object), [
        this, complete = ZuMv(complete)
      ](auto object_) mutable {
	// mutate and commit object_
        object_->commit();
	complete(true);
      });
    });
});
return {};
```

Refactor the current `Saga::mutate` and `findMutate` paths accordingly. They
must not bounce from the target shard to the DB control SID and back before
the effect. Any validation or recovery preparation needed before application
code runs must be staged by `DB::sagaRun`. Before invoking the application
mutation callback, the wrapper binds its target shard to the current saga step
so `complete(true)` can capture that shard in the DB-SID completion post. This
is a one-way ownership handoff: the shard writes the current `m_locs` slot,
`complete(true)` snapshots it on that same shard, and only the captured value
crosses to the DB SID. The `Executed` path must not post `sagaResult` or
`sagaRun`; the application's queued completion is the only advance.

Delete the `Executed` and `Skipped` advancement arms from the current
`DB::sagaResult`. `Executed` is completed only by the application-provided
continuation; `Skipped` is completed only by the wrapper-created
`SagaStepComplete`. Retain the result handler solely for `NotReady` rotation
and fatal `Missing`/`Invalid` handling. This removes the current second route
to `++m_step` and makes the completion invariant structural rather than
conventional.

Do not put saga-dependent verification or state mutation after invoking the
application mutation callback. The DB SID can process `complete(true)` while
the shard stack is unwinding. This is safe because the application calls
`complete(true)` only after the in-memory commit, makes it the callback's final
action, and accesses neither captured `this` nor `saga` afterward. A violation
of that application contract is not something the framework should detect or
work around.

For recovery outcomes, route `Skipped` through the same internal successful
completion callback with the wrapper's known shard by constructing a fresh
`SagaStepComplete` from the wrapper-held saga reference, epoch, and step. Do not
store an owning callback in the saga or try to recover the application's moved
handle from the opaque mutation lambda. Route `NotReady` only to recovery queue
rotation; it must neither invoke completion nor advance the current saga.
`Missing`, `Invalid`, a target mismatch, and a complete recovery rotation with
no progress remain fatal activation errors. During activation, preserve the
existing strictly serial replay/rotation discipline when making recovered step
and reserved-UN state available to the on-shard wrapper; do not introduce a
lock or a steady-state container. The implementation audit must specifically
prove that no DB-control post occurs between an application on-shard read and
the intent/effect pair, that no yield occurs between the intent commit and the
ordinary `Table` call, and that every next-step `sagaRun` is reached only from
a successful completion handler.

Use `ZuSwitch`, not a function-pointer table or recursive run-time chain. Cache
the step value before branching, keep captures explicit, and do not add a
continuation allocation beyond those already needed by the mutation invoked by
the step. This follows the discrete-dispatch, no-duplication, and hot-path
rules in [`GUIDELINES.md:266-305`](GUIDELINES.md#L266) and the compile-time
dispatch guidance in [`GUIDELINES.md:388-405`](GUIDELINES.md#L388).

The macro deliberately exposes `ZdbSaga *saga`, as proposed. Capture and pass
both the saga-definition `this` and raw `saga` pointer through the asynchronous
chain together with `complete`. The continuation's owning reference guarantees
that the complete `MSaga` allocation, including the active union member and its
`Saga` base, remains in scope until `complete` is called. Do not add a second
reference, DB ownership of live sagas, or a self-reference for this path.
Document this lifetime coupling at the macro/API example. It preserves the
current recovery-only `SagaHash` and scalar graceful-drain design and follows
the explicit ownership/callback guidance in
[`GUIDELINES.md:408-484`](GUIDELINES.md#L408).

## 5. Migrate all saga definitions

Propagate the breaking change in one pass:

- rewrite `zdb/example/zdbsaga.cc` to match `zdbsaga.proposed.cc`, including
  deterministic shard functions derived from primary keys, `NSteps`, four
  `ZdbSagaStep` bodies, and removal of the manual switch and explicit `done()`;
- retain the restored `Table::run` placement used by the proposal, and
  explicitly capture both `saga` and each local `shard` used by a lambda; do
  not introduce global captures; mark each lambda that moves or invokes the
  one-shot `complete` callable `mutable`;
- rewrite saga fixtures in `zdb/test/ZdbSagaTest.cc`, including the catalog,
  payload, altered-definition, admission, recovery, yield, and failure cases;
- rewrite `zdb/itest/zdbsagareptest.cc` and
  `zdb_pq/itest/zdbsagatest.cc` without changing their crash/replay semantics;
- update `goal.md` and `plan.md` examples and terminology to make `plan2.md`'s
  API the sole current design; and
- leave `zdbsaga.proposed.cc` as the design input unless it is intentionally
  deleted after the canonical example fully supersedes it.

For shared setup that formerly preceded a switch, use a non-template member
called by each step or a step-indexed member template. Do not copy large common
blocks into every overload. Keep all lambda captures explicit; the migration
must introduce neither `[&]` nor `[=]`. Preserve reads and arbitrary
application work between mutations: only the mutation boundaries are counted
steps. Capture `this` and `saga`, move `complete` through all asynchronous work,
and invoke it after `commit()` when the application mutation callback executes
(or with `false` if the application abandons the step before handing it to a
saga mutation wrapper). A recovery skip may instead destroy it uncalled. End
the body with `return {};`; do not use the returned descriptor as the run-time
result. More precisely, on `Skipped` the wrapper enters the same completion
path by constructing and invoking a fresh `SagaStepComplete{...}` with
`true`, while the application callback containing its moved handle is destroyed
without being invoked.

Do not change persisted saga `Type` strings or their table/op sequences during
this syntactic migration. The startup catalog conflict tests must continue to
prove that a real sequence change under the same type is fatal.

## 6. Verification

Add focused unit coverage before relying on the existing end-to-end tests:

1. Statically compare `SagaSteps<BalanceTransfer>` with the former explicit
   four-element descriptor list, including both `account` updates.
2. Verify `MSaga::catalog`, `stepCount`, and `stepDef` expose the same rows and
   operations as before.
3. Record which templated overload executes and prove steps `0..N-1` each run
   in order without an application switch.
4. Verify the empty descriptor return is ignored by `MSaga::run` and remains
   usable by `SagaSteps` for compile-time metadata deduction.
5. Prove `complete(false)` enters the existing failure path exactly once and
   leaves the saga durable for recovery; prove `complete(true)` is called after
   commit and is the only path that advances an executed step. Prove a
   recovered `Skipped` operation enters the same internal successful-completion
   path without invoking the application mutation callback.
   Instrument `sagaRun` and prove no mutation-result path directly dispatches
   the next step.
6. Prove the framework calls completion after step `N-1`, deletes the saga
   rows, and invokes `doneFn` without an application `done()` call.
7. Preserve tests for a yielded read before a mutation, moving `complete` and
   the raw `saga` pointer through the continuation, and verify replay starts
   again at step zero without an additional `ZmRef<ZdbSaga>`.
8. Preserve compile-time rejection coverage for empty bundles, duplicate saga
   type names, zero-step definitions, missing numbered overloads, and invalid
   operation descriptors.
9. Preserve startup catalog exact-match/conflict, sparse recovered journal,
   `NotReady` rotation, multi-shard replication, hard-crash PostgreSQL replay,
   and graceful-shutdown tests.

Build `zdb/src` before all dependents with the configured Clang debug build,
then build and run `zdb/test`, `zdb/itest`, `zdb/example`, and
`zdb_pq/itest` against live PostgreSQL using the established environment. Run
the saga unit binary, replication saga binary, canonical example, and
PostgreSQL saga binary through `libtool --mode=execute valgrind`. Require no
compiler warnings, all TAP checks passing, the example's expected balances,
zero Valgrind errors or lost bytes, and a clean `git diff --check`.

## Audit constraints

- The change adds compile-time syntax, typed dispatch, and continuation-driven
  completion: no new container, lock, atomic, persisted field, or run-time
  registry. `SagaStepComplete` adds no explicit allocation; normal scheduler
  closures retain their existing allocation behavior, and the generic failure
  exception remains cold-path allocation.
- `SagaSteps` is derived on demand as a type; it is not stored in each saga or
  DB.
- Step metadata comes directly from the unevaluated return type; there is no
  second probe declaration to keep synchronized.
- The descriptor is empty and has no persistence, heap allocation, or per-saga
  storage; execution deliberately ignores its value.
- The step continuation reuses ordinary templated callback forwarding and owns
  the saga across application scheduling; do not add an independently
  type-erased callback allocation.
- The `ZmRef<Saga>` in `SagaStepComplete` is an intentional transfer of primary
  ownership for an in-flight saga, not a short-lived dependent's back-pointer
  to a separately owned long-lived object. It is always moved, is not stored
  by `Saga`, and cannot form a cycle; this is the required exception to the
  callback/back-pointer warning in
  [`GUIDELINES.md:330-344`](GUIDELINES.md#L330).
- Mutation wrappers never advance an `Executed` step. `complete(true)` and the
  equivalent internal completion for `Skipped` are the only next-step paths;
  all false/error/deferral paths are non-advancing.
- The typed `ZuSwitch` adds no indirect call or virtual dispatch.
- `NSteps` replaces, rather than duplicates, the old typelist count.
- Automatic completion removes public application ceremony and one invalid
  early-completion surface.
- Existing `ZuUnion` narrowing, `cdispatch` save, mutable `dispatch` run,
  journaling, exact-UN recovery, and DB lifecycle ownership remain unchanged.
