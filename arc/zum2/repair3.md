# `zum/src` repair plan

This plan addresses the findings in `findings3.md` in dependency order. It
does not include generated FlatBuffers/build artifacts or the guideline areas
that already passed.

## Change and build policy

- This is an intentional breaking change. Propagate renamed enum/group types,
  signature changes, and limit-policy changes to every dependent in the
  repository, including Zum tests, integration tests, examples, and programs.
  Do not add legacy aliases, forwarding wrappers, compatibility overloads, or
  dual behavior.
- Retain the current configured build. Rebuild and retest only at major
  milestones, not after each individual edit.
- Rebuild incrementally and reuse the current build artifacts. Do not clean
  unrelated modules or trigger a broad configuration change.
- Other compilers, release/debug flag sets, and platforms are out of scope.

## Phase 1: remove unambiguous guideline violations

1. **Close the OIDC iterator scope.**

   In `zum/src/zumd_oidc.cc:717`, put the `m_pending` cancellation traversal
   in a nested block. Ensure the iterator is destroyed before clearing
   `m_http` and `m_clock`. Preserve the current cancellation order and
   callback behavior.

2. **Cache repeated values in hot predicates and loop conditions.**

   - Cache `claims.amr[0]` once in `ZumJWTVerify.cc:161` and
     `zumd_jwt.cc:267,338`.
   - Cache `name[begin]` in `zumd_core.cc:20`.
   - Rewrite the slash-trimming loops in `zumd_discovery.cc:73,277` so the
     current string length is obtained once per iteration and reused for the
     test, index, and truncation.

   Keep these changes representation-preserving and add no temporary string
   or span allocations.

3. **Convert named enums to the project form.**

   Update the named enums in `zumd_oauth.hh`, `zumd_daemon_admin.cc`,
   `ZumService.cc`, and `zumd_sagas.hh` to either namespace-scoped anonymous
   enums or the required `struct E { enum { ... }; };` grouping. For parser
   field groups, use an unsigned/integer field parameter rather than retaining
   a named enum type solely for `has()`. For state/kind fields, retain the
   existing compact integer storage and update qualified constant references.

   Do not change the numeric values: they are used as bit positions, switch
   labels, and persisted saga values. Propagate the resulting source/API
   changes to all repository dependents; do not retain the old names as
   compatibility shims.

4. **Use the Z compile-time assertion idiom.**

   Replace `static_assert` at `zum/src/ZumMgmt.cc:117` with
   `ZuAssert(routes.length() == MgmtOp::N)`.

## Phase 2: make capacity policy explicit and coherent

5. **Separate transport ceilings from configurable service limits.**

   Inventory every HTTP parser `QueryLimit`/`BodyLimit` in
   `zumd_http.hh` and `zumd_daemon.hh`. Decide for each whether it is:

   - a fixed protocol or transport ceiling, in which case give it a prominent
     named constant and a maintenance comment; or
   - a service policy, in which case route it through `ServerLimits` and make
     the parser ceiling an explicitly documented maximum.

   Add validation that rejects a `ServerLimits` value that cannot be honored
   by the parser, or expose separate `*Max` names so a caller can distinguish
   the two layers. Avoid silently accepting a configured value that is
   unreachable.

6. **Name the database and pagination bounds.**

   Replace the literal pairs at `zumd_db.cc:256,317` with constants such as a
   logical maximum and a scan limit defined together, with comments explaining
   the extra-row overflow check. Apply the same treatment to
   `AdminQuery_::ScanSize` at `zumd_daemon_admin.cc:2411` and the JSON state
   limit at line 2593. Reuse the relevant service limit where the bound is
   intended to follow configuration; otherwise document it as a fixed
   security/workload ceiling.

   Review `RequestTimeout`, `ResponseMax`, and `Concurrency` in
   `zumd_oidc.cc:1150-1152` at the same time. Keep standards/security limits
   fixed only when the source or scaling rationale is recorded.

7. **Preserve overflow and pagination semantics with tests.**

   Add or extend tests for exactly-at-limit, limit-plus-one, and configured
   lower-limit cases for HTTP input, role mappings, client access rows, admin
   queries, and state JSON. Verify that the extra-row scan still reports
   overflow rather than truncating an unsafe result.

