# Zdb saga rollback plan

## Objective and exact contract

Add in-memory reverse compensation to Zdb sagas and propagate the intentional
breaking application API to every in-tree dependent. Preserve the existing forward
intent/UN recovery, epoch, sharding, and drain behavior.

The seven required features are acceptance criteria:

1. Replace `ZdbSagaStep` with the revised one-argument macro.
2. Add the explicit defaulted `bool Fwd = true` template parameter. Roll forward
   with `operator()<Step>()`; only rollback spells
   `operator()<Step, false>()`.
3. Add and require `ZdbSagaBase<Context>`.
4. Set `ZdbSagaBase::context` and `ZdbSagaBase::saga` once, before the first
   `operator()` call on that loaded definition.
5. Reduce asynchronous step captures to `this` and moved `complete` wherever that
   is sufficient; re-derive cheap values such as shards in the destination turn.
6. Use `if constexpr (Fwd)` in each application step. After forward step `k`
   reports false, invoke successfully completed steps `k - 1` through `0` with
   `Fwd == false`.
7. Make application rollback silently idempotent. An already-undone operation is a
   success without an application error or error log. If the application remains
   stuck after its own best-effort cleanup, rollback `complete(false)` is a fatal,
   non-returning last resort: log diagnostics, drain logging, and exit for human
   intervention.

The failing forward step is not reversed. It may report false only when it has not
left an effect that itself requires compensation.

### Application-visible terminal outcome

While its original in-memory saga object survives, an admitted saga run has exactly
one normal application-visible outcome:

- **success:** the final forward step calls `complete(true)`; after normal journal
  cleanup the framework calls that saga submission's application `complete(true)`
  once;
- **failure:** any forward step calls `complete(false)`; after every required
  reverse step has completed successfully and its original intent has been deleted,
  the framework performs terminal cleanup and calls that saga submission's
  application `complete(false)` once. If step zero fails, no reverse body is
  necessary and
  the saga proceeds directly to this failure outcome after failed-step intent
  cleanup.

Rollback completion is therefore not saga success; it is the orderly culmination
of saga failure. Do not notify the application at the moment forward failure is
reported, because compensation is still in progress. Reverse `complete(false)` is
outside these normal outcomes: it logs and exits because the saga cannot reach a
consistent terminal state without human intervention.

Admission and terminal status use separate perfectly-forwarded per-call callbacks:

```c++
template <typename Context, typename Sagas>
template <typename Submit, typename Complete>
inline bool SagaDB<Context, Sagas>::saga(
  Shard shard, SagaID id, ZmRef<M> saga,
  Submit &&submit, Complete &&complete);
```

`submit(true)` means the saga row was durably admitted and execution will start;
`submit(false)` means admission failed and `complete` will not run. A false
immediate return from `saga()` means the request could not be queued, so neither
callback runs. After successful admission, application `complete(true)` means final
forward success and application `complete(false)` means orderly failure after
rollback. The decayed
completion object is moved through the live continuation chain and is never stored
in `DB`, `Saga_`, or persistent data. It is destroyed if that chain is discarded by
crash or deactivation; recovered execution uses an internal no-op completion.
The context is configured once by `sagas(context)` and retained in the typed
`SagaDB` as `ZmRef<Context>`. Each active definition's `SagaBase` receives only the
raw pointer before its first step; the owning DB scope guarantees that pointer until
finalization without per-saga reference-count churn.
Invoke both callbacks on the DB thread, after their associated internal state
transition is complete; callers must return promptly or post work elsewhere rather
than blocking that thread.

## Corrected design constraints

### Rollback is deliberately not durable

Rollback direction is an in-memory runtime decision. Do not persist it.

- Do not change `SagaData`, `SagaStep`, either FlatBuffers schema, the saga catalog,
  `SagaStepKey`, or recovery scan formats.
- Do not add a rollback journal, reverse intent, rollback cursor, persistent phase
  or direction field, error payload, table, hash, callback outbox, or queue. The
  transient `m_fwd`/completion direction described below is the only direction
  state.
- Do not reserve UNs for rollback mutations.
- Do not teach activation/recovery to resume rollback.

Every admitted or recovered saga starts in forward mode at step zero. If the
process or leader fails during rollback, the existing persisted saga and remaining
forward intents cause normal roll-forward to be attempted again. That attempt may
succeed. If it fails, a new in-memory rollback begins.

This is the smallest design consistent with the required semantics and with
`GUIDELINES.md` **General principles** (minimize state, allocation, and work),
**Persistence** (reuse the existing Zdb saga mechanism), and **Audit flags / Scope
and cleanup** (delete non-goal machinery and compatibility code).

### Forward intents are consumed by successful reversal

Rollback mutations use the ordinary table mutation path without calling
`sagaPrepare`, writing `SagaStep`, reserving a UN, or replaying a reverse intent.
After reverse step `i` reports success, delete its original forward `SagaStep` row
using the recorded `m_locs[i]`. Proceed to `i - 1` only after that deletion commits.
Treat an already-absent intent row as success.

If the failed forward step wrote an intent before reporting false but made no
effect, delete that intent before starting step `k - 1`; if there is no such row,
continue silently. This prevents a known-failed attempt from being mistaken for a
completed forward step after a later crash.

The order is intentional:

