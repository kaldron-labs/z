# Repair plan for `zum_findings.md`

## IMPORTANT

retain current build configuration:
- rebuild **infrequently**, ideally only at end
  - batch source code changes into large batches
  - do not rebuild or retest until major milestones are reached
- rebuild incrementally, re-using current build artifacts
- other build configurations are out of scope
  - other compilers
  - other release/debug build flags
  - other platforms
- defer dependent rebuilding to the end of the implementation
- use valgrind sparingly:
  - to debug memory corruption as needed
  - as a final memory leak and corruption gate

run the full `zum` unit and integration test suite, ensure NO regressions
- fix any Z framework dependency compilation errors or warnings on the fly

## Plan

This plan repairs the findings in dependency order.  It is an implementation
plan; no repair is considered complete until its focused tests pass and the
full Zum test/build gates are green.

## Invariants to preserve

- All timer state is owned and mutated on the scheduler thread that owns it.
- No database callback may dereference, retain, or post through a destroyed
  daemon/context.
- Cleanup is bounded, restartable, idempotent, and makes progress past owned or
  concurrently changed rows.
- Saga rollback treats missing rows according to an explicit idempotency rule;
  it never crashes and never deletes a row belonging to another operation.
- Secret material is cleared on every failure and teardown path.
- Saga payload size, allocation count, and per-request CPU remain bounded by
  named workload limits.

## Phase 1: establish lifecycle and ownership primitives

1. Map the actual scheduler affinity of `Daemon`, Zdb tables, request queues,
   and cleanup callbacks.  Add debug assertions at timer entry, cleanup
   completion, and daemon stop/final boundaries.
2. Replace the current boolean cleanup state with an explicit maintenance
   operation state, using raw back-pointers only where the owner drains all
   continuations.  Capture no daemon-owned mutable state in a database
   callback.
3. Implement three-phase cleanup teardown:
   - cancel `m_cleanupTimer` and reject new cleanup work;
   - post a scheduler-owner continuation that waits for any active database
     chain to finish;
   - only then permit `Daemon::final()` and release the context/scheduler
     references.
4. Marshal every database completion back to the scheduler owner before
   changing `m_cleanupActive`, counters, timer state, or calling
   `m_mx->add()`/`del()`.
5. Make `start()`, `stop()`, and `final()` idempotent across timer callbacks,
   database failure, and concurrent shutdown.  Ensure a failed cleanup
   reschedules with bounded backoff rather than immediately retrying.

## Phase 2: repair cleanup selection and saga idempotency

1. Refactor evidence/consent selection to request bounded pages, skip owned
   rows, and retain a continuation key.  Do not stop at the first noncandidate
   or conflict.  Revalidate app identity, ownership, version, and update
   timestamp in the deletion saga.
2. Define row outcomes for every cleanup race:
   - missing row: already deleted, success;
   - unchanged unowned matching image: delete;
   - owned/version/deadline/state mismatch: skip or report conflict without
     starving the remainder of the batch.
3. Audit all `findDel` and `findUpd` callbacks in `zumd_app_db.hh`,
   `zumd_catalog.hh`, `zumd_sagas.hh`, `zumd_revoke.hh`, and related saga
   headers.  Add null checks before any `data()` or `commit()` access.
4. Check both forward and reverse paths for ownership, version, state, and
   key identity before mutation.  Make rollback of an already-missing insert
   or delete a successful no-op where that is the intended saga contract.
5. Centralize safe row-finalization helpers where they remove duplicated
   callback logic without hiding table-specific validation.
6. Preserve saga replay compatibility by incrementing each changed saga type’s
   type/version identifier where payload semantics change, and verify old
   in-flight sagas fail safely rather than being misinterpreted.

## Phase 3: reduce maintenance payload and allocation pressure

1. Replace full `SagaImage` row snapshots for maintenance with typed compact
   deletion images.  Include only the key and fields needed for the race check:
   ownership, version, state, update timestamp, and expiry/deadlines as
   applicable.