## Phase 3: remove or justify the fixed route table

8. **Replace the enum-indexed route array if the API permits it.**

   Refactor `ZumMgmt.cc:21-50` and `managementRoute()` so route lookup is a
   `ZuSwitch`/compile-time mapping returning the route value, rather than a
   `ZuArray` whose storage and indexing are tied to `MgmtOp::N`. Preserve the
   retired-operation behavior and invalid-operation checks.

   If pointer-stable route records are required by the parser, retain the
   immutable table as a documented exception instead: keep the existing size
   assertion, add a comment explaining why a switch/value mapping is not
   suitable, and add a test that every live operation has exactly one route
   with the expected method and path.

## Phase 4: repair header dependency boundaries

9. **Make `SagaImage` self-contained.**

   In `zumd_saga_image.hh`, include the defining headers for `Zfb::IOBuilder`,
   `ZfbStruct`, and `ZiIOBufAlloc` directly. Remove `zumd.hh` if the template
   does not require its domain declarations. Compile a translation unit that
   includes `zumd_saga_image.hh` alone to verify that no transitive include is
   being relied upon.

## Validation and handoff

10. Run formatting and source checks:

    - `git diff --check`;
    - search for remaining `enum T`, repeated invariant accesses, and the
      audited raw literals;
    - search all repository dependents for old enum/group names and old limit
      signatures;
    - compile only with the currently configured compiler and flags, with
      warnings enabled as configured by the repository.

11. Rebuild at milestones using the current configuration and existing
    artifacts:

    - **Milestone 1:** after Phase 1 and dependent propagation, run the
      incremental `zum/src` build to catch enum/signature errors;
    - **Milestone 2:** after Phase 2 and its boundary tests, incrementally
      rebuild the affected Zum targets and run the focused limit tests;
    - **Milestone 3:** after route-table and include repairs, run the final
      incremental module build and the selected tests.

    Serialize any required configure/autoreconf refresh before parallel make.
    The prior forced module build raced recursive `am--refresh`/configure
    jobs. Do not use a parallel forced module build while the top-level
    configure is being regenerated, and do not clean the existing artifacts.

12. At Milestone 3, run the module's default build and tests, followed by the
    relevant Zum integration tests if available. Exercise shutdown with
    pending OIDC requests. Sanitizer/lifecycle tests are only in scope if they
    are available under the current configured build; do not create another
    build configuration for them.

13. Review the final diff for complete dependent propagation of the breaking
    enum and limit changes, confirm generated files were not hand-edited, and
    record the exact current compiler, flags, build, and test results in the
    change summary.

## Completion criteria

The repair is complete when all seven findings are either fixed or explicitly
documented as justified exceptions; no named `enum T` or out-of-scope iterator
remains; configurable limits cannot exceed an undisclosed parser ceiling; the
route mapping has a recorded storage rationale; standalone header compilation
passes; all repository dependents use the new APIs with no legacy shims; and
the incremental current-configuration rebuild/test results are clean.

## Execution record

- Compiler: clang++ 22.1.8, target `x86_64-pc-linux-gnu`.
- Configuration retained: GNU C++2b, `-march=x86-64-v2`, `-g -DZDEBUG`, and the
  repository's existing warning and floating-point flags. No alternate compiler,
  optimization/debug mode, or platform configuration was used.
- Incremental builds: `make -C zum/src -j3` and `make -C zum/test -j3` passed,
  reusing current artifacts.
- Module tests: `make -C zum/test test` passed; 4 binaries and 30 tests.
- Integration tests: `make -C zum/itest test` passed for discovery, restart,
  HTTP, OIDC HTTP, federation, and cluster coverage.
- Standalone header check: a configured `clang++ -fsyntax-only` translation unit
  including only `zlib/zumd_saga_image.hh` passed.
- `git diff --check` passed. Repository-wide searches found no remaining named
  `enum T` declarations, removed enum type references, or legacy compatibility
  wrappers for the changed APIs. Generated files were not hand-edited.