1. application compensation succeeds;
2. its original forward intent is deleted;
3. the runner dispatches the preceding reverse step.

A hard crash at any point abandons the in-memory rollback. Recovery always follows
the ordinary forward path using whichever original intents remain; it must not
infer or reconstruct reverse progress.

This uses deterministic deletion rather than scanning and keeps scheduler work
continuation-based, in line with `GUIDELINES.md` **Audit flags / Storage and
capacity**, **Algorithm and control flow**, **Liveness**, and **Callbacks**.

### Compile-time direction; one irreducible runtime bit

Add only one rollback-state field, `bool m_fwd`, to `Saga_` and initialize it true
on every `init_`. The per-submission completion callback is transient continuation
state, not a member of `Saga_`. `m_fwd` is DB-thread-owned and
`MSaga::run` branches once on it:

- forward: `operator()<I>(complete)`, intentionally using the `Fwd = true` default;
- rollback: `operator()<I, false>(complete)`.

Continue using `ZuSwitch::dispatch` for the runtime step index and the existing
`ZuTypeList`, `ZuSeq`, `ZuTypeMap`, and `ZuStringT` metadata. `if constexpr (Fwd)`
removes the unused application branch. Do not add a runtime operation map, virtual
direction interface, inverse-operation trait, fixed lookup table, concepts, or
`requires`.

This follows `GUIDELINES.md` **Core language**, **Static polymorphism and
constraints**, **Compile-time data and matching**, **Enums**, and **Audit flags /
Storage and capacity**.

## Implementation

### 1. Public application contract (`zdb/src/ZdbSaga.hh`)

1. Add the internal base and public alias:

   ```c++
   namespace Zdb_ {
   template <typename Context_>
   struct SagaBase {
     Context_	*context = nullptr;
     Saga	*saga = nullptr;
   };
   }

   template <typename Context>
   using ZdbSagaBase = Zdb_::SagaBase<Context>;
   ```

   `SagaBase` is an all-public data `struct`, so its members have no `m_` prefix.
   The public alias has the module prefix. Use the already-declared internal `Saga`
   type rather than reordering declarations to expose the later `ZdbSaga` alias.
   Applications use `using Base::context; using Base::saga;`, not forwarding
   accessors. This follows `GUIDELINES.md` **Class and struct shape**, **Naming /
   Casing and member prefixes**, and **Core language** (prefer `using` to base
   forwarders).
2. Replace the macro, with no compatibility overload:

   ```c++
   #define ZdbSagaStep(step_, table, op) \
     template <unsigned Step, bool Fwd = true, typename Complete> \
     ZuIfT<Step == step_, \
       ZdbSagaStep_<ZuStringT<ZuPP_Q(table)>, ZdbSagaOp::op>> \
     operator ()(Complete &&complete)
   ```

   This follows `GUIDELINES.md` **General principles** and **Audit flags / Scope
   and cleanup**: dependent compatibility is a non-goal, so delete the old
   three-argument operator contract rather than forwarding it.
3. Change `SagaSteps_` metadata discovery to call the one-argument
   `operator()<I{}>(complete)` and thereby use the default `Fwd == true`. Retain
   the existing `ZuTypeMap`/`ZuSeq` design.
4. Validate with the established SFINAE/`ZuIfT` idiom, parameterized by the
   bundle-level `Context`, that each definition derives from `SagaBase<Context>`
   and that its default-forward operator returns valid step metadata. A definition
   must not declare its own context-type alias. Do not add a redundant
   reverse-signature detector:
   the macro supplies both values of `Fwd`, and the rollback body is instantiated by
   `MSaga::run`. This follows `GUIDELINES.md` **Static polymorphism and
   constraints** and **Assertions and type mechanics**.
   Keep callback-independent step/metadata validation in `MSaga<Sagas>`; put the
   context/base compatibility assertion in `SagaDB<Context, Sagas>`, the first type
   which knows both inputs.
5. Account explicitly for the non-empty aggregate base in construction. Add
   `using Base = ZdbSagaBase<Context>` to each definition. When `MSaga::load` calls
   `ZfbStruct::ctor<Def>`, pass a default `typename Def::Base{}` as its leading
   constructor argument so the loaded fields initialize the derived aggregate,
   not the base subobject. Update positional application/test construction such as
   `LiveSaga{42}` to include an empty base or use an existing valid designated form.
   Confirm both gcc and clang accept the chosen aggregate form. Do not add
   per-definition forwarding constructors or change generic `ZfbStruct` behavior.
   This follows `GUIDELINES.md` **Data movement and initialization**, **Metadata**,
   and **Scope and cleanup**.
6. Keep declarations that do not depend on template parameters outside enclosing
   templates, per `GUIDELINES.md` **Audit flags / Code structure**.
