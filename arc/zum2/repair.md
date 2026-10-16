# Repair plan for `findings2.md`

## Objective

Repair every finding in the pending `zrest/src` routing change without adding
compatibility shims.  In particular, remove `MReqRouter` and improve
`MReqParser` so one parser implementation supports both ordinary request
catalogs and hierarchical root catalogs.  Preserve static dispatch, embedded
hash storage, allocation-free path routing, and the projected-target contract.

## Change and compatibility policy

This repair is an intentional breaking change.

- Propagate the finalized `MReqParser` and catalog API through every in-tree
  dependent, including libraries, tests, examples, utilities, and integration
  programs.
- Do not retain `MReqRouter`, legacy aliases, deprecated overloads, forwarding
  wrappers, dual template signatures, or conditional compatibility paths.
- Update dependent code to the new API in the same change.  An old call site
  is a migration defect, not a reason to preserve the old interface.
- Confirm the migration with repository-wide searches for removed symbols and
  old invocation forms.

## Build scope and cadence

Retain the repository's current build configuration and reuse its existing
artifacts throughout this repair.

- Do not rerun `configure`, `z.config`, `autoreconf`, clean the tree, or switch
  build modes.
- Rebuild and retest infrequently.  Do not run builds or tests after individual
  edits; wait until a major milestone described below is complete.
- At each milestone, rebuild incrementally with the existing makefiles and
  object files.  Rebuild the changed library source directory before its
  dependent test/example directories, and let `make` reuse everything that is
  still current.
- Other build configurations are out of scope: do not verify other compilers,
  release/debug flag combinations, sanitizer modes, or other platforms as
  part of this repair.
- Do not perform a clean or from-scratch build merely to broaden validation.

## Design constraints

- Keep route selection out of line through the existing catalog
  `...Derive`/`...Impl` pattern so large request and root type lists do not
  spread through dependents.
- Keep the selected request or child parser in one `ZuUnion`; all headers,
  body callbacks, completion, and reset must dispatch to that retained
  alternative without repeating route lookup.
- Do not add virtual dispatch, heap allocation, locks, or linear route probes.
- A concrete request must receive exactly the path view used for matching and
  exact/query validation.  A nested child must receive exactly the suffix
  produced by root selection.
- A public `Zhttp::Target` passed by the caller must have its original `path`
  restored before `operation()` returns, including rejection paths.

## 1. Validate request catalogs before constructing matchers

Add request-catalog validation traits in `zrest/src/Zrest.hh` and assert them
at the catalog lookup/matcher boundary.

- For each HTTP method, derive the exact and prefix request subsets
  independently.
- Map each subset to its declared `Path` types and require uniqueness within
  that subset.  The same literal remains valid for different methods.  An
  exact and a prefix route with the same literal remain separate categories;
  exact lookup deliberately has precedence over prefix matching.
- Use the existing `ExactReq` trait for the exact subset and `PrefixReq` for
  the prefix subset.  Expose a `ReqCatalogValid`-style trait so invalid cases
  can be tested without intentionally breaking compilation, then place a
  `ZuAssert` where every real catalog is instantiated.
- Size/build the exact hash from the exact subset, not the complete method
  subset.  Avoid instantiating a zero-entry hash while retaining `-1` as the
  no-exact-route result.
- Ensure the prefix `ZuMatcher` can only be instantiated after prefix-path
  uniqueness has been established.

Add compile-time tests for duplicate exact routes, duplicate prefix routes,
the same path under different methods, and the permitted exact/prefix overlap.
Add a run-time test proving the intended exact-before-prefix precedence.

## 2. Make invalid dispatch indices fail closed

Change the outer `ZuSwitch::dispatch` calls in `reqMatch()` and `resMatch()` to
use the overload with an explicit `-1` default.

- An invalid HTTP method passed to `reqMatch()` must return `-1`.
- A request index greater than or equal to `Reqs::N` passed to `resMatch()`
  must return `-1`, including the unsigned value produced when an empty
  request-builder union type is decremented.