2. Keep batch vectors bounded by a dedicated named maintenance limit.  Avoid
   serializing a row into a temporary FlatBuffer and copying it into a second
   allocation when a single builder/buffer or direct typed payload can be
   used.  Assign explicit `ZmHeapID`/`ZmVHeap` identities to variable storage.
3. Use explicit `ZmRef` ownership for `ExpiredCleanup_` and `AppCleanup_`
   operations.  Test synchronous validation failure, database failure, normal
   completion, and shutdown while active.
4. Measure peak bytes, allocations, and scheduler turn duration before and
   after the change.  Reject a design that trades the current UAF for an
   unbounded retained operation or saga payload.

## Phase 4: bound catalog publication

1. Add named limits for action, role, client, role-action, redirect-URI, and
   total catalog sizes.  Validate structural counts before duplicate scans and
   document whether each limit is protocol-mandated or a workload safeguard.
2. Build reusable lookup structures for action and role names during validation;
   eliminate repeated `actionIndex_()`/`roleIndex_()` linear scans.  Use the
   project’s intrusive/hash facilities and assign appropriate heap IDs.
3. Keep catalog validation and digest generation from retaining unnecessary
   duplicate representations.  Bound serialized JSON size and release/move
   temporary buffers promptly.
4. Preserve replay semantics: an identical revision/digest remains idempotent,
   while stale or conflicting revisions fail without partial visible catalog
   state.

## Phase 5: secret hygiene

1. Add one OIDC request cleanup routine that clears `clientSecret` and any
   other sensitive fields before every failure return in `begin_()`, including
   random-generation and duplicate-state failures.
2. Audit all OIDC discovery, callback, timeout, stop, final, and allocation
   failure paths for the same guarantee.  Do not depend on ordinary string
   destruction to erase secret bytes.
3. Add tests that force each early-return branch and inspect the resulting
   request/config lifecycle under ASan/LSan; ensure no secret is logged or
   serialized into a saga image.

## Phase 6: operational defaults and diagnostics

1. Separate interactive query, cleanup scan, saga batch, and payload limits;
   name each constant/configuration property by workload.  Retain fixed
   scheduler topology only where deployment requires it and document the
   rationale.
2. Extend maintenance results with selected, deleted, skipped, conflicted,
   failed, and rescheduled counts.  Emit structured logs without IDs or
   payloads that disclose secrets/token material.
3. Add bounded retry/backoff state and expose enough information to diagnose a
   repeatedly blocked table or a cleanup population that is not shrinking.

## Required tests

### Lifecycle and concurrency

- stop during each cleanup table operation;
- finalization immediately after timer cancellation;
- cleanup completion arriving after stop request;
- repeated start/stop and database failure;
- assertions proving timer state is only touched on its owning scheduler.

### Cleanup and sagas

- owned row before unowned rows, proving later rows are deleted;
- concurrent renewal/revocation/update versus cleanup;
- missing-row replay for every repaired `findDel`/`findUpd` path;
- duplicate/replayed cleanup request;
- restart during each repeat-step saga;
- bounded batches over more than one page with no starvation.

### Performance and storage

- peak maintenance payload and allocation benchmark;
- catalog count-limit rejection;
- worst-case catalog validation CPU;
- catalog replay/conflict behavior;
- cleanup turn duration under a large expired population.

### Secret handling

- OIDC random failure, duplicate state, discovery failure, callback failure,
  timeout, stop, and final paths;
- logging and persisted-saga inspection for plaintext token/secret absence.

## Verification gates

1. Rebuild changed source and dependent tests with `make -j8`.
2. Run the focused Zum unit/integration tests, including newly added cleanup,
   saga-recovery, OIDC, and catalog tests.
3. Run `make test` from a fully built tree.
4. Run clang AddressSanitizer/LeakSanitizer and valgrind memcheck with leak
   checking; execute source-tree binaries through `libtool exec`.
5. Review the final diff against `zum_findings.md` item by item.  Mark a
   finding resolved only with a code reference and test evidence; otherwise
   leave it open with the remaining risk.