7. Replace the heavyweight submission union and DB-wide terminal handler with a
   fully templated per-call interface:

   ```c++
   template <typename Submit, typename Complete>
   bool saga(
     Shard, SagaID, ZmRef<M>, Submit &&submit, Complete &&complete);
   ```

   Decay each callable once at the asynchronous ownership boundary and move it
   thereafter. Delete `SagaSubmitFn`, `SagaCompleteFn`, `SagaSubmitResult`,
   `SagaDoneFn`, `SagaErrorFn`, `SagaHandler`, and all corresponding exported
   aliases. Do not retain compatibility aliases, adapters, or a generic callable
   wrapper. This removes framework type erasure, indirect invocation, and possible
   heap fallback from live submission, following `GUIDELINES.md` **Callbacks**,
   **Heap allocation**, **Data movement and initialization**, **General
   principles**, and **Audit flags / Scope and cleanup**.
   Use the established detector/`ZuIfT` idiom to reject callables which cannot be
   decay-constructed into owned asynchronous state and invoked with `bool`; do not
   require default construction, copying, a `ZmFn` conversion, concepts, or
   `requires`. Document that both callables must return promptly and must not throw
   across the DB callback boundary.

   The accepted cost is one instantiation of the callback-carrying path per
   `Submit`/`Complete` type and a continuation whose size includes the concrete
   completion capture. Keep callbacks small and move-only, measure carrier/post
   sizes, and factor all callback-independent work out of the templates. Do not
   solve code size or a large caller closure pre-emptively with framework type
   erasure or allocation; those are the only material reasons to prefer the deleted
   `ZmFn` aliases here.
   Delete application-visible `Saga::fail(ZeException)` and protected
   `Saga::done()`. Application failure now enters only through forward
   `complete(false)`, and `MSaga` enters callback-aware terminal cleanup directly
   after its final step; internal invariant/storage failure remains a private
   framework path. No in-tree application caller needs either old function.
8. Make the common context type explicit in
   `template <typename Context, typename Sagas> struct SagaDB`; do not derive or
   rediscover it from application saga definitions. Propagate that parameter only
   through the public `ZdbSagaDB<Context, Sagas>` alias, definition validation, and
   the initial runner call. Keep `MSaga<Sagas>`, `MSagaHeap`,
   `ZdbMSaga<Sagas>`, and completion carriers independent of `Context`; subsequent
   runs use the pointer already stashed in `SagaBase`. The explicit `SagaDB`
   template parameter is authoritative; do not expose or infer a context alias from
   every saga definition.

   `SagaDB::sagas(...)` accepts and retains one `ZmRef<Context>` for that configured
   DB. The runner assigns
   `m_sagaContext.ptr()` and the saga pointer to each active definition's
   `SagaBase` exactly once before step zero. `SagaDB::saga(...)` therefore needs no
   context parameter. No context type map, uniqueness scan, cast, or type-erased
   context is needed, and no saga or step continuation takes another strong
   reference.

   The same configured instance is used for every live submission and every saga
   loaded during startup recovery, regardless of saga type. It is retained only as
   in-memory runner configuration and released at finalization; never serialize a
   process address into saga data or a journal. If recovery needs request-specific
   state, the serialized saga definition must contain the stable identifier needed
   for the common context to resolve it. Delete the multi-member `SagaHandler`.
   This follows
   `GUIDELINES.md` **General principles**, **Static polymorphism and constraints**,
   **Type and API friction**, and **Scope and cleanup**.
9. Rename the internal zero-argument callback type from `SagaStartFn` to
   `SagaRecoveryFn`, including its heap ID:

   ```c++
   using SagaRecoveryFn =
     ZmFn<void(), ZmFnHeapID<"Zdb.Saga.RecoveryFn">>;
   ```

   Use it for both catalog reconciliation and persisted-saga scanning. Rename
   `m_sagaStartFn` to the role-specific `m_sagaCatalogFn`; retain
   `m_sagaScanFn`, and use `m_sagaCatalogFn` as the existing configured sentinel.
   Update `DB::sagas_` parameters, initialization, startup, activation, and `final()`
   cleanup. Keep `SagaRunFn` because it remains the required type-erasure bridge
   from non-template `DB` to `MSaga<Sagas>`. Do not retain a
   `SagaStartFn` alias.
   `SagaRecoveryFn` is the explicitly required name and remains below the 28-byte
   limit; the shorter member names follow `GUIDELINES.md` **Naming / Length and
   abbreviations** and **Scope and cleanup**.

### 2. Minimal runtime state, callback threading, and one-time base initialization

In `zdb/src/ZdbSaga.hh`, `zdb/src/ZdbSaga.cc`, `zdb/src/Zdb.hh`, and
`zdb/src/Zdb.cc`:

1. Add only `bool m_fwd = true` to the private data tail of class `Saga_`; reset it
   to true in every `init_`. Do not add a callback member or extend `init_` with a
   callable. The field is not serialized. Its concise camel-case name and `m_`
   prefix follow `GUIDELINES.md` **Naming / Length and abbreviations**, **Casing and
   member prefixes**, and **Class and struct shape**. Audit member order for
   padding.
2. Delete `DB::m_sagaHandler` and remove the `SagaHandler` parameter from
   `DB::sagas_`. Add `ZmRef<Context> m_sagaContext` to
   `SagaDB<Context, Sagas>`, retain `using M = MSaga<Sagas>`, and move the context
   from `SagaDB::sagas(ZmRef<Context> context)`. Use the existing
   `m_sagaCatalogFn` sentinel to distinguish a configured null context from an
   unconfigured DB; do not add another flag. Both live submission and the installed
   recovery runner pass `m_sagaContext.ptr()` to `MSaga::run`; the installed runner
   captures only `this`, not another `ZmRef`. Do not add a raw or
   type-erased context member to non-template `DB` or `Saga_`. Neither `submit` nor
   `complete` may be captured by `m_sagaRunFn`, `m_sagaCatalogFn`, or
   `m_sagaScanFn`. Add the typed finalization wrapper:

   ```c++
   void final() {
     DB::final();
     m_sagaContext = nullptr;
   }
   ```

   Do not release the last context reference until base finalization has drained
   every saga continuation. Audit dependent finalization call sites so none erase a
   saga DB to `DB *` before calling `final()` and thereby bypass this non-virtual
   wrapper; do not introduce virtual dispatch solely for cleanup.