- Keep the inner response and prefix translations explicit about failure as
  well; no route-selection path may obtain a plausible index from value
  initialization.

Add direct boundary tests and an `MResParser::status()` regression test using
an uninitialized/empty request builder.  Verify that it rejects the status and
does not construct response alternative zero.

## 3. Leave root-literal validity to applications

Do not impose compile-time validation of root literals in the routing library.
Root catalog paths are application-owned configuration; malformed, empty, or
otherwise unconventional literals must not cause a library assertion or a
compile-time failure.  Keep root lookup construction focused on matching the
declared literals, and do not add duplicate-root or URI-component validation
traits/tests at this boundary.

## 4. Add a scoped path projection instead of copying `Zhttp::Target`

Add a small non-template or parameter-dependent utility near the routing code
that saves only `target.path`, installs a projected span for a synchronous
call, and restores the saved span on scope exit.

- Use it in ordinary `MReqParser` suffix dispatch and in hierarchical root
  dispatch, replacing both complete `Target` copies.
- Make nested projections compose naturally: the child saves the root suffix,
  installs its own typed/suffix projection, and restores the root suffix before
  the parent restores the caller's full path.
- Do not copy path bytes or any other `Target` member.

Extend routing tests to verify the original target path after successful
selection, child rejection, malformed paths, and nested typed-prefix routing.

## 5. Replace `MReqRouter` with a capable `MReqParser`

Refactor `zrest/src/ZrestServer.hh` around one `MReqParser` implementation.
Do not retain `MReqRouter`, and do not merely hide the duplicate blocks behind
a second parser façade.

### Selection policies

Give `MReqParser` a compile-time route/selection policy, defaulting to the
ordinary request-catalog policy.  The exact spelling can follow nearby Z
idioms, but the responsibilities should be equivalent to:

- an ordinary-request policy parameterized by catalog and optional fixed
  component skip count;
- a root-child policy parameterized by the root catalog and server/context
  type.

The policy supplies only the differences needed by `operation()`:

- the union alternative list;
- the header-list trait for an alternative (`GetHdrs<Req>` for a concrete
  request, `Parser::Headers` for a child parser);
- method/path or root/suffix selection;
- selected-alternative initialization (`request.init()` versus
  `child.init(server)`);
- exact/query acceptance for leaf requests;
- completion validity (`ok && request.object` at a leaf, plain `ok` while
  forwarding into a child, which will apply its own leaf validity check).

The root policy owns only the typed server pointer needed to initialize the
selected child.  `MReqParser::init(server)` forwards that context into the
policy; the ordinary default remains a zero-cost no-op.  Invalid selection or
failed child operation must destroy/null the just-created union alternative.

### One protocol-facing forwarding layer

Keep the following methods exactly once, on `MReqParser`, dispatching through
the active union and consulting the policy only for alternative header traits
or completion validity:

- all three `header()` forms;
- `bodyInfo()`;
- `body()`;
- `complete()`;
- `reset()`.

Use one consistent pointer capture in `complete()`.  Preserve the existing
semantic difference between leaf validation and parent-to-child forwarding
through the policy rather than through duplicated callback implementations.

### API migration

- Delete `MReqRouter` after the unified parser is in place.
- Keep `ZrestRootCatalogDerive`/`ZrestRootCatalogImpl` as the named,
  out-of-line root lookup facility, but make their product consumable by the
  root selection policy of `MReqParser`.
- Update `zrest/test/ZrestTest.cc`, `zrest/example/zrestd.cc`,
  `zum/src/zumd_daemon.hh`, and `zum/test/ZumTest.cc` from `MReqRouter` to the
  root-policy form of `MReqParser`.
- Update fixed-skip users to the ordinary selection-policy spelling if adding
  the policy changes the current `MReqParser<Catalog, Skip>` signature.  Since
  this is an intentional breaking change, update every in-tree dependent and
  remove the old spelling rather than adding an alias or forwarding wrapper.