3. Provide an initial
   `MSaga::run<Context>(Context *, ZmRef<M>, Complete &&)` and
   a continuation overload `MSaga::run(ZmRef<M>, Complete &&)`. The initial
   overload uses `def.saga` as the initialization sentinel, assigns the
   already-typed context and saga pointer before first dispatch, and asserts if an
   initialized definition is passed with different pointers. The continuation
   overload asserts `def.saga` still identifies the same object and never assigns
   either base field. `context` may legitimately be null and is not a sentinel. Do
   not add an `initialized` flag, context lookup dispatch, or cast. This follows
   `GUIDELINES.md` **Audit flags / Framework fit**, **Type and API friction**, and
   **Assertions and type mechanics**.
4. Template both `MSaga::run` overloads on the application completion type. In
   forward mode, dispatch `operator()<I>(stepComplete)`—never spell the true
   template argument.
   In rollback mode, dispatch `operator()<I, false>(stepComplete)`. Move the carrier
   into the application call so that the application's mandated
   `[this, complete = ZuMv(complete)]` capture retains the original completion.
   Share runtime preconditions and terminal handling; do not duplicate whole
   runners. Live admission and every live follow-on transition call this templated
   runner directly; they must not pass through the erased `DB::sagaRun` recovery
   path.
   This follows `GUIDELINES.md` **Static polymorphism and constraints**,
   **Compile-time data and matching**, and **Audit flags / Algorithm and control
   flow** (DRY and flat control flow).
5. Make the step completion carrier generic over both the concrete saga bundle and
   the decayed application completion type. Retaining `M` is essential: otherwise
   the carrier has only `Saga *` and must re-enter the next step through erased
   `SagaRunFn`.

   ```c++
   template <typename, typename> struct SagaStepComplete;

   template <typename M, typename Complete>
   struct SagaStepComplete {
     ZmRef<M> saga;
     Complete complete;
     uint64_t epoch;
     uint32_t step;
     bool fwd;

     void operator ()(bool);
   };
   ```

   Update the forward declaration and `Saga_` friendship to
   `template <typename, typename> friend struct SagaStepComplete`; do not broaden
   friendship or make transition internals public.

   The actual field order may change after measuring padding. `fwd` is immutable
   invocation metadata which rejects stale, duplicate, or wrong-direction calls;
   it is not persistent state. The carrier owns the original application
   completion and moves it through every step, intent-deletion continuation, and
   terminal cleanup. Define its template-dependent operations in the header, but
   keep callback-independent DB mutations in existing non-template `.cc` helpers
   to contain code size. This follows `GUIDELINES.md` **Class and struct shape**,
   **Callbacks**, **Audit flags / Code structure**, **Storage and capacity**, and
   **Naming**.
6. Change `SagaDB<Context, Sagas>::saga` to the templated five-argument signature
   above, with no `SagaSubmitResult` construction. Decay `Submit` and `Complete` into
   owned objects exactly once before posting asynchronous work. Thread `submit`
   only through admission and thread `complete` separately into the first
   `SagaStepComplete<M, ZuDecay<Complete>>`. Never copy either callable into `DB`
   or `Saga_`. After durable admission, start the initial runner with the configured
   `m_sagaContext.ptr()`; subsequent steps use the pointer stashed in the active
   definition's `SagaBase`.

   Invoke `submit(false)` for every asynchronously processed rejection and
   `submit(true)` only after the saga row commits, immediately before starting
   execution. If the public method cannot enqueue and returns false, invoke neither
   callable. If submission is rejected, destroy `complete` without invoking it.
   Remove the rejection utility which constructs `ZeException` only to place it in
   `SagaSubmitResult`; operational failures continue through their existing
   diagnostic paths. Use `ZuFwd` at initial ownership and `ZuMv` thereafter,
   following `GUIDELINES.md` **Callbacks**, **Data movement and initialization**,
   **Heap allocation**, and **Algorithm and control flow**.
7. Retain `SagaRunFn` only for the existing non-template `DB` to `MSaga<Sagas>`
   recovery bridge. Add a small non-template `SagaNoop` callable, outside enclosing
   templates, whose `operator()(bool)` does nothing; recovered execution enters
   the initial `MSaga::run(m_sagaContext.ptr(), saga, SagaNoop{})` overload through
   the installed lambda because no live application callback survives a crash. Do not
   erase a live callback merely to reuse this bridge. This follows
   `GUIDELINES.md` **Audit flags / Code structure**, **Callbacks**, and **Scope and
   cleanup**.

### 3. Completion and intent deletion

Implement the state transitions without a new state machine object:

1. `SagaStepComplete<M, Complete>::operator()` owns the decayed application
   completion. On every non-terminal transition, move that same callable into the
   next step carrier or the asynchronous intent-deletion continuation; never copy,
   reconstruct, or park it on `Saga_`. Consequently the `complete` captured by an
   application step contains the original application completion in its capture
   pack and carries it all the way through forward execution, rollback, and final
   cleanup. Make all callback-carrying transition functions templates in the
   header and factor callback-independent storage work behind non-template
   operations to avoid multiplying the DB implementation.
2. Forward step `complete(true)` retains current behavior: validate
   saga/epoch/step and direction, clear any recovered in-memory reservation,
   increment `m_step`, retire the invocation, and move the application completion
   into the next forward step.
3. Forward step `complete(false)` validates and retires the invocation, switches
   `m_fwd` to false in memory, and keeps `m_step` as the failed-step index. Delete
   any original intent for the failed step while carrying the application
   completion through that deletion, then:

   - if `m_step == 0`, finish as rolled back;
   - otherwise invoke logical step `--m_step` with explicit `Fwd == false`.

   After deleting a failed-step intent during recovery, also remove its matching
   `SagaRec` and UN reservation from the in-memory recovery hashes before starting
   rollback or terminating at step zero.

4. Reverse step `complete(true)` deletes the original forward intent for the current
   logical step on `m_locs[m_step]`. In that asynchronous commit continuation,
   clear the matching recovered `SagaRec`/UN reservation if present, then move the
   application completion into either the `--m_step` reverse carrier or terminal
   failure cleanup when step zero has been consumed.
5. Delete or replace the live callback-dropping `DB::sagaStepComplete`,
   `DB::sagaDone`, `DB::sagaCleanup`, and `DB::sagaComplete` signatures. Their live
   callback-carrying portions become `M`/`Complete` templates in the header; compact
   callback-independent validation, counters, queue/hash removal, and storage
   primitives remain non-template where that materially contains instantiations.
   Keep `DB::sagaFail` non-template for internal invariant/storage failures, delete
   its `SagaHandler::errorFn` dispatch, and retain the existing DB failure path.

   The saved-UN replay shortcuts are the exception: current `Saga::complete_()`
   advances a recovered step whose target operation was already executed, so its
   discarded application step closure contains only `SagaNoop`, never a live
   application completion. Replace it with private `Saga::stepRecovered_(Shard)`
   and non-template `DB::sagaStepRecovered(...)`, which re-enters through
   `SagaRunFn` and creates the next `SagaNoop`. These concise event names distinguish
   the saved replay result from live application completion and apply the trailing
   underscore only to the internal shard-side helper, per `GUIDELINES.md` **Naming
   / Trailing underscores** and **Standard function name vocabulary**. Assert the
   recovery preconditions; do not make the generic live carrier optional,
   synthesize an application completion, or use this shortcut from a newly admitted
   live saga.

   Factor the existing single-step intent deletion from `sagaCleanup` into one
   concise internal continuation utility used by both successful final cleanup and
   rollback. Use a trailing `_` only if it is specifically the DB-thread/on-shard
   variant. Do not add wrapper functions which merely preserve old names. This
   follows `GUIDELINES.md` **Naming / Trailing underscores**, **Standard function
   name vocabulary**, and **Audit flags / Algorithm and control flow**.
6. Intent deletion failure is a storage failure handled through the existing DB
   failure/deactivation path. It is distinct from application rollback
   `complete(false)`.
7. Reverse step `complete(false)` is non-returning. The application must perform its
   best-effort cleanup before making the call. The framework then:

   - snapshots saga type, ID, and step by value;
   - emits `ZiLOGBT(Fatal, ...)` without capturing saga/DB pointers or references;
   - calls `ZiLog::stop()` to synchronously drain the asynchronous logger;
   - calls `::exit(EXIT_FAILURE)`.

   It must not delete intents, retry, deactivate, invoke the application
   completion, or dispatch another step. Add `<stdlib.h>` as the direct C header
   dependency in the
   implementation unit. This follows `GUIDELINES.md` **Core language** (prefer C
   headers), **I/O and system integration** (use `ZiLog`), **Tracing and debug
   logging**, **Audit flags / Data movement and initialization** (log captures by
   value), and **Scope and cleanup**.
8. After all reversals and original-intent deletions complete, delete the main saga
   row through the existing terminal cleanup path and move-invoke the carried
   application `complete(false)`. Keep the DB active. If
   no reversal is needed because forward step zero failed, take this same failure
   path after deleting any failed-step intent. Conversely, only final forward
   `complete(true)` enters the success path and ultimately move-invokes
   the carried application `complete(true)`.
   Preserve the existing callback crash contract: delivery is at most once in a
   live activation; do not add a durable callback outbox.
   Move the callback out of the terminal carrier to a local before invocation so
   re-entrant application code cannot observe or trigger it twice. Complete
   internal live-count/recovery-queue removal before invoking application code,
   and keep the saga reference alive until the call returns. A recovered saga
   carries `SagaNoop` and completes cleanup silently.
   Internal storage/activation failure remains DB failure and does not translate to
   application `complete(false)`, which is reserved for an application step failure followed
   by completed rollback.
9. Preserve pending counts and epoch checks at every post. Follow-on work occurs
   only in the posted operation's continuation; never assume `run`/`invoke` is
   blocking. Short-lived continuations may retain the saga with the existing
   `ZmRef<Saga>`, but capture longer-lived DB/table owners as raw pointers and rely
   on the existing drain protocol. This follows `GUIDELINES.md` **Callbacks**,
   **Audit flags / Algorithm and control flow**, **Scope and cleanup**, and **I/O
   sharding / Lifetime and Teardown**.

### 4. Bypass saga journaling during rollback

Make the existing saga mutation entry points direction-aware without adding public
APIs:

1. While `m_fwd` is true, preserve `sagaPrepare`, forward intent insertion,
   recovery reservation, and saved-UN replay exactly as today.
2. While `m_fwd` is false, `insert`, `update`, `findUpd`, `del`, and `findDel`
   delegate directly to the corresponding ordinary `Table` operation on the
   already-owned shard. They must not call `sagaPrepare`, create `SagaStep`, inspect
   `SagaRec`, reserve a UN, or call the forward replay helpers.
3. Factor only the common table-operation expressions needed to avoid near-identical
   blocks. Prefer a compile-time `if constexpr` in direction-templated internal
   code where direction is available; retain a single cheap `m_fwd` branch at a
   non-templated public boundary where the `ZdbSaga *` contract makes runtime state
   irreducible. Do not create a rollback façade or virtual interface.
4. Keep shard assertions (`ZiAssert`), row ownership, placement construction,
   `ZuMv`/`ZuFwd`, and callback types consistent with the existing hot path. Add no
   locks, atomics, allocations, STL containers, or payload copies.

This follows `GUIDELINES.md` **Static polymorphism and constraints**, **Framework
fit**, **Callbacks**, **Heap allocation**, **Data movement and initialization**,
**Audit flags / Algorithm and control flow**, and **Unnecessary atomic operations,
locking and copies**.

## Dependent migration

Update every repository-owned `ZdbSagaStep` definition found by `rg`:

- `zdb/example/zdbsaga.cc`
- `zdb/test/ZdbSagaTest.cc`
- `zdb/itest/zdbsagareptest.cc`
- `zdb_pq/itest/zdbsagatest.cc`

For each saga:

1. Define one context type beside the saga bundle, never inside or independently
   aliased by each saga definition. Derive every definition from
   `ZdbSagaBase<Context>` and use `using Base::context; using Base::saga;`.
   Instantiate the DB as `ZdbSagaDB<Context, Sagas>` while saga objects remain
   `ZdbMSaga<Sagas>` (prefer the DB's existing `M` alias locally where it is
   clearer). Keep names camel-case and under 28 bytes, per `GUIDELINES.md`
   **Naming** and **Class and struct shape**.
   Update positional construction for the new aggregate base as described above;
   do not hide it behind per-saga forwarding constructors.
2. Remove `context_` casts and old operator parameters. Capture only
   `[this, complete = ZuMv(complete)]` when sufficient. Do not use `[&]` or `[=]`;
   do not capture `context`, `saga`, shard, or IDs which are cheaply available or
   re-derived through `this`. This follows `GUIDELINES.md` **Core language**,
   **Callbacks**, **Audit flags / Data movement and initialization**, and **I/O
   sharding / Cross-Shard Handoffs**.
3. Put forward work under `if constexpr (Fwd)` and compensation under `else`.
   Store any data needed to recreate a deleted row or undo an update in the saga's
   existing serialized application payload; do not add framework rollback fields.
   Avoid new allocations/copies; use existing `ZdbRowRef`, placement construction,
   and move/forward paths. This follows `GUIDELINES.md` **Persistence**, **Heap
   allocation**, **Data movement and initialization**, and **Framework fit**.
4. Pass `bool(row->commit())` to `complete`. A missing required forward/update row
   is false. A reverse delete uses
   `complete(row ? bool(row->commit()) : true)`; absence is silent success. Other
   reverse operations fail only when their required row is unavailable or commit
   fails. Perform any best-effort application cleanup before the exceptional
   reverse `complete(false)` call; nothing returns from that call.
5. Complete `BalanceTransfer` in `zdb/example/zdbsaga.cc`:

   | Step | default `Fwd` | explicit `!Fwd` |
   | --- | --- | --- |
   | 0 | insert pending transfer | delete transfer if present |
   | 1 | debit source | credit source |
   | 2 | credit destination | debit destination |
   | 3 | mark transfer complete | restore pending status |

   Re-derive `transferShard`/`accountShard` inside the shard callback. Do not add a
   second example or compatibility variant.
6. Do not edit `zdbsaga.reversible.cc` or `zdbsaga.proposed.cc`; they are untracked
   design inputs, not dependents.
7. Replace `db->sagas(ZdbSagaHandler{...})` with the typed
   `db->sagas(ZuMv(context))` form, where `context` is a
   `ZmRef<Context>` (`db->sagas({})` when no context is needed). Make the common
   application context ref-countable using the nearest established
   `ZmPolymorph`/heap pattern, allocate it once during DB setup, and keep ordinary
   raw-pointer access in saga steps. Do not add a reference capture to any saga or
   step callback.
   Replace every `db->saga(...)` call with concrete `submit` and `complete`
   callables. Pass an explicit `[](bool) { }` when a result is intentionally
   ignored; bare `{}` cannot deduce a forwarding-reference template argument.
   Convert submission checks from `SagaSubmitResult` union inspection to the
   submission boolean, and move completion semaphores/status into the per-call
   completion lambda rather than the shared context. Prefer small, move-only
   captures and let each concrete closure remain inline; use test-local type erasure
   only where a fixture genuinely must store heterogeneous callback types.