- Keep response-builder interoperability available for both ordinary and
  nested parsers.  Tests and dependents that inspect `parser.u` must continue
  to see the active alternative at the appropriate parser level, or be
  migrated to a concise typed accessor if the refactor introduces one.

Add focused tests proving root hash selection, unknown/boundary root
rejection, server-context initialization, typed dynamic-prefix projection,
header/body callback continuity, completion propagation on valid and invalid
leaf objects, reset/destruction, and target restoration.  These tests should
exercise the unified `MReqParser`; there should be no `MReqRouter` symbol left
in the tree.

## 6. Remove redundant span construction

Pass `path` and `root` directly to `lookup.findVal()` in `reqMatch()` and
`rootMatch()`.  Rely on the designed interoperability of the Z span types; do
not add replacement casts or temporary spans.

## 7. Remove or use dead declarations

- `ExactReq` becomes part of the request-catalog validation and exact-lookup
  construction described in section 1.
- Remove the unused `Paths` alias from `GetRootLookup_` unless the finalized
  validation directly uses it.  Do not leave a convenience alias with no
  consumer.

Run the compiler with warnings enabled through the normal build and confirm no
new unused declarations remain.

## 8. Make header dependencies direct

Add the defining includes for every facility used directly by
`zrest/src/Zrest.hh`:

- `ZuIntrin.hh` for `ZuIntrin::log2`;
- `ZuSwitch.hh` for `ZuSwitch::dispatch`;
- `ZuUnroll.hh` for `ZuUnroll`.

Place them with the other ordered `Zu` dependencies and do not rely on
`ZuMatcher`, `Zhttp`, or other transitive includes.

## 9. Use the project assertion idiom

Replace the `static_assert` in `MResBuilder::init()` with `ZuAssert`, retaining
the check that `Res` belongs to `GetResponses<Req>`.  Replace the new
request-catalog validation `static_assert` in the test with `ZuAssert` as well
so all new compile-time checks demonstrate the required idiom.  Do not add a
root-path assertion; root literal validity remains application-owned.

## Implementation order

1. Add direct includes and request-catalog validation traits.  Leave root
   literal validity to the application as specified in section 3.
2. Correct invalid-index dispatch defaults and add their regression tests.
3. Introduce scoped path projection and convert ordinary request dispatch.
4. Generalize `MReqParser` with ordinary and root selection policies, migrate
   all callers, then delete `MReqRouter`.
5. Remove redundant conversions/dead aliases and convert assertions.
6. Complete focused `zrest` tests.  This is the first major milestone: only
   now incrementally rebuild `zrest/src` and `zrest/test`, then run the focused
   `zrest` tests.
7. Resolve issues found at the first milestone, complete the repository-wide
   dependent migration, and update dependent tests/examples without
   intermediate rebuilds.
8. Treat the completed dependent migration as the second major milestone:
   incrementally rebuild the affected dependents and then run their relevant
   tests.
9. Perform final repository-wide symbol/style checks without changing the
   build configuration or starting a clean build.

## Verification

At the first major milestone, run:

```text
make -C zrest/src -j8
make -C zrest/test -j8
make -C zrest/test test
```

At the second major milestone, use the current top-level build incrementally
to rebuild affected dependents and run the configured test suite:

```text
make -j8
make test
git diff --check
```

Also use `rg` to confirm there are no remaining `MReqRouter` references and no
legacy `MReqParser` invocation forms, compatibility wrappers, or new direct
`static_assert` in the touched routing code.  Do not reconfigure or repeat the
same build/test sequence between milestones unless a milestone build exposes a
failure that requires a targeted incremental rebuild.  The repair is complete
when invalid catalogs fail at compile time, invalid run-time indices return
`-1`, hierarchical and ordinary routing share one protocol-forwarding
implementation, callers observe their original target after dispatch, every
dependent uses the breaking API directly, and all configured `zrest` plus
dependent tests pass without warnings.