The intended new names pass `GUIDELINES.md` **Naming**: public `ZdbSagaBase`;
internal `SagaBase`, `SagaRecoveryFn`, `SagaRunFn`, `m_sagaCatalogFn`, and
`m_sagaScanFn`; typed `SagaDB` member `m_sagaContext`; macro parameters `Step`,
`Fwd`, and `Complete`; public base fields `context` and `saga`; private member
`m_fwd`; recovery helpers `stepRecovered_` and `sagaStepRecovered`; internal
`SagaNoop`; completion fields `complete` and `fwd`. Do not
introduce names such as `rollbackDirection`, `rollbackStepCursor`, or
`sagaInitializationState`.

## Tests

Use existing TAP programs, `ZuTestUtil`, `ZmBlock`, and `ZmSemaphore`. Do not add
sleep, polling, arbitrary capacities, or a new harness (`GUIDELINES.md` **Tests**
and **Audit flags / Algorithm and control flow**).

### Unit tests: `zdb/test/ZdbSagaTest.cc`

1. API/compile time: metadata discovery uses default `Fwd`; forward dispatch calls
   `operator()<Step>()`; reverse dispatch calls `operator()<Step, false>()`; missing
   `ZdbSagaBase` is rejected; `context`/`saga` are set before step zero and never
   replaced on the same loaded object.
   Verify `SagaDB<ContextA, Sagas>` rejects any definition derived from
   `ZdbSagaBase<ContextB>` and accepts every definition derived from
   `ZdbSagaBase<ContextA>` without requiring a per-definition `Context` alias.
   Compile the new two-parameter `ZdbSagaDB` API and unchanged one-parameter
   `ZdbMSaga` API. Configure two distinct DB fixtures with different instances of
   the common type and prove every live and recovered definition receives its own
   DB's configured pointer without an application cast. Verify FlatBuffers round-trip
   construction initializes the base to null without shifting serialized ctor
   fields.
   Instrument context reference counts: configuration acquires/transfers the one DB
   ownership reference, no submission, step, rollback, or recovery continuation
   changes it, and finalization releases it only after all saga work drains.
2. Direction: fail at step zero, an intermediate step, and the final step. Verify
   reverse order, exclusion of the failed step, no terminal callback while rollback
   is pending, then one application `complete(false)` after rollback completes.
   Verify final forward success produces one application `complete(true)`, and that
   the DB continues admitting sagas after a completed rollback/failure outcome.
3. No rollback journal: instrument `saga_step` writes and prove reversal creates no
   intent and reserves no replay UN. Verify each successful reverse step deletes
   its original forward intent before the preceding reverse step begins.
4. Idempotence: cover insert/delete, update/update, and delete/insert compensation;
   prove an absent reverse delete succeeds without a false completion or error-log
   noise.
5. Crash semantics: cut before/after a reverse target commit and before/after its
   original-intent deletion. On restart, prove the saga begins at forward step zero,
   never resumes rollback, and may either complete forward or fail and begin a new
   rollback according to the fixture.
6. Failed-step intent: fail after an intent but without a target effect; prove the
   failed intent is deleted before rollback and cannot influence a later forward
   restart.
7. Fatal reverse failure: use a subprocess/death-test path. Verify a fatal
   diagnostic/backtrace names the saga type/ID/step, logging drains, the process
   exits with `EXIT_FAILURE`, and neither terminal callback nor another step runs.
8. Completion safety: delayed, duplicate, wrong-direction, and stale-epoch
   completions cannot move state. Preserve existing admission, storage-failure,
   pagination, orphan cleanup, graceful-stop, and forward recovery tests.
9. Callback API: cover immediate enqueue failure (neither callable), asynchronous
   rejection (`submit(false)` only), successful admission (`submit(true)` before
   execution), forward success (application `complete(true)`), orderly rolled-back
   failure (application `complete(false)`), re-entrant terminal callbacks, and
   callback loss on deactivation/recovery. Prove recovered cleanup does not invoke
   a callback from a previous in-memory saga. Use an instrumented move-only
   completion with non-trivial captured state to prove its identity survives every
   forward step, intent deletion, rollback step, and terminal cleanup without a
   copy. Add compile-time/type instrumentation proving that no live callback is
   converted to `ZmFn`, and record the carrier and posted-closure sizes for both a
   small and deliberately large completion. Do not misattribute an allocation made
   by the existing scheduler for an oversized posted closure to a deleted callback
   wrapper; large caller captures remain the caller's responsibility.

Keep large recovery fixtures paged/bounded, release iterator scope before posts or
cleanup, and count records during traversal rather than snapshotting container
counts. This applies `GUIDELINES.md` **Audit flags / Code structure**, **Algorithm
and control flow**, and **Liveness**.

### Integration tests

1. In `zdb/itest/zdbsagareptest.cc`, lose leadership during in-memory rollback and
   verify the new leader starts ordinary forward replay rather than reverse replay.
   Cover both a successful re-attempt and a re-attempt which fails and rolls back.
2. In `zdb_pq/itest/zdbsagatest.cc`, exercise reconnect and child crash during
   reversal. Verify restart is forward, no reverse intent was stored, successfully
   reversed forward intents were deleted, and terminal cleanup leaves no journal
   residue.
3. Use semaphores and exact commit hooks, never timing intervals. Run source-tree
   binaries through `libtool exec` under sanitizers/Valgrind where applicable, per
   `GUIDELINES.md` **Tests**, **Acceptance**, and **Debugging**.

## Complete audit-flag checklist

Review the implementation diff against every `GUIDELINES.md` **Audit flags**
subsection:

| Audit subsection | Required disposition |
| --- | --- |
| Code structure | Keep only callback-dependent carriers/transitions in headers; keep non-dependent declarations and callback-independent DB work outside templates; cache invariant loop bounds/accesses and factor only genuinely repeated code. Audit generated size so templating callers does not duplicate storage algorithms. |
| Storage and capacity | Add no persistent fields, fixed arrays/tables/capacities, reverse containers, or scanning GC; order `Saga_`'s sole new runtime bit, `SagaDB`'s one context owner, and each instantiated completion carrier to avoid padding. |
| Heap allocation | Add no framework state-object, live-callback `ZmFn`, or hot-path scratch allocation. The one long-lived application context allocation is explicit and occurs during DB setup; store concrete callables inline in existing continuation captures and move them; retain existing scheduler and intrusive-owner allocations. Audit large closures rather than silently adding a type-erased heap fallback. |
| Data movement and initialization | Decay callbacks once, use perfect forwarding at acquisition and moves thereafter; add no payload/string copies or overwrite churn; use placement construction; fatal log captures stable values, never pointers/references. |
| Algorithm and control flow | One runtime direction bit, `ZuSwitch` step dispatch, compile-time application branch and callback type, flat continuations, no duplicated pipeline, polling, sleeps, hand hash, or assumed-blocking post. |
| Framework fit | Reuse the forward journal, ordinary table mutations, `ZfbStruct`, `Zu*` types, existing scheduler, and cleanup/drain paths; retain `ZmFn` only at the recovery callbacks that cross the non-template DB boundary; add no STL or parallel facility. |
| Type and API friction | Take the common `Context` explicitly only in `SagaDB<Context, Sagas>` and validate each definition's base against it; do not rediscover it through per-definition aliases or type maps or propagate it into `MSaga`. Retain one typed `ZmRef` owner per `SagaDB` and pass only its raw pointer to live and recovery runners without casts. Add no enum/integer mapping or CRTP cast. |
| Scope and cleanup | Delete the old contract, `SagaHandler`, submission union, live-callback aliases, and stored completion; add no shim, schema change, rollback persistence, inverse-op trait, alternate example, callback outbox, or dead branch. |
| Unnecessary atomic operations, locking and copies | `m_fwd` is a DB-thread-owned primitive; the context incurs ownership churn only at configuration/finalization and is passed to definitions as a raw pointer; add no lock/atomic, copy no callback, and capture only destination metadata plus the moved completion carrier. |
| Liveness | Every store/shard action completes by continuation, recovery remains paged, intent cleanup advances asynchronously, and no mixed-workload thread runs an unbounded loop. |

Apply `GUIDELINES.md` **Header layout**, **Indentation**, **Multiline layout**,
**Compact layout and alignment**, and **Naming** to every changed declaration. New
internal/thread-owned functions use concise camel-case names and trailing `_` only
for internal/on-shard variants; no new name may exceed 28 bytes.

## Verification and completion

1. Run `git diff --check`. No generated FlatBuffers artifact should change.
2. Use `rg` to prove that, outside the two scratch inputs, no old three-argument
   saga operator, application `context_` cast, non-derived saga definition, non-specific lambda
   capture, `SagaHandler`, `SagaStartFn`, `SagaSubmitResult`, `SagaSubmitFn`,
   `SagaCompleteFn`, `m_completeFn`, `Saga::complete_`, `Saga::done`, public
   `Saga::fail`, old
   callback-dropping terminal signature, compatibility shim, rollback journal
   field, reverse `SagaStep` write, one-parameter `SagaDB` instantiation,
   two-parameter `MSaga` instantiation, or application definition of a `Context`
   alias remains.
3. Run `make -j8`, then `make test`, `make -C zdb/itest test`, and
   `make -C zdb_pq/itest test`. Report a PostgreSQL skip separately if its external
   environment is unavailable.
4. Run the canonical `zdb/example/zdbsaga` success path. Exercise rollback in the
   focused tests rather than bloating the example.
5. Run applicable clang ASan/LSan and Valgrind suites via `libtool exec`, per
   `GUIDELINES.md` **Acceptance** and **Debugging**.
6. Completion requires forward success, live reverse execution, original-intent
   deletion after successful reversal, forward restart after a crash during
   rollback, silent/idempotent compensation, fatal exit on reverse false, correct
   epoch/drain behavior, one explicit context type per saga bundle, exactly one
   context ownership reference held by each configured `SagaDB` with raw pointers
   in `SagaBase`, exactly one normal application outcome after terminal
   cleanup (application `complete(true)` for final forward success or application
   `complete(false)` after completed rollback), a healthy DB after an orderly
   failure outcome, and no journal residue after terminal cleanup.
